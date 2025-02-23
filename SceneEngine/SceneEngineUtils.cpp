// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SceneEngineUtils.h"
#include "MetalStubs.h"
#include "Noise.h"

#include "../BufferUploads/ResourceLocator.h"
#include "../RenderCore/Techniques/CommonResources.h"
#include "../RenderCore/Techniques/RenderPass.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../RenderCore/Techniques/DeferredShaderResource.h"
#include "../RenderCore/Techniques/Services.h"
#include "../RenderCore/Metal/DeviceContext.h"
#include "../RenderCore/Metal/DeviceContextImpl.h"
#include "../RenderCore/Metal/Shader.h"
#include "../RenderCore/Metal/TextureView.h"
#include "../RenderCore/Metal/InputLayout.h"
#include "../RenderCore/Metal/ObjectFactory.h"
#include "../FixedFunctionModel/DelayedDrawCall.h"
#include "../RenderOverlays/Font.h"
#include "../Assets/Assets.h"
#include "../ConsoleRig/ResourceBox.h"
#include "../Utility/IteratorUtils.h"
#include "../xleres/FileList.h"

#include "../RenderCore/DX11/Metal/IncludeDX11.h"
#include "../RenderCore/DX11/Metal/DX11Utils.h" // for TextureDesc

namespace SceneEngine
{
    using namespace RenderCore;

	        //
        //      Reserve some states as global states
        //      These remain with some fixed value throughout
        //      the entire scene. Don't change the values bound
        //      here -- we are expecting these to be set once, and 
        //      remain bound
        //
        //          PS t14 -- normals fitting texture
        //          CS t14 -- normals fitting texture
        //          
        //          PS s0, s1, s2, s4 -- default sampler, clamping sampler, anisotropic wrapping sampler, point sampler
        //          VS s0, s1, s2, s4 -- default sampler, clamping sampler, anisotropic wrapping sampler, point sampler
        //          PS s6 -- samplerWrapU
        //
    void SetFrameGlobalStates(Metal::DeviceContext& context)
    {
        Metal::SamplerState 
            samplerDefault, 
            samplerClamp(FilterMode::Trilinear, AddressMode::Clamp, AddressMode::Clamp, AddressMode::Clamp), 
            samplerAnisotrophic(FilterMode::Anisotropic),
            samplerPoint(FilterMode::Point, AddressMode::Clamp, AddressMode::Clamp, AddressMode::Clamp),
            samplerWrapU(FilterMode::Trilinear, AddressMode::Wrap, AddressMode::Clamp);
        MetalStubs::GetGlobalNumericUniforms(context, ShaderStage::Pixel).Bind(RenderCore::MakeResourceList(samplerDefault, samplerClamp, samplerAnisotrophic, samplerPoint));
        MetalStubs::GetGlobalNumericUniforms(context, ShaderStage::Vertex).Bind(RenderCore::MakeResourceList(samplerDefault, samplerClamp, samplerAnisotrophic, samplerPoint));
        MetalStubs::GetGlobalNumericUniforms(context, ShaderStage::Pixel).Bind(RenderCore::MakeResourceList(6, samplerWrapU));

        const auto& normalsFittingResource = ::Assets::MakeAsset<RenderCore::Techniques::DeferredShaderResource>("xleres/DefaultResources/normalsfitting.dds:LT")->Actualize()->GetShaderResource();
		const auto& distintColors = ::Assets::MakeAsset<RenderCore::Techniques::DeferredShaderResource>("xleres/DefaultResources/distinctcolors.dds:T")->Actualize()->GetShaderResource();
        MetalStubs::GetGlobalNumericUniforms(context, ShaderStage::Pixel).Bind(RenderCore::MakeResourceList(14, normalsFittingResource, distintColors));
        MetalStubs::GetGlobalNumericUniforms(context, ShaderStage::Compute).Bind(RenderCore::MakeResourceList(14, normalsFittingResource));

            // perlin noise resources in standard slots
        auto& perlinNoiseRes = ConsoleRig::FindCachedBox2<PerlinNoiseResources>();
        MetalStubs::GetGlobalNumericUniforms(context, ShaderStage::Pixel).Bind(MakeResourceList(12, perlinNoiseRes._gradShaderResource, perlinNoiseRes._permShaderResource));

            // procedural scratch texture for scratches test
        // context.BindPS(MakeResourceList(18, Assets::GetAssetDep<Techniques::DeferredShaderResource>("xleres/scratchnorm.dds:L").GetShaderResource()));
        // context.BindPS(MakeResourceList(19, Assets::GetAssetDep<Techniques::DeferredShaderResource>("xleres/scratchocc.dds:L").GetShaderResource()));
    }

