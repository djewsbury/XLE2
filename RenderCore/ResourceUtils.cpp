// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ResourceUtils.h"
#include "ResourceDesc.h"
#include "Format.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/PtrUtils.h"
#include <algorithm>

namespace RenderCore
{
    bool operator==(const Box2D& lhs, const Box2D& rhs) 
    {
        return      lhs._left == rhs._left
                &&  lhs._top == rhs._top
                &&  lhs._right == rhs._right
                &&  lhs._bottom == rhs._bottom
                ;
    }

    unsigned CopyMipLevel(
        void* destination, size_t destinationDataSize, TexturePitches dstPitches,
        const TextureDesc& mipMapDesc,
        const SubResourceInitData& srcData)
    {
        // copy a single mip level in a single depth slice in a single array layer
        // todo -- we should expand this to support depth slices
        auto originalDest = destination;
        (void)originalDest;

        auto copiedBytes = 0u;
        if (dstPitches._rowPitch != srcData._pitches._rowPitch) {
            // unsigned sourceRowPitch;
            // unsigned rows;
            // if (isDXTCompressed) {
            //     sourceRowPitch = ByteCount(RoundBCDim(mipMapDesc._width), BlockCompDim, 1, 1, mipMapDesc._format);
            //     rows = (mipMapDesc._height + BlockCompDim - 1) / BlockCompDim;
            // } else {
            //     sourceRowPitch = ByteCount(mipMapDesc._width, 1, 1, 1, mipMapDesc._format);
            //     rows = mipMapDesc._height;
            // }

            auto compressionParam = GetCompressionParameters(mipMapDesc._format);

            auto sourceRowPitch = srcData._pitches._rowPitch;
            auto rows = mipMapDesc._height;
            rows /= compressionParam._blockHeight;  // in block compressed formats, we're dealing with 4x4 blocks of texels

            // prevent reading off the end of our src data, or writing past our dst data
            rows = std::min(rows, unsigned(srcData._data.size()/sourceRowPitch));
            rows = std::min(rows, unsigned(destinationDataSize/dstPitches._rowPitch));

            auto sourceData = srcData._data.begin();
            for (unsigned j = 0; j < rows; j++) {
                assert((size_t(destination) + sourceRowPitch - size_t(originalDest)) <= size_t(originalDest) + destinationDataSize);
                XlCopyMemory/*Align16*/(destination, sourceData, sourceRowPitch);
                sourceData = PtrAdd(sourceData, sourceRowPitch);
                destination = PtrAdd(destination, dstPitches._rowPitch);
            }

            copiedBytes = rows * sourceRowPitch;

        } else {
            int nPitch = ByteCount(mipMapDesc._width, 1, 1, 1, mipMapDesc._format);
            assert(srcData._data.size() % nPitch == 0); (void)nPitch;
            assert(size_t(destination) + srcData._data.size() <= size_t(originalDest) + destinationDataSize);
            XlCopyMemory/*Align16*/((uint8*)destination, srcData._data.begin(), srcData._data.size());
            copiedBytes = (unsigned)srcData._data.size();
        }

        return copiedBytes;
    }

    unsigned CopyMipLevel(
        void* destination, size_t destinationDataSize, TexturePitches dstPitches,
        const TextureDesc& dstDesc,
        const Box2D& dst2D,
        const SubResourceInitData& srcData)
    {
        // note -- we could simplify this by dividing widths and box dimensions by 4 when it's BlockCompressed
        //          (ie, just treat it as a smaller texture with larger pixels)
        auto compressionParam = GetCompressionParameters(dstDesc._format);
        assert(compressionParam._blockHeight != 0);

        Box2D adjustedBox = dst2D;
        adjustedBox._top = std::min(adjustedBox._top, int(dstDesc._height));
        adjustedBox._bottom = std::min(adjustedBox._bottom, int(dstDesc._height));

        auto* srcIterator = srcData._data.begin();
        if (adjustedBox._top < 0) {
            assert((-adjustedBox._top)%compressionParam._blockHeight == 0);
            srcIterator = PtrAdd(srcIterator, (-adjustedBox._top)/compressionParam._blockHeight * srcData._pitches._rowPitch);
            adjustedBox._top = 0;
        }

        if (adjustedBox._bottom <= adjustedBox._top) return 0 ;

        assert(adjustedBox._top%compressionParam._blockHeight == 0);

        auto bbp = BitsPerPixel(dstDesc._format);

        auto totalCopiedBytes = 0u;
        for (int r=adjustedBox._top; r<adjustedBox._bottom;) {
            auto left = dst2D._left;
            auto right = dst2D._right;
            auto* dstRowStart = PtrAdd(destination, r*dstPitches._rowPitch);
            auto* srcRowStart = srcIterator;

            if (left < 0) {
                assert((-left)%compressionParam._blockWidth==0);
                srcRowStart = PtrAdd(srcRowStart, (-left) / compressionParam._blockWidth * bbp * compressionParam._blockWidth * compressionParam._blockHeight / 8);
                left = 0;
            }
            right = std::min(right, int(dstDesc._width));

            if (left < right) {

                void* dstStart; size_t copyBytes;
                assert(left%compressionParam._blockWidth==0 && right%compressionParam._blockWidth == 0);
                dstStart = PtrAdd(dstRowStart, left / compressionParam._blockWidth * bbp * compressionParam._blockWidth * compressionParam._blockHeight / 8);
                copyBytes = (right - left) / compressionParam._blockWidth * bbp * compressionParam._blockWidth * compressionParam._blockHeight / 8;

                assert((size_t(dstStart) + copyBytes) <= (size_t(destination) + destinationDataSize));
                assert((size_t(srcRowStart) + copyBytes) <= (size_t(srcData._data.end())));
                XlCopyMemory(dstStart, srcRowStart, copyBytes);
                totalCopiedBytes += (unsigned)copyBytes;
            }

            r += compressionParam._blockHeight;
            srcIterator = PtrAdd(srcIterator, srcData._pitches._rowPitch);
        }

        return totalCopiedBytes;
    }

