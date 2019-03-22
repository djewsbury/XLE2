// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "IFileSystem.h"
#include "MountingTree.h"
#include "../ConsoleRig/Log.h"
#include "../Utility/Streams/PathUtils.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/Threading/ThreadingUtils.h"

namespace Assets
{
	static std::shared_ptr<MountingTree> s_mainMountingTree;
	static std::shared_ptr<IFileSystem> s_defaultFileSystem;

	static IFileSystem::IOReason AsIOReason(IFileSystem::TranslateResult transResult)
	{
		switch (transResult) {
		case IFileSystem::TranslateResult::Mounting: return IFileSystem::IOReason::Mounting;
		case IFileSystem::TranslateResult::Invalid: return IFileSystem::IOReason::Invalid;
		default: return IFileSystem::IOReason::Invalid;
		}
	}

	static FileDesc::State AsFileState(IFileSystem::TranslateResult transResult)
	{
		switch (transResult) {
		case IFileSystem::TranslateResult::Mounting: return FileDesc::State::Mounting;
		case IFileSystem::TranslateResult::Invalid: return FileDesc::State::Invalid;
		default: return FileDesc::State::Invalid;
		}
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	namespace Internal 
	{
		using LookupResult = MountingTree::EnumerableLookup::Result;

		template<typename FileType, typename CharType>
			static IFileSystem::IOReason TryOpen(FileType& result, StringSection<CharType> filename, const char openMode[], FileShareMode::BitField shareMode)
		{
			result = FileType();

			MountingTree::CandidateObject candidateObject;
			auto lookup = s_mainMountingTree->Lookup(filename);
			for (;;) {
				auto r = lookup.TryGetNext(candidateObject);
				if (r == LookupResult::Invalidated) {
                    // "Mounting point lookup was invalidated when the mounting tree changed. Do not change the mount or unmount filesystems while other threads may be accessing the same mounting tree."
                    lookup = s_mainMountingTree->Lookup(filename);
                    continue;
                }

				if (r == LookupResult::NoCandidates)
					break;

				assert(candidateObject._fileSystem);
				auto ioRes = candidateObject._fileSystem->TryOpen(result, candidateObject._marker, openMode, shareMode);
				if (ioRes != IFileSystem::IOReason::FileNotFound && ioRes != IFileSystem::IOReason::Invalid)
					return ioRes;
			}

			// attempt opening with the default file system...
			if (s_defaultFileSystem)
				return ::Assets::TryOpen(result, *s_defaultFileSystem, filename, openMode, shareMode);
			return IFileSystem::IOReason::FileNotFound;
		}

		template<typename FileType, typename CharType>
			static IFileSystem::IOReason TryOpen(FileType& result, StringSection<CharType> filename, uint64 size, const char openMode[], FileShareMode::BitField shareMode)
		{
			result = FileType();

			MountingTree::CandidateObject candidateObject;
			auto lookup = s_mainMountingTree->Lookup(filename);
			for (;;) {
                auto r = lookup.TryGetNext(candidateObject);
                if (r == LookupResult::Invalidated) {
                    // "Mounting point lookup was invalidated when the mounting tree changed. Do not change the mount or unmount filesystems while other threads may be accessing the same mounting tree."
                    lookup = s_mainMountingTree->Lookup(filename);
                    continue;
                }

				if (r == LookupResult::NoCandidates)
					break;

				assert(candidateObject._fileSystem);
				auto ioRes = candidateObject._fileSystem->TryOpen(result, candidateObject._marker, size, openMode, shareMode);
				if (ioRes != IFileSystem::IOReason::FileNotFound && ioRes != IFileSystem::IOReason::Invalid)
					return ioRes;
			}

			// attempt opening with the default file system...
			if (s_defaultFileSystem) 
				return ::Assets::TryOpen(result, *s_defaultFileSystem, filename, size, openMode, shareMode);
			return IFileSystem::IOReason::FileNotFound;
		}

		template<typename CharType>
			IFileSystem::IOReason TryMonitor(StringSection<CharType> filename, const std::shared_ptr<IFileMonitor>& evnt)
		{
			MountingTree::CandidateObject candidateObject;
			auto lookup = s_mainMountingTree->Lookup(filename);
			for (;;) {
				auto r = lookup.TryGetNext(candidateObject);
				if (r == LookupResult::Invalidated) {
                    // "Mounting point lookup was invalidated when the mounting tree changed. Do not change the mount or unmount filesystems while other threads may be accessing the same mounting tree."
                    lookup = s_mainMountingTree->Lookup(filename);
                    continue;
                }

				if (r == LookupResult::NoCandidates)
					break;

				// We must call TryMonitor for each filesystem, because the filesystems return
				// "success" even if the file doesn't exist. So if we stop early, on the first
				// filesystem will be monitored
				assert(candidateObject._fileSystem);
				auto ioRes = candidateObject._fileSystem->TryMonitor(candidateObject._marker, evnt);
				(void)ioRes;
			}

			if (s_defaultFileSystem) 
				return ::Assets::TryMonitor(*s_defaultFileSystem, filename, evnt);
			return IFileSystem::IOReason::FileNotFound;
		}

		template<typename CharType>
			FileDesc TryGetDesc(StringSection<CharType> filename)
		{
			MountingTree::CandidateObject candidateObject;
			auto lookup = s_mainMountingTree->Lookup(filename);
			for (;;) {
				auto r = lookup.TryGetNext(candidateObject);
				if (r == LookupResult::Invalidated) {
                    // "Mounting point lookup was invalidated when the mounting tree changed. Do not change the mount or unmount filesystems while other threads may be accessing the same mounting tree."
                    lookup = s_mainMountingTree->Lookup(filename);
                    continue;
                }

				if (r == LookupResult::NoCandidates) 
					break;

				assert(candidateObject._fileSystem);
				auto res = candidateObject._fileSystem->TryGetDesc(candidateObject._marker);
				if (res._state != FileDesc::State::DoesNotExist)
					return res;
			}

			if (s_defaultFileSystem)
				return ::Assets::TryGetDesc(*s_defaultFileSystem, filename);
			return FileDesc{ std::basic_string<utf8>(), FileDesc::State::DoesNotExist };
		}
	}

	//
	// note -- the UTF8 and UTF16 versions of these functions are identical... They could be implemented
	//			with a template. But the C++ method resolution works better when they are explicitly separated
	//			like this.
	//		eg, because 
	//			MainFileSystem::FileOpen(u("SomeFile.txt"),...);
	//		relies on automatic conversion for StringSection<utf8>, it works in this case, but not in the
	//		template case.
	//

	auto MainFileSystem::TryOpen(std::unique_ptr<IFileInterface>& result, StringSection<utf8> filename, const char openMode[], FileShareMode::BitField shareMode) -> IOReason
	{
		return Internal::TryOpen(result, filename, openMode, shareMode);
	}

	auto MainFileSystem::TryOpen(BasicFile& result, StringSection<utf8> filename, const char openMode[], FileShareMode::BitField shareMode) -> IOReason
	{
		return Internal::TryOpen(result, filename, openMode, shareMode);
	}

	auto MainFileSystem::TryOpen(MemoryMappedFile& result, StringSection<utf8> filename, uint64 size, const char openMode[], FileShareMode::BitField shareMode) -> IOReason
	{
		return Internal::TryOpen(result, filename, size, openMode, shareMode);
	}

	IFileSystem::IOReason MainFileSystem::TryMonitor(StringSection<utf8> filename, const std::shared_ptr<IFileMonitor>& evnt)
	{
		return Internal::TryMonitor(filename, evnt);
	}

	FileDesc MainFileSystem::TryGetDesc(StringSection<utf8> filename)
	{
		return Internal::TryGetDesc(filename);
	}

	auto MainFileSystem::TryOpen(std::unique_ptr<IFileInterface>& result, StringSection<utf16> filename, const char openMode[], FileShareMode::BitField shareMode) -> IOReason
	{
		return Internal::TryOpen(result, filename, openMode, shareMode);
	}

	auto MainFileSystem::TryOpen(BasicFile& result, StringSection<utf16> filename, const char openMode[], FileShareMode::BitField shareMode) -> IOReason
	{
		return Internal::TryOpen(result, filename, openMode, shareMode);
	}

	auto MainFileSystem::TryOpen(MemoryMappedFile& result, StringSection<utf16> filename, uint64 size, const char openMode[], FileShareMode::BitField shareMode) -> IOReason
	{
		return Internal::TryOpen(result, filename, size, openMode, shareMode);
	}

	IFileSystem::IOReason MainFileSystem::TryMonitor(StringSection<utf16> filename, const std::shared_ptr<IFileMonitor>& evnt)
	{
		return Internal::TryMonitor(filename, evnt);
	}

	FileDesc MainFileSystem::TryGetDesc(StringSection<utf16> filename)
	{
		return Internal::TryGetDesc(filename);
	}

	BasicFile MainFileSystem::OpenBasicFile(StringSection<utf8> filename, const char openMode[], FileShareMode::BitField shareMode)
	{
		BasicFile result;
		auto ioRes = TryOpen(result, filename, openMode, shareMode);
		if (ioRes != IOReason::Success)
			Throw(Utility::Exceptions::IOException(ioRes, "Failure while opening file (%s) in mode (%s)", std::string((const char*)filename.begin(), (const char*)filename.end()).c_str(), openMode));
		return result;
	}

	MemoryMappedFile MainFileSystem::OpenMemoryMappedFile(StringSection<utf8> filename, uint64 size, const char openMode[], FileShareMode::BitField shareMode)
	{
		MemoryMappedFile result;
		auto ioRes = TryOpen(result, filename, size, openMode, shareMode);
		if (ioRes != IOReason::Success)
			Throw(Utility::Exceptions::IOException(ioRes, "Failure while opening file (%s) in mode (%s)", std::string((const char*)filename.begin(), (const char*)filename.end()).c_str(), openMode));
		return result;
	}

	std::unique_ptr<IFileInterface> MainFileSystem::OpenFileInterface(StringSection<utf8> filename, const char openMode[], FileShareMode::BitField shareMode)
	{
		std::unique_ptr<IFileInterface> result;
		auto ioRes = TryOpen(result, filename, openMode, shareMode);
		if (ioRes != IOReason::Success)
			Throw(Utility::Exceptions::IOException(ioRes, "Failure while opening file (%s) in mode (%s)", std::string((const char*)filename.begin(), (const char*)filename.end()).c_str(), openMode));
		return result;
	}

	IFileSystem* MainFileSystem::GetFileSystem(FileSystemId id)
	{
		return s_mainMountingTree->GetMountedFileSystem(id);		// in all current cases the FileSystemId overlaps with the MountId in s_mainMountingTree
	}

	FileSystemWalker MainFileSystem::BeginWalk(StringSection<utf8> initialSubDirectory)
	{
		return s_mainMountingTree->BeginWalk(initialSubDirectory);
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	const std::shared_ptr<MountingTree>& MainFileSystem::GetMountingTree() { return s_mainMountingTree; }
	void MainFileSystem::Init(const std::shared_ptr<MountingTree>& mountingTree, const std::shared_ptr<IFileSystem>& defaultFileSystem)
	{
		s_mainMountingTree = mountingTree;
		s_defaultFileSystem = defaultFileSystem;
	}

    void MainFileSystem::Shutdown()
    {
        Init(nullptr, nullptr);
    }

	T2(CharType, FileObject) IFileSystem::IOReason TryOpen(FileObject& result, IFileSystem& fs, StringSection<CharType> fn, const char openMode[], FileShareMode::BitField shareMode)
	{
		result = FileObject();

		IFileSystem::Marker marker;
		auto transResult = fs.TryTranslate(marker, fn);
		if (transResult == IFileSystem::TranslateResult::Success)
			return fs.TryOpen(result, marker, openMode);

		return AsIOReason(transResult);
	}

	T2(CharType, FileObject) IFileSystem::IOReason TryOpen(FileObject& result, IFileSystem& fs, StringSection<CharType> fn, uint64 size, const char openMode[], FileShareMode::BitField shareMode)
	{
		result = FileObject();

		IFileSystem::Marker marker;
		auto transResult = fs.TryTranslate(marker, fn);
		if (transResult == IFileSystem::TranslateResult::Success)
			return fs.TryOpen(result, marker, size, openMode);

		return AsIOReason(transResult);
	}

	T1(CharType) IFileSystem::IOReason TryMonitor(IFileSystem& fs, StringSection<CharType> fn, const std::shared_ptr<IFileMonitor>& evnt)
	{
		IFileSystem::Marker marker;
		auto transResult = fs.TryTranslate(marker, fn);
		if (transResult == IFileSystem::TranslateResult::Success)
			return fs.TryMonitor(marker, evnt);
		return AsIOReason(transResult);
	}

	T1(CharType) FileDesc TryGetDesc(IFileSystem& fs, StringSection<CharType> fn)
	{
		IFileSystem::Marker marker;
		auto transResult = fs.TryTranslate(marker, fn);
		if (transResult == IFileSystem::TranslateResult::Success)
			return fs.TryGetDesc(marker);
		return FileDesc{std::basic_string<utf8>(), AsFileState(transResult)};
	}

	template IFileSystem::IOReason TryOpen<utf8, std::unique_ptr<IFileInterface>>(std::unique_ptr<IFileInterface>& result, IFileSystem& fs, StringSection<utf8> fn, const char openMode[], FileShareMode::BitField shareMode);
	template IFileSystem::IOReason TryOpen<utf8, BasicFile>(BasicFile& result, IFileSystem& fs, StringSection<utf8> fn, const char openMode[], FileShareMode::BitField shareMode);
	template IFileSystem::IOReason TryOpen<utf8, MemoryMappedFile>(MemoryMappedFile& result, IFileSystem& fs, StringSection<utf8> fn, uint64 size, const char openMode[], FileShareMode::BitField shareMode);
	template IFileSystem::IOReason TryMonitor<utf8>(IFileSystem& fs, StringSection<utf8> fn, const std::shared_ptr<IFileMonitor>& evnt);
	template FileDesc TryGetDesc<utf8>(IFileSystem& fs, StringSection<utf8> fn);
	template IFileSystem::IOReason TryOpen<utf16, std::unique_ptr<IFileInterface>>(std::unique_ptr<IFileInterface>& result, IFileSystem& fs, StringSection<utf16> fn, const char openMode[], FileShareMode::BitField shareMode);
	template IFileSystem::IOReason TryOpen<utf16, BasicFile>(BasicFile& result, IFileSystem& fs, StringSection<utf16> fn, const char openMode[], FileShareMode::BitField shareMode);
	template IFileSystem::IOReason TryOpen<utf16, MemoryMappedFile>(MemoryMappedFile& result, IFileSystem& fs, StringSection<utf16> fn, uint64 size, const char openMode[], FileShareMode::BitField shareMode);
	template IFileSystem::IOReason TryMonitor<utf16>(IFileSystem& fs, StringSection<utf16> fn, const std::shared_ptr<IFileMonitor>& evnt);
	template FileDesc TryGetDesc<utf16>(IFileSystem& fs, StringSection<utf16> fn);

	std::unique_ptr<uint8[]> TryLoadFileAsMemoryBlock(StringSection<char> sourceFileName, size_t* sizeResult)
	{
		std::unique_ptr<IFileInterface> file;
		if (MainFileSystem::TryOpen(file, sourceFileName, "rb", FileShareMode::Read) == IFileSystem::IOReason::Success) {
			file->Seek(0, FileSeekAnchor::End);
			size_t size = file->TellP();
			file->Seek(0);
			if (size) {
				auto result = std::make_unique<uint8[]>(size);
				file->Read(result.get(), 1, size);
				if (sizeResult) {
					*sizeResult = size;
				}
				return result;
			}
		}

		// on missing file (or failed load), we return the equivalent of an empty file
		if (sizeResult) { *sizeResult = 0; }
		return nullptr;
	}

	Blob TryLoadFileAsBlob(StringSection<char> sourceFileName)
	{
		std::unique_ptr<IFileInterface> file;
		if (MainFileSystem::TryOpen(file, sourceFileName, "rb", FileShareMode::Read) == IFileSystem::IOReason::Success) {
			file->Seek(0, FileSeekAnchor::End);
			size_t size = file->TellP();
			file->Seek(0);
			if (size) {
				auto result = std::make_shared<std::vector<uint8_t>>(size);
				file->Read(result->data(), 1, size);
				return result;
			}
		}

		return nullptr;
	}

	FileSystemWalker BeginWalk(const std::shared_ptr<ISearchableFileSystem>& fs)
	{
		std::vector<FileSystemWalker::StartingFS> startingFS;
		startingFS.push_back({{}, {}, fs, 0});
		return FileSystemWalker(std::move(startingFS));
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class FileSystemWalker::Pimpl
	{
	public:
		std::vector<StartingFS> _fileSystems;

		struct SubFile
		{
			unsigned _filesystemIndex;
			IFileSystem::Marker _marker;
			FileDesc _desc;
			uint64_t _naturalNameHash;
		};
		std::vector<SubFile> _files;

		struct SubDirectory
		{
			std::basic_string<utf8> _name;
			uint64_t _nameHash;
			std::vector<unsigned> _filesystemIndices;
		};
		std::vector<SubDirectory> _directories;

		bool _foundFiles = false;
		bool _foundDirectories = false;

		Pimpl(std::vector<StartingFS>&& fileSystems)
		: _fileSystems(std::move(fileSystems)) {}

		void FindFiles()
		{
			if (_foundFiles) return;
			assert(_files.empty());

			for (unsigned fsIdx=0; fsIdx<_fileSystems.size(); ++fsIdx) {
				auto& fs = _fileSystems[fsIdx];
				if (!fs._pendingDirectories.empty()) continue;

				auto foundMarkers = fs._fs->FindFiles(MakeStringSection(fs._internalPoint), u(".*"));

				auto* baseFS = dynamic_cast<IFileSystem*>(fs._fs.get());
				assert(baseFS);
				for (auto&m:foundMarkers) {
					// The filesystem will give us it's internal "marker" representation of the filename
					// But we're probably more interested in the natural name of the file; but we'll have
					// to query that from the filesystem again
					auto desc = baseFS->TryGetDesc(m);
					if (desc._state != FileDesc::State::Normal) {
						Log(Warning) << "Unexpected file state found while searching directory tree" << std::endl;
						continue;
					}

					auto hash = HashFilenameAndPath(MakeStringSection(desc._naturalName));
					auto existing = std::find_if(
						_files.begin(), _files.end(),
						[hash](const SubFile& file) { return file._naturalNameHash == hash; });

					// When we multiple files with the same name, we'll always keep whichever we found
					// first. Normally this should only happen when 2 different filesystems have a file
					// with the same name, mounted at the same location.
					if (existing == _files.end()) {
						_files.emplace_back(SubFile{
							fsIdx, std::move(m), 
							std::move(desc), hash});
					} else {
						assert(existing->_filesystemIndex != fsIdx);
					}
				}
			}

			_foundFiles = true;
		}

		void FindDirectories()
		{
			if (_foundDirectories) return;
			assert(_directories.empty());

			for (unsigned fsIdx=0; fsIdx<_fileSystems.size(); ++fsIdx) {
				auto& fs = _fileSystems[fsIdx];
				if (!fs._pendingDirectories.empty()) {
					auto splitPath = MakeSplitPath(fs._pendingDirectories);
					auto dir = splitPath.GetSections()[0];
					auto hash = HashFilenameAndPath(dir);
					auto existing = std::find_if(
						_directories.begin(), _directories.end(),
						[hash](const SubDirectory& file) { return file._nameHash == hash; });
					if (existing == _directories.end())
						existing = _directories.emplace(existing, SubDirectory{dir.AsString(), hash});
					existing->_filesystemIndices.push_back(fsIdx);
					continue;
				}

				auto foundSubDirs = fs._fs->FindSubDirectories(MakeStringSection(fs._internalPoint));
				for (auto&m:foundSubDirs) {
					auto hash = HashFilenameAndPath(MakeStringSection(m));
					auto existing = std::find_if(
						_directories.begin(), _directories.end(),
						[hash](const SubDirectory& file) { return file._nameHash == hash; });
					if (existing == _directories.end())
						existing = _directories.emplace(existing, SubDirectory{m, hash});
					existing->_filesystemIndices.push_back(fsIdx);
					continue;
				}
			}

			_foundDirectories = true;
		}
	};

	auto FileSystemWalker::begin_directories() const -> DirectoryIterator
	{
		_pimpl->FindDirectories();
		return DirectoryIterator{this, 0};
	}

	auto FileSystemWalker::end_directories() const -> DirectoryIterator
	{
		_pimpl->FindDirectories();
		return DirectoryIterator{this, (unsigned)_pimpl->_directories.size()};
	}

	auto FileSystemWalker::begin_files() const -> FileIterator
	{
		_pimpl->FindFiles();
		return FileIterator{this, 0};
	}

	auto FileSystemWalker::end_files() const -> FileIterator
	{
		_pimpl->FindFiles();
		return FileIterator{this, (unsigned)_pimpl->_files.size()};
	}

	FileSystemWalker FileSystemWalker::RecurseTo(const std::basic_string<utf8>& subDirectory) const
	{
		std::vector<StartingFS> nextStep;

		auto hash = HashFilenameAndPath(MakeStringSection(subDirectory));

		_pimpl->FindDirectories();
		auto i = std::find_if(
			_pimpl->_directories.begin(), _pimpl->_directories.end(),
			[hash](const Pimpl::SubDirectory& file) { return file._nameHash == hash; });
		if (i == _pimpl->_directories.end())
			return {};

		for (auto fsIdx:i->_filesystemIndices) {
			auto& fs = _pimpl->_fileSystems[fsIdx];
			if (!fs._pendingDirectories.empty()) {
				auto splitPath = MakeSplitPath(fs._pendingDirectories);
				assert(HashFilenameAndPath(splitPath.GetSection(0)) == hash);

				// strip off the first part of the path name
				auto sections = splitPath.GetSections();
				utf8 newPending[MaxPath];
				SplitPath<utf8>(std::vector<SplitPath<utf8>::Section>{&sections[1], sections.end()}).Rebuild(newPending);
				nextStep.emplace_back(StartingFS{newPending, fs._internalPoint, fs._fs, fs._fsId});
			} else {
				auto newInternalPoint = fs._internalPoint;
				if (!newInternalPoint.empty()) newInternalPoint += u("/");
				newInternalPoint += subDirectory;
				nextStep.emplace_back(StartingFS{{}, newInternalPoint, fs._fs, fs._fsId});
			}
		}

		return FileSystemWalker{std::move(nextStep)};
	}

	FileSystemWalker::FileSystemWalker()
	{
		_pimpl = std::make_unique<Pimpl>(std::vector<StartingFS>{});
	}

	FileSystemWalker::FileSystemWalker(std::vector<StartingFS>&& fileSystems)
	{
		_pimpl = std::make_unique<Pimpl>(std::move(fileSystems));
	}

	FileSystemWalker::~FileSystemWalker() {}

	FileSystemWalker::FileSystemWalker(FileSystemWalker&& moveFrom)
	: _pimpl(std::move(moveFrom._pimpl))
	{
	}
	FileSystemWalker& FileSystemWalker::operator=(FileSystemWalker&& moveFrom)
	{
		_pimpl.reset();
		_pimpl = std::move(moveFrom._pimpl);
		return *this;
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	FileSystemWalker FileSystemWalker::DirectoryIterator::get() const
	{
		return _helper->RecurseTo(_helper->_pimpl->_directories[_idx]._name);
	}

	std::basic_string<utf8> FileSystemWalker::DirectoryIterator::Name() const
	{
		return _helper->_pimpl->_directories[_idx]._name;
	}

	FileSystemWalker::DirectoryIterator::DirectoryIterator(const FileSystemWalker* helper, unsigned idx)
	: _helper(helper), _idx(idx)
	{
	}

	auto FileSystemWalker::FileIterator::get() const -> Value
	{
		auto fsIdx = _helper->_pimpl->_files[_idx]._filesystemIndex;
		return {
			_helper->_pimpl->_files[_idx]._marker,
			_helper->_pimpl->_fileSystems[fsIdx]._fsId};
	}

	FileDesc FileSystemWalker::FileIterator::Desc() const
	{
		return _helper->_pimpl->_files[_idx]._desc;
	}

	FileSystemWalker::FileIterator::FileIterator(const FileSystemWalker* helper, unsigned idx)
	: _helper(helper), _idx(idx)
	{
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	std::unique_ptr<uint8[]> TryLoadFileAsMemoryBlock_TolerateSharingErrors(StringSection<char> sourceFileName, size_t* sizeResult)
	{
		std::unique_ptr<::Assets::IFileInterface> file;

        unsigned retryCount = 0;
        for (;;) {
            auto openResult = ::Assets::MainFileSystem::TryOpen(file, sourceFileName, "rb", FileShareMode::Read);
            if (openResult == ::Assets::IFileSystem::IOReason::Success) {
                file->Seek(0, FileSeekAnchor::End);
                size_t size = file->TellP();
                file->Seek(0);
                if (sizeResult) {
                    *sizeResult = size;
                }
                if (size) {
                    auto result = std::make_unique<uint8[]>(size);
                    file->Read(result.get(), 1, size);
                    return result;
                } else {
                    return nullptr;
                }
            }

            // If we get an access denied error, we're going to try a few more times, with short
            // delays in between. This can be important when hot reloading a resource -- because
            // we will get the filesystem update trigger on write, before an editor has closed
            // the file. During that window, we can get a sharing failure. We just have to yield
            // some CPU time and allow the editor to close the file.
            if (openResult != ::Assets::IFileSystem::IOReason::ExclusiveLock || retryCount >= 5) break;

            ++retryCount;
            Threading::Sleep(retryCount*retryCount*15);
        }

        // on missing file (or failed load), we return the equivalent of an empty file
        if (sizeResult) { *sizeResult = 0; }
        return nullptr;
	}

	Blob TryLoadFileAsBlob_TolerateSharingErrors(StringSection<char> sourceFileName)
	{
		std::unique_ptr<IFileInterface> file;
		unsigned retryCount = 0;
        for (;;) {
			auto openResult = MainFileSystem::TryOpen(file, sourceFileName, "rb", FileShareMode::Read);
			if (openResult == IFileSystem::IOReason::Success) {
				file->Seek(0, FileSeekAnchor::End);
				size_t size = file->TellP();
				file->Seek(0);
				if (size) {
					auto result = std::make_shared<std::vector<uint8_t>>(size);
					file->Read(result->data(), 1, size);
					return result;
				} else {
					return nullptr;
				}
			}

			// See similar logic in TryLoadFileAsMemoryBlock_TolerateSharingErrors for retrying
			// after getting a "ExclusiveLock" error result
			if (openResult != ::Assets::IFileSystem::IOReason::ExclusiveLock || retryCount >= 5) break;

            ++retryCount;
            Threading::Sleep(retryCount*retryCount*15);
		}

		return nullptr;
	}

	IFileInterface::~IFileInterface() {}
	IFileSystem::~IFileSystem() {}
	ISearchableFileSystem::~ISearchableFileSystem() {}
}
