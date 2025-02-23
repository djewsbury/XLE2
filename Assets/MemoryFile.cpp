// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "MemoryFile.h"
#include <algorithm>
#if !defined(EXCLUDE_Z_LIB)
    #include "../Foreign/zlib/zlib.h"
#endif
#include "../Utility/Conversion.h"

namespace Assets
{
	class MemoryFile : public IFileInterface
	{
	public:
		size_t			Write(const void * source, size_t size, size_t count) never_throws override;
		size_t			Read(void * destination, size_t size, size_t count) const never_throws override;
		ptrdiff_t		Seek(ptrdiff_t seekOffset, FileSeekAnchor) never_throws override;
		size_t			TellP() const never_throws override;

		size_t			GetSize() const never_throws override;
		FileDesc		GetDesc() const never_throws override;

		MemoryFile(const Blob& blob);
		~MemoryFile();
	private:
		Blob			_blob;
		mutable size_t	_ptr;
	};

	size_t			MemoryFile::Write(const void * source, size_t size, size_t count) never_throws
	{
		size_t finalSize = size * count;
		if (!_blob) _blob = std::make_shared<std::vector<uint8_t>>();
		_blob->insert(_blob->begin() + _ptr, (const uint8_t*)source, (const uint8_t*)PtrAdd(source, finalSize));
		_ptr += finalSize;
		return finalSize;
	}

	size_t			MemoryFile::Read(void * destination, size_t size, size_t count) const never_throws
	{
		if (!size || !count) return 0;
		if (!_blob) return 0;

		ptrdiff_t spaceLeft = _blob->size() - _ptr;
		ptrdiff_t maxCount = spaceLeft / size;
		ptrdiff_t finalCount = std::min(ptrdiff_t(count), maxCount);

		std::memcpy(
			destination,
			PtrAdd(AsPointer(_blob->begin()), _ptr),
			finalCount * size);
		_ptr += finalCount * size;
		return finalCount;
	}

	ptrdiff_t		MemoryFile::Seek(ptrdiff_t seekOffset, FileSeekAnchor anchor) never_throws
	{
		ptrdiff_t newPtr = 0;
		switch (anchor) {
		case FileSeekAnchor::Start:		newPtr = seekOffset; break;
		case FileSeekAnchor::Current:	newPtr = _ptr + seekOffset; break;
		case FileSeekAnchor::End:		newPtr = (_blob ? _blob->size() : 0) + seekOffset; break;
		default:
			assert(0);
		}
		newPtr = std::max(ptrdiff_t(0), newPtr);
		newPtr = std::min(ptrdiff_t(_blob->size()), newPtr);
		_ptr = newPtr;
		return _ptr;
	}

	size_t			MemoryFile::TellP() const never_throws
	{
		return _ptr;
	}

	size_t			MemoryFile::GetSize() const never_throws
	{
		return _blob->size();
	}

	FileDesc		MemoryFile::GetDesc() const never_throws
	{
		return FileDesc
			{
				u("<<in memory>>"), {},
				FileDesc::State::Normal,
				0, uint64_t(_blob ? _blob->size() : 0)
			};
	}

	MemoryFile::MemoryFile(const Blob& blob)
	: _blob(blob)
	, _ptr(0)
	{}

	MemoryFile::~MemoryFile()
	{
	}

	std::unique_ptr<IFileInterface> CreateMemoryFile(const Blob& blob)
	{
		return std::make_unique<MemoryFile>(blob);
	}

////////////////////////////////////////////////////////////////////////////////////////////////

	class ArchiveSubFile : public ::Assets::IFileInterface
	{
	public:
		size_t      Read(void *buffer, size_t size, size_t count) const never_throws override;
		size_t      Write(const void *buffer, size_t size, size_t count) never_throws override;
		ptrdiff_t	Seek(ptrdiff_t seekOffset, FileSeekAnchor) never_throws override;
		size_t      TellP() const never_throws override;

		size_t				GetSize() const never_throws override;
		::Assets::FileDesc	GetDesc() const never_throws override;

		ArchiveSubFile(
			const std::shared_ptr<MemoryMappedFile>& archiveFile,
			const IteratorRange<const void*> memoryRange);
		~ArchiveSubFile();
	protected:
		std::shared_ptr<MemoryMappedFile> _archiveFile;
		const void*			_dataStart;
		const void*			_dataEnd;
		mutable const void*	_tellp;
	};

