// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "FileUtils.h"
#include "../../Core/Exceptions.h"
#include "../../Conversion.h"
#include <stdio.h>

namespace Utility
{
    namespace RawFS
    {
        Exceptions::IOException::Reason BasicFile::TryOpen(const utf8 filename[], const char openMode[], FileShareMode::BitField shareMode) never_throws
        {
            assert(_file == nullptr);
            _file = fopen((const char*)filename, openMode);
            if (!_file)
                return Exceptions::IOException::Reason::FileNotFound;    // just assume file not found, but could actually be any of a number of issues...!
            return Exceptions::IOException::Reason::Success;
        }
        
        Exceptions::IOException::Reason BasicFile::TryOpen(const utf16 filename[], const char openMode[], FileShareMode::BitField shareMode) never_throws
        {
            utf8 buffer[MaxPath];
            Conversion::ConvertNullTerminated(buffer, dimof(buffer), filename);
            return TryOpen(buffer, openMode, shareMode);
        }

        BasicFile::BasicFile(const utf8 filename[], const char openMode[], FileShareMode::BitField shareMode)
        {
            assert(shareMode==0);       // (share mode not supported by this implementation)
            _file = fopen((const char*)filename, openMode);
            if (!_file) {
                Throw(::Exceptions::BasicLabel("Failure during file open. Probably missing file or bad privileges: (%s), openMode: (%s)", filename, openMode));
            }
        }
        
        BasicFile::BasicFile(const utf16 filename[], const char openMode[], FileShareMode::BitField shareMode)
        {
            utf8 buffer[MaxPath];
            Conversion::ConvertNullTerminated(buffer, dimof(buffer), filename);
            *this = BasicFile(buffer, openMode, shareMode);
        }
        
        BasicFile::BasicFile() {}
    }

    BasicFile::BasicFile(BasicFile&& moveFrom) never_throws
    {
        _file = moveFrom._file;
        moveFrom._file = nullptr;
    }

    BasicFile& BasicFile::operator=(BasicFile&& moveFrom) never_throws
    {
        if (_file != nullptr) {
            fclose((FILE*)_file);
        }
        _file = moveFrom._file;
        moveFrom._file = nullptr;
        return *this;
    }
    
    BasicFile::BasicFile(const BasicFile& copyFrom) never_throws
    {
        assert(0);      // cannot be implemented with the basic stdio types (without reopening the file from scratch)
    }
    
    BasicFile& BasicFile::operator=(const BasicFile& copyFrom) never_throws
    {
        assert(0);      // cannot be implemented with the basic stdio types (without reopening the file from scratch)
        return *this;
    }

    BasicFile::BasicFile()
    {
        _file = nullptr;
    }

    BasicFile::~BasicFile()
    {
        if (_file != nullptr) {
            fclose((FILE*)_file);
        }
    }
    
    size_t   BasicFile::Read(void *buffer, size_t size, size_t count) const never_throws
    {
        return fread(buffer, size, count, (FILE*)_file);
    }

    size_t   BasicFile::Write(const void *buffer, size_t size, size_t count) never_throws
    {
        return fwrite(buffer, size, count, (FILE*)_file);
    }

    size_t   BasicFile::Seek(size_t offset, FileSeekAnchor anchor) never_throws
    {
        return fseek((FILE*)_file, long(offset), int(anchor));
    }

    size_t   BasicFile::TellP() const never_throws
    {
        return ftell((FILE*)_file);
    }

    void    BasicFile::Flush() const never_throws
    {
        fflush((FILE*)_file);
    }

    uint64      BasicFile::GetSize() const never_throws
    {
        if (!_file) return 0;

        auto originalPosition = ftell((FILE*)_file);
        fseek((FILE*)_file, 0, SEEK_END);
        size_t size = TellP();
        fseek((FILE*)_file, originalPosition, SEEK_SET);

        return uint64(size);
    }
    
    bool        BasicFile::IsGood() const never_throws { return _file != nullptr; }
    
    FileTime	BasicFile::GetFileTime() const never_throws
    {
        assert(0);
        return FileTime(0);
    }
    
    namespace RawFS
    {
        Exceptions::IOException::Reason MemoryMappedFile::TryOpen(const utf8 filename[], uint64 size, const char openMode[], FileShareMode::BitField shareMode) never_throws
        {
            assert(0);      // not implemented yet
            return Exceptions::IOException::Reason::Complex;
        }
        
        Exceptions::IOException::Reason MemoryMappedFile::TryOpen(const utf16 filename[], uint64 size, const char openMode[], FileShareMode::BitField shareMode) never_throws
        {
            utf8 buffer[MaxPath];
            Conversion::ConvertNullTerminated(buffer, dimof(buffer), filename);
            return MemoryMappedFile::TryOpen(buffer, size, openMode, shareMode);
        }
        
        MemoryMappedFile::MemoryMappedFile(
                         const utf8 filename[], uint64 size,
                         const char openMode[],
                         FileShareMode::BitField shareMode)
        {
            assert(0);      // not implemented yet
        }
        
        MemoryMappedFile::MemoryMappedFile(
                         const utf16 filename[], uint64 size,
                         const char openMode[],
                         FileShareMode::BitField shareMode)
        {
            utf8 buffer[MaxPath];
            Conversion::ConvertNullTerminated(buffer, dimof(buffer), filename);
            *this = MemoryMappedFile(buffer, size, openMode, shareMode);
        }
        
        MemoryMappedFile::MemoryMappedFile() {}
    }
}


