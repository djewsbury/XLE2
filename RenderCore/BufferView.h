// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "RenderUtils.h"        // (for SharedPkt)
#include "IDevice_Forward.h"    // (for IResourcePtr)
#include "Format.h"

namespace RenderCore
{
    class ConstantBufferView
    {
    public:
        SharedPkt           _packet;
        const IResource*    _prebuiltBuffer;
        unsigned            _prebuiltRangeBegin = 0, _prebuiltRangeEnd = 0;     // (use 0, 0 for the entire buffer)
        // flags / desc ?

        ConstantBufferView() : _prebuiltBuffer(nullptr) {}
        ConstantBufferView(const SharedPkt& pkt) : _packet(pkt), _prebuiltBuffer(nullptr) {}
        ConstantBufferView(SharedPkt&& pkt) : _packet(std::move(pkt)), _prebuiltBuffer(nullptr) {}
        ConstantBufferView(const IResourcePtr& prebuilt, unsigned begin = 0, unsigned end = 0) : _prebuiltBuffer(prebuilt.get()), _prebuiltRangeBegin(begin), _prebuiltRangeEnd(end) {}
        ConstantBufferView(const IResource* prebuilt, unsigned begin = 0, unsigned end = 0) : _prebuiltBuffer(prebuilt), _prebuiltRangeBegin(begin), _prebuiltRangeEnd(end) {}
    };

    class VertexBufferView
    {
    public:
        const IResource*    _resource = nullptr;
        unsigned            _offset = 0;

        VertexBufferView(const IResource* res, unsigned offset = 0) : _resource(res), _offset(offset) {}
        VertexBufferView(const IResourcePtr& res, unsigned offset = 0) : _resource(res.get()), _offset(offset) {}
        VertexBufferView() {}
    };

    class IndexBufferView
    {
    public:
        const IResource*    _resource = nullptr;
        Format              _indexFormat = Format::R16_UINT;
        unsigned            _offset = 0;

        IndexBufferView(const IResource* res, Format indexFormat = Format::R16_UINT, unsigned offset = 0) : _resource(res), _indexFormat(indexFormat), _offset(offset) {}
        IndexBufferView(const IResourcePtr& res, Format indexFormat = Format::R16_UINT, unsigned offset = 0) : _resource(res.get()), _indexFormat(indexFormat), _offset(offset) {}
        IndexBufferView() {}
    };
}