	size_t      ArchiveSubFile::Read(void *buffer, size_t size, size_t count) const never_throws 
	{ 
		auto remainingSpace = ptrdiff_t(_dataEnd) - ptrdiff_t(_tellp);
		auto objectsToRead = (size_t)std::max(ptrdiff_t(0), std::min(remainingSpace / ptrdiff_t(size), ptrdiff_t(count)));
		std::memcpy(buffer, _tellp, objectsToRead*size);
		_tellp = PtrAdd(_tellp, objectsToRead*size);
		return objectsToRead;
	}

	size_t      ArchiveSubFile::Write(const void *buffer, size_t size, size_t count) never_throws 
	{ 
		Throw(::Exceptions::BasicLabel("BSAFile::Write() unimplemented"));
	}

	ptrdiff_t	ArchiveSubFile::Seek(ptrdiff_t seekOffset, FileSeekAnchor anchor) never_throws 
	{
		ptrdiff_t result = ptrdiff_t(_tellp) - ptrdiff_t(_dataStart);
		switch (anchor) {
		case FileSeekAnchor::Start: _tellp = PtrAdd(_dataStart, seekOffset); break;
		case FileSeekAnchor::Current: _tellp = PtrAdd(_tellp, seekOffset); break;
		case FileSeekAnchor::End: _tellp = PtrAdd(_dataEnd, -ptrdiff_t(seekOffset)); break;
		default:
			Throw(::Exceptions::BasicLabel("Unknown seek anchor in BSAFile::Seek(). Only Start/Current/End supported"));
		}
		return result;
	}

	size_t      ArchiveSubFile::TellP() const never_throws 
	{ 
		return ptrdiff_t(_tellp) - ptrdiff_t(_dataStart);
	}

	size_t		ArchiveSubFile::GetSize() const never_throws
	{
		return PtrDiff(_dataEnd, _dataStart);
	}

	::Assets::FileDesc ArchiveSubFile::GetDesc() const never_throws
	{
		Throw(::Exceptions::BasicLabel("BSAFile::GetDesc() unimplemented"));
	}

	ArchiveSubFile::ArchiveSubFile(const std::shared_ptr<MemoryMappedFile>& archiveFile, const IteratorRange<const void*> memoryRange)
	: _archiveFile(archiveFile)
	{
		_dataStart = memoryRange.begin();
		_dataEnd = memoryRange.end();
		_tellp = _dataStart;
	}

	ArchiveSubFile::~ArchiveSubFile() {}

	std::unique_ptr<IFileInterface> CreateSubFile(
		const std::shared_ptr<MemoryMappedFile>& archiveFile,
		const IteratorRange<const void*> memoryRange)
	{
		return std::make_unique<ArchiveSubFile>(archiveFile, memoryRange);
	}

////////////////////////////////////////////////////////////////////////////////////////////////

	class FileDecompressOnRead : public ::Assets::IFileInterface
	{
	public:
		size_t      Read(void *buffer, size_t size, size_t count) const never_throws override;
		size_t      Write(const void *buffer, size_t size, size_t count) never_throws override;
		ptrdiff_t	Seek(ptrdiff_t seekOffset, FileSeekAnchor) never_throws override;
		size_t      TellP() const never_throws override;

		size_t				GetSize() const never_throws override;
		::Assets::FileDesc	GetDesc() const never_throws override;

		FileDecompressOnRead(
			const std::shared_ptr<MemoryMappedFile>& archiveFile,
			const IteratorRange<const void*> memoryRange,
			size_t decompressedSize,
			unsigned fixedWindowSize);
		~FileDecompressOnRead();
	protected:
		std::shared_ptr<MemoryMappedFile> _archiveFile;
		#if !defined(EXCLUDE_Z_LIB)
            mutable z_stream	_stream;
        #endif
		size_t				_decompressedSize;
		mutable size_t		_tellp = 0;
	};

	size_t      FileDecompressOnRead::Write(const void *buffer, size_t size, size_t count) never_throws
	{
		Throw(::Exceptions::BasicLabel("BSAFileDecompressOnRead::Seek() unimplemented"));
	}