    TextureDesc  CalculateMipMapDesc(const TextureDesc& topMostMipDesc, unsigned mipMapIndex)
    {
        assert(mipMapIndex<topMostMipDesc._mipCount);
        auto compressionParam = GetCompressionParameters(topMostMipDesc._format);
        TextureDesc result = topMostMipDesc;
        result._width    = std::max(result._width  >> mipMapIndex, compressionParam._blockWidth);
        result._height   = std::max(result._height >> mipMapIndex, compressionParam._blockHeight);
        result._depth    = std::max(result._depth  >> mipMapIndex, 1u);
        result._mipCount -= uint8(mipMapIndex);
        return result;
    }
    
    unsigned ByteCount(unsigned width, unsigned height, unsigned depth, unsigned mipCount, Format format)
    {
        if (format == Format::Unknown)
            return 0;

        auto compressionParam = GetCompressionParameters(format);
        const auto bbp = BitsPerPixel(format);

        mipCount = std::max(mipCount, 1u);
        unsigned result = 0;
        if (compressionParam._blockWidth > 1 || compressionParam._blockHeight > 1) {
            for (unsigned mipIterator = 0; mipIterator < mipCount; ++mipIterator) {
                auto blockWidth = std::max((width + compressionParam._blockWidth - 1u) / compressionParam._blockWidth, 1u);
                auto blockHeight = std::max((height + compressionParam._blockHeight - 1u) / compressionParam._blockHeight, 1u);
                result += blockWidth * blockHeight * std::max(depth, 1u) * compressionParam._blockBytes;
                width >>= 1; height >>= 1; depth >>= 1;
            }
        } else {
            for (unsigned mipIterator = 0; mipIterator < mipCount; ++mipIterator) {
                result += std::max(width, 1u) * std::max(height, 1u) * std::max(depth, 1u) * bbp / 8u;
                width >>= 1; height >>= 1; depth >>= 1;
            }
        }

        return result;
    }

    unsigned ByteCount(const TextureDesc& tDesc)
    {
        return 
            ByteCount(tDesc._width, tDesc._height, tDesc._depth, tDesc._mipCount, tDesc._format) 
            * std::max(1u, unsigned(tDesc._arrayCount));
    }

    unsigned ByteCount(const ResourceDesc& desc)
    {
        if (desc._type == ResourceDesc::Type::LinearBuffer) {
            return desc._linearBufferDesc._sizeInBytes;
        } else if (desc._type == ResourceDesc::Type::Texture) {
            return ByteCount(desc._textureDesc);
        }
        return 0;
    }

