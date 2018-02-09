// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "HighlightEffects.h"
#include "../../RenderCore/Metal/DeviceContext.h"
#include "../../RenderCore/Metal/Shader.h"
#include "../../RenderCore/Metal/TextureView.h"
#include "../../RenderCore/Metal/Resource.h"
#include "../../RenderCore/Metal/ObjectFactory.h"
#include "../../RenderCore/Metal/InputLayout.h"
#include "../../RenderCore/Techniques/CommonResources.h"
#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../RenderCore/Techniques/CommonBindings.h"
#include "../../RenderCore/Format.h"
#include "../../Assets/Assets.h"
#include "../../ConsoleRig/ResourceBox.h"
#include "../../Utility/StringFormat.h"

namespace RenderOverlays
{
    using namespace RenderCore;

    const UInt4 HighlightByStencilSettings::NoHighlight = UInt4(0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff);

    HighlightByStencilSettings::HighlightByStencilSettings()
    {
        _outlineColor = Float3(1.5f, 1.35f, .7f);

        _highlightedMarker = NoHighlight;
        for (unsigned c=0; c<dimof(_stencilToMarkerMap); ++c) 
            _stencilToMarkerMap[c] = NoHighlight;
    }

    static void ExecuteHighlightByStencil(
        Metal::DeviceContext& metalContext,
        Metal::ShaderResourceView& stencilSrv,
        const HighlightByStencilSettings& settings,
        bool onlyHighlighted)
    {
		auto cbData = MakeIteratorRange(&settings, PtrAdd(&settings, sizeof(settings)));
        metalContext.BindPS(MakeResourceList(Metal::MakeConstantBuffer(Metal::GetObjectFactory(), cbData)));
        metalContext.BindPS(MakeResourceList(stencilSrv));
        metalContext.Bind(Techniques::CommonResources()._dssDisable);
        metalContext.Bind(Techniques::CommonResources()._blendAlphaPremultiplied);
        metalContext.Bind(Topology::TriangleStrip);
        metalContext.Unbind<Metal::BoundInputLayout>();

        auto desc = Metal::ExtractDesc(stencilSrv.GetResource());
        if (desc._type != ResourceDesc::Type::Texture) return;
        
        auto components = GetComponents(desc._textureDesc._format);
        bool stencilInput = 
                components == FormatComponents::DepthStencil
            ||  components == FormatComponents::Stencil;
                
        StringMeld<64, ::Assets::ResChar> params;
        params << "ONLY_HIGHLIGHTED=" << unsigned(onlyHighlighted);
        params << ";INPUT_MODE=" << (stencilInput?0:1);

        {
            auto& shader = ::Assets::GetAssetDep<Metal::ShaderProgram>(
                "xleres/basic2D.vsh:fullscreen:vs_*", 
                "xleres/Vis/HighlightVis.psh:HighlightByStencil:ps_*",
                (const ::Assets::ResChar*)params);
                
            metalContext.Bind(shader);
            metalContext.Draw(4);
        }

        {
            auto& shader = ::Assets::GetAssetDep<Metal::ShaderProgram>(
                "xleres/basic2D.vsh:fullscreen:vs_*", 
                "xleres/Vis/HighlightVis.psh:OutlineByStencil:ps_*",
                (const ::Assets::ResChar*)params);
                
            metalContext.Bind(shader);
            metalContext.Draw(4);
        }

        metalContext.UnbindPS<Metal::ShaderResourceView>(0, 1);
    }