    void ReturnToSteadyState(Metal::DeviceContext& context)
    {
            //
            //      Change some frequently changed states back
            //      to their defaults.
            //      Most rendering operations assume these states
            //      are at their defaults.
            //

        context.Bind(Techniques::CommonResources()._dssReadWrite);
        context.Bind(Techniques::CommonResources()._blendOpaque);
        context.Bind(Techniques::CommonResources()._defaultRasterizer);
        context.Bind(Topology::TriangleList);
        context.GetNumericUniforms(ShaderStage::Vertex).Bind(RenderCore::MakeResourceList(Metal::ConstantBuffer(), Metal::ConstantBuffer(), Metal::ConstantBuffer(), Metal::ConstantBuffer(), Metal::ConstantBuffer()));
        context.GetNumericUniforms(ShaderStage::Pixel).Bind(RenderCore::MakeResourceList(Metal::ConstantBuffer(), Metal::ConstantBuffer(), Metal::ConstantBuffer(), Metal::ConstantBuffer(), Metal::ConstantBuffer()));
        MetalStubs::UnbindGeometryShader(context);
    }

    BufferUploads::IManager& GetBufferUploads()
    {
        return RenderCore::Techniques::Services::GetBufferUploads();
    }

    SavedTargets::SavedTargets(Metal::DeviceContext& context)
    {
		#if GFXAPI_ACTIVE == GFXAPI_DX11
			_oldViewportCount = dimof(_oldViewports);
			std::fill(_oldTargets, &_oldTargets[dimof(_oldTargets)], nullptr);
			context.GetUnderlying()->OMGetRenderTargets(dimof(_oldTargets), _oldTargets, &_oldDepthTarget);
			context.GetUnderlying()->RSGetViewports(&_oldViewportCount, (D3D11_VIEWPORT*)_oldViewports);
		#endif
    }

    SavedTargets::SavedTargets()
    {
		#if GFXAPI_ACTIVE == GFXAPI_DX11
			_oldViewportCount = 0;
			std::fill(_oldTargets, &_oldTargets[dimof(_oldTargets)], nullptr);
			_oldDepthTarget = nullptr;
		#endif
    }

    SavedTargets::SavedTargets(SavedTargets&& moveFrom) never_throws
    {
		#if GFXAPI_ACTIVE == GFXAPI_DX11
			_oldViewportCount = moveFrom._oldViewportCount; moveFrom._oldViewportCount = 0;
			for (unsigned c=0; c<MaxSimultaneousRenderTargetCount; ++c) {
				_oldTargets[c] = moveFrom._oldTargets[c];
				_oldViewports[c] = moveFrom._oldViewports[c];
				moveFrom._oldTargets[c] = nullptr;
				moveFrom._oldViewports[c] = Metal::ViewportDesc();
			}
			_oldDepthTarget = moveFrom._oldDepthTarget; moveFrom._oldDepthTarget = nullptr;
		#endif
    }

    SavedTargets& SavedTargets::operator=(SavedTargets&& moveFrom) never_throws
    {
		#if GFXAPI_ACTIVE == GFXAPI_DX11
			_oldViewportCount = moveFrom._oldViewportCount; moveFrom._oldViewportCount = 0;
			for (unsigned c=0; c<MaxSimultaneousRenderTargetCount; ++c) {
				_oldTargets[c] = moveFrom._oldTargets[c];
				_oldViewports[c] = moveFrom._oldViewports[c];
				moveFrom._oldTargets[c] = nullptr;
				moveFrom._oldViewports[c] = Metal::ViewportDesc();
			}
			_oldDepthTarget = moveFrom._oldDepthTarget; moveFrom._oldDepthTarget = nullptr;
		#endif
        return *this;
    }

