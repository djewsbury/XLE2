// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "AssetUtils.h"
#include "CompilerLibrary.h"
#include "AssetFuture.h"
#include "DepVal.h"
#include "IFileSystem.h"
#include "NascentChunk.h"		// (for AsBlob)
#include "AssetServices.h"
#include "CompileAndAsyncManager.h"
#include "IntermediateAssets.h"		// (used in BeginCompileOperation)
#include "../ConsoleRig/Log.h"
#include "../Utility/StringUtils.h"
#include "../Utility/StringFormat.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/Threading/Mutex.h"
#include "../Utility/Threading/ThreadingUtils.h"
#include "../Utility/Streams/PathUtils.h"
#include "../Utility/Streams/FileUtils.h"
#include "../Utility/SystemUtils.h"     // for XlGetCurrentDirectory
#include <vector>
#include <algorithm>

namespace Assets
{
    static Utility::Threading::RecursiveMutex ResourceDependenciesLock;
    static std::vector<std::pair<const OnChangeCallback*, std::weak_ptr<DependencyValidation>>> ResourceDependencies;
    static unsigned ResourceDepsChangeId = 0;

    void Dependencies_Shutdown()
    {
        decltype(ResourceDependencies) temp;
        temp.swap(ResourceDependencies);
    }

    void    DependencyValidation::OnChange()
    { 
        ++_validationIndex;
        ResourceDependenciesLock.lock();

        auto range = std::equal_range(
            ResourceDependencies.begin(), ResourceDependencies.end(), 
            this, CompareFirst<const OnChangeCallback*, std::weak_ptr<DependencyValidation>>());

        unsigned changeIdStart = ResourceDepsChangeId;

        bool foundExpired = false;
        for (auto i=range.first; i!=range.second; ++i) {
            auto l = i->second.lock();
            if (l) { 
                l->OnChange();

                if (ResourceDepsChangeId != changeIdStart) {
                    // another object may have changed the list of objects 
                    // during the OnChange() call. If this happens,
                    // we need to search through and find the range again.
                    // then we need to set 'i' to the position of the 
                    // same element in the new range (it's guaranteed to
                    // be there, because we have a lock on it!
                    // Oh, it's a wierd hack... But it should work well.

                    range = std::equal_range(
                        ResourceDependencies.begin(), ResourceDependencies.end(), 
                        this, CompareFirst<const OnChangeCallback*, std::weak_ptr<DependencyValidation>>());

                    for (i=range.first;; ++i) {
                        assert(i!=range.second);
                        if (Equivalent(i->second, l)) break;
                    }

                    changeIdStart = ResourceDepsChangeId;
                }
            }
            else foundExpired = true;
        }

        if (foundExpired) {
                // Remove any pointers that have expired
                // (note that we only check matching pointers. Non-matching pointers
                // that have expired are untouched)
            ResourceDependencies.erase(
                std::remove_if(range.first, range.second, 
                    [](std::pair<const OnChangeCallback*, std::weak_ptr<DependencyValidation>>& i)
                    { return i.second.expired(); }),
                range.second);
            ++ResourceDepsChangeId; // signal to callers that this has changed
        }

        ResourceDependenciesLock.unlock();
    }

