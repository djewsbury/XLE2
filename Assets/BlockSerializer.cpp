// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#define _SCL_SECURE_NO_WARNINGS

#include "BlockSerializer.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/Streams/Serialization.h"
#include <vector>

namespace Serialization
{

        ////////////////////////////////////////////////////////////

    template<typename Type>
        void PushBack(std::vector<uint8>& buffer, const Type& type)
    {
        std::copy(  (const uint8*)&type, (const uint8*)PtrAdd(&type, sizeof(Type)), 
                    std::back_inserter(buffer));
    }

    void NascentBlockSerializer::PushBackPointer(size_t value)
    {
        PushBack(_memory, value);
    }

    void    NascentBlockSerializer::PushBackPlaceholder(SpecialBuffer::Enum specialBuffer)
    {
        if (specialBuffer == SpecialBuffer::String) {
            _memory.insert(_memory.end(), sizeof(std::string), 0);
        } else if (specialBuffer == SpecialBuffer::VertexBuffer || specialBuffer == SpecialBuffer::IndexBuffer) {
            assert(0);
        } else if (specialBuffer == SpecialBuffer::Vector) {
            _memory.insert(_memory.end(), sizeof(SerializableVector<unsigned>), 0);
        } else if (specialBuffer == SpecialBuffer::UniquePtr) {
            _memory.insert(_memory.end(), sizeof(std::unique_ptr<void, BlockSerializerDeleter<void>>), 0);
        } else if (specialBuffer == SpecialBuffer::Unknown) {
            PushBackPointer(0);
        }
    }

    void    NascentBlockSerializer::SerializeSpecialBuffer( 
                    SpecialBuffer::Enum specialBuffer, 
                    IteratorRange<const void*> range)
    {
        InternalPointer newPointer;
        newPointer._pointerOffset    = _memory.size();
        newPointer._subBlockOffset   = _trailingSubBlocks.size();
        newPointer._subBlockSize     = ptrdiff_t(range.end()) - ptrdiff_t(range.begin());
        newPointer._specialBuffer    = specialBuffer;
        _internalPointers.push_back(newPointer);

        std::copy((const uint8*)range.begin(), (const uint8*)range.end(), std::back_inserter(_trailingSubBlocks));

            //
            //      =<>=    Write blank space for this special buffer   =<>=
            //
        PushBackPlaceholder(specialBuffer);
    }

    void    NascentBlockSerializer::SerializeValue(uint8     value)
    {
        std::copy(  (const uint8*)&value, (const uint8*)PtrAdd(&value, sizeof(value)), 
                    std::back_inserter(_memory));
    }

    void    NascentBlockSerializer::SerializeValue(uint16    value)
    {
        std::copy(  (const uint8*)&value, (const uint8*)PtrAdd(&value, sizeof(value)), 
                    std::back_inserter(_memory));
    }

    void    NascentBlockSerializer::SerializeValue(uint32    value)
    {
        std::copy(  (const uint8*)&value, (const uint8*)PtrAdd(&value, sizeof(value)), 
                    std::back_inserter(_memory));
    }

    void    NascentBlockSerializer::SerializeValue(uint64    value)
    {
        std::copy(  (const uint8*)&value, (const uint8*)PtrAdd(&value, sizeof(value)), 
                    std::back_inserter(_memory));
    }

    void    NascentBlockSerializer::SerializeValue(float     value)
    {
        std::copy(  (const uint8*)&value, (const uint8*)PtrAdd(&value, sizeof(value)), 
                    std::back_inserter(_memory));
    }

    void    NascentBlockSerializer::AddPadding(unsigned sizeInBytes)
    {
        _memory.insert(_memory.end(), sizeInBytes, 0);
    }

    void NascentBlockSerializer::PushBackRaw(const void* data, size_t size)
    {
        std::copy(  (const uint8*)data, (const uint8*)PtrAdd(data, size), 
                    std::back_inserter(_memory));
    }

