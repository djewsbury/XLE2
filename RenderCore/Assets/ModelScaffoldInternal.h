// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Types_Forward.h"
#include "../../Math/Vector.h"
#include "../../Math/Matrix.h"
#include "../../Assets/BlockSerializer.h"
#include "../../Utility/Streams/Serialization.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/StringUtils.h"

namespace RenderCore { class MiniInputElementDesc; }

namespace RenderCore { namespace Assets 
{
    typedef uint64_t MaterialGuid;

    #pragma pack(push)
    #pragma pack(1)

///////////////////////////////////////////////////////////////////////////////////////////////////
    //      g e o m e t r y         //

    class ModelCommandStream
    {
    public:
            //  "Geo calls" & "draw calls". Geo calls have 
            //  a vertex buffer and index buffer, and contain
            //  draw calls within them.
        class GeoCall
        {
        public:
            unsigned        _geoId;
            unsigned        _transformMarker;
            MaterialGuid*   _materialGuids;
            size_t          _materialCount;
            unsigned        _levelOfDetail;
        };

        class InputInterface
        {
        public:
            uint64_t*	_jointNames;
            size_t      _jointCount;
        };

            /////   -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-   /////
        const GeoCall&  GetGeoCall(size_t index) const;
        size_t          GetGeoCallCount() const;

        const GeoCall&  GetSkinCall(size_t index) const;
        size_t          GetSkinCallCount() const;

        auto            GetInputInterface() const -> const InputInterface& { return _inputInterface; }

        ~ModelCommandStream();
    private:
        GeoCall*        _geometryInstances;
        size_t          _geometryInstanceCount;
        GeoCall*        _skinControllerInstances;
        size_t          _skinControllerInstanceCount;
        InputInterface  _inputInterface;

        ModelCommandStream(const ModelCommandStream&) = delete;
        ModelCommandStream& operator=(const ModelCommandStream&) = delete;
    };

    inline auto         ModelCommandStream::GetGeoCall(size_t index) const -> const GeoCall&    { return _geometryInstances[index]; }
    inline size_t       ModelCommandStream::GetGeoCallCount() const                             { return _geometryInstanceCount; }
    inline auto         ModelCommandStream::GetSkinCall(size_t index) const -> const GeoCall&   { return _skinControllerInstances[index]; }
    inline size_t       ModelCommandStream::GetSkinCallCount() const                            { return _skinControllerInstanceCount; }

///////////////////////////////////////////////////////////////////////////////////////////////////

    class DrawCallDesc
    {
    public:
        unsigned    _firstIndex, _indexCount;
        unsigned    _firstVertex;
        unsigned    _subMaterialIndex;
        Topology	_topology;
    };

    class VertexElement
    {
    public:
        char            _semanticName[16];  // limited max size for semantic name (only alternative is to use a hash value)
        unsigned        _semanticIndex;
        Format			_nativeFormat;
        unsigned        _alignedByteOffset;

        VertexElement();
		VertexElement(const char name[], unsigned semanticIndex, Format nativeFormat, unsigned offset);
        VertexElement(const VertexElement&) never_throws;
        VertexElement& operator=(const VertexElement&) never_throws;
    };

    class GeoInputAssembly
    {
    public:
        SerializableVector<VertexElement>   _elements;
        unsigned                            _vertexStride = 0;

        uint64_t BuildHash() const;
    };

    class VertexData
    {
    public:
        GeoInputAssembly    _ia;
        unsigned            _offset, _size;
    };

    class IndexData
    {
    public:
        Format		 _format;
        unsigned    _offset, _size;
    };

///////////////////////////////////////////////////////////////////////////////////////////////////

    class RawGeometry
    {
    public:
        VertexData  _vb;
        IndexData   _ib;
        SerializableVector<DrawCallDesc>   _drawCalls;
    };

    class BoundSkinnedGeometry : public RawGeometry
    {
    public:

            //  The "RawGeometry" base class contains the 
            //  unanimated vertex elements (and draw calls for
            //  rendering the object as a whole)
        VertexData      _animatedVertexElements;
        VertexData      _skeletonBinding;

        Float4x4*       _bindShapeByInverseBindMatrices;
        size_t          _bindShapeByInverseBindMatrixCount;
        uint16*         _jointMatrices;         // (uint16 or uint8 for this array)
        size_t          _jointMatrixCount;

        DrawCallDesc*   _preskinningDrawCalls;
        size_t          _preskinningDrawCallCount;

        std::pair<Float3, Float3>   _localBoundingBox;

        ~BoundSkinnedGeometry();
    private:
        BoundSkinnedGeometry();
    };

    class SupplementGeo
    {
    public:
        unsigned    _geoId;
        VertexData  _vb;
    };

	unsigned BuildLowLevelInputAssembly(
        IteratorRange<InputElementDesc*> dst,
        IteratorRange<const VertexElement*> source,
        unsigned lowLevelSlot = 0);

	std::vector<MiniInputElementDesc> BuildLowLevelInputAssembly(
		IteratorRange<const VertexElement*> source);

///////////////////////////////////////////////////////////////////////////////////////////////////

    #pragma pack(pop)

	inline VertexElement::VertexElement()
	{
		_nativeFormat = Format(0); _alignedByteOffset = 0; _semanticIndex = 0;
		XlZeroMemory(_semanticName);
	}

	inline VertexElement::VertexElement(const char name[], unsigned semanticIndex, Format nativeFormat, unsigned offset)
	{
		XlZeroMemory(_semanticName);
		XlCopyString(_semanticName, name);
		_semanticIndex = semanticIndex;
		_nativeFormat = nativeFormat;
		_alignedByteOffset = offset;
	}

	inline VertexElement::VertexElement(const VertexElement& ele) never_throws
	{
		_nativeFormat = ele._nativeFormat; _alignedByteOffset = ele._alignedByteOffset; _semanticIndex = ele._semanticIndex;
		XlCopyMemory(_semanticName, ele._semanticName, sizeof(_semanticName));
	}

	inline VertexElement& VertexElement::operator=(const VertexElement& ele) never_throws
	{
		_nativeFormat = ele._nativeFormat; _alignedByteOffset = ele._alignedByteOffset; _semanticIndex = ele._semanticIndex;
		XlCopyMemory(_semanticName, ele._semanticName, sizeof(_semanticName));
		return *this;
	}
}}

template<>
inline void Serialize(
	Serialization::NascentBlockSerializer& outputSerializer,
	const RenderCore::Assets::GeoInputAssembly& ia)
{
	outputSerializer.SerializeRaw(ia._elements);
	Serialize(outputSerializer, ia._vertexStride);
}

template<>
inline void Serialize(
	Serialization::NascentBlockSerializer& outputSerializer,
	const RenderCore::Assets::IndexData& indexData)
{
	Serialize(outputSerializer, (unsigned&)indexData._format);
	Serialize(outputSerializer, indexData._offset);
	Serialize(outputSerializer, indexData._size);
}

template<>
inline void Serialize(
	Serialization::NascentBlockSerializer& outputSerializer,
	const RenderCore::Assets::VertexData& vertexData)
{
	Serialize(outputSerializer, vertexData._ia);
	Serialize(outputSerializer, vertexData._offset);
	Serialize(outputSerializer, vertexData._size);
}

template<>
inline void Serialize(
	Serialization::NascentBlockSerializer& outputSerializer,
	const RenderCore::Assets::DrawCallDesc& drawCall)
{
	outputSerializer.SerializeValue(drawCall._firstIndex);
	outputSerializer.SerializeValue(drawCall._indexCount);
	outputSerializer.SerializeValue(drawCall._firstVertex);
	outputSerializer.SerializeValue(drawCall._subMaterialIndex);
	outputSerializer.SerializeValue((unsigned&)drawCall._topology);
}

