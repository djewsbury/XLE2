// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "VulkanCore.h"
#include "IncludeVulkan.h"      // required because we're deriving from Vulkan types
#include "../../StateDesc.h"
#include <utility>

namespace RenderCore { namespace Metal_Vulkan
{
    class DeviceContext;

    static const unsigned s_mrtLimit = 4;

////////////////////////////////////////////////////////////////////////////////////////////////

    /// <summary>States for sampling from textures</summary>
    /// 
    ///     Sets states that are used by the hardware when sampling from textures. Most useful
    ///     states are:
    ///         <list>
    ///              <item> filtering mode (FilterMode::Enum)
    ///                      Set to point / linear / trilinear / anisotrophic (etc)
    ///              <item> addressing mode (AddressMode::Enum)
    ///                      Wrapping / mirroring / clamping on texture edges
    ///         </list>
    ///
    ///     Currently the following D3D states are not exposed by this interface:
    ///         <list>
    ///             <item> MipLODBias
    ///             <item> MaxAnisotrophy
    ///             <item> ComparisonFunc
    ///             <item> BorderColor
    ///             <item> MinLOD
    ///             <item> MaxLOD
    ///         </list>
    ///
    ///     It's best to limit the total number of SamplerState objects used by
    ///     the system. In an ideal game, there should be D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT (16)
    ///     sampler states or less. Then, they could all be bound at the start of the frame, and
    ///     avoid any further state thrashing.
    /// 
    /// <exception cref="::RenderCore::Exceptions::AllocationFailure">
    ///     Failed to create underlying object. Could be caused by invalid input values, or a corrupt/lost device.
    /// </exception>
    class SamplerState
    {
    public:
        SamplerState(   FilterMode filter = FilterMode::Trilinear,
                        AddressMode addressU = AddressMode::Wrap, 
                        AddressMode addressV = AddressMode::Wrap, 
                        AddressMode addressW = AddressMode::Wrap,
						CompareOp comparison = CompareOp::Never);
		~SamplerState();

        using UnderlyingType = VkSampler;
        VkSampler GetUnderlying() const { return _sampler.get(); }

    private:
        VulkanSharedPtr<VkSampler> _sampler;
    };

        ////////////////////////////////////////////////////////////////////////////////////////////////

    /// <summary>States used while rasterising triangles</summary>
    ///
    ///     These states are used during triangle setup for interpolator
    ///     initialising and culling.
    ///
    ///     Common states:
    ///         <list>
    ///              <item> cull mode (CullMode::Enum)
    ///                     this is the most commonly used state
    ///         </list>
    ///
    ///     D3D states not exposed by this interface:
    ///         <list>
    ///             <item> FillMode
    ///             <item> FrontCounterClockwise
    ///             <item> DepthBias
    ///             <item> DepthBiasClamp
    ///             <item> SlopeScaledDepthBias
    ///             <item> DepthClipEnable
    ///             <item> ScissorEnable
    ///             <item> MultisampleEnable        (defaults to true)
    ///             <item> AntialiasedLineEnable
    ///         </list>
    ///
    /// <exception cref="::RenderCore::Exceptions::AllocationFailure">
    ///     Failed to create underlying object. Could be caused by invalid input values, or a corrupt/lost device.
    /// </exception>
    class RasterizerState : public VkPipelineRasterizationStateCreateInfo
    {
    public:
        RasterizerState(CullMode cullmode = CullMode::Back, bool frontCounterClockwise = true);
        RasterizerState(
            CullMode cullmode, bool frontCounterClockwise,
            FillMode fillmode,
            int depthBias, float depthBiasClamp, float slopeScaledBias);

		static RasterizerState Null() { return RasterizerState(); }

        using UnderlyingType = const RasterizerState*;
        UnderlyingType GetUnderlying() const { return this; }
    };

        ////////////////////////////////////////////////////////////////////////////////////////////////

    /// <summary>States used while drawing pixels to the render targets</summary>
    ///
    ///     These states are used to blend the pixel shader output with
    ///     the existing colours in the render targets.
    ///
    ///     In D3D11, we can define separate blending modes for each render target
    ///     (and separate blending for colour and alpha parts).
    ///
    ///     However, most of the time we want to just use the same blend mode
    ///     for all render targets. So, the simple interface will smear the
    ///     given blend mode across all render target settings.
    ///
    ///     BlendState can be constructed with just "BlendOp::NoBlending" if you
    ///     want to disable all blending. For example:
    ///         <code>
    ///             context->Bind(BlendOp::NoBlending);
    ///         </code>
    ///
    ///     Common states:
    ///         <list>
    ///             <item> blending operation (BlendOp::Enum)
    ///             <item> blending parameter for source (Blend::Enum)
    ///             <item> blending parameter for destination (Blend::Enum)
    ///         </list>
    ///
    ///     D3D states not exposed by this interface:
    ///         <list>
    ///             <item> AlphaToCoverageEnable
    ///             <item> IndependentBlendEnable
    ///             <item> RenderTargetWriteMask
    ///         </list>
    ///
    /// <exception cref="::RenderCore::Exceptions::AllocationFailure">
    ///     Failed to create underlying object. Could be caused by invalid input values, or a corrupt/lost device.
    /// </exception>
    class BlendState : public VkPipelineColorBlendStateCreateInfo
    {
    public:
        BlendState( BlendOp blendingOperation = BlendOp::Add, 
                    Blend srcBlend = Blend::SrcAlpha,
                    Blend dstBlend = Blend::InvSrcAlpha,
                    BlendOp alphaBlendingOperation = BlendOp::NoBlending,
                    Blend alphaSrcBlend = Blend::One,
                    Blend alphaDstBlend = Blend::Zero);

