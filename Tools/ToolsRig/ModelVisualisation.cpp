// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ModelVisualisation.h"
#include "VisualisationUtils.h"
#include "../../PlatformRig/InputTranslator.h"
#include "../../PlatformRig/Screenshot.h"
#include "../../SceneEngine/SceneParser.h"
#include "../../SceneEngine/LightDesc.h"
#include "../../SceneEngine/LightingParser.h"
#include "../../SceneEngine/RayVsModel.h"
#include "../../SceneEngine/IntersectionTest.h"
#include "../../SceneEngine/PreparedScene.h"
#include "../../SceneEngine/LightingParserContext.h"
#include "../../SceneEngine/LightingParserStandardPlugin.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../RenderOverlays/OverlayContext.h"
#include "../../RenderOverlays/HighlightEffects.h"
#include "../../RenderCore/IThreadContext.h"
#include "../../RenderCore/IDevice.h"
#include "../../RenderCore/Techniques/TechniqueUtils.h"
#include "../../RenderCore/Techniques/CommonResources.h"
#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Assets/SharedStateSet.h"
#include "../../RenderCore/Assets/ModelUtils.h"
#include "../../RenderCore/Format.h"
#include "../../RenderCore/Metal/DeviceContext.h"
#include "../../Assets/AssetUtils.h"
#include "../../Assets/Assets.h"
#include "../../ConsoleRig/Console.h"
#include "../../ConsoleRig/Log.h"
#include "../../Math/Transformations.h"
#include "../../Utility/HeapUtils.h"
#include "../../Utility/StringFormat.h"

#include "../../RenderCore/Assets/ModelCache.h"
#include "../../RenderCore/Assets/ModelRunTime.h"
#include "../../RenderCore/Assets/ModelImmutableData.h"

// #include "../../RenderCore/Metal/DeviceContext.h"
// #include "../../RenderCore/Metal/Shader.h"
#include "../../SceneEngine/SceneEngineUtils.h"
// #include "../../RenderCore/DX11/Metal/DX11Utils.h"

#if GFXAPI_ACTIVE == GFXAPI_DX11
    #include "../../RenderCore/Metal/ObjectFactory.h"
    #include "../../RenderCore/DX11/Metal/IncludeDX11.h"
#endif

#include <map>

namespace ToolsRig
{
    using RenderCore::Assets::ModelRenderer;
    using RenderCore::Assets::ModelScaffold;
    using RenderCore::Assets::MaterialScaffold;
    using RenderCore::Assets::SharedStateSet;
    using RenderCore::Assets::ModelCache;
	using RenderCore::Assets::ModelCacheModel;
    using RenderCore::Assets::SkeletonMachine;
    using RenderCore::Assets::DelayedDrawCallSet;

///////////////////////////////////////////////////////////////////////////////////////////////////

    static void RenderWithEmbeddedSkeleton(
        const RenderCore::Assets::ModelRendererContext& context,
        const ModelRenderer& model,
        const SharedStateSet& sharedStateSet,
        const ModelScaffold* scaffold)
    {
        if (scaffold) {
            model.Render(
                context, sharedStateSet, Identity<Float4x4>(), 
                RenderCore::Assets::EmbeddedSkeletonPose(*scaffold).GetMeshToModel());
        } else {
            model.Render(context, sharedStateSet, Identity<Float4x4>());
        }
    }

    static void PrepareWithEmbeddedSkeleton(
        DelayedDrawCallSet& dest, 
        const ModelRenderer& model,
        const SharedStateSet& sharedStateSet,
        const ModelScaffold* scaffold)
    {
        if (scaffold) {
            model.Prepare(
                dest, sharedStateSet, Identity<Float4x4>(), 
                RenderCore::Assets::EmbeddedSkeletonPose(*scaffold).GetMeshToModel());
        } else {
            model.Prepare(dest, sharedStateSet, Identity<Float4x4>());
        }
    }
    