	ptrdiff_t	FileDecompressOnRead::Seek(ptrdiff_t seekOffset, FileSeekAnchor anchor) never_throws
	{
		// We can't easily seek, because the underlying stream is compressed. Seeking would require
		// decompressing the buffer as we go along.
		auto offset = seekOffset;
		if (anchor == FileSeekAnchor::Current) {
			offset += _tellp;
		} else if (anchor == FileSeekAnchor::End) {
			offset = _decompressedSize - seekOffset;
		} else {
			assert(anchor == FileSeekAnchor::Start);
		}
		if (offset == (ptrdiff_t)_tellp)
			return _tellp;

		if (size_t(offset) < _tellp)	//(we could reset and restart from the top here, but that would be inefficient)
			Throw(::Exceptions::BasicLabel("BSAFileDecompressOnRead::Seek() unimplemented"));

		// Move the pointer forward by just reading in dummy bytes
		auto dummyBuffer = std::make_unique<uint8_t[]>(offset-_tellp);
		Read(dummyBuffer.get(), 1, offset-_tellp);
		return _tellp;
	}

	size_t      FileDecompressOnRead::TellP() const never_throws
	{
		return _tellp;
	}

	size_t		FileDecompressOnRead::GetSize() const never_throws 
	{
		return _decompressedSize;
	}

	::Assets::FileDesc	FileDecompressOnRead::GetDesc() const never_throws
	{
		// Throw(::Exceptions::BasicLabel("BSAFileDecompressOnRead::GetDesc() unimplemented"));
		return {
			{}, {}, FileDesc::State::Normal, 0,
			_decompressedSize
		};
	}

	size_t      FileDecompressOnRead::Read(void *buffer, size_t size, size_t count) const never_throws
	{
        #if !defined(EXCLUDE_Z_LIB)
            _stream.next_out = (Bytef*)buffer;
            _stream.avail_out = (uInt)(size*count);
            auto err = inflate(&_stream, Z_SYNC_FLUSH);
            assert(err >= 0); (void) err;
			auto readSize = _stream.total_out - _tellp;
			_tellp = _stream.total_out;
            return readSize / size;
        #else
            return 0;
        #endif
	}

	FileDecompressOnRead::FileDecompressOnRead(
		const std::shared_ptr<MemoryMappedFile>& archiveFile,
		const IteratorRange<const void*> memoryRange,
		size_t decompressedSize,
		unsigned fixedWindowSize)
	: _archiveFile(archiveFile)
	, _decompressedSize(decompressedSize)
	{
        #if !defined(EXCLUDE_Z_LIB)
            _stream.next_in = (z_const Bytef *)memoryRange.begin();
            _stream.avail_in = (uInt)memoryRange.size();

            _stream.next_out = nullptr;
            _stream.avail_out = 0;

            _stream.zalloc = (alloc_func)0;
            _stream.zfree = (free_func)0;

			int err;
			if (fixedWindowSize) {
				err = inflateInit2(&_stream, -(signed)fixedWindowSize);
			} else {
				err = inflateInit(&_stream);
			}
            assert(err == Z_OK); (void) err;

			_tellp = 0;
        #else
            assert(0);
        #endif
	}

	FileDecompressOnRead::~FileDecompressOnRead() 
	{
        #if !defined(EXCLUDE_Z_LIB)
		    auto err = inflateEnd(&_stream);
		    assert(err == Z_OK); (void) err;
        #endif
	}

