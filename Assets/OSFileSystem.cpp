// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#define _SCL_SECURE_NO_WARNINGS		// (so we can use std::copy in this file)

#include "OSFileSystem.h"
#include "IFileSystem.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/Streams/PathUtils.h"
#include "../Utility/Streams/FileSystemMonitor.h"
#include "../Utility/Conversion.h"
#include "../Core/Exceptions.h"
#include <sstream>
#include <vector>
#include <regex>

namespace Assets
{
	using IOReason = Utility::Exceptions::IOException::Reason;

	/// <summary>Provides access to the underlying OS file system</summary>
	/// This type of file is a layer over the OS filesystem. The rules and behaviour are
	/// defined by the OS.
	class File_OS : public IFileInterface
	{
	public:
		size_t      Read(void *buffer, size_t size, size_t count) const never_throws override;
		size_t      Write(const void *buffer, size_t size, size_t count) never_throws override;
		ptrdiff_t	Seek(ptrdiff_t seekOffset, FileSeekAnchor) never_throws override;
		size_t      TellP() const never_throws override;
		void        Flush() const never_throws;

		size_t		GetSize() const never_throws override;
		FileDesc	GetDesc() const never_throws override;

		IOReason TryOpen(const utf8 filename[], const char openMode[], FileShareMode::BitField shareMode) never_throws;
		IOReason TryOpen(const utf16 filename[], const char openMode[], FileShareMode::BitField shareMode) never_throws;

		File_OS();
		~File_OS();

		File_OS(File_OS&& moveFrom) never_throws = default;
		File_OS& operator=(File_OS&& moveFrom) never_throws = default;
		File_OS(const File_OS& copyFrom) = default;
		File_OS& operator=(const File_OS& copyFrom) = default;
	private:
		RawFS::BasicFile _file;
		std::basic_string<utf8> _fn;
	};

	size_t      File_OS::Read(void *buffer, size_t size, size_t count) const never_throws { return _file.Read(buffer, size, count); }
	size_t      File_OS::Write(const void *buffer, size_t size, size_t count) never_throws { return _file.Write(buffer, size, count); }
	ptrdiff_t	File_OS::Seek(ptrdiff_t seekOffset, FileSeekAnchor anchor) never_throws { return _file.Seek(seekOffset, anchor); }
	size_t      File_OS::TellP() const never_throws { return _file.TellP(); }
	void        File_OS::Flush() const never_throws { _file.Flush(); }

	size_t		File_OS::GetSize() const never_throws 
	{
		auto originalPos = _file.TellP();
		const_cast<RawFS::BasicFile&>(_file).Seek(0, FileSeekAnchor::End);
		auto result = _file.TellP();
		const_cast<RawFS::BasicFile&>(_file).Seek(originalPos);
		return result;
	}

	FileDesc File_OS::GetDesc() const never_throws
	{
		if (!_file.IsGood()) 
			return FileDesc { std::basic_string<utf8>(), std::basic_string<utf8>(), FileDesc::State::DoesNotExist };

		auto size = _file.GetSize();
		auto ft = _file.GetFileTime();

		return FileDesc
		{ 
			_fn, _fn, FileDesc::State::Normal, 
			ft, size
		};
	}

	IOReason File_OS::TryOpen(const utf8 filename[], const char openMode[], FileShareMode::BitField shareMode) never_throws
	{
		auto res = _file.TryOpen(filename, openMode, shareMode);
        if (res == Utility::Exceptions::IOException::Reason::Success) {
            _fn = filename;
        }
        return res;
	}

	IOReason File_OS::TryOpen(const utf16 filename[], const char openMode[], FileShareMode::BitField shareMode) never_throws
	{
        auto res = _file.TryOpen(filename, openMode, shareMode);
        if (res == Utility::Exceptions::IOException::Reason::Success) {
            _fn = Conversion::Convert<std::basic_string<utf8>>(MakeStringSection(filename));
        }
        return res;
	}

	File_OS::File_OS() {}
	File_OS::~File_OS() {}

////////////////////////////////////////////////////////////////////////////////////////////////////

	class FileSystem_OS : public IFileSystem, public ISearchableFileSystem
	{
	public:
		virtual TranslateResult		TryTranslate(Marker& result, StringSection<utf8> filename);
		virtual TranslateResult		TryTranslate(Marker& result, StringSection<utf16> filename);