    void    DependencyValidation::RegisterDependency(const std::shared_ptr<DependencyValidation>& dependency)
    {
		bool hasInvalidationAtStart = false;
		{
			ScopedLock(ResourceDependenciesLock);
			hasInvalidationAtStart = dependency->GetValidationIndex() != 0;
			auto i = LowerBound(ResourceDependencies, (const OnChangeCallback*)dependency.get());
			ResourceDependencies.insert(i, std::make_pair(dependency.get(), shared_from_this()));
		}

            // We must hold a reference to the dependency -- otherwise it can be destroyed,
            // and links to downstream assets/files might be lost
            // It's a little awkward to hold it here, but it's the only way
            // to make sure that it gets destroyed when "this" gets destroyed

		bool isOverflow = true;
        for (unsigned c=0; c<dimof(_dependencies); ++c)
            if (!_dependencies[c]) { _dependencies[c] = dependency; isOverflow = false; break; }
        
		if (isOverflow)
			_dependenciesOverflow.push_back(dependency);

		// If the attached dependency already has a invalidation index, we must propagate it up the tree now.
		// This is because of an awkward race condition:
		//		1) Construct asset A
		//		2) Begin construction of asset B, using a pointer to asset A
		//		3) Asset A changes on disk
		//		4) Filesystem monitor records change to asset A and updates the dependency validation
		//		5) Asset A is registered as a dependency to asset B
		// Now, as we complete the construction of asset B, asset A is already invalidated on disk. 
		// However, the invalidation will not flow up to asset B's dependency invalidation, because the change
		// was recorded before we completed registering the dependency relationship between the 2 assets.
		//
		// However, if we check for any pending invalidations before we unlock the mutex above, and propagate 
		// the invalidation up the tree now, then we will guard against this case.

		if (hasInvalidationAtStart)
			OnChange();
    }

    DependencyValidation::DependencyValidation(DependencyValidation&& moveFrom) never_throws
    {
        _validationIndex = moveFrom._validationIndex;
        for (unsigned c=0; c<dimof(_dependencies); ++c)
            _dependencies[c] = std::move(moveFrom._dependencies[c]);
        _dependenciesOverflow = std::move(moveFrom._dependenciesOverflow);
    }

    DependencyValidation& DependencyValidation::operator=(DependencyValidation&& moveFrom) never_throws
    {
        _validationIndex = moveFrom._validationIndex;
        for (unsigned c=0; c<dimof(_dependencies); ++c)
            _dependencies[c] = std::move(moveFrom._dependencies[c]);
        _dependenciesOverflow = std::move(moveFrom._dependenciesOverflow);
        return *this;
    }

    DependencyValidation::~DependencyValidation() {}

    void RegisterFileDependency(
        const std::shared_ptr<Utility::OnChangeCallback>& validationIndex, 
        StringSection<ResChar> filename)
    {
		MainFileSystem::TryMonitor(filename, validationIndex);
    }

    void RegisterAssetDependency(
        const std::shared_ptr<DependencyValidation>& dependentResource, 
        const std::shared_ptr<DependencyValidation>& dependency)
    {
        assert(dependentResource && dependency);
        dependentResource->RegisterDependency(dependency);
    }

    void DirectorySearchRules::AddSearchDirectory(StringSection<ResChar> dir)
    {
            //  Attempt to fit this directory into our buffer.
            //  note that we have limited space in the buffer, but we can't really
            //  rely on all directory names remaining small and convenient... If we 
            //  overflow our fixed size buffer, we can use the dynamically 
            //  allocated "_bufferOverflow"
        assert((_startPointCount+1) <= dimof(_startOffsets));
        if ((_startPointCount+1) > dimof(_startOffsets)) {
                //  limited number of directories that can be pushed into a single "search rules"
                //  this allows us to avoid a little bit of awkward dynamic memory allocation
            return; 
        }

            // Check for duplicates
            //  Duplicates are bad because they will increase the number of search operations
        if (HasDirectory(dir)) return;

        unsigned allocationLength = (unsigned)(dir.Length() + 1);
        if (_bufferOverflow.empty() && (_bufferUsed + allocationLength <= dimof(_buffer))) {
                // just append this new string to our buffer, and add a new start offset
            XlCopyMemory(&_buffer[_bufferUsed], dir.begin(), (allocationLength-1) * sizeof(ResChar));
            _buffer[_bufferUsed+allocationLength-1] = '\0';
        } else {
            if (_bufferOverflow.empty()) {
                _bufferOverflow.resize(_bufferUsed + allocationLength);
                XlCopyMemory(AsPointer(_bufferOverflow.begin()), _buffer, _bufferUsed * sizeof(ResChar));
                XlCopyMemory(PtrAdd(AsPointer(_bufferOverflow.begin()), _bufferUsed * sizeof(ResChar)), dir.begin(), (allocationLength-1) * sizeof(ResChar));
                _bufferOverflow[_bufferUsed+allocationLength-1] = '\0';
            } else {
                assert(_bufferOverflow.size() == allocationLength);
                auto i = _bufferOverflow.insert(_bufferOverflow.end(), dir.begin(), dir.end());
                _bufferOverflow.insert(i + dir.Length(), 0);
            }
        }

        _startOffsets[_startPointCount++] = _bufferUsed;
        _bufferUsed += allocationLength;
    }