        BlendState(const BlendState&);
        BlendState& operator=(const BlendState&);

		static BlendState Null() { return BlendState(); }

        using UnderlyingType = const BlendState*;
        UnderlyingType GetUnderlying() const { return this; }

    private:
        VkPipelineColorBlendAttachmentState _attachments[s_mrtLimit];
    };

        ////////////////////////////////////////////////////////////////////////////////////////////////

    class StencilMode
    {
    public:
		CompareOp _comparison;
        StencilOp _onPass;
        StencilOp _onDepthFail;
        StencilOp _onStencilFail;
        StencilMode(
			CompareOp comparison     = CompareOp::Always,
            StencilOp onPass          = StencilOp::Replace,
            StencilOp onStencilFail   = StencilOp::DontWrite,
            StencilOp onDepthFail     = StencilOp::DontWrite)
            : _comparison(comparison)
            , _onPass(onPass)
            , _onStencilFail(onStencilFail)
            , _onDepthFail(onDepthFail) {}

        static StencilMode NoEffect;
        static StencilMode AlwaysWrite;
    };

    /// <summary>States used reading and writing to the depth buffer</summary>
    ///
    ///     These states are used by the hardware to determine how pixels are
    ///     rejected by the depth buffer, and whether we write new depth values to
    ///     the depth buffer.
    ///
    ///     Common states:
    ///         <list>
    ///             <item> depth enable (enables/disables both reading to and writing from the depth buffer)
    ///             <item> write enable (enables/disables writing to the depth buffer)
    ///         </list>
    ///
    ///     Stencil states:
    ///         <list>
    ///             <item> For both front and back faces:
    //                  <list>
    ///                     <item> comparison mode
    ///                     <item> operation for stencil and depth pass
    ///                     <item> operation for stencil fail
    ///                     <item> operation for depth fail
    ///                 </list>
    ///         </list>
    ///
    ///     D3D states not exposed by this interface:
    ///         <list>
    ///             <item> DepthFunc (always D3D11_COMPARISON_LESS_EQUAL)
    ///             <item> StencilReadMask
    ///             <item> StencilWriteMask
    ///         </list>
    ///
    /// <exception cref="::RenderCore::Exceptions::AllocationFailure">
    ///     Failed to create underlying object. Could be caused by invalid input values, or a corrupt/lost device.
    /// </exception>
    class DepthStencilState : public VkPipelineDepthStencilStateCreateInfo
    {
    public:
        explicit DepthStencilState(bool enabled=true, bool writeEnabled=true, CompareOp comparison = CompareOp::LessEqual);
        DepthStencilState(
            bool depthTestEnabled, bool writeEnabled, 
            unsigned stencilReadMask, unsigned stencilWriteMask,
            const StencilMode& frontFaceStencil = StencilMode::NoEffect,
            const StencilMode& backFaceStencil = StencilMode::NoEffect);

        using UnderlyingType = const DepthStencilState*;
        UnderlyingType GetUnderlying() const { return this; }
    };

        ////////////////////////////////////////////////////////////////////////////////////////////////

    /// <summary>Utility for querying low level viewport</summary>
    ///
    ///     Pass the device context to the constructor of "ViewportDesc" to
    ///     query the current low level viewport.
    ///
    ///     For example:
    ///         <code>
    ///             ViewportDesc currentViewport0(*context);
    ///             auto width = currentViewport0.Width;
    ///         </code>
    ///
    ///     Note that the type is designed for compatibility with the D3D11 type,
    ///     D3D11_VIEWPORT (for convenience).
    ///
    ///     There can actually be multiple viewports in D3D11. But usually only
    ///     the 0th viewport is used.
    class ViewportDesc
    {
    public:
            // (compatible with D3D11_VIEWPORT)
        float TopLeftX;
        float TopLeftY;
        float Width;
        float Height;
        float MinDepth;
        float MaxDepth;

        ViewportDesc(float topLeftX, float topLeftY, float width, float height, float minDepth=0.f, float maxDepth=1.f)
            : TopLeftX(topLeftX), TopLeftY(topLeftY), Width(width), Height(height)
            , MinDepth(minDepth), MaxDepth(maxDepth) {}
        ViewportDesc(const DeviceContext& context);
        ViewportDesc();
    };
}}