    void ExecuteHighlightByStencil(
        RenderCore::IThreadContext& threadContext,
        Techniques::AttachmentPool& namedRes,
        const HighlightByStencilSettings& settings,
        bool onlyHighlighted)
    {
        auto stencilSrv = namedRes.GetSRV(
            RenderCore::Techniques::Attachments::MainDepthStencil,
            TextureViewDesc(
                {TextureViewDesc::Aspect::Stencil},
                TextureDesc::Dimensionality::Undefined, TextureViewDesc::All, TextureViewDesc::All,
                TextureViewDesc::Flags::JustStencil));
        if (!stencilSrv.IsGood()) return;

        auto metalContext = RenderCore::Metal::DeviceContext::Get(threadContext);
        ExecuteHighlightByStencil(*metalContext, stencilSrv, settings, onlyHighlighted);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    static const RenderCore::AttachmentName s_commonOffscreen = 15u;

    class HighlightShaders
    {
    public:
        class Desc {};

        const Metal::ShaderProgram* _drawHighlight;
        Metal::BoundUniforms _drawHighlightUniforms;

        const Metal::ShaderProgram* _drawShadow;
        Metal::BoundUniforms _drawShadowUniforms;

        const std::shared_ptr<::Assets::DependencyValidation>& GetDependencyValidation() const { return _validationCallback; }

        HighlightShaders(const Desc&);
    protected:
        std::shared_ptr<::Assets::DependencyValidation>  _validationCallback;
    };

    HighlightShaders::HighlightShaders(const Desc&)
    {
        //// ////
        _drawHighlight = &::Assets::GetAssetDep<Metal::ShaderProgram>(
            "xleres/basic2D.vsh:fullscreen:vs_*", 
            "xleres/effects/outlinehighlight.psh:main:ps_*");
        _drawHighlightUniforms = Metal::BoundUniforms(*_drawHighlight);
        _drawHighlightUniforms.BindConstantBuffer(Hash64("$Globals"), 0, 1);

        //// ////
        _drawShadow = &::Assets::GetAssetDep<Metal::ShaderProgram>(
            "xleres/basic2D.vsh:fullscreen:vs_*", 
            "xleres/effects/outlinehighlight.psh:main_shadow:ps_*");
        _drawShadowUniforms = Metal::BoundUniforms(*_drawShadow);
        _drawShadowUniforms.BindConstantBuffer(Hash64("ShadowHighlightSettings"), 0, 1);

        //// ////
        _validationCallback = std::make_shared<::Assets::DependencyValidation>();
        ::Assets::RegisterAssetDependency(_validationCallback, _drawHighlight->GetDependencyValidation());
    }

    class BinaryHighlight::Pimpl
    {
    public:
        IThreadContext*                 _threadContext;
        Techniques::AttachmentPool*	_namedRes;
        Techniques::RenderPassInstance  _rpi;

        Pimpl(IThreadContext& threadContext, Techniques::AttachmentPool& namedRes)
        : _threadContext(&threadContext), _namedRes(&namedRes) {}
        ~Pimpl() {}
    };

    BinaryHighlight::BinaryHighlight(
        IThreadContext& threadContext, 
        Techniques::AttachmentPool& namedRes)
    {
        using namespace RenderCore;
        _pimpl = std::make_unique<Pimpl>(threadContext, namedRes);

		AttachmentDesc d_commonOffscreen {
			Format::R8G8B8A8_UNORM, 1.f, 1.f, 0u,
			TextureViewDesc::ColorLinear,
			AttachmentDesc::DimensionsMode::OutputRelative, 
			AttachmentDesc::Flags::RenderTarget | AttachmentDesc::Flags::ShaderResource };

        namedRes.DefineAttachment(s_commonOffscreen, d_commonOffscreen);

		using namespace RenderCore::Techniques::Attachments;
        const bool doDepthTest = true;
		AttachmentViewDesc v_commonOffscreen { s_commonOffscreen, LoadStore::Clear, LoadStore::Retain };
		AttachmentViewDesc v_mainColor { PresentationTarget, LoadStore::Retain, LoadStore::Retain };
		AttachmentViewDesc v_mainDepth { doDepthTest?MainDepthStencil:~0u, LoadStore::Retain, LoadStore::Retain };
        FrameBufferDesc fbLayout {
			SubpassDesc {{v_commonOffscreen}, v_mainDepth}, 
			SubpassDesc {{v_mainColor}, SubpassDesc::Unused, {v_commonOffscreen}}
		};
        _pimpl->_rpi = Techniques::RenderPassInstance(
            threadContext, fbLayout, 0u, namedRes,
            {{MakeClearValue(0.f, 0.f, 0.f, 0.f)}});
    }

    void BinaryHighlight::FinishWithOutlineAndOverlay(RenderCore::IThreadContext& threadContext, Float3 outlineColor, unsigned overlayColor)
    {
        auto srv = _pimpl->_namedRes->GetSRV(s_commonOffscreen);
        assert(srv.IsGood());
        if (!srv.IsGood()) return;

        _pimpl->_rpi.NextSubpass();

        static Float3 highlightColO(1.5f, 1.35f, .7f);
        static unsigned overlayColO = 1;

        outlineColor = highlightColO;
        overlayColor = overlayColO;

        auto metalContext = Metal::DeviceContext::Get(threadContext);

        HighlightByStencilSettings settings;
        settings._outlineColor = outlineColor;
        for (unsigned c=1; c<dimof(settings._stencilToMarkerMap); ++c)
            settings._stencilToMarkerMap[c] = UInt4(overlayColor, overlayColor, overlayColor, overlayColor);
        ExecuteHighlightByStencil(
            *metalContext, srv, 
            settings, false);

        _pimpl->_rpi.End();
    }

    void BinaryHighlight::FinishWithOutline(RenderCore::IThreadContext& threadContext, Float3 outlineColor)
    {
            //  now we can render these objects over the main image, 
            //  using some filtering

        auto srv = _pimpl->_namedRes->GetSRV(s_commonOffscreen);
        assert(srv.IsGood());
        if (!srv.IsGood()) return;

        _pimpl->_rpi.NextSubpass();
        auto metalContext = Metal::DeviceContext::Get(threadContext);
        metalContext->BindPS(MakeResourceList(srv));

        struct Constants { Float3 _color; unsigned _dummy; } constants = { outlineColor, 0 };
        SharedPkt pkts[] = { MakeSharedPkt(constants) };

        auto& shaders = ConsoleRig::FindCachedBoxDep<HighlightShaders>(HighlightShaders::Desc());
        shaders._drawHighlightUniforms.Apply(
            *metalContext, 
            Metal::UniformsStream(), 
            Metal::UniformsStream(pkts, nullptr, dimof(pkts)));
        metalContext->Bind(*shaders._drawHighlight);
        metalContext->Bind(Techniques::CommonResources()._blendAlphaPremultiplied);
        metalContext->Bind(Techniques::CommonResources()._dssDisable);
        metalContext->Bind(Topology::TriangleStrip);
        metalContext->Draw(4);

        _pimpl->_rpi.End();
    }

    void BinaryHighlight::FinishWithShadow(RenderCore::IThreadContext& threadContext, Float4 shadowColor)
    {
        auto srv = _pimpl->_namedRes->GetSRV(s_commonOffscreen);
        assert(srv.IsGood());
        if (srv.IsGood()) return;

            //  now we can render these objects over the main image, 
            //  using some filtering

        _pimpl->_rpi.NextSubpass();
        auto metalContext = Metal::DeviceContext::Get(threadContext);
        metalContext->BindPS(MakeResourceList(srv));

        struct Constants { Float4 _shadowColor; } constants = { shadowColor };
        SharedPkt pkts[] = { MakeSharedPkt(constants) };

        auto& shaders = ConsoleRig::FindCachedBoxDep<HighlightShaders>(HighlightShaders::Desc());
        shaders._drawShadowUniforms.Apply(
            *metalContext, 
            Metal::UniformsStream(), 
            Metal::UniformsStream(pkts, nullptr, dimof(pkts)));
        metalContext->Bind(*shaders._drawShadow);
        metalContext->Bind(Techniques::CommonResources()._blendStraightAlpha);
        metalContext->Bind(Techniques::CommonResources()._dssDisable);
        metalContext->Bind(Topology::TriangleStrip);
        metalContext->Draw(4);

        _pimpl->_rpi.End();
    }

    BinaryHighlight::~BinaryHighlight() {}

}