		virtual IOReason	TryOpen(std::unique_ptr<IFileInterface>& result, const Marker& uri, const char openMode[], FileShareMode::BitField shareMode);
		virtual IOReason	TryOpen(BasicFile& result, const Marker& uri, const char openMode[], FileShareMode::BitField shareMode);
		virtual IOReason	TryOpen(MemoryMappedFile& result, const Marker& uri, uint64 size, const char openMode[], FileShareMode::BitField shareMode);
		virtual IOReason	TryMonitor(const Marker& marker, const std::shared_ptr<IFileMonitor>& evnt);
		virtual	FileDesc	TryGetDesc(const Marker& marker);

        virtual std::vector<IFileSystem::Marker> FindFiles(
            StringSection<utf8> baseDirectory,
            StringSection<utf8> regexMatchPattern);
        virtual std::vector<std::basic_string<utf8>> FindSubDirectories(
            StringSection<utf8> baseDirectory);

		FileSystem_OS(StringSection<utf8> root, bool ignorePaths);
		~FileSystem_OS();

	protected:
		std::basic_string<utf8> _rootUTF8;
		std::basic_string<utf16> _rootUTF16;
		bool _ignorePaths;
	};

	auto FileSystem_OS::TryTranslate(Marker& result, StringSection<utf8> filename) -> TranslateResult
	{
		if (filename.IsEmpty())
			return TranslateResult::Invalid;

		// We're just going to translate this filename into a "marker" format that can be used
		// with file open. We don't have to do any other validation here -- and we don't want to use
		// any OS API functions here. We will also prepend the root directory at this point.
		// We're going to attempt to avoid changing the character type of "filename", so we will
		// store the marker in the same format as "filename";
		//
		// The format of marker is:
		// [\1|\2]     content    \0
		// 2bytes    ...bytes     1 * sizeof(char_type) bytes
		// where \1 indicates content is encoded by utf8 while \2 by utf16.
		//
		// Copying into another buffer here is required for two reasons:
		//		1. prepending the root dir
		//		2. adding a null terminator to the end of the string
		StringSection<utf8> name = filename;
		if (_ignorePaths) {
			auto splitName = MakeFileNameSplitter(filename);
			name = splitName.FileAndExtension();
		}
		result.resize(2 + (_rootUTF8.size() + name.Length() + 1) * sizeof(utf8));
		auto* out = AsPointer(result.begin());
		*(uint16*)out = 1;
		uint8* dst = (uint8*)PtrAdd(out, 2);
		std::copy(_rootUTF8.begin(), _rootUTF8.end(), dst);
		std::copy(name.begin(), name.end(), &dst[_rootUTF8.size()]);
		dst[_rootUTF8.size() + name.Length()] = 0;
		return TranslateResult::Success;
	}

	auto FileSystem_OS::TryTranslate(Marker& result, StringSection<utf16> filename) -> TranslateResult
	{
		if (filename.IsEmpty())
			return TranslateResult::Invalid;

		StringSection<utf16> name = filename;
		if (_ignorePaths) {
			auto splitName = MakeFileNameSplitter(filename);
			name = splitName.FileAndExtension();
		}
		result.resize(2 + (_rootUTF16.size() + name.Length() + 1) * sizeof(utf16));
		auto* out = AsPointer(result.begin());
		*(uint16*)out = 2;
		uint16* dst = (uint16*)PtrAdd(out, 2);
		std::copy(_rootUTF16.begin(), _rootUTF16.end(), dst);
		std::copy(name.begin(), name.end(), &dst[_rootUTF16.size()]);
		dst[_rootUTF16.size() + name.Length()] = 0;
		return TranslateResult::Success;
	}

	auto FileSystem_OS::TryOpen(std::unique_ptr<IFileInterface>& result, const Marker& marker, const char openMode[], FileShareMode::BitField shareMode) -> IOReason
	{
		if (marker.size() <= 2) return IOReason::FileNotFound;

		// "marker" always contains a null terminated string (important, because the underlying API requires
		// a null terminated string, not a begin/end pair)
		auto type = *(uint16*)AsPointer(marker.cbegin());
		if (type == 1) {
			auto r = std::make_unique<File_OS>();
			auto reason = r->TryOpen((const utf8*)PtrAdd(AsPointer(marker.begin()), 2), openMode, shareMode);
			result = std::move(r);
			return reason;
		} else if (type == 2) {
			auto r = std::make_unique<File_OS>();
			auto reason = r->TryOpen((const utf16*)PtrAdd(AsPointer(marker.begin()), 2), openMode, shareMode);
			result = std::move(r);
			return reason;
		}

		return IOReason::FileNotFound;
	}