    void DirectorySearchRules::AddSearchDirectoryFromFilename(StringSection<ResChar> filename)
    {
        AddSearchDirectory(MakeFileNameSplitter(filename).DriveAndPath());
    }

    std::string DirectorySearchRules::AnySearchDirectory() const
    {
        assert(_startPointCount > 0);
        return std::string(&_buffer[_startOffsets[0]]);
    }
    
    bool DirectorySearchRules::HasDirectory(StringSection<ResChar> dir)
    {
        const ResChar* b = _buffer;
        if (!_bufferOverflow.empty()) {
            b = AsPointer(_bufferOverflow.begin());
        }

            // note --  just doing a string insensitive compare here...
            //          we should really do a more sophisticated path compare
            //          to get a more accurate result
            //          Actually, it might be better to store the paths in some
            //          format that is more convenient for comparisons and combining
            //          paths.
        for (unsigned c=0; c<_startPointCount; ++c)
            if (XlEqStringI(dir, &b[_startOffsets[c]]))
                return true;

        return false;
    }

	static bool DoesFileExist(StringSection<ResChar> fn)
	{
		return MainFileSystem::TryGetDesc(fn)._state == FileDesc::State::Normal;
	}

	static std::vector<std::basic_string<ResChar>> FindFiles(
		StringSection<ResChar> searchPath, 
		RawFS::FindFilesFilter::BitField filter)
	{
		return RawFS::FindFiles(searchPath.AsString(), filter);
	}

    void DirectorySearchRules::ResolveFile(ResChar destination[], unsigned destinationCount, StringSection<ResChar> baseName) const
    {
        ResChar tempBuffer[MaxPath];

        auto splitter = MakeFileNameSplitter(baseName);
        bool baseFileExist = false;
        if (!splitter.ParametersWithDivider().IsEmpty()) {
            XlCopyString(tempBuffer, splitter.AllExceptParameters());
            baseFileExist = DoesFileExist(tempBuffer);
        } else {
            baseFileExist = DoesFileExist(baseName);
        }

            // by definition, we always check the unmodified file name first
        if (!baseFileExist) {
            const ResChar* b = _buffer;
            if (!_bufferOverflow.empty()) {
                b = AsPointer(_bufferOverflow.begin());
            }

                // We want to support the case were destination == baseName
                // But that cases requires another temporary buffer, because we
                // don't want to trash "baseName" while searching for matches
            bool baseNameOverlapsDestination = !(baseName.end() <= destination || baseName.begin() >= &destination[destinationCount]);
            ResChar* workingBuffer = (!baseNameOverlapsDestination) ? destination : tempBuffer;
            unsigned workingBufferSize = (!baseNameOverlapsDestination) ? destinationCount : unsigned(dimof(tempBuffer));

            for (unsigned c=0; c<_startPointCount; ++c) {
                XlConcatPath(workingBuffer, workingBufferSize, &b[_startOffsets[c]], 
                    splitter.AllExceptParameters().begin(), splitter.AllExceptParameters().end());
                if (DoesFileExist(workingBuffer)) {
                    SplitPath<ResChar>(workingBuffer).Simplify().Rebuild(workingBuffer, workingBufferSize);
                    if (workingBuffer != destination) {
                        auto workingBufferLen = std::min((ptrdiff_t)XlStringLen(workingBuffer), ptrdiff_t(destinationCount) - 1);
                        auto colonLen = (ptrdiff_t)splitter.ParametersWithDivider().Length();
                        auto colonCopy = std::min(ptrdiff_t(destinationCount) - workingBufferLen - 1, colonLen);
                        assert((workingBufferLen + colonCopy) < ptrdiff_t(destinationCount));
                        if (colonCopy > 0)
                            XlMoveMemory(&destination[workingBufferLen], splitter.ParametersWithDivider().begin(), colonCopy);
                        destination[workingBufferLen + colonCopy] = '\0';
                        assert(workingBufferLen < (ptrdiff_t(destinationCount)-1));
                        XlCopyMemory(destination, workingBuffer, workingBufferLen);
                    } else {
                        XlCatString(destination, destinationCount, splitter.ParametersWithDivider());
                    }
                    return;
                }
            }
        }

        if (baseName.begin() != destination)
            XlCopyString(destination, destinationCount, baseName);
        SplitPath<ResChar>(destination).Simplify().Rebuild(destination, destinationCount);
    }