    SavedTargets::~SavedTargets()
    {
		#if GFXAPI_ACTIVE == GFXAPI_DX11
			for (unsigned c=0; c<dimof(_oldTargets); ++c) {
				if (_oldTargets[c]) {
					_oldTargets[c]->Release();
				}
			}
			if (_oldDepthTarget) {
				_oldDepthTarget->Release();
			}
		#endif
    }
	#if GFXAPI_ACTIVE == GFXAPI_DX11
		void SavedTargets::SetDepthStencilView(ID3D::DepthStencilView* dsv)
		{
			if (_oldDepthTarget) {
				_oldDepthTarget->Release();
			}
			_oldDepthTarget = dsv;
			_oldDepthTarget->AddRef();
		}
	#else
		void SavedTargets::SetDepthStencilView(const Metal::DepthStencilView& dsv)
		{}
	#endif

    void        SavedTargets::ResetToOldTargets(Metal::DeviceContext& context)
    {
		#if GFXAPI_ACTIVE == GFXAPI_DX11
			context.GetUnderlying()->OMSetRenderTargets(dimof(_oldTargets), _oldTargets, _oldDepthTarget);
			context.GetUnderlying()->RSSetViewports(_oldViewportCount, (D3D11_VIEWPORT*)_oldViewports);
		#endif
    }

    BufferUploads::BufferDesc BuildRenderTargetDesc( 
        BufferUploads::BindFlag::BitField bindFlags, 
        const BufferUploads::TextureDesc& textureDesc,
        const char name[])
    {
        return CreateDesc(
            bindFlags, 0, GPUAccess::Read|GPUAccess::Write,
            textureDesc, name);
    }

    void SetupVertexGeneratorShader(Metal::DeviceContext& context)
    {
        context.Bind(Topology::TriangleStrip);
        context.UnbindInputLayout();
    }

    void BuildGaussianFilteringWeights(float result[], float standardDeviation, unsigned weightsCount)
    {
            //      Interesting experiment with gaussian blur standard deviation values here:
            //          http://theinstructionlimit.com/tag/gaussian-blur
        float total = 0.f;
        for (int c=0; c<int(weightsCount); ++c) {
            const int centre = weightsCount / 2;
            unsigned xb = XlAbs(centre - c);
            result[c] = std::exp(-float(xb * xb) / (2.f * standardDeviation * standardDeviation));
            total += result[c];
        }
            //  have to balance the weights so they add up to one -- otherwise
            //  the final result will be too bright/dark
        for (unsigned c=0; c<weightsCount; ++c) {
            result[c] /= total;
        }
    }

    float PowerForHalfRadius(float halfRadius, float powerFraction)
    {
        const float attenuationScalar = 1.f;
        return (attenuationScalar*(halfRadius*halfRadius)+1.f) * (1.0f / (1.f-powerFraction));
    }

    IResourcePtr CreateResourceImmediate(const BufferUploads::BufferDesc& desc)
    {
        return RenderCore::Techniques::Services::GetDevice().CreateResource(desc);
    }


    template<int Count>
        class UCS4Buffer
        {
        public:
            ucs4 _buffer[Count];
            UCS4Buffer(const char input[])
            {
                utf8_2_ucs4((const utf8*)input, XlStringLen(input), _buffer, dimof(_buffer)-1);
            }
            UCS4Buffer(const char* start, const char* end)
            {
                utf8_2_ucs4((const utf8*)start, end-start, _buffer, dimof(_buffer)-1);
            }
            UCS4Buffer(const std::string& input)
            {
                utf8_2_ucs4((const utf8*)input.c_str(), input.size(), _buffer, dimof(_buffer)-1);
            }

            operator const ucs4*() const { return _buffer; }
			const ucs4* get() const { return _buffer; }
        };