    SubResourceOffset GetSubResourceOffset(
        const TextureDesc& tDesc, unsigned mipIndex, unsigned arrayLayer)
    {
        // Given the texture description, where do we expect to find the requested
        // subresource?
        // For a single arrayLayer, we will just have the biggest mipmap, followed by the full mipchain.
        // If there are more array layers, they will follow on afterwards.
        // So, each array layer is stored contiguously with its full array chain.
        //
        // Could also perhaps align the start of each array layer to some convenient boundary
        // (eg, 16 bytes?)

        assert(mipIndex < std::max(1u, (unsigned)tDesc._mipCount));
        assert(arrayLayer < std::max(1u, (unsigned)tDesc._arrayCount));

        SubResourceOffset mipOffset = { 0, 0, {} };
        if (tDesc._format == Format::Unknown)
            return mipOffset;

        auto compressionParam = GetCompressionParameters(tDesc._format);
        const auto bbp = BitsPerPixel(tDesc._format);

        auto width = tDesc._width, height = tDesc._height, depth = tDesc._depth;
        auto mipCount = std::max(1u, (unsigned)tDesc._mipCount);

        auto workingOffset = 0;
        if (compressionParam._blockWidth > 1 || compressionParam._blockHeight > 1) {
            for (unsigned mipIterator = 0; mipIterator < mipCount; ++mipIterator) {
                auto blockWidth = std::max((width + compressionParam._blockWidth - 1u) / compressionParam._blockWidth, 1u);
                auto blockHeight = std::max((height + compressionParam._blockHeight - 1u) / compressionParam._blockHeight, 1u);
                auto mipSize = blockWidth * blockHeight * std::max(depth, 1u) * compressionParam._blockBytes;
                
                if (mipIterator == mipIndex) {
                    mipOffset._offset = workingOffset;
                    mipOffset._size = mipSize;
                    mipOffset._pitches._rowPitch = blockWidth * bbp * 16u / 8u;
                    mipOffset._pitches._slicePitch = mipOffset._pitches._rowPitch * blockHeight;
                }
                
                workingOffset += mipSize;
                width >>= 1; height >>= 1; depth >>= 1;
            }
        } else {
            for (unsigned mipIterator = 0; mipIterator < mipCount; ++mipIterator) {
                auto mipSize = std::max(width, 1u) * std::max(height, 1u) * std::max(depth, 1u) * bbp / 8u;

                if (mipIterator == mipIndex) {
                    mipOffset._offset = workingOffset;
                    mipOffset._size = mipSize;
                    mipOffset._pitches._rowPitch = std::max(width, 1u) * bbp / 8u;
                    mipOffset._pitches._slicePitch = mipOffset._pitches._rowPitch * std::max(height, 1u);
                }

                workingOffset += mipSize;
                width >>= 1; height >>= 1; depth >>= 1;
            }
        }

        mipOffset._pitches._arrayPitch = workingOffset;
        mipOffset._offset += arrayLayer * mipOffset._pitches._arrayPitch;
        return mipOffset;
    }

    TexturePitches MakeTexturePitches(const TextureDesc& desc)
    {
        TexturePitches result = {0u, 0u, 0u};
        result._slicePitch = ByteCount(
            desc._width, desc._height, 1, 1, 
            desc._format);
            
            //  row pitch calculation is a little platform-specific here...
            //  (eg, DX9 and DX11 use different systems)
            //  Perhaps this could be moved into the platform interface layer
        auto compressionParam = GetCompressionParameters(desc._format);
        if (compressionParam._blockWidth > 1 || compressionParam._blockHeight > 1) {
            auto blockWidth = std::max((desc._width + compressionParam._blockWidth - 1u) / compressionParam._blockWidth, 1u);
            result._rowPitch = compressionParam._blockBytes * blockWidth;
        } else {
            result._rowPitch = ByteCount(
                desc._width, 1, 1, 1, 
				desc._format);
        }

        return result;
    }

    static const char* AsString(TextureDesc::Dimensionality dims)
    {
        switch (dims) {
        case TextureDesc::Dimensionality::Undefined: return "Undefined";
        case TextureDesc::Dimensionality::T1D: return "T1D";
        case TextureDesc::Dimensionality::T2D: return "T2D";
        case TextureDesc::Dimensionality::T3D: return "T3D";
        case TextureDesc::Dimensionality::CubeMap: return "CubeMap";
        }
        return "<<undefined>>";
    }

    std::ostream& operator<<(std::ostream& strm, const TextureDesc& textureDesc)
    {
        strm << textureDesc._width << " x " << textureDesc._height << " x " << textureDesc._depth;
        strm << ", " << AsString(textureDesc._format);
        strm << " (" << AsString(textureDesc._dimensionality) << ") Mips: " << (unsigned)textureDesc._mipCount;
        strm << ", ArrayCount: " << textureDesc._arrayCount << ", SampleCount: " << (unsigned)textureDesc._samples._sampleCount;
        return strm;
    }

    std::ostream& operator<<(std::ostream& strm, const ResourceDesc& resDesc)
    {
        if (resDesc._type == ResourceDesc::Type::LinearBuffer) {
            strm << "LinearBuffer (" << resDesc._name << ") size: " << resDesc._linearBufferDesc._sizeInBytes / 1024.f << "KiB";
            strm << ", BindFlags: " << resDesc._bindFlags;
            strm << ", CPUAccess: " << resDesc._cpuAccess;
            strm << ", GPUAccess: " << resDesc._gpuAccess;
            strm << ", AllocationRules: " << resDesc._allocationRules;
        } else if (resDesc._type == ResourceDesc::Type::Texture) {
            strm << "Texture (" << resDesc._name << ") [" << resDesc._textureDesc << "]";
            strm << ", BindFlags: " << resDesc._bindFlags;
            strm << ", CPUAccess: " << resDesc._cpuAccess;
            strm << ", GPUAccess: " << resDesc._gpuAccess;
            strm << ", AllocationRules: " << resDesc._allocationRules;
        }
        return strm;
    }
}