    void DirectorySearchRules::ResolveDirectory(
            ResChar destination[], unsigned destinationCount, 
            StringSection<ResChar> baseName) const
    {
            //  We have a problem with basic paths (like '../')
            //  These will match for most directories -- which means that
            //  there is some ambiguity. Let's prefer to use the first
            //  registered path for simple relative paths like this.
        bool useBaseName = 
            (baseName[0] != '.' && RawFS::DoesDirectoryExist(baseName));

        if (!useBaseName) {
            const ResChar* b = _buffer;
            if (!_bufferOverflow.empty()) {
                b = AsPointer(_bufferOverflow.begin());
            }

            ResChar tempBuffer[MaxPath];
            bool baseNameOverlapsDestination = !(baseName.end() <= destination || baseName.begin() >= &destination[destinationCount]);
            ResChar* workingBuffer = (!baseNameOverlapsDestination) ? destination : tempBuffer;
            unsigned workingBufferSize = (!baseNameOverlapsDestination) ? destinationCount : unsigned(dimof(tempBuffer));

            for (unsigned c=0; c<_startPointCount; ++c) {
                XlConcatPath(workingBuffer, workingBufferSize, &b[_startOffsets[c]], baseName.begin(), baseName.end());
                if (RawFS::DoesDirectoryExist(workingBuffer)) {
                    if (workingBuffer != destination)
                        XlCopyString(destination, destinationCount, workingBuffer);
                    return;
                }
            }
        }

        if (baseName.begin() != destination)
            XlCopyString(destination, destinationCount, baseName);
    }

    void DirectorySearchRules::Merge(const DirectorySearchRules& mergeFrom)
    {
            // Merge in the settings from the given search rules (if the directories
            // don't already exist here)
            // We should really do a better job of comparing directories. Where strings
            // resolve to the same directory, we should consider them identical
        const ResChar* b = mergeFrom._buffer;
        if (!mergeFrom._bufferOverflow.empty())
            b = AsPointer(mergeFrom._bufferOverflow.begin());

        for (unsigned c=0; c<mergeFrom._startPointCount; ++c)
            AddSearchDirectory(&b[mergeFrom._startOffsets[c]]);
    }

	std::vector<std::basic_string<ResChar>> DirectorySearchRules::FindFiles(StringSection<char> wildcardSearch) const
	{
		const ResChar* b = _buffer;
        if (!_bufferOverflow.empty()) {
            b = AsPointer(_bufferOverflow.begin());
        }

		ResChar workingBuffer[MaxPath];
		std::vector<std::basic_string<ResChar>> result;

		for (unsigned c=0; c<_startPointCount; ++c) {
			XlConcatPath(workingBuffer, dimof(workingBuffer), &b[_startOffsets[c]], wildcardSearch.begin(), wildcardSearch.end());
			auto partialRes = Assets::FindFiles(workingBuffer, RawFS::FindFilesFilter::File);
			result.insert(result.end(), partialRes.begin(), partialRes.end());
		}
		
		return result;
	}

