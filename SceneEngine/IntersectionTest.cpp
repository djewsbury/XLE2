// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "IntersectionTest.h"
#include "RayVsModel.h"
#include "LightingParser.h"
#include "Terrain.h"
#include "PlacementsManager.h"
#include "SceneParser.h"

#include "../BufferUploads/DataPacket.h"
#include "../RenderCore/Metal/DeviceContext.h"
#include "../RenderCore/Metal/State.h"
#include "../RenderCore/RenderUtils.h"
#include "../RenderCore/IDevice.h"
#include "../RenderCore/Techniques/CommonBindings.h"
#include "../RenderCore/Techniques/ParsingContext.h"

#include "../Math/Transformations.h"
#include "../Math/Vector.h"
#include "../Math/ProjectionMath.h"
#include "../Utility/BitUtils.h"

namespace SceneEngine
{
    

////////////////////////////////////////////////////////////////////////////////////////////////////////

    static std::pair<Float3, bool> FindTerrainIntersection(
        RenderCore::IThreadContext& context,
        RenderCore::Techniques::ParsingContext& parserContext,
        TerrainManager& terrainManager,
        std::pair<Float3, Float3> worldSpaceRay)
    {
        CATCH_ASSETS_BEGIN
            TerrainManager::IntersectionResult intersections[8];
            unsigned intersectionCount = terrainManager.CalculateIntersections(
                intersections, dimof(intersections), worldSpaceRay, context, parserContext);

            if (intersectionCount > 0)
                return std::make_pair(intersections[0]._intersectionPoint, true);
        CATCH_ASSETS_END(parserContext)
        return std::make_pair(Float3(0,0,0), false);
    }

////////////////////////////////////////////////////////////////////////////////////////////////////////

    static std::pair<Float3, bool> FindTerrainIntersection(
        const IntersectionTestContext& intersectionContext,
        TerrainManager& terrainManager,
        std::pair<Float3, Float3> worldSpaceRay)
    {
            //  create a new device context and lighting parser context, and use
            //  this to find an accurate terrain collision.
        auto viewportDims = intersectionContext.GetViewportSize();
        RenderCore::Metal::ViewportDesc newViewport(
            0.f, 0.f, float(viewportDims[0]), float(viewportDims[1]), 0.f, 1.f);
        RenderCore::Metal::DeviceContext::Get(*intersectionContext.GetThreadContext())->Bind(newViewport);

        RenderCore::Techniques::ParsingContext parserContext(intersectionContext.GetTechniqueContext());
        LightingParser_SetupScene(*intersectionContext.GetThreadContext(), parserContext);
        LightingParser_SetGlobalTransform(
            *intersectionContext.GetThreadContext(), parserContext,
            BuildProjectionDesc(intersectionContext.GetCameraDesc(), viewportDims));

        return FindTerrainIntersection(*intersectionContext.GetThreadContext(), parserContext, terrainManager, worldSpaceRay);
    }

    static std::vector<ModelIntersectionStateContext::ResultEntry> PlacementsIntersection(
        RenderCore::Metal::DeviceContext& metalContext, ModelIntersectionStateContext& stateContext,
        SceneEngine::PlacementsRenderer& placementsRenderer, SceneEngine::PlacementCellSet& cellSet,
        SceneEngine::PlacementGUID object)
    {
            // Using the GPU, look for intersections between the ray
            // and the given model. Since we're using the GPU, we need to
            // get a device context. 
            //
            // We'll have to use the immediate context
            // because we want to get the result get right. But that means the
            // immediate context can't be doing anything else in another thread.
            //
            // This will require more complex threading support in the future!
        assert(metalContext.IsImmediate());

            //  We need to invoke the render for the given object
            //  now. Afterwards we can query the buffers for the result
        placementsRenderer.RenderFiltered(
            metalContext, stateContext.GetParserContext(), RenderCore::Techniques::TechniqueIndex::RayTest,
            cellSet, &object, &object+1);
        return stateContext.GetResults();
    }

////////////////////////////////////////////////////////////////////////////////////////////////////////