    void DrawPendingResources(   
        RenderCore::IThreadContext& context, 
        SceneEngine::Techniques::ParsingContext& parserContext, 
        const std::shared_ptr<RenderOverlays::Font>& font)
    {
        if (    !parserContext._stringHelpers->_pendingAssets[0]
            &&  !parserContext._stringHelpers->_invalidAssets[0]
            &&  !parserContext._stringHelpers->_errorString[0])
            return;

        {
            auto& metalContext = *Metal::DeviceContext::Get(context);
            metalContext.Bind(Techniques::CommonResources()._blendStraightAlpha);
        }

        using namespace RenderOverlays;
        TextStyle style; 
        Float2 textPosition(16.f, 16.f);
        float lineHeight = font->GetFontProperties()._lineHeight;
        const auto alignment = TextAlignment::TopLeft;
        const unsigned colour = 0xff7f7f7fu;

        if (parserContext._stringHelpers->_pendingAssets[0]) {
            UCS4Buffer<64> text("Pending assets:");
            Float2 alignedPosition2 = AlignText(*font, Quad::MinMax(textPosition, Float2(1024.f, 1024.f)), alignment, text.get());
            Draw(
                context, *font, style,
				alignedPosition2[0], alignedPosition2[1], text.get(),
                0.f, 1.f, 0.f, 0.f, 0xffff7f7f, true, nullptr);
            textPosition[1] += lineHeight;

            auto i = parserContext._stringHelpers->_pendingAssets;
            for (;;) {
                while (*i && *i == ',') ++i;
                auto* start = i;
                while (*i && *i != ',') ++i;
                if (start == i) break;

                UCS4Buffer<256> text2(start, i);
                Float2 alignedPosition = AlignText(*font, Quad::MinMax(textPosition + Float2(32.f, 0.f), Float2(1024.f, 1024.f)), alignment, text2.get());
                Draw(
                    context, *font, style, 
					alignedPosition[0], alignedPosition[1], text2.get(),
                    0.f, 1.f, 0.f, 0.f, colour, true, nullptr);
                textPosition[1] += lineHeight;
            }
        }

        if (parserContext._stringHelpers->_invalidAssets[0]) {
            UCS4Buffer<64> text("Invalid assets:");
            Float2 alignedPosition2 = AlignText(*font, Quad::MinMax(textPosition, Float2(1024.f, 1024.f)), alignment, text.get());
            Draw(
                context, *font, style,
				alignedPosition2[0], alignedPosition2[1], text.get(),
                0.f, 1.f, 0.f, 0.f, colour, true, nullptr);
            textPosition[1] += lineHeight;

            auto i = parserContext._stringHelpers->_invalidAssets;
            for (;;) {
                while (*i && *i == ',') ++i;
                auto* start = i;
                while (*i && *i != ',') ++i;
                if (start == i) break;

                UCS4Buffer<256> text2(start, i);
                Float2 alignedPosition = AlignText(*font, Quad::MinMax(textPosition + Float2(32.f, 0.f), Float2(1024.f, 1024.f)), alignment, text2.get());
                Draw(
                    context, *font, style,
					alignedPosition[0], alignedPosition[1], text2.get(),
                    0.f, 1.f, 0.f, 0.f, colour, true, nullptr);
                textPosition[1] += lineHeight;
            }
        }

        if (parserContext._stringHelpers->_errorString[0]) {
            auto i = parserContext._stringHelpers->_errorString;
            for (;;) {
                while (*i && (*i == '\n' || *i == '\r')) ++i;
                auto* start = i;
                while (*i && *i != '\n' && *i != '\r') ++i;
                if (start == i) break;

                UCS4Buffer<512> text2(start, i);
                Float2 alignedPosition = AlignText(*font, Quad::MinMax(textPosition, Float2(1024.f, 1024.f)), alignment, text2.get());
                Draw(
                    context, *font, style,
					alignedPosition[0], alignedPosition[1], text2.get(),
                    0.f, 1.f, 0.f, 0.f, colour, true, nullptr);
                textPosition[1] += lineHeight;
            }
        }
    }

    void DrawQuickMetrics(   
        RenderCore::IThreadContext& context, 
        SceneEngine::Techniques::ParsingContext& parserContext, 
		const std::shared_ptr<RenderOverlays::Font>& font)
	{
		if (parserContext._stringHelpers->_quickMetrics[0])
			DrawString(context, font, parserContext._stringHelpers->_quickMetrics);
	}