    DirectorySearchRules::DirectorySearchRules()
    {
        _buffer[0] = '\0';
        _startPointCount = 0;
        _bufferUsed = 0;
        std::fill(_startOffsets, &_startOffsets[dimof(_startOffsets)], 0);
    }

    DirectorySearchRules::DirectorySearchRules(const DirectorySearchRules& copyFrom)
    : _bufferOverflow(copyFrom._bufferOverflow)
    {
        std::copy(copyFrom._buffer, &copyFrom._buffer[dimof(_buffer)], _buffer);
        std::copy(copyFrom._startOffsets, &copyFrom._startOffsets[dimof(_startOffsets)], _startOffsets);
        _bufferUsed = copyFrom._bufferUsed;
        _startPointCount = copyFrom._startPointCount;
    }

    DirectorySearchRules& DirectorySearchRules::operator=(const DirectorySearchRules& copyFrom)
    {
        std::copy(copyFrom._buffer, &copyFrom._buffer[dimof(_buffer)], _buffer);
        std::copy(copyFrom._startOffsets, &copyFrom._startOffsets[dimof(_startOffsets)], _startOffsets);
        _bufferOverflow = copyFrom._bufferOverflow;
        _bufferUsed = copyFrom._bufferUsed;
        _startPointCount = copyFrom._startPointCount;
        return *this;
    }

	DirectorySearchRules::DirectorySearchRules(DirectorySearchRules&& moveFrom) never_throws
	: _bufferOverflow(std::move(moveFrom._bufferOverflow))
    {
        std::copy(moveFrom._buffer, &moveFrom._buffer[dimof(_buffer)], _buffer);
        std::copy(moveFrom._startOffsets, &moveFrom._startOffsets[dimof(_startOffsets)], _startOffsets);
        _bufferUsed = moveFrom._bufferUsed;
        _startPointCount = moveFrom._startPointCount;

		moveFrom._buffer[0] = '\0';
        moveFrom._startPointCount = 0;
        moveFrom._bufferUsed = 0;
        std::fill(moveFrom._startOffsets, &moveFrom._startOffsets[dimof(moveFrom._startOffsets)], 0);
    }
        
	DirectorySearchRules& DirectorySearchRules::operator=(DirectorySearchRules&& moveFrom) never_throws
	{
		std::copy(moveFrom._buffer, &moveFrom._buffer[dimof(_buffer)], _buffer);
        std::copy(moveFrom._startOffsets, &moveFrom._startOffsets[dimof(_startOffsets)], _startOffsets);
        _bufferOverflow = std::move(moveFrom._bufferOverflow);
        _bufferUsed = moveFrom._bufferUsed;
        _startPointCount = moveFrom._startPointCount;

		moveFrom._buffer[0] = '\0';
        moveFrom._startPointCount = 0;
        moveFrom._bufferUsed = 0;
        std::fill(moveFrom._startOffsets, &moveFrom._startOffsets[dimof(moveFrom._startOffsets)], 0);
        return *this;
	}

    DirectorySearchRules DefaultDirectorySearchRules(StringSection<ResChar> baseFile)
    {
        Assets::DirectorySearchRules searchRules;
        searchRules.AddSearchDirectoryFromFilename(baseFile);
        return searchRules;
    }

    namespace Exceptions
    {
		RetrievalError::RetrievalError(StringSection<ResChar> initializer) never_throws
        {
            XlCopyString(_initializer, dimof(_initializer), initializer); 
        }

        InvalidAsset::InvalidAsset(StringSection<ResChar> initializer, const DepValPtr& depVal, const Blob& actualizationLog) never_throws
        : RetrievalError(initializer)
		, _depVal(depVal)
		, _actualizationLog(actualizationLog)
        {
        }