    void NascentBlockSerializer::PushBackRaw_SubBlock(const void* data, size_t size)
    {
        std::copy(  (const uint8*)data, (const uint8*)PtrAdd(data, size), 
                    std::back_inserter(_trailingSubBlocks));
    }

    void NascentBlockSerializer::RegisterInternalPointer(const InternalPointer& ptr)
    {
        _internalPointers.push_back(ptr);
    }

    void    NascentBlockSerializer::SerializeSubBlock(NascentBlockSerializer& subBlock, SpecialBuffer::Enum specialBuffer)
    {
            //
            //      Merge in the block we've just serialised, and write
            //      an internal pointer record for it.
            //

        InternalPointer ptr;
        ptr._pointerOffset   = _memory.size();
        ptr._subBlockOffset  = _trailingSubBlocks.size();
        ptr._subBlockSize    = subBlock._memory.size();
        ptr._specialBuffer   = specialBuffer;
        RegisterInternalPointer(ptr);

        PushBackPlaceholder(specialBuffer);

            //
            //      All of the internal pointer records should be merged in
            //      on an offset, also.
            //

        for (auto i=subBlock._internalPointers.cbegin(); i!=subBlock._internalPointers.cend(); ++i) {
            InternalPointer p = *i;
            if (p._pointerOffset & PtrFlagBit) {
                p._pointerOffset     = (p._pointerOffset&PtrMask) + subBlock._memory.size();
            }
            p._pointerOffset    +=  ptr._subBlockOffset;

                //
                //      Because the pointer itself is in the "subblock" part, we
                //      need tag it some way, to prevent confusion with pointers
                //      in the "memory" part. Let's just set it negative.
                //
            p._pointerOffset     |= PtrFlagBit;
            p._subBlockOffset    +=  ptr._subBlockOffset + subBlock._memory.size();
            RegisterInternalPointer(p);
        }

        PushBackRaw_SubBlock(AsPointer(subBlock._memory.begin()), subBlock._memory.size());
        PushBackRaw_SubBlock(AsPointer(subBlock._trailingSubBlocks.begin()), subBlock._trailingSubBlocks.size());
    }

    void NascentBlockSerializer::SerializeRawSubBlock(IteratorRange<const void*> range, SpecialBuffer::Enum specialBuffer)
    {
        auto size = size_t(range.end()) - size_t(range.begin());

        InternalPointer ptr;
        ptr._pointerOffset   = _memory.size();
        ptr._subBlockOffset  = _trailingSubBlocks.size();
        ptr._subBlockSize    = size;
        ptr._specialBuffer   = specialBuffer;
        RegisterInternalPointer(ptr);

        PushBackPlaceholder(specialBuffer);
        PushBackRaw_SubBlock(range.begin(), range.size());
    }

    void NascentBlockSerializer::SerializeValue(const std::string& value)
    {
        SerializeSpecialBuffer(SpecialBuffer::String, MakeIteratorRange(value));
    }

    class Header
    {
    public:
        uint64_t  _rawMemorySize;
        uint64_t  _internalPointerCount;
    };
    
    size_t      NascentBlockSerializer::Size() const
    {
        return sizeof(Header)
            + _memory.size()
            + _trailingSubBlocks.size()
            + _internalPointers.size() * sizeof(InternalPointer);
    }

    std::unique_ptr<uint8[]>      NascentBlockSerializer::AsMemoryBlock() const
    {
        std::unique_ptr<uint8[]> result = std::make_unique<uint8[]>(Size());

        ((Header*)result.get())->_rawMemorySize = _memory.size() + _trailingSubBlocks.size();
        ((Header*)result.get())->_internalPointerCount = _internalPointers.size();

        std::copy(  AsPointer(_memory.begin()), AsPointer(_memory.end()),
                    PtrAdd(result.get(), sizeof(Header)));

        std::copy(  AsPointer(_trailingSubBlocks.begin()), AsPointer(_trailingSubBlocks.end()),
                    PtrAdd(result.get(), _memory.size() + sizeof(Header)));

        InternalPointer* d = (InternalPointer*)PtrAdd(result.get(), _memory.size() + _trailingSubBlocks.size() + sizeof(Header));
        for (auto i=_internalPointers.cbegin(); i!=_internalPointers.cend(); ++i, ++d) {
            *d = *i;
                //      pointers in the subblock part are marked as negative... But what about zero? 
                //      It could be in the memory part, or the subblock part
            if (d->_pointerOffset & PtrFlagBit) {
                d->_pointerOffset = (d->_pointerOffset&PtrMask) + _memory.size();
            }
            d->_subBlockOffset += _memory.size();
        }

        return result;
    }