    auto IntersectionTestScene::FirstRayIntersection(
        const IntersectionTestContext& context,
        std::pair<Float3, Float3> worldSpaceRay,
        Type::BitField filter) const -> Result
    {
        Result result;

        auto metalContext = RenderCore::Metal::DeviceContext::Get(*context.GetThreadContext());

        if ((filter & Type::Terrain) && _terrainManager) {
            auto intersection = FindTerrainIntersection(
                context, *_terrainManager.get(), worldSpaceRay);
            if (intersection.second) {
                float distance = Magnitude(intersection.first - worldSpaceRay.first);
                if (distance < result._distance) {
                    result = Result();
                    result._type = Type::Terrain;
                    result._worldSpaceCollision = intersection.first;
                    result._distance = distance;
                }
            }
        }

        if ((filter & Type::Placement) && _placements && _placementsEditor) {
            auto intersections = _placementsEditor->GetManager()->GetIntersections();
            auto roughIntersection = 
                intersections->Find_RayIntersection(*_placements, worldSpaceRay.first, worldSpaceRay.second, nullptr);

            if (!roughIntersection.empty()) {

                    // we can improve the intersection by doing ray-vs-triangle tests
                    // on the roughIntersection geometry

                    //  we need to create a temporary transaction to get
                    //  at the information for these objects.
                auto trans = _placementsEditor->Transaction_Begin(
                    AsPointer(roughIntersection.cbegin()), AsPointer(roughIntersection.cend()));

                TRY
                {
                    float rayLength = Magnitude(worldSpaceRay.second - worldSpaceRay.first);

                    auto cam = context.GetCameraDesc();
                    ModelIntersectionStateContext stateContext(
                        ModelIntersectionStateContext::RayTest,
                        context.GetThreadContext(), context.GetTechniqueContext(), &cam);
                    stateContext.SetRay(worldSpaceRay);

                    // note --  we could do this all in a single render call, except that there
                    //          is no way to associate a low level intersection result with a specific
                    //          draw call.
                    auto count = trans->GetObjectCount();
                    for (unsigned c=0; c<count; ++c) {
                        auto guid = trans->GetGuid(c);
                        auto results = PlacementsIntersection(
                            *metalContext.get(), stateContext, 
                            *_placementsEditor->GetManager()->GetRenderer(), *_placements,
                            guid);

                        bool gotGoodResult = false;
                        unsigned drawCallIndex = 0;
                        uint64 materialGuid = 0;
                        float intersectionDistance = FLT_MAX;
                        for (auto i=results.cbegin(); i!=results.cend(); ++i) {
                            if (i->_intersectionDepth < intersectionDistance) {
                                intersectionDistance = i->_intersectionDepth;
                                drawCallIndex = i->_drawCallIndex;
                                materialGuid = i->_materialGuid;
                                gotGoodResult = true;
                            }
                        }

                        if (gotGoodResult && intersectionDistance < result._distance) {
                            result = Result();
                            result._type = Type::Placement;
                            result._worldSpaceCollision = LinearInterpolate(
                                worldSpaceRay.first, worldSpaceRay.second, 
                                intersectionDistance / rayLength);
                            result._distance = intersectionDistance;
                            result._objectGuid = guid;
                            result._drawCallIndex = drawCallIndex;
                            result._materialGuid = materialGuid;
                            result._materialName = trans->GetMaterialName(c, materialGuid);
                            result._modelName = trans->GetObject(c)._model;
                        }
                    }
                }
                CATCH(const ::Assets::Exceptions::RetrievalError&) {}
                CATCH_END

                trans->Cancel();
            }
        }

        unsigned firstExtraBit = IntegerLog2(uint32(Type::Extra));
        for (size_t c=0; c<_extraTesters.size(); ++c) {
            if (!(filter & 1<<uint32(c+firstExtraBit))) continue;
            TRY
            {
                auto res = _extraTesters[c]->FirstRayIntersection(context, worldSpaceRay);
                if (res._distance >= 0.f && res._distance <result._distance) {
                    result = res;
                    result._type = (Type::Enum)(1<<uint32(c+firstExtraBit));
                }
            } 
            CATCH(const ::Assets::Exceptions::RetrievalError&) {}
            CATCH_END
        }

        return result;
    }