	void DrawString(   
        RenderCore::IThreadContext& context, 
		const std::shared_ptr<RenderOverlays::Font>& font,
		StringSection<> string)
    {
        auto& metalContext = *Metal::DeviceContext::Get(context);
        metalContext.Bind(Techniques::CommonResources()._blendStraightAlpha);

        using namespace RenderOverlays;
        TextStyle style;
        Float2 textPosition(16.f, 150.f);
        float lineHeight = font->GetFontProperties()._lineHeight;
        const auto alignment = TextAlignment::TopLeft;
        const unsigned colour = 0xffcfcfcfu;

        auto i = string.begin(), end = string.end();
        for (;;) {
            while (i != end && (*i == '\n' || *i == '\r')) ++i;
            auto* start = i;
            while (i != end && *i != '\n' && *i != '\r') ++i;
            if (start == i) break;

            UCS4Buffer<512> text2(start, i);
            Float2 alignedPosition = AlignText(*font, Quad::MinMax(textPosition, Float2(1024.f, 1024.f)), alignment, text2.get());
            Draw(
                context, *font, style,
				alignedPosition[0], alignedPosition[1], text2.get(),
                0.f, 1.f, 0.f, 0.f, colour, true, nullptr);
            textPosition[1] += lineHeight;
        }
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    Int2 GetCursorPos()
    {
        POINT cursorPos;
        GetCursorPos(&cursorPos);
        ScreenToClient((HWND)::GetActiveWindow(), &cursorPos);
        return Int2(cursorPos.x, cursorPos.y);
    }

    bool IsLButtonDown() { return GetKeyState(VK_LBUTTON)<0; }
    bool IsShiftDown() { return GetKeyState(VK_LSHIFT)<0; }

    void CheckSpecularIBLMipMapCount(const RenderCore::Metal::ShaderResourceView& srv)
    {
        // Specular ibl textures must always have 10 mipmaps. This value is hardcoded in
        // the shader code.
        // 9 mipmaps corresponds to a cubemap with 512x512 faces.
        // #if defined(_DEBUG)
        //     Metal::TextureDesc2D desc(srv.GetUnderlying());
        //     assert(desc.ArraySize == 6);
        //     assert(desc.MipLevels == 10);
        // #endif
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    IteratorRange<FixedFunctionModel::DelayStep*> AsDelaySteps(
        Techniques::BatchFilter filter)
    {
        using BF = Techniques::BatchFilter;
        using V = std::vector<FixedFunctionModel::DelayStep>;
        using DelayStep = FixedFunctionModel::DelayStep;

        switch (filter) {
        case BF::General:
            {
                static DelayStep result[] { DelayStep::OpaqueRender };
                return MakeIteratorRange(result);
            }

        case BF::PostOpaque:
            {
                static DelayStep result[] { DelayStep::PostDeferred };
                return MakeIteratorRange(result);
            }
        
        case BF::PreDepth:
            {
                static DelayStep result[] { DelayStep::PostDeferred, DelayStep::SortedBlending };
                return MakeIteratorRange(result);
            }
        
        case BF::SortedBlending:
            {
                static DelayStep result[] { DelayStep::SortedBlending };
                return MakeIteratorRange(result);
            }
        
        /*case BF::DMShadows:
        case BF::RayTracedShadows:
            {
                static DelayStep result[] { DelayStep::OpaqueRender, DelayStep::PostDeferred, DelayStep::SortedBlending };
                return MakeIteratorRange(result);
            }*/
        }

        return IteratorRange<FixedFunctionModel::DelayStep*>();
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    ProtectState::ProtectState(Metal::DeviceContext& context, States::BitField states)
    : _context(&context), _states(states)
    {
		#if GFXAPI_ACTIVE == GFXAPI_DX11
			if (_states & States::RenderTargets || _states & States::Viewports)
				_targets = SavedTargets(context);
			if (_states & States::DepthStencilState)
				_depthStencilState = Metal::DepthStencilState(context);
			if (_states & States::BlendState) {
				ID3D::BlendState* rawptr = nullptr;
				context.GetUnderlying()->OMGetBlendState(&rawptr, _blendFactor, &_blendSampleMask);
				_blendState = moveptr(rawptr);
			}
			if (_states & States::RasterizerState) 
				_rasterizerState = Metal::RasterizerState(context);
			if (_states & States::InputLayout)
				_inputLayout = Metal::BoundInputLayout(context);

			if (_states & States::VertexBuffer) {
				ID3D::Buffer* rawptrs[s_vbCount];
				context.GetUnderlying()->IAGetVertexBuffers(0, s_vbCount, rawptrs, _vbStrides, _vbOffsets);
				for (unsigned c=0; c<s_vbCount; ++c)
					_vertexBuffers[c] = moveptr(rawptrs[c]);
			}

			if (_states & States::IndexBuffer) {
				ID3D::Buffer* rawptr = nullptr;
				context.GetUnderlying()->IAGetIndexBuffer(&rawptr, (DXGI_FORMAT*)&_ibFormat, &_ibOffset);
				_indexBuffer = moveptr(rawptr);
			}

			if (_states & States::Topology) {
				context.GetUnderlying()->IAGetPrimitiveTopology((D3D11_PRIMITIVE_TOPOLOGY*)&_topology);
			}
		#endif
    }

    ProtectState::ProtectState()
    {
        _context = nullptr;
        _states = 0;
		#if GFXAPI_ACTIVE == GFXAPI_DX11
			_ibFormat = DXGI_FORMAT_UNKNOWN;
			_ibOffset = 0;
			_topology = (D3D11_PRIMITIVE_TOPOLOGY)0;
		#endif
    }

    ProtectState::ProtectState(ProtectState&& moveFrom)
	#if GFXAPI_ACTIVE == GFXAPI_DX11
        : _targets(std::move(moveFrom._targets))
		, _depthStencilState(std::move(moveFrom._depthStencilState))
		, _inputLayout(std::move(moveFrom._inputLayout))
		, _indexBuffer(std::move(moveFrom._indexBuffer))
		, _blendState(std::move(moveFrom._blendState))
		, _rasterizerState(std::move(moveFrom._rasterizerState))
	#endif
    {
        _context = moveFrom._context; moveFrom._context = nullptr;
        _states = moveFrom._states; moveFrom._states = 0;
        
		#if GFXAPI_ACTIVE == GFXAPI_DX11
			_ibFormat = moveFrom._ibFormat;
			_ibOffset = moveFrom._ibOffset;

			for (unsigned s=0; s<s_vbCount; ++s) {
				_vertexBuffers[s] = std::move(moveFrom._vertexBuffers[s]);
				_vbStrides[s] = moveFrom._vbStrides[s];
				_vbOffsets[s] = moveFrom._vbOffsets[s];
			}

			for (unsigned c=0; c<4; ++c)
				_blendFactor[c] = moveFrom._blendFactor[c];
			_blendSampleMask = moveFrom._blendSampleMask;

			_viewports = moveFrom._viewports;
			_topology = moveFrom._topology;
		#endif
    }

    ProtectState& ProtectState::operator=(ProtectState&& moveFrom)
    {
        _context = moveFrom._context; moveFrom._context = nullptr;
        _states = moveFrom._states; moveFrom._states = 0;

		#if GFXAPI_ACTIVE == GFXAPI_DX11
            _targets = std::move(moveFrom._targets);
			_depthStencilState = std::move(moveFrom._depthStencilState);
			_inputLayout = std::move(moveFrom._inputLayout);

			_indexBuffer = std::move(moveFrom._indexBuffer);
			_ibFormat = moveFrom._ibFormat;
			_ibOffset = moveFrom._ibOffset;

			for (unsigned s=0; s<s_vbCount; ++s) {
				_vertexBuffers[s] = std::move(moveFrom._vertexBuffers[s]);
				_vbStrides[s] = moveFrom._vbStrides[s];
				_vbOffsets[s] = moveFrom._vbOffsets[s];
			}

			_blendState = std::move(moveFrom._blendState);
			for (unsigned c=0; c<4; ++c)
				_blendFactor[c] = moveFrom._blendFactor[c];
			_blendSampleMask = moveFrom._blendSampleMask;

			_rasterizerState = std::move(moveFrom._rasterizerState);

			_viewports = moveFrom._viewports;
			_topology = moveFrom._topology;
		#endif
        return *this;
    }

    void ProtectState::ResetStates()
    {
		#if GFXAPI_ACTIVE == GFXAPI_DX11
			if (_context) {
				if (_states & States::RenderTargets|| _states & States::Viewports)
					_targets.ResetToOldTargets(*_context);
				if (_states & States::DepthStencilState)
					_context->Bind(_depthStencilState);
				if (_states & States::BlendState)
					_context->GetUnderlying()->OMSetBlendState(_blendState.get(), _blendFactor, _blendSampleMask);
				if (_states & States::RasterizerState)
					_context->Bind(_rasterizerState);
				if (_states & States::InputLayout)
					_inputLayout.Apply(*_context, {});

				if (_states & States::VertexBuffer) {
					ID3D::Buffer* rawptrs[s_vbCount];
					for (unsigned c=0; c<s_vbCount; ++c)
						rawptrs[c] = _vertexBuffers[c].get();
					_context->GetUnderlying()->IASetVertexBuffers(0, s_vbCount, rawptrs, _vbStrides, _vbOffsets);
				}

				if (_states & States::IndexBuffer)
					_context->GetUnderlying()->IASetIndexBuffer(_indexBuffer.get(), (DXGI_FORMAT)_ibFormat, _ibOffset);
				if (_states & States::Topology)
					_context->GetUnderlying()->IASetPrimitiveTopology((D3D11_PRIMITIVE_TOPOLOGY)_topology);

				_states = 0;
			}
		#endif
    }

    ProtectState::~ProtectState()
    {
        ResetStates();
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    void ShaderBasedCopy(
        Metal::DeviceContext& context,
        const Metal::DepthStencilView& dest,
        const Metal::ShaderResourceView& src,
        ProtectState::States::BitField protectStates)
    {
        using States = ProtectState::States;
        const States::BitField effectedStates = 
            States::RenderTargets | States::Viewports | States::DepthStencilState 
            | States::Topology | States::InputLayout | States::VertexBuffer
            ;
        ProtectState savedStates(context, effectedStates & protectStates);

		auto desc = Metal::ExtractDesc(dest);
        context.Bind(Metal::ViewportDesc(0.f, 0.f, float(desc._textureDesc._width), float(desc._textureDesc._height)));

        context.Bind(ResourceList<Metal::RenderTargetView, 0>(), &dest);
        context.Bind(Techniques::CommonResources()._dssWriteOnly);
        context.Bind(
            ::Assets::GetAssetDep<Metal::ShaderProgram>(
                BASIC2D_VERTEX_HLSL ":fullscreen:vs_*",
                BASIC_PIXEL_HLSL ":copy_depth:ps_*"));
        context.GetNumericUniforms(ShaderStage::Pixel).Bind(MakeResourceList(src));
        SetupVertexGeneratorShader(context);
        context.Draw(4);
        MetalStubs::UnbindPS<Metal::ShaderResourceView>(context, 0, 1);
    }

    class ShaderBasedCopyRes
    {
    public:
        class Desc 
        {
        public:
            CopyFilter _filter;
            Desc(CopyFilter filter) : _filter(filter) {}
        };

        const Metal::ShaderProgram* _shader;
        Metal::BoundUniforms _uniforms;

        ShaderBasedCopyRes(const Desc& desc)
        {
            switch (desc._filter) {
            case CopyFilter::BoxFilter:
                    // The box filter is designed for generating
                    // mip-maps. And it can work even when building
                    // very small mip-maps from the top-most level.
                    // It will sample all of the pixels in
                    // the source (unlike bilinear filter, which will
                    // only sample some pixels in large downsampling 
                    // operations)
                _shader = &::Assets::GetAssetDep<Metal::ShaderProgram>(
                    BASIC2D_VERTEX_HLSL ":screenspacerect:vs_*",
                    BASIC_PIXEL_HLSL ":copy_boxfilter:ps_*");
                break;

            case CopyFilter::BoxFilterAlphaComplementWeight:
                _shader = &::Assets::GetAssetDep<Metal::ShaderProgram>(
                    BASIC2D_VERTEX_HLSL ":screenspacerect:vs_*",
                    BASIC_PIXEL_HLSL ":copy_boxfilter_alphacomplementweight:ps_*");
                break;

            default:
                _shader = &::Assets::GetAssetDep<Metal::ShaderProgram>(
                    BASIC2D_VERTEX_HLSL ":screenspacerect:vs_*",
                    BASIC_PIXEL_HLSL ":copy_bilinear:ps_*");
                break;
            }
			UniformsStreamInterface usi;
			usi.BindConstantBuffer(0, {Hash64("ScreenSpaceOutput")});
            _uniforms = Metal::BoundUniforms(
				*_shader,
				Metal::PipelineLayoutConfig{},
				UniformsStreamInterface{},
				usi);

            _validationCallback = _shader->GetDependencyValidation();
        }

        ~ShaderBasedCopyRes() {}

        const std::shared_ptr<::Assets::DependencyValidation>& GetDependencyValidation() const     
            { return _validationCallback; }

    private:
        std::shared_ptr<::Assets::DependencyValidation>   _validationCallback;
    };

    void ShaderBasedCopy(
        Metal::DeviceContext& context,
        const RenderCore::Metal::RenderTargetView& dest,
        const RenderCore::Metal::ShaderResourceView& src,
        std::pair<UInt2, UInt2> destination,
        std::pair<UInt2, UInt2> source,
        CopyFilter filter, ProtectState::States::BitField protectStates)
    {
        using States = ProtectState::States;
        const States::BitField effectedStates = 
            States::RenderTargets | States::Viewports | States::DepthStencilState 
            | States::Topology | States::InputLayout | States::VertexBuffer
            ;
        ProtectState savedStates(context, effectedStates & protectStates);

        auto& res = ConsoleRig::FindCachedBoxDep2<ShaderBasedCopyRes>(filter);

		auto dstDesc = Metal::ExtractDesc(dest);
		auto srcDesc = Metal::ExtractDesc(src);
        context.Bind(Metal::ViewportDesc(0.f, 0.f, float(dstDesc._textureDesc._width), float(dstDesc._textureDesc._height)));

        Float2 coords[6] = 
        {
            Float2(float(destination.first[0]), float(destination.first[1])), 
            Float2(float(destination.second[0]), float(destination.second[1])),
            Float2(source.first[0] / float(srcDesc._textureDesc._width), source.first[1] / float(srcDesc._textureDesc._height)),
            Float2(source.second[0] / float(srcDesc._textureDesc._width), source.second[1] / float(srcDesc._textureDesc._height)),
            Float2(float(dstDesc._textureDesc._width), float(dstDesc._textureDesc._height)),
            Zero<Float2>()
        };

        context.Bind(MakeResourceList(dest), nullptr);
        context.Bind(Techniques::CommonResources()._dssWriteOnly);
        context.Bind(*res._shader);
		ConstantBufferView cbvs[] = {MakeSharedPkt(coords)};
		res._uniforms.Apply(context, 1, UniformsStream{MakeIteratorRange(cbvs)});
        context.GetNumericUniforms(ShaderStage::Pixel).Bind(MakeResourceList(src));
        SetupVertexGeneratorShader(context);
        context.Draw(4);
		MetalStubs::UnbindPS<Metal::ShaderResourceView>(context, 0, 1);
    }

	RenderCore::Metal::Buffer MakeMetalCB(const void* data, size_t size)
	{
		if (data) {
			return RenderCore::Metal::MakeConstantBuffer(
				RenderCore::Metal::GetObjectFactory(),
				MakeIteratorRange(data, PtrAdd(data, size)));
		} else {
			return RenderCore::Metal::Buffer(
				RenderCore::Metal::GetObjectFactory(),
				CreateDesc(
					BindFlag::ConstantBuffer,
					CPUAccess::WriteDynamic,
					GPUAccess::Read,
					LinearBufferDesc::Create(unsigned(size)),
					"buf"),
				IteratorRange<const void*>{});
		}
	}

	RenderCore::Metal::Buffer MakeMetalVB(const void* data, size_t size)
	{
		return RenderCore::Metal::MakeVertexBuffer(
			RenderCore::Metal::GetObjectFactory(),
			MakeIteratorRange(data, PtrAdd(data, size)));
	}

	RenderCore::Metal::Buffer MakeMetalIB(const void* data, size_t size)
	{
		return RenderCore::Metal::MakeIndexBuffer(
			RenderCore::Metal::GetObjectFactory(),
			MakeIteratorRange(data, PtrAdd(data, size)));
	}
	
}