    NascentBlockSerializer::NascentBlockSerializer()
    {
    }

    NascentBlockSerializer::~NascentBlockSerializer()
    {

    }

    #undef new

    static void SetPtr(void* dst, uint64_t value)
    {
        ((uint32*)dst)[0] = (uint32)value;
        ((uint32*)dst)[1] = (uint32)(value >> 32ull);
    }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshorten-64-to-32"
    void        Block_Initialize(void* block, const void* base)
    {
        if (!base) { base = block; }

        const Header& h = *(const Header*)block;

        // Use uintptr_t even though this is of type (NascentBlockSerializer::InternalPointer*) to avoid
        // SIGBUS errors
        uintptr_t ptrTable = (uintptr_t)PtrAdd(block, ptrdiff_t(sizeof(Header)+h._rawMemorySize));

        for (unsigned c=0; c<h._internalPointerCount; ++c) {
             NascentBlockSerializer::InternalPointer ptr;
             // Use memcpy to avoid SIGBUS errors if the data isn't aligned
             memcpy(&ptr, (void*)(ptrTable + (sizeof(ptr) * c)), sizeof(ptr));
            if (ptr._specialBuffer == NascentBlockSerializer::SpecialBuffer::Unknown) {
                SetPtr(PtrAdd(block, ptrdiff_t(sizeof(Header)+ptr._pointerOffset)),
                       ptr._subBlockOffset + size_t(base) + sizeof(Header));
            } else if (ptr._specialBuffer == NascentBlockSerializer::SpecialBuffer::Vector) {
                uint64_t* o = (uint64_t*)PtrAdd(block, ptrdiff_t(sizeof(Header)+ptr._pointerOffset));
                SetPtr(&o[0], ptr._subBlockOffset + size_t(base) + sizeof(Header));
                SetPtr(&o[1], ptr._subBlockOffset + ptr._subBlockSize + size_t(base) + sizeof(Header));
                SetPtr(&o[2], 0);
            } else if (     ptr._specialBuffer == NascentBlockSerializer::SpecialBuffer::UniquePtr
                       ||   ptr._specialBuffer == NascentBlockSerializer::SpecialBuffer::VertexBuffer
                       ||   ptr._specialBuffer == NascentBlockSerializer::SpecialBuffer::IndexBuffer
                       ||   ptr._specialBuffer == NascentBlockSerializer::SpecialBuffer::String) {
                // these are deprecated types -- they need a stronger cross-platform implementation to work reliably
                assert(false);
            }
        }
    }
#pragma GCC diagnostic pop

    #if !defined(DEBUG_NEW)
        #define new DEBUG_NEW
    #endif

    const void*       Block_GetFirstObject(const void* blockStart)
    {
        return PtrAdd(blockStart, sizeof(Header));
    }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshorten-64-to-32"
    size_t          Block_GetSize(const void* block)
    {
        const Header& h = *(const Header*)block;
        return size_t(h._rawMemorySize + h._internalPointerCount * sizeof(NascentBlockSerializer::InternalPointer) + sizeof(Header));
    }
#pragma GCC diagnostic pop
    std::unique_ptr<uint8[]>  Block_Duplicate(const void* block)
    {
        size_t size = Block_GetSize(block);
        std::unique_ptr<uint8[]> result = std::make_unique<uint8[]>(size);
        XlCopyMemory(result.get(), block, size);
        return result;
    }



}