    auto IntersectionTestScene::FrustumIntersection(
        const IntersectionTestContext& context,
        const Float4x4& worldToProjection,
        Type::BitField filter) const -> std::vector<Result>
    {
        std::vector<Result> result;

        auto metalContext = RenderCore::Metal::DeviceContext::Get(*context.GetThreadContext());

        if ((filter & Type::Placement) && _placements && _placementsEditor) {
            auto intersections = _placementsEditor->GetManager()->GetIntersections();
            auto roughIntersection = 
                intersections->Find_FrustumIntersection(*_placements, worldToProjection, nullptr);

                // we can improve the intersection by doing ray-vs-triangle tests
                // on the roughIntersection geometry

            if (!roughIntersection.empty()) {
                    //  we need to create a temporary transaction to get
                    //  at the information for these objects.
                auto trans = _placementsEditor->Transaction_Begin(
                    AsPointer(roughIntersection.cbegin()), AsPointer(roughIntersection.cend()));

                TRY
                {
                    auto cam = context.GetCameraDesc();
                    ModelIntersectionStateContext stateContext(
                        ModelIntersectionStateContext::FrustumTest,
                        context.GetThreadContext(), context.GetTechniqueContext(), &cam);
                    stateContext.SetFrustum(worldToProjection);

                    // note --  we could do this all in a single render call, except that there
                    //          is no way to associate a low level intersection result with a specific
                    //          draw call.
                    auto count = trans->GetObjectCount();
                    for (unsigned c=0; c<count; ++c) {
                        
                            //  We only need to test the triangles if the bounding box is 
                            //  intersecting the edge of the frustum... If the entire bounding
                            //  box is within the frustum, then we must have a hit
                        auto boundary = trans->GetLocalBoundingBox(c);
                        auto boundaryTest = TestAABB(
                            Combine(trans->GetObject(c)._localToWorld, worldToProjection),
                            boundary.first, boundary.second, RenderCore::Techniques::GetDefaultClipSpaceType());
                        if (boundaryTest == AABBIntersection::Culled) continue; // (could happen because earlier tests were on the world space bounding box)

                        auto guid = trans->GetGuid(c);
                        
                        bool isInside = boundaryTest == AABBIntersection::Within;
                        if (!isInside) {
                            auto results = PlacementsIntersection(
                                *metalContext.get(), stateContext, 
                                *_placementsEditor->GetManager()->GetRenderer(), *_placements, guid);
                            isInside = !results.empty();
                        }

                        if (isInside) {
                            Result r;
                            r._type = Type::Placement;
                            r._worldSpaceCollision = Float3(0.f, 0.f, 0.f);
                            r._distance = 0.f;
                            r._objectGuid = guid;
                            result.push_back(r);
                        }
                    }
                } 
                CATCH(const ::Assets::Exceptions::RetrievalError&) {}
                CATCH_END

                trans->Cancel();
            }
        }

        unsigned firstExtraBit = IntegerLog2(uint32(Type::Extra));
        for (size_t c=0; c<_extraTesters.size(); ++c) {
            if (!(filter & 1<<uint32(c+firstExtraBit))) continue;
            _extraTesters[c]->FrustumIntersection(result, context, worldToProjection);
        }

        return result;
    }

    auto IntersectionTestScene::UnderCursor(
        const IntersectionTestContext& context,
        Int2 cursorPosition, Type::BitField filter) const -> Result
    {
        return FirstRayIntersection(
            context, context.CalculateWorldSpaceRay(cursorPosition), filter);
    }

    IntersectionTestScene::IntersectionTestScene(
        std::shared_ptr<TerrainManager> terrainManager,
        std::shared_ptr<PlacementCellSet> placements,
        std::shared_ptr<PlacementsEditor> placementsEditor,
        std::initializer_list<std::shared_ptr<IIntersectionTester>> extraTesters)
    : _terrainManager(std::move(terrainManager))
    , _placements(std::move(placements))
    , _placementsEditor(std::move(placementsEditor))
    {
        for (size_t c=0; c<extraTesters.size(); ++c) 
            _extraTesters.push_back(std::move(extraTesters.begin()[c]));
    }