        bool InvalidAsset::CustomReport() const
        {
			if (_actualizationLog) {
				Log(Error) << "Invalid asset (" << Initializer() << "):" << MakeStringSection((const char*)AsPointer(_actualizationLog->begin()), (const char*)AsPointer(_actualizationLog->end())) << std::endl;
			} else {
				Log(Error) << "Invalid asset (" << Initializer() << std::endl;
			}
            return true;
        }

        auto InvalidAsset::State() const -> AssetState { return AssetState::Invalid; }

        PendingAsset::PendingAsset(StringSection<ResChar> initializer) never_throws
        : RetrievalError(initializer)
        {}

        bool PendingAsset::CustomReport() const
        {
            // "pending asset" exceptions occur very frequently. It may be better to suppress
            // any logging information.
            // LogAlwaysWarning << "Pending asset: " << Initializer();
            return true;
        }

        auto PendingAsset::State() const -> AssetState { return AssetState::Pending; }

		bool ConstructionError::CustomReport() const
		{
			if (_actualizationLog) {
				Log(Error) << "Error during asset construction: " << MakeStringSection((const char*)AsPointer(_actualizationLog->begin()), (const char*)AsPointer(_actualizationLog->end())) << std::endl;
			}
			else {
				Log(Error) << "Error during asset construction (unspecified)" << std::endl;
			}
			return true;
		}

        const char* ConstructionError::what() const noexcept
        {
            static char buffer[4096];
            if (_actualizationLog) {
                strcpy(buffer, "Error during asset construction: ");
                auto* d = buffer;
                while (*d) ++d;
                auto* end = &buffer[dimof(buffer)-2];
                for (auto s:*_actualizationLog) {
                    if (d == end) break;
                    *d++ = s;
                }
                *d = '\0';
            } else {
                strcpy(buffer, "Error during asset construction (unspecified)");
            }
            return buffer;
        }

		ConstructionError::ConstructionError(Reason reason, const DepValPtr& depVal, const Blob& actualizationLog) never_throws
		: _reason(reason), _depVal(depVal)
		, _actualizationLog(actualizationLog)
		{
		}

		ConstructionError::ConstructionError(Reason reason, const DepValPtr& depVal, const char format[], ...) never_throws
		: _reason(reason), _depVal(depVal)
		{
			char buffer[512];
			va_list args;
			va_start(args, format);
			std::vsnprintf(buffer, dimof(buffer), format, args);
			va_end(args);

			_actualizationLog = AsBlob(MakeIteratorRange(buffer, XlStringEnd(buffer)));
		}

		ConstructionError::ConstructionError(const std::exception& e, const DepValPtr& depVal) never_throws
		: _reason(Reason::Unknown)
		, _depVal(depVal)
		, _actualizationLog(AsBlob(e))
		{}

		ConstructionError::ConstructionError(const ConstructionError& copyFrom, const DepValPtr& depVal) never_throws
		: _reason(copyFrom._reason)
		, _depVal(copyFrom._depVal)
		, _actualizationLog(copyFrom._actualizationLog)
		{
			// merge the depvals by creating a tree
			if (_depVal && depVal && _depVal != depVal) {
				auto parentDepVal = std::make_shared<DependencyValidation>();
				RegisterAssetDependency(parentDepVal, _depVal);
				RegisterAssetDependency(parentDepVal, depVal);
				_depVal = std::move(parentDepVal);
			} else if (depVal) {
				_depVal = depVal;
			}
		}
    }

	Blob AsBlob(const std::exception& e)
	{
		const char* w = e.what();
		return AsBlob(MakeIteratorRange(w, XlStringEnd(w)));
	}


    GenericFuture::GenericFuture(AssetState state) 
    : _state(state)
    {
        DEBUG_ONLY(_initializer[0] = '\0');
    }

    GenericFuture::~GenericFuture() {}

    const char* GenericFuture::Initializer() const
    {
        #if defined(_DEBUG)
            return _initializer;
        #else
            return "";
        #endif
    }