	auto FileSystem_OS::TryOpen(BasicFile& result, const Marker& marker, const char openMode[], FileShareMode::BitField shareMode) -> IOReason
	{
		result = BasicFile();
		if (marker.size() <= 2) return IOReason::FileNotFound;

		// "marker" always contains a null terminated string (important, because the underlying API requires
		// a null terminated string, not a begin/end pair)
		auto type = *(uint16*)AsPointer(marker.cbegin());
		if (type == 1) {
			return ((RawFS::BasicFile&)result).TryOpen((const utf8*)PtrAdd(AsPointer(marker.begin()), 2), openMode, shareMode);
		} else if (type == 2) {
			return ((RawFS::BasicFile&)result).TryOpen((const utf16*)PtrAdd(AsPointer(marker.begin()), 2), openMode, shareMode);
		}

		return IOReason::FileNotFound;
	}

	auto FileSystem_OS::TryOpen(MemoryMappedFile& result, const Marker& marker, uint64 size, const char openMode[], FileShareMode::BitField shareMode) -> IOReason
	{
		result = MemoryMappedFile();
		if (marker.size() <= 2) return IOReason::FileNotFound;

		auto type = *(uint16*)AsPointer(marker.cbegin());
		if (type == 1) {
			return ((RawFS::MemoryMappedFile&)result).TryOpen((const utf8*)PtrAdd(AsPointer(marker.begin()), 2), size, openMode, shareMode);
		} else if (type == 2) {
			return ((RawFS::MemoryMappedFile&)result).TryOpen((const utf16*)PtrAdd(AsPointer(marker.begin()), 2), size, openMode, shareMode);
		}

		return IOReason::FileNotFound;
	}

	auto FileSystem_OS::TryMonitor(const Marker& marker, const std::shared_ptr<IFileMonitor>& evnt) -> IOReason
	{
		// note -- we can install monitors even for files and directories that don't exist
		//			when they are created, the monitor should start to take effect.
		auto type = *(uint16*)AsPointer(marker.cbegin());
		if (type == 1) {
			auto fn = MakeStringSection(
				(const utf8*)PtrAdd(AsPointer(marker.cbegin()), 2),
				(const utf8*)PtrAdd(AsPointer(marker.cend()), -(ptrdiff_t)sizeof(utf8)));
			auto split = MakeFileNameSplitter(fn);
			utf8 directoryName[MaxPath];
			MakeSplitPath(split.DriveAndPath()).Simplify().Rebuild(directoryName);
			AttachFileSystemMonitor(directoryName, split.FileAndExtension(), evnt);
			return IOReason::Success;
		} else if (type == 2) {
			auto fn = MakeStringSection(
				(const utf16*)PtrAdd(AsPointer(marker.cbegin()), 2),
				(const utf16*)PtrAdd(AsPointer(marker.cend()), -(ptrdiff_t)sizeof(utf16)));
			auto split = MakeFileNameSplitter(fn);
			utf16 directoryName[MaxPath];
			MakeSplitPath(split.DriveAndPath()).Simplify().Rebuild(directoryName);
			AttachFileSystemMonitor(directoryName, split.FileAndExtension(), evnt);
			return IOReason::Success;
		}
		return IOReason::Complex;
	}

	FileDesc FileSystem_OS::TryGetDesc(const Marker& marker)
	{
		// Given the filename in the "marker", try to find some basic information about the file.
		// In this version, we're not going to open the file. We'll just query the information from 
		// the filesystem directory table.
		auto type = *(uint16*)AsPointer(marker.cbegin());
		if (type == 1) {
			std::basic_string<utf8> str((const utf8*)PtrAdd(AsPointer(marker.begin()), 2));
			auto attrib = RawFS::TryGetFileAttributes(str.c_str());
			if (!attrib)
				return FileDesc { std::basic_string<utf8>(), std::basic_string<utf8>(), FileDesc::State::DoesNotExist };

			std::basic_string<utf8> mountedName((const utf8*)PtrAdd(AsPointer(marker.begin()), 2 + _rootUTF8.size()));

            auto attribv = *attrib;
			return FileDesc
				{
					str, mountedName, FileDesc::State::Normal,
					attribv._lastWriteTime, attribv._size
				};
		} else if (type == 2) {
			std::basic_string<ucs2> str((const ucs2*)PtrAdd(AsPointer(marker.begin()), 2));
			auto attrib = RawFS::TryGetFileAttributes(str.c_str());
			if (!attrib)
				return FileDesc { std::basic_string<utf8>(), std::basic_string<utf8>(), FileDesc::State::DoesNotExist };

			std::basic_string<ucs2> mountedName((const ucs2*)PtrAdd(AsPointer(marker.begin()), 2 + 2*_rootUTF16.size()));

			auto attribv = *attrib;
            return FileDesc
				{
					Conversion::Convert<std::basic_string<utf8>>(str), 
					Conversion::Convert<std::basic_string<utf8>>(mountedName),
					FileDesc::State::Normal,
					attribv._lastWriteTime, attribv._size
				};
		} 

		return FileDesc{ std::basic_string<utf8>(), std::basic_string<utf8>(), FileDesc::State::DoesNotExist };
	}

