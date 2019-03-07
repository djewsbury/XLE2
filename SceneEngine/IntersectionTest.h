// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../RenderCore/IThreadContext_Forward.h"
#include "../RenderCore/Techniques/TechniqueUtils.h"        // for CameraDesc
#include "../Assets/AssetsCore.h"   // for rstring
#include "../Core/Types.h"
#include "../Math/Vector.h"
#include <memory>

namespace RenderCore { namespace Techniques { class CameraDesc; class TechniqueContext; }}
namespace RenderCore { class PresentationChainDesc; }

namespace SceneEngine
{
    class TerrainManager;
    class PlacementsEditor;
    class PlacementCellSet;

    /// <summary>Context for doing ray & box intersection test<summary>
    /// This context is intended for performing ray intersections for tools.
    /// Frequently we need to do "hit tests" and various projection and 
    /// unprojection operations. This context contains the minimal references
    /// to do this.
    /// Note that we need some camera information for LOD calculations. We could
    /// assume everything is at top LOD; but we will get a better match with 
    /// the rendered result if we take into account LOD. We even need viewport
    /// size -- because this can effect LOD as well. It's frustrating, but all 
    /// this is required!
    /// <seealso cref="IntersectionResolver" />
    class IntersectionTestContext
    {
    public:

            // "CameraDesc" member is only require for the following utility
            //  methods
        std::pair<Float3, Float3> CalculateWorldSpaceRay(Int2 screenCoord) const;
        static std::pair<Float3, Float3> CalculateWorldSpaceRay(
            const RenderCore::Techniques::CameraDesc& sceneCamera,
            Int2 screenCoord, UInt2 viewMins, UInt2 viewMaxs);
        
        Float2 ProjectToScreenSpace(const Float3& worldSpaceCoord) const;
        RenderCore::Techniques::CameraDesc GetCameraDesc() const;
        UInt2 GetViewportSize() const;

            // technique context & thread context is enough for most operations
        const RenderCore::Techniques::TechniqueContext& GetTechniqueContext() const { return *_techniqueContext.get(); }
        const std::shared_ptr<RenderCore::IThreadContext>& GetThreadContext() const;

        IntersectionTestContext(
            const std::shared_ptr<RenderCore::IThreadContext>& threadContext,
            const RenderCore::Techniques::CameraDesc& cameraDesc,
            const std::shared_ptr<RenderCore::PresentationChainDesc>& viewportContext,
            const std::shared_ptr<RenderCore::Techniques::TechniqueContext>& techniqueContext);
        ~IntersectionTestContext();
    protected:
        std::shared_ptr<RenderCore::IThreadContext> _threadContext;
        std::shared_ptr<RenderCore::PresentationChainDesc> _viewportContext;
        RenderCore::Techniques::CameraDesc _cameraDesc;
        std::shared_ptr<RenderCore::Techniques::TechniqueContext>  _techniqueContext;
    };

	class IntersectionTestContext2
    {
    public:
        std::pair<Float3, Float3> CalculateWorldSpaceRay(Int2 screenCoord) const;
        Float2 ProjectToScreenSpace(const Float3& worldSpaceCoord) const;

		RenderCore::Techniques::CameraDesc _cameraDesc;
		Int2 _viewportMins, _viewportMaxs;
		std::shared_ptr<RenderCore::Techniques::TechniqueContext> _techniqueContext;
    };

    class IIntersectionTester;

    /// <summary>Resolves ray and box intersections for tools</summary>
    /// This object can calculate intersections of basic primitives against
    /// the scene. This is intended for tools to perform interactive operations
    /// (like selecting objects in the scene).
    /// Note that much of the intersection math is performed on the GPU. This means
    /// that any intersection operation will probably involve a GPU synchronisation.
    /// This isn't intended to be used at runtime in a game, because it may cause
    /// frame-rate hitches. But for tools, it should not be an issue.
    class IntersectionTestScene
    {
    public:
        struct Type 
        {
            enum Enum 
            {
                Terrain = 1<<0, 
                Placement = 1<<1,

                Extra = 1<<6
            };
            typedef unsigned BitField;
        };
        
        class Result
        {
        public:
            Type::Enum						_type = Type::Enum(0);
            Float3							_worldSpaceCollision;
			std::pair<uint64_t, uint64_t>   _objectGuid = {0ull, 0ull};
            float							_distance = FLT_MAX;
            unsigned						_drawCallIndex = ~0u;
            uint64_t						_materialGuid = 0;
            ::Assets::rstring				_materialName;
            ::Assets::rstring				_modelName;
        };

        Result FirstRayIntersection(
            const IntersectionTestContext2& context,
            std::pair<Float3, Float3> worldSpaceRay,
            Type::BitField filter = ~Type::BitField(0)) const;

        std::vector<Result> FrustumIntersection(
            const IntersectionTestContext2& context,
            const Float4x4& worldToProjection,
            Type::BitField filter = ~Type::BitField(0)) const;

        Result UnderCursor(
            const IntersectionTestContext2& context,
            Int2 cursorPosition,
            Type::BitField filter = ~Type::BitField(0)) const;

        const std::shared_ptr<TerrainManager>& GetTerrain() const { return _terrainManager; }

        IntersectionTestScene(
            std::shared_ptr<TerrainManager> terrainManager = nullptr,
            std::shared_ptr<PlacementCellSet> placements = nullptr,
            std::shared_ptr<PlacementsEditor> placementsEditor = nullptr,
            std::initializer_list<std::shared_ptr<IIntersectionTester>> extraTesters = std::initializer_list<std::shared_ptr<IIntersectionTester>>());
        ~IntersectionTestScene();
    protected:
        std::shared_ptr<TerrainManager> _terrainManager;
        std::shared_ptr<PlacementCellSet> _placements;
        std::shared_ptr<PlacementsEditor> _placementsEditor;
        std::vector<std::shared_ptr<IIntersectionTester>> _extraTesters;
    };

    /// <summary>Resolves ray and box intersections for tools</summary>
    /// Interface class for extending IntersectionTestScene to support intersections
    /// against other types of objects.
    class IIntersectionTester
    {
    public:
        using Result = IntersectionTestScene::Result;
        virtual Result FirstRayIntersection(
            const IntersectionTestContext2& context,
            std::pair<Float3, Float3> worldSpaceRay) const = 0;

        virtual void FrustumIntersection(
            std::vector<Result>& results,
            const IntersectionTestContext2& context,
            const Float4x4& worldToProjection) const = 0;

        virtual ~IIntersectionTester();
    };
}