    class ModelSceneParser : public VisSceneParser
    {
    public:
        void ExecuteScene(  
            RenderCore::IThreadContext& context,
			RenderCore::Techniques::ParsingContext& parserContext,
            SceneEngine::LightingParserContext& lightingParserContext, 
            const SceneEngine::SceneParseSettings& parseSettings,
            SceneEngine::PreparedScene& preparedPackets,
            unsigned techniqueIndex) const 
        {
            auto delaySteps = SceneEngine::AsDelaySteps(parseSettings._batchFilter);
            if (delaySteps.empty()) return;

            auto metalContext = RenderCore::Metal::DeviceContext::Get(context);

            RenderCore::Assets::SharedStateSet::CaptureMarker captureMarker;
            if (_sharedStateSet)
                captureMarker = _sharedStateSet->CaptureState(context, parserContext.GetStateSetResolver(), parserContext.GetStateSetEnvironment());

            using namespace RenderCore;
            Metal::ConstantBuffer drawCallIndexBuffer(
				Metal::GetObjectFactory(), 
				CreateDesc(BindFlag::ConstantBuffer, CPUAccess::WriteDynamic, GPUAccess::Read, LinearBufferDesc::Create(sizeof(unsigned)*4), "drawCallIndex"));
            metalContext->GetNumericUniforms(ShaderStage::Geometry).Bind(MakeResourceList(drawCallIndexBuffer));

            if (Tweakable("RenderSkinned", false)) {
                if (delaySteps[0] == RenderCore::Assets::DelayStep::OpaqueRender) {
                    auto preparedAnimation = _model->CreatePreparedAnimation();
                    RenderCore::Assets::SkinPrepareMachine prepareMachine(
                        *_modelScaffold, _modelScaffold->EmbeddedSkeleton());
                    RenderCore::Assets::AnimationState animState = {0.f, 0u};
                    prepareMachine.PrepareAnimation(context, *preparedAnimation, animState);
                    _model->PrepareAnimation(context, *preparedAnimation, prepareMachine.GetSkeletonBinding());

                    RenderCore::Assets::MeshToModel meshToModel(
                        *preparedAnimation, &prepareMachine.GetSkeletonBinding());

                    _model->Render(
                        RenderCore::Assets::ModelRendererContext(*metalContext, parserContext, techniqueIndex),
                        *_sharedStateSet, Identity<Float4x4>(), 
                        meshToModel, preparedAnimation.get());

                    if (Tweakable("RenderSkeleton", false)) {
                        prepareMachine.RenderSkeleton(
                            context, parserContext, 
                            animState, Identity<Float4x4>());
                    }
                }
            } else {
                const bool fillInStencilInfo = (_settings->_colourByMaterial != 0);

                for (auto i:delaySteps)
                    ModelRenderer::RenderPrepared(
                        RenderCore::Assets::ModelRendererContext(*metalContext.get(), parserContext, techniqueIndex),
                        *_sharedStateSet, _delayedDrawCalls, i,
                        [&metalContext, &drawCallIndexBuffer, &fillInStencilInfo](ModelRenderer::DrawCallEvent evnt)
                        {
                            if (fillInStencilInfo) {
                                // hack -- we just adjust the depth stencil state to enable the stencil buffer
                                //          no way to do this currently without dropping back to low level API
                                #if GFXAPI_ACTIVE == GFXAPI_DX11
                                    Metal::DepthStencilState dss(*metalContext);
                                    D3D11_DEPTH_STENCIL_DESC desc;
                                    dss.GetUnderlying()->GetDesc(&desc);
                                    desc.StencilEnable = true;
                                    desc.StencilWriteMask = 0xff;
                                    desc.StencilReadMask = 0xff;
                                    desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
                                    desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
                                    desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
                                    desc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
                                    desc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
                                    desc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
                                    desc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
                                    desc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
                                    auto newDSS = Metal::GetObjectFactory().CreateDepthStencilState(&desc);
                                    metalContext->GetUnderlying()->OMSetDepthStencilState(newDSS.get(), 1+evnt._drawCallIndex);
                                #endif
                            }

                            unsigned drawCallIndexB[4] = { evnt._drawCallIndex, 0, 0, 0 };
                            drawCallIndexBuffer.Update(*metalContext, drawCallIndexB, sizeof(drawCallIndexB));

                            metalContext->DrawIndexed(evnt._indexCount, evnt._firstIndex, evnt._firstVertex);
                        });
            }
        }

        bool HasContent(const SceneEngine::SceneParseSettings& parseSettings) const
        {
            auto delaySteps = AsDelaySteps(parseSettings._batchFilter);
            if (delaySteps.empty()) return false;
            for (auto i:delaySteps)
                if (!_delayedDrawCalls.IsEmpty(i))
                    return true;
            return false;
        }