    std::vector<IFileSystem::Marker> FileSystem_OS::FindFiles(
        StringSection<utf8> baseDirectory,
        StringSection<utf8> regexMatchPattern)
    {
        std::string dir;
		if (!baseDirectory.IsEmpty()) {
			dir = Conversion::Convert<std::string>(_rootUTF8 + baseDirectory.AsString());
			if (baseDirectory[baseDirectory.size()-1] != '/' && baseDirectory[baseDirectory.size()-1] != '\\')
				dir += '/';
		} else {
			dir = Conversion::Convert<std::string>(_rootUTF8);
		}
		dir += "*";
        auto temp = RawFS::FindFiles(dir, RawFS::FindFilesFilter::File);
        std::vector<IFileSystem::Marker> res;
        res.reserve(temp.size());
        std::regex r(regexMatchPattern.Cast<char>().AsString());
        for (const auto&t:temp) {
            std::smatch match;
            if (std::regex_match(t, match, r)) {

                Marker marker;
                marker.resize(2 + (t.size()+ 1) * sizeof(utf8));
                auto* out = AsPointer(marker.begin());
                *(uint16*)out = 1;
                uint8* dst = (uint8*)PtrAdd(out, 2);
                std::copy(t.begin(), t.end(), dst);
                dst[t.size()] = 0;

                res.push_back(marker);
            }
        }
        return res;
    }

    std::vector<std::basic_string<utf8>> FileSystem_OS::FindSubDirectories(
        StringSection<utf8> baseDirectory)
    {
        std::string dir;
		if (!baseDirectory.IsEmpty()) {
			dir = Conversion::Convert<std::string>(_rootUTF8 + baseDirectory.AsString());
			if (baseDirectory[baseDirectory.size()-1] != '/' && baseDirectory[baseDirectory.size()-1] != '\\')
				dir += '/';
		} else {
			dir = Conversion::Convert<std::string>(_rootUTF8);
		}
		dir += "*";
        auto temp = RawFS::FindFiles(dir, RawFS::FindFilesFilter::Directory);
        std::vector<std::basic_string<utf8>> res;
        res.reserve(temp.size());
        auto rootSplit = MakeSplitPath(_rootUTF8);
        for (const auto&t:temp) {
            auto asUTF8 = Conversion::Convert<std::basic_string<utf8>>(t);
			auto fn = MakeFileNameSplitter(asUTF8).FileAndExtension();
			if (!XlEqString(fn, u(".")) && !XlEqString(fn, u("..")))
				res.push_back(fn.AsString());
        }
        return res;
    }

	FileSystem_OS::FileSystem_OS(StringSection<utf8> root, bool ignorePaths)
	: _ignorePaths(ignorePaths)
	{
		if (!root.IsEmpty()) {
			_rootUTF8 = root.AsString() + u("/");

			// primitive utf8 -> utf16 conversion
			// todo -- better implementation
            _rootUTF16.reserve(root.size() + 1);
			for (const auto*i = root.begin(); i<root.end();)
				_rootUTF16.push_back((utf16)utf8_nextchar(i, root.end()));
			_rootUTF16.push_back((utf16)'/');
		}
	}

	FileSystem_OS::~FileSystem_OS() {}


	std::shared_ptr<IFileSystem>	CreateFileSystem_OS(StringSection<utf8> root, bool flattenPaths)
	{
		return std::make_shared<FileSystem_OS>(root, flattenPaths);
	}

}

