// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../RenderCore/IThreadContext_Forward.h"
#include "../../RenderCore/Metal/Forward.h"
#include "../../Math/Vector.h"
#include "../../Core/Types.h"
#include <utility>

namespace RenderCore { namespace Techniques { class ParsingContext; class SequencerContext; }}
namespace SceneEngine 
{ 
    class PlacementCellSet;
    class PlacementsRenderer;
    typedef std::pair<uint64, uint64> PlacementGUID;
}

namespace ToolsRig
{
    void Placements_RenderHighlight(
        RenderCore::IThreadContext& threadContext,
        RenderCore::Techniques::ParsingContext& parserContext,
        SceneEngine::PlacementsRenderer& renderer,
        const SceneEngine::PlacementCellSet& cellSet,
        const SceneEngine::PlacementGUID* filterBegin = nullptr,
        const SceneEngine::PlacementGUID* filterEnd = nullptr,
        uint64 materialGuid = ~0ull);

	void Placements_RenderHighlightWithOutlineAndOverlay(
        RenderCore::IThreadContext& threadContext,
        RenderCore::Techniques::ParsingContext& parserContext,
        SceneEngine::PlacementsRenderer& renderer,
        const SceneEngine::PlacementCellSet& cellSet,
		const SceneEngine::PlacementGUID* filterBegin = nullptr,
        const SceneEngine::PlacementGUID* filterEnd = nullptr,
        uint64 materialGuid = ~0ull);

    void Placements_RenderFiltered(
        RenderCore::IThreadContext& threadContext,
        RenderCore::Techniques::ParsingContext& parserContext,
        const RenderCore::Techniques::SequencerContext& sequencerTechnique,
        SceneEngine::PlacementsRenderer& renderer,
        const SceneEngine::PlacementCellSet& cellSet,
        const SceneEngine::PlacementGUID* filterBegin = nullptr,
        const SceneEngine::PlacementGUID* filterEnd = nullptr,
        uint64 materialGuid = ~0ull);

    void Placements_RenderShadow(
        RenderCore::IThreadContext& threadContext,
        RenderCore::Techniques::ParsingContext& parserContext,
        SceneEngine::PlacementsRenderer& renderer,
        const SceneEngine::PlacementCellSet& cellSet,
        const SceneEngine::PlacementGUID* filterBegin = nullptr,
        const SceneEngine::PlacementGUID* filterEnd = nullptr,
        uint64 materialGuid = ~0ull);

    void RenderCylinderHighlight(
        RenderCore::IThreadContext& threadContext, 
        RenderCore::Techniques::ParsingContext& parserContext,
        const Float3& centre, float radius);

    void DrawWorldSpaceCylinder(
        RenderCore::IThreadContext& threadContext, RenderCore::Techniques::ParsingContext& parserContext,
        Float3 origin, Float3 axis, float radius);

	enum class RectangleHighlightType { Tool, LockedArea };
    void RenderRectangleHighlight(
        RenderCore::IThreadContext& threadContext, 
        RenderCore::Techniques::ParsingContext& parserContext,
        const Float3& mins, const Float3& maxs,
		RectangleHighlightType type = RectangleHighlightType::Tool);

    void DrawQuadDirect(
        RenderCore::IThreadContext& threadContext, 
        const RenderCore::Metal::ShaderResourceView& srv, 
        Float2 screenMins, Float2 screenMaxs);
}