	std::unique_ptr<IFileInterface> CreateDecompressOnReadFile(
		const std::shared_ptr<MemoryMappedFile>& archiveFile,
		const IteratorRange<const void*> memoryRange,
		size_t decompressedSize,
		unsigned fixedWindowSize)
	{
		return std::make_unique<FileDecompressOnRead>(archiveFile, memoryRange, decompressedSize, fixedWindowSize);
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class FileSystem_Memory : public IFileSystem
	{
	public:
		virtual TranslateResult		TryTranslate(Marker& result, StringSection<utf8> filename);
		virtual TranslateResult		TryTranslate(Marker& result, StringSection<utf16> filename);

		virtual IOReason	TryOpen(std::unique_ptr<IFileInterface>& result, const Marker& uri, const char openMode[], FileShareMode::BitField shareMode);
		virtual IOReason	TryOpen(BasicFile& result, const Marker& uri, const char openMode[], FileShareMode::BitField shareMode);
		virtual IOReason	TryOpen(MemoryMappedFile& result, const Marker& uri, uint64 size, const char openMode[], FileShareMode::BitField shareMode);
		virtual IOReason	TryMonitor(const Marker& marker, const std::shared_ptr<IFileMonitor>& evnt);
		virtual	FileDesc	TryGetDesc(const Marker& marker);

		FileSystem_Memory(const std::unordered_map<std::string, Blob>& filesAndContents);
		~FileSystem_Memory();

	protected:
		std::unordered_map<std::string, Blob> _filesAndContents;

		struct MarkerStruct
		{
			size_t _fileIdx;
		};
	};

	auto FileSystem_Memory::TryTranslate(Marker& result, StringSection<utf8> filename) -> TranslateResult
	{
		if (filename.IsEmpty())
			return TranslateResult::Invalid;

		// Note -- case sensitive lookup here
		auto i = _filesAndContents.find(filename.Cast<char>().AsString());
		if (i == _filesAndContents.end())
			return TranslateResult::Invalid;

		result.resize(sizeof(MarkerStruct));
		auto* out = (MarkerStruct*)AsPointer(result.begin());
		out->_fileIdx = std::distance(_filesAndContents.begin(), i);
		return TranslateResult::Success;
	}

	auto FileSystem_Memory::TryTranslate(Marker& result, StringSection<utf16> filename) -> TranslateResult
	{
		if (filename.IsEmpty())
			return TranslateResult::Invalid;

		auto converted = Conversion::Convert<std::basic_string<utf8>>(filename);
		return TryTranslate(result, MakeStringSection(converted));
	}

	auto FileSystem_Memory::TryOpen(std::unique_ptr<IFileInterface>& result, const Marker& marker, const char openMode[], FileShareMode::BitField shareMode) -> IOReason
	{
		if (marker.size() < sizeof(MarkerStruct)) return IOReason::FileNotFound;

		const auto& m = *(const MarkerStruct*)AsPointer(marker.begin());
		if (m._fileIdx >= _filesAndContents.size())
			return IOReason::FileNotFound;

		auto i = _filesAndContents.begin();
		std::advance(i, m._fileIdx);
		result = CreateMemoryFile(i->second);
		return IOReason::Success;
	}

	auto FileSystem_Memory::TryOpen(BasicFile& result, const Marker& marker, const char openMode[], FileShareMode::BitField shareMode) -> IOReason
	{
		// Cannot open memory files in this way
		return IOReason::Invalid;
	}

	auto FileSystem_Memory::TryOpen(MemoryMappedFile& result, const Marker& marker, uint64 size, const char openMode[], FileShareMode::BitField shareMode) -> IOReason
	{
		if (marker.size() < sizeof(MarkerStruct)) return IOReason::FileNotFound;

		const auto& m = *(const MarkerStruct*)AsPointer(marker.begin());
		if (m._fileIdx >= _filesAndContents.size())
			return IOReason::FileNotFound;

		auto i = _filesAndContents.begin();
		std::advance(i, m._fileIdx);
		result = MemoryMappedFile(MakeIteratorRange(*i->second), MemoryMappedFile::CloseFn{});
		return IOReason::Success;
	}

	auto FileSystem_Memory::TryMonitor(const Marker& marker, const std::shared_ptr<IFileMonitor>& evnt) -> IOReason
	{
		return IOReason::Invalid;
	}

	FileDesc FileSystem_Memory::TryGetDesc(const Marker& marker)
	{
		if (marker.size() < sizeof(MarkerStruct)) return FileDesc{ std::basic_string<utf8>(), std::basic_string<utf8>(), FileDesc::State::DoesNotExist };;

		const auto& m = *(const MarkerStruct*)AsPointer(marker.begin());
		if (m._fileIdx >= _filesAndContents.size())
			return FileDesc{ std::basic_string<utf8>(), std::basic_string<utf8>(), FileDesc::State::DoesNotExist };;

		auto i = _filesAndContents.begin();
		std::advance(i, m._fileIdx);

		auto name = Conversion::Convert<std::basic_string<utf8>>(i->first);
		return FileDesc
			{
				name, name,
				FileDesc::State::Normal,
				0, (uint64_t)i->second->size()
			};
	}

	FileSystem_Memory::FileSystem_Memory(const std::unordered_map<std::string, Blob>& filesAndContents)
	: _filesAndContents(filesAndContents)
	{		
	}

	FileSystem_Memory::~FileSystem_Memory() {}


	std::shared_ptr<IFileSystem>	CreateFileSystem_Memory(const std::unordered_map<std::string, Blob>& filesAndContents)
	{
		return std::make_shared<FileSystem_Memory>(filesAndContents);
	}
}

