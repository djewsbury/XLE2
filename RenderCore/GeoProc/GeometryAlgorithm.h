// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Types_Forward.h"
#include "../Format.h"
#include "../../Math/Matrix.h"
#include "../../Utility/IteratorUtils.h"

namespace RenderCore { namespace Assets { namespace GeoProc { class MeshDatabase; }}}
namespace RenderCore { namespace Assets { class VertexElement; }}
namespace RenderCore { class InputElementDesc; }

namespace RenderCore { namespace Assets { namespace GeoProc
{
    void GenerateNormalsAndTangents( 
        RenderCore::Assets::GeoProc::MeshDatabase& mesh, 
        unsigned normalMapTextureCoordinateSemanticIndex,
		float equivalenceThreshold,
        const void* rawIb = nullptr, size_t indexCount = 0, Format ibFormat = Format::Unknown);

    void Transform(
        RenderCore::Assets::GeoProc::MeshDatabase& mesh, 
        const Float4x4& transform);

    void RemoveRedundantBitangents(
        RenderCore::Assets::GeoProc::MeshDatabase& mesh);

    void CopyVertexElements(
        IteratorRange<void*> destinationBuffer,            size_t destinationVertexStride,
        IteratorRange<const void*> sourceBuffer,           size_t sourceVertexStride,
        IteratorRange<const Assets::VertexElement*> destinationLayout,
        IteratorRange<const Assets::VertexElement*> sourceLayout,
        IteratorRange<const uint32_t*> reordering);

    unsigned CalculateVertexSize(IteratorRange<const Assets::VertexElement*> layout);
    unsigned CalculateVertexSize(IteratorRange<const InputElementDesc*> layout);

}}}