        ModelSceneParser(
            const ModelVisSettings& settings,
            const std::shared_ptr<VisEnvSettings>& envSettings,
            ModelRenderer& model, const std::pair<Float3, Float3>& boundingBox, SharedStateSet& sharedStateSet,
            const ModelScaffold* modelScaffold = nullptr)
        : VisSceneParser(settings._camera, envSettings)
        , _model(&model), _boundingBox(boundingBox), _sharedStateSet(&sharedStateSet)
        , _settings(&settings), _modelScaffold(modelScaffold) 
        , _delayedDrawCalls(typeid(ModelRenderer).hash_code())
        {
            PrepareWithEmbeddedSkeleton(
                _delayedDrawCalls, *_model,
                *_sharedStateSet, modelScaffold);
            ModelRenderer::Sort(_delayedDrawCalls);
        }

        ~ModelSceneParser() {}

    protected:
        ModelRenderer* _model;
        SharedStateSet* _sharedStateSet;
        std::pair<Float3, Float3> _boundingBox;
        const ModelVisSettings* _settings;
        const ModelScaffold* _modelScaffold;
        DelayedDrawCallSet _delayedDrawCalls;
    };

    std::unique_ptr<SceneEngine::ISceneParser> CreateModelScene(const ModelCacheModel& model)
    {
        ModelVisSettings settings;
        *settings._camera = AlignCameraToBoundingBox(40.f, model._boundingBox);
        return std::make_unique<ModelSceneParser>(
            settings, std::make_shared<VisEnvSettings>(),
            *model._renderer, model._boundingBox, *model._sharedStateSet);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    static ModelCacheModel GetModel(
        ModelCache& cache,
        ModelVisSettings& settings)
    {
        std::vector<ModelCache::SupplementGUID> supplements;
        {
            const auto& s = settings._supplements;
            size_t offset = 0;
            for (;;) {
                auto comma = s.find_first_of(',', offset);
                if (comma == std::string::npos) comma = s.size();
                if (offset == comma) break;
                auto hash = ConstHash64FromString(AsPointer(s.begin()) + offset, AsPointer(s.begin()) + comma);
                supplements.push_back(hash);
                offset = comma;
            }
        }

        return cache.GetModel(
            MakeStringSection(settings._modelName), 
			MakeStringSection(settings._materialName),
            MakeIteratorRange(supplements),
            settings._levelOfDetail);
    }

    class ModelVisLayer::Pimpl
    {
    public:
        std::shared_ptr<ModelCache> _cache;
        std::shared_ptr<ModelVisSettings> _settings;
        std::shared_ptr<VisEnvSettings> _envSettings;

        ModelCacheModel GetModel() { return ToolsRig::GetModel(*_cache, *_settings); }
    };

    auto ModelVisLayer::GetInputListener() -> std::shared_ptr<IInputListener>
    {
        return nullptr;
    }

    void ModelVisLayer::RenderToScene(
        RenderCore::IThreadContext& context, 
        RenderCore::Techniques::ParsingContext& parserContext)
    {
        using namespace SceneEngine;

        auto model = _pimpl->GetModel();
		if (!model._renderer || !model._sharedStateSet)
			return;
        // assert(model._renderer && model._sharedStateSet);

        if (_pimpl->_settings->_pendingCameraAlignToModel) {
                // After the model is loaded, if we have a pending camera align,
                // we should reset the camera to the match the model.
                // We also need to trigger the change event after we make a change...
            *_pimpl->_settings->_camera = AlignCameraToBoundingBox(
                _pimpl->_settings->_camera->_verticalFieldOfView,
                model._boundingBox);
            _pimpl->_settings->_pendingCameraAlignToModel = false;
            _pimpl->_settings->_changeEvent.Trigger();
        }

        auto envSettings = _pimpl->_envSettings;
        if (!envSettings)
            envSettings = ::Assets::MakeAsset<VisEnvSettings>(
                MakeStringSection(_pimpl->_settings->_envSettingsFile))->TryActualize();
		if (!envSettings)
			envSettings = std::make_shared<VisEnvSettings>();

        ModelSceneParser sceneParser(
            *_pimpl->_settings, envSettings,
            *model._renderer, model._boundingBox, *model._sharedStateSet,
            model._model);
        sceneParser.Prepare();

		std::shared_ptr<SceneEngine::ILightingParserPlugin> lightingPlugins[] = {
			std::make_shared<SceneEngine::LightingParserStandardPlugin>()
		};
        auto qualSettings = SceneEngine::RenderingQualitySettings{
			UInt2(context.GetStateDesc()._viewportDimensions[0], context.GetStateDesc()._viewportDimensions[1]),
			SceneEngine::RenderingQualitySettings::LightingModel::Deferred,
			MakeIteratorRange(lightingPlugins)};

        auto& screenshot = Tweakable("Screenshot", 0);
        if (screenshot) {
            PlatformRig::TiledScreenshot(
                context, parserContext,
                sceneParser, sceneParser.GetCameraDesc(),
                qualSettings, UInt2(screenshot, screenshot));
            screenshot = 0;
        }

        LightingParser_ExecuteScene(
            context, parserContext, sceneParser, sceneParser.GetCameraDesc(),
            qualSettings);
    }

    void ModelVisLayer::RenderWidgets(
        RenderCore::IThreadContext& device, 
        RenderCore::Techniques::ParsingContext& parsingContext)
    {
    }

    void ModelVisLayer::SetActivationState(bool newState)
    {}

    void ModelVisLayer::SetEnvironment(std::shared_ptr<VisEnvSettings> envSettings)
    {
        _pimpl->_envSettings = envSettings;
    }

    ModelVisLayer::ModelVisLayer(
        std::shared_ptr<ModelVisSettings> settings,
        std::shared_ptr<ModelCache> cache) 
    {
        _pimpl = std::make_unique<Pimpl>();
        _pimpl->_settings = std::move(settings);
        _pimpl->_cache = std::move(cache);
    }

    ModelVisLayer::~ModelVisLayer() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    class VisualisationOverlay::Pimpl
    {
    public:
        std::shared_ptr<ModelCache> _cache;
        std::shared_ptr<ModelVisSettings> _settings;
        std::shared_ptr<VisMouseOver> _mouseOver;
    };
    
    void VisualisationOverlay::RenderToScene(
        RenderCore::IThreadContext& context, 
        RenderCore::Techniques::ParsingContext& parserContext)
    {
        using namespace RenderCore;
        if (_pimpl->_settings->_drawWireframe || _pimpl->_settings->_drawNormals) {

			AttachmentViewDesc v_mainColor {0u};
			AttachmentViewDesc v_mainDepth {2u};
			SubpassDesc mainPass { {v_mainColor}, v_mainDepth };
			FrameBufferDesc fbDesc{mainPass};
            Techniques::RenderPassInstance rpi{
				context, 
				parserContext.GetFrameBufferPool().BuildFrameBuffer(fbDesc, parserContext.GetNamedResources()),
				fbDesc, parserContext.GetNamedResources()};

            if (_pimpl->_settings->_drawWireframe) {

                CATCH_ASSETS_BEGIN
                    auto model = GetModel(*_pimpl->_cache, *_pimpl->_settings);
                    assert(model._renderer && model._sharedStateSet);

                    RenderCore::Assets::SharedStateSet::CaptureMarker captureMarker;
                    if (model._sharedStateSet)
                        captureMarker = model._sharedStateSet->CaptureState(context, parserContext.GetStateSetResolver(), parserContext.GetStateSetEnvironment());

                    const auto techniqueIndex = Techniques::TechniqueIndex::VisWireframe;

                    RenderWithEmbeddedSkeleton(
                        RenderCore::Assets::ModelRendererContext(context, parserContext, techniqueIndex),
                        *model._renderer, *model._sharedStateSet, model._model);
                CATCH_ASSETS_END(parserContext)
            }

            if (_pimpl->_settings->_drawNormals) {

                CATCH_ASSETS_BEGIN
                    auto model = GetModel(*_pimpl->_cache, *_pimpl->_settings);
                    assert(model._renderer && model._sharedStateSet);

                    RenderCore::Assets::SharedStateSet::CaptureMarker captureMarker;
                    if (model._sharedStateSet)
                        captureMarker = model._sharedStateSet->CaptureState(context, parserContext.GetStateSetResolver(), parserContext.GetStateSetEnvironment());

                    const auto techniqueIndex = Techniques::TechniqueIndex::VisNormals;

                    RenderWithEmbeddedSkeleton(
                        RenderCore::Assets::ModelRendererContext(context, parserContext, techniqueIndex),
                        *model._renderer, *model._sharedStateSet, model._model);
                CATCH_ASSETS_END(parserContext)
            }
        }

            //  Draw an overlay over the scene, 
            //  containing debugging / profiling information
        if (_pimpl->_settings->_colourByMaterial) {

            if (_pimpl->_settings->_colourByMaterial == 2 && !_pimpl->_mouseOver->_hasMouseOver) {
                return;
            }

            CATCH_ASSETS_BEGIN
                // auto metalContext = Metal::DeviceContext::Get(*context);

                RenderOverlays::HighlightByStencilSettings settings;

                    //  We need to query the model to build a lookup table between draw call index
                    //  and material binding index. The shader reads a draw call index from the 
                    //  stencil buffer and remaps that into a material index using this table.
                if (_pimpl->_cache) {
                    auto model = GetModel(*_pimpl->_cache, *_pimpl->_settings);
                    assert(model._renderer && model._sharedStateSet);

                    auto bindingVec = model._renderer->DrawCallToMaterialBinding();
                    unsigned t = 0;
                    settings._stencilToMarkerMap[t++] = UInt4(0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff);
                    for (auto i=bindingVec.cbegin(); i!=bindingVec.cend() && t < dimof(settings._stencilToMarkerMap); ++i, ++t) {
                        settings._stencilToMarkerMap[t] = UInt4((unsigned)*i, (unsigned)*i, (unsigned)*i, (unsigned)*i);
                    }

                    auto guid = _pimpl->_mouseOver->_materialGuid;
                    settings._highlightedMarker = UInt4(unsigned(guid), unsigned(guid), unsigned(guid), unsigned(guid));
                }

                {
					AttachmentViewDesc v_mainColor {0u};
					SubpassDesc mainPass { {v_mainColor} };
					FrameBufferDesc fbDesc{mainPass};
                    Techniques::RenderPassInstance rpi{
						context, 
						parserContext.GetFrameBufferPool().BuildFrameBuffer(fbDesc, parserContext.GetNamedResources()),
						fbDesc, parserContext.GetNamedResources()};
                    ExecuteHighlightByStencil(
                        context, parserContext.GetNamedResources(), 
                        settings, _pimpl->_settings->_colourByMaterial==2);
                }
            CATCH_ASSETS_END(parserContext)
        }
    }

    void VisualisationOverlay::RenderWidgets(
        RenderCore::IThreadContext& threadContext, 
        RenderCore::Techniques::ParsingContext& parserContext)
    {
            
    }

    auto VisualisationOverlay::GetInputListener() -> std::shared_ptr<IInputListener>
    { return nullptr; }

    void VisualisationOverlay::SetActivationState(bool) {}

    VisualisationOverlay::VisualisationOverlay(
        std::shared_ptr<ModelVisSettings> settings,
        std::shared_ptr<ModelCache> cache,
        std::shared_ptr<VisMouseOver> mouseOver)
    {
        _pimpl = std::make_unique<Pimpl>();
        _pimpl->_settings = std::move(settings);
        _pimpl->_cache = std::move(cache);
        _pimpl->_mouseOver = std::move(mouseOver);
    }

    VisualisationOverlay::~VisualisationOverlay() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    class SingleModelIntersectionResolver : public SceneEngine::IIntersectionTester
    {
    public:
        virtual Result FirstRayIntersection(
            const SceneEngine::IntersectionTestContext& context,
            std::pair<Float3, Float3> worldSpaceRay) const;

        virtual void FrustumIntersection(
            std::vector<Result>& results,
            const SceneEngine::IntersectionTestContext& context,
            const Float4x4& worldToProjection) const;

        SingleModelIntersectionResolver(
            std::shared_ptr<ModelVisSettings> settings,
            std::shared_ptr<ModelCache> cache);
        ~SingleModelIntersectionResolver();
    protected:
        std::shared_ptr<ModelVisSettings> _settings;
        std::shared_ptr<ModelCache> _cache;
    };

    auto SingleModelIntersectionResolver::FirstRayIntersection(
        const SceneEngine::IntersectionTestContext& context,
        std::pair<Float3, Float3> worldSpaceRay) const -> Result
    {
        using namespace SceneEngine;

		Log(Verbose) << "Disabling GPU ray intersection test because it causes device removal errors on DX11 currently" << std::endl;
		return {};

        auto model = GetModel(*_cache, *_settings);
		if (!model._renderer || !model._sharedStateSet)
			return Result();

        auto cam = context.GetCameraDesc();
        ModelIntersectionStateContext stateContext(
            ModelIntersectionStateContext::RayTest,
            context.GetThreadContext(), context.GetTechniqueContext(), 
            &cam);
        RenderCore::Techniques::ParsingContext parserContext(context.GetTechniqueContext());
        stateContext.SetRay(worldSpaceRay);

        {
            auto captureMarker = model._sharedStateSet->CaptureState(*context.GetThreadContext(), parserContext.GetStateSetResolver(), parserContext.GetStateSetEnvironment());
            RenderWithEmbeddedSkeleton(
                RenderCore::Assets::ModelRendererContext(*context.GetThreadContext(), parserContext, 6),
                *model._renderer, *model._sharedStateSet, model._model);
        }

        auto results = stateContext.GetResults();
        if (!results.empty()) {
            const auto& r = results[0];

            Result result;
            result._type = IntersectionTestScene::Type::Extra;
            result._worldSpaceCollision = 
                worldSpaceRay.first + r._intersectionDepth * Normalize(worldSpaceRay.second - worldSpaceRay.first);
            result._distance = r._intersectionDepth;
            result._drawCallIndex = r._drawCallIndex;
            result._materialGuid = r._materialGuid;
            result._materialName = _settings->_materialName;
            result._modelName = _settings->_modelName;

                // fill in the material guid if it wasn't correctly set by the shader
            if (result._materialGuid == ~0x0ull) {
                auto matBinding = model._renderer->DrawCallToMaterialBinding();
                if (result._drawCallIndex < matBinding.size())
                    result._materialGuid = matBinding[result._drawCallIndex];
            }
            return result;
        }

        return Result();
    }

    void SingleModelIntersectionResolver::FrustumIntersection(
        std::vector<Result>& results,
        const SceneEngine::IntersectionTestContext& context,
        const Float4x4& worldToProjection) const
    {}

    SingleModelIntersectionResolver::SingleModelIntersectionResolver(
        std::shared_ptr<ModelVisSettings> settings,
        std::shared_ptr<ModelCache> cache)
    : _settings(settings), _cache(cache)
    {}

    SingleModelIntersectionResolver::~SingleModelIntersectionResolver()
    {}

    std::shared_ptr<SceneEngine::IntersectionTestScene> CreateModelIntersectionScene(
        std::shared_ptr<ModelVisSettings> settings, std::shared_ptr<ModelCache> cache)
    {
        std::shared_ptr<SceneEngine::IIntersectionTester> resolver = 
            std::make_shared<SingleModelIntersectionResolver>(std::move(settings), std::move(cache));
        return std::shared_ptr<SceneEngine::IntersectionTestScene>(
            new SceneEngine::IntersectionTestScene(nullptr, nullptr, nullptr, { resolver }));
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    class MouseOverTrackingListener : public RenderOverlays::DebuggingDisplay::IInputListener
    {
    public:
        bool OnInputEvent(const RenderOverlays::DebuggingDisplay::InputSnapshot& evnt)
        {
            using namespace SceneEngine;

            auto cam = AsCameraDesc(*_camera);
            IntersectionTestContext testContext(
                _threadContext, cam, 
                std::make_shared<RenderCore::PresentationChainDesc>(
					PlatformRig::InputTranslator::s_hackWindowSize[0],
					PlatformRig::InputTranslator::s_hackWindowSize[1]),
                _techniqueContext);
            auto worldSpaceRay = testContext.CalculateWorldSpaceRay(
                evnt._mousePosition);
                
            auto intr = _scene->FirstRayIntersection(testContext, worldSpaceRay);
            if (intr._type != 0) {
                if (        intr._drawCallIndex != _mouseOver->_drawCallIndex
                        ||  intr._materialGuid != _mouseOver->_materialGuid
                        ||  !_mouseOver->_hasMouseOver) {

                    _mouseOver->_hasMouseOver = true;
                    _mouseOver->_drawCallIndex = intr._drawCallIndex;
                    _mouseOver->_materialGuid = intr._materialGuid;
                    _mouseOver->_changeEvent.Trigger();
                }
            } else {
                if (_mouseOver->_hasMouseOver) {
                    _mouseOver->_hasMouseOver = false;
                    _mouseOver->_changeEvent.Trigger();
                }
            }

            return false;
        }

        MouseOverTrackingListener(
            std::shared_ptr<VisMouseOver> mouseOver,
            std::shared_ptr<RenderCore::IThreadContext> threadContext,
            std::shared_ptr<RenderCore::Techniques::TechniqueContext> techniqueContext,
            std::shared_ptr<VisCameraSettings> camera,
            std::shared_ptr<SceneEngine::IntersectionTestScene> scene)
            : _mouseOver(std::move(mouseOver))
            , _threadContext(std::move(threadContext))
            , _techniqueContext(std::move(techniqueContext))
            , _camera(std::move(camera))
            , _scene(std::move(scene))
        {}
        MouseOverTrackingListener::~MouseOverTrackingListener() {}

    protected:
        std::shared_ptr<VisMouseOver> _mouseOver;
        std::shared_ptr<RenderCore::IThreadContext> _threadContext;
        std::shared_ptr<RenderCore::Techniques::TechniqueContext> _techniqueContext;
        std::shared_ptr<VisCameraSettings> _camera;
        std::shared_ptr<SceneEngine::IntersectionTestScene> _scene;
    };

    auto MouseOverTrackingOverlay::GetInputListener() -> std::shared_ptr<IInputListener>
    {
        return _inputListener;
    }

    void MouseOverTrackingOverlay::RenderToScene(
        RenderCore::IThreadContext&, 
        RenderCore::Techniques::ParsingContext&) {}
    
    void MouseOverTrackingOverlay::RenderWidgets(
        RenderCore::IThreadContext& threadContext, 
        RenderCore::Techniques::ParsingContext& parsingContext) 
    {
        if (!_mouseOver->_hasMouseOver || !_overlayFn) return;

        using namespace RenderOverlays::DebuggingDisplay;
        RenderOverlays::ImmediateOverlayContext overlays(
            threadContext, &parsingContext.GetNamedResources(),
            parsingContext.GetProjectionDesc());
        _overlayFn(overlays, *_mouseOver);
    }

    void MouseOverTrackingOverlay::SetActivationState(bool) {}

    MouseOverTrackingOverlay::MouseOverTrackingOverlay(
        std::shared_ptr<VisMouseOver> mouseOver,
        std::shared_ptr<RenderCore::IThreadContext> threadContext,
        std::shared_ptr<RenderCore::Techniques::TechniqueContext> techniqueContext,
        std::shared_ptr<VisCameraSettings> camera,
        std::shared_ptr<SceneEngine::IntersectionTestScene> scene,
        OverlayFn&& overlayFn)
    : _overlayFn(std::move(overlayFn))
    {
        _mouseOver = mouseOver;
        _inputListener = std::make_shared<MouseOverTrackingListener>(
            std::move(mouseOver),
            std::move(threadContext), std::move(techniqueContext), 
            std::move(camera), std::move(scene));
    }

    MouseOverTrackingOverlay::~MouseOverTrackingOverlay() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    void ChangeEvent::Trigger() 
    {
        for (auto i=_callbacks.begin(); i!=_callbacks.end(); ++i) {
            (*i)->OnChange();
        }
    }
    ChangeEvent::~ChangeEvent() {}

    ModelVisSettings::ModelVisSettings()
    {
        _modelName = "game/model/galleon/galleon.dae";
        _materialName = "game/model/galleon/galleon.material";
        _envSettingsFile = "defaultenv.txt:environment";
        _levelOfDetail = 0;
        _pendingCameraAlignToModel = true;
        _doHighlightWireframe = false;
        _highlightRay = std::make_pair(Zero<Float3>(), Zero<Float3>());
        _highlightRayWidth = 0.f;
        _colourByMaterial = 0;
        _camera = std::make_shared<VisCameraSettings>();
        _drawNormals = false;
        _drawWireframe = false;
    }

}