    IntersectionTestScene::~IntersectionTestScene()
    {}

////////////////////////////////////////////////////////////////////////////////////////////////////////
    
    IIntersectionTester::~IIntersectionTester() {}
    
    static Float4x4 CalculateWorldToProjection(const RenderCore::Techniques::CameraDesc& sceneCamera, float viewportAspect)
    {
        auto projectionMatrix = RenderCore::Techniques::Projection(sceneCamera, viewportAspect);
        return Combine(InvertOrthonormalTransform(sceneCamera._cameraToWorld), projectionMatrix);
    }

    std::pair<Float3, Float3> IntersectionTestContext::CalculateWorldSpaceRay(
        const RenderCore::Techniques::CameraDesc& sceneCamera,
        Int2 screenCoord, UInt2 viewportDims)
    {
        auto worldToProjection = CalculateWorldToProjection(sceneCamera, viewportDims[0] / float(viewportDims[1]));

        Float3 frustumCorners[8];
        CalculateAbsFrustumCorners(frustumCorners, worldToProjection, RenderCore::Techniques::GetDefaultClipSpaceType());
        Float3 cameraPosition = ExtractTranslation(sceneCamera._cameraToWorld);

        return BuildRayUnderCursor(
            screenCoord, frustumCorners, cameraPosition, 
            sceneCamera._nearClip, sceneCamera._farClip,
            std::make_pair(Float2(0.f, 0.f), Float2(float(viewportDims[0]), float(viewportDims[1]))));
    }

    std::pair<Float3, Float3> IntersectionTestContext::CalculateWorldSpaceRay(Int2 screenCoord) const
    {
        return CalculateWorldSpaceRay(GetCameraDesc(), screenCoord, GetViewportSize());
    }

    Float2 IntersectionTestContext::ProjectToScreenSpace(const Float3& worldSpaceCoord) const
    {
        auto viewport = GetViewportSize();
        auto worldToProjection = CalculateWorldToProjection(GetCameraDesc(), viewport[0] / float(viewport[1]));
        auto projCoords = worldToProjection * Expand(worldSpaceCoord, 1.f);

        return Float2(
            (projCoords[0] / projCoords[3] * 0.5f + 0.5f) * float(viewport[0]),
            (projCoords[1] / projCoords[3] * -0.5f + 0.5f) * float(viewport[1]));
    }

    UInt2 IntersectionTestContext::GetViewportSize() const
    {
        return UInt2(_viewportContext->_width, _viewportContext->_height);
    }

    const std::shared_ptr<RenderCore::IThreadContext>& IntersectionTestContext::GetThreadContext() const
    {
        return _threadContext;
    }

    RenderCore::Techniques::CameraDesc IntersectionTestContext::GetCameraDesc() const 
    {
        if (_sceneParser)
            return _sceneParser->GetCameraDesc();
        return _cameraDesc;
    }

    IntersectionTestContext::IntersectionTestContext(
        std::shared_ptr<RenderCore::IThreadContext> threadContext,
        const RenderCore::Techniques::CameraDesc& cameraDesc,
        std::shared_ptr<RenderCore::PresentationChainDesc> viewportContext,
        std::shared_ptr<RenderCore::Techniques::TechniqueContext> techniqueContext)
    : _threadContext(threadContext)
    , _cameraDesc(cameraDesc)
    , _techniqueContext(std::move(techniqueContext))
    , _viewportContext(std::move(viewportContext))
    {}

    IntersectionTestContext::IntersectionTestContext(
        std::shared_ptr<RenderCore::IThreadContext> threadContext,
        std::shared_ptr<SceneEngine::ISceneParser> sceneParser,
        std::shared_ptr<RenderCore::PresentationChainDesc> viewportContext,
        std::shared_ptr<RenderCore::Techniques::TechniqueContext> techniqueContext)
    : _threadContext(threadContext)
    , _sceneParser(sceneParser)
    , _techniqueContext(std::move(techniqueContext))
    , _viewportContext(std::move(viewportContext))
    {}

    IntersectionTestContext::~IntersectionTestContext() {}

}