    void GenericFuture::SetInitializer(const char initializer[])
    {
        DEBUG_ONLY(XlCopyString(_initializer, initializer));
    }

    void GenericFuture::SetState(AssetState newState)
    {
        _state = newState;
    }

    AssetState GenericFuture::StallWhilePending() const
    {
            // Stall until the _state variable changes
            // in another thread.
            // there is no semaphore, so we must poll
            // note -- we should start a progress bar here...
        volatile AssetState* state = const_cast<AssetState*>(&_state);
		uint32 waitCount = 0u;
        while (*state == AssetState::Pending) {
			float sleepValue = float(waitCount);
			sleepValue -= 64.f;
			sleepValue /= 16.f;
            ++waitCount;
			Threading::Sleep(uint32(std::min(100.0f, std::max(0.0f, sleepValue))));
		}

        return *state;
    }

	IAsyncMarker::~IAsyncMarker() {}


///////////////////////////////////////////////////////////////////////////////////////////////////

    static const SplitPath<ResChar>& BaseDir()
    {
            // hack -- find the base directory we'll use to build relative names from 
            //  Note that this is going to call XlGetCurrentDirectory at some unpredictable
            //  time; so we're assuming that the current directory is set at app start, and
            //  remains constant
        static ResolvedAssetFile buffer;
        static SplitPath<ResChar> result;
        static bool init = false;
        if (!init) {
			static Threading::Mutex lock;
			ScopedLock(lock);
            XlGetCurrentDirectory(dimof(buffer._fn), buffer._fn);
            SplitPath<ResChar>(buffer._fn).Rebuild(buffer._fn, dimof(buffer._fn));
            result = SplitPath<ResChar>(buffer._fn);
            init = true;
        }
        return result;
    }

    void MakeAssetName(ResolvedAssetFile& dest, StringSection<ResChar> src)
    {
        auto rules = s_defaultFilenameRules;
        FileNameSplitter<ResChar> srcSplit(src);
        auto relPath = 
            MakeRelativePath(
                BaseDir(),
                SplitPath<ResChar>(srcSplit.DriveAndPath()),
                rules);

        XlCopyString(dest._fn, relPath.c_str());

        auto* i = XlStringEnd(dest._fn);
        auto* iend = ArrayEnd(dest._fn);
        auto fileAndExtension = srcSplit.FileAndExtension();
        auto* s = fileAndExtension._start;
        auto* send = fileAndExtension._end;

        while (i!=iend && s!=send) { *i = ConvertPathChar(*s, rules); ++i; ++s; }
        
        if (!srcSplit.ParametersWithDivider().IsEmpty()) {
            s = srcSplit.ParametersWithDivider()._start;
            send = srcSplit.ParametersWithDivider()._end;
            while (i!=iend && s!=send) { *i = *s; ++i; ++s; }
        }

        *std::min(i, iend-1) = '\0';
    }

    void MakeAssetName(ResolvedAssetFile& dest, StringSection<utf8> src)
    {
        MakeAssetName(
            dest, 
            StringSection<ResChar>(
                (const ::Assets::ResChar*)src.begin(),
                (const ::Assets::ResChar*)src.end()));
    }

	DepValPtr AsDepVal(IteratorRange<const DependentFileState*> deps)
	{
		auto result = std::make_shared<DependencyValidation>();
		for (const auto& i : deps)
			::Assets::RegisterFileDependency(result, MakeStringSection(i._filename));
		return result;
	}

	ICompileOperation::~ICompileOperation() {}
	ICompilerDesc::~ICompilerDesc() {}

	namespace Internal
	{
		std::shared_ptr<ICompileMarker> BeginCompileOperation(
			uint64_t typeCode, const StringSection<ResChar> initializers[],
			unsigned initializerCount)
		{
			auto& compilers = Services::GetAsyncMan().GetIntermediateCompilers();
			auto& store = Services::GetAsyncMan().GetIntermediateStore();
			return compilers.PrepareAsset(typeCode, initializers, initializerCount, store);
		}
	}

}

