// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <cstdint>

namespace RenderCore
{
		/// <summary>Texture address modes</summary>
	///
	///     These are used to determine how the texture sampler
	///     reads texture data outside of the [0, 1] range.
	///     Normally Wrap and Clamp are used.
	///     <seealso cref="SamplerState"/>
	enum class AddressMode
    {
        Wrap = 1,   // D3D11_TEXTURE_ADDRESS_WRAP
        Mirror = 2, // D3D11_TEXTURE_ADDRESS_MIRROR
        Clamp = 3,  // D3D11_TEXTURE_ADDRESS_CLAMP
        Border = 4  // D3D11_TEXTURE_ADDRESS_BORDER
    };

    enum class FaceWinding
    {
        CCW = 0,    // Front faces are counter clockwise
        CW = 1      // Front faces are clockwise
    };

    /// <summary>Texture filtering modes</summary>
    ///
    ///     These are used when sampling a texture at a floating
    ///     point address. In other words, when sampling at a
    ///     midway point between texels, how do we filter the 
    ///     surrounding texels?
    ///     <seealso cref="SamplerState"/>
    enum class FilterMode
    {
        Point = 0,                  // D3D11_FILTER_MIN_MAG_MIP_POINT
        Trilinear = 0x15,           // D3D11_FILTER_MIN_MAG_MIP_LINEAR
        Anisotropic = 0x55,         // D3D11_FILTER_ANISOTROPIC
        Bilinear = 0x14,            // D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT
        ComparisonBilinear = 0x94   // D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT
    };

    enum class CompareOp
    {
        Never = 1,          // D3D11_COMPARISON_NEVER
        Less = 2,           // D3D11_COMPARISON_LESS
        Equal = 3,          // D3D11_COMPARISON_EQUAL
        LessEqual = 4,      // D3D11_COMPARISON_LESS_EQUAL
        Greater = 5,        // D3D11_COMPARISON_GREATER
        NotEqual = 6,       // D3D11_COMPARISON_NOT_EQUAL
        GreaterEqual = 7,   // D3D11_COMPARISON_GREATER_EQUAL
        Always = 8          // D3D11_COMPARISON_ALWAYS
    };

	/// <summary>Back face culling mode</summary>
	///
	///     Used to determine which side of a triangle to cull.
	///
	///     Note that there is another flag the in rasteriser state
	///     that determines which side of a triangle is the "back"
	///     (ie, clockwise or counterclockwise order). 
	///     Only use the "Front" option if you really want to cull
	///     the front facing triangles (useful for some effects)
	///     <seealso cref="RasterizerState"/>
	enum class CullMode
	{
		None = 1,   // D3D11_CULL_NONE,
		Front = 2,  // D3D11_CULL_FRONT,
		Back = 3    // D3D11_CULL_BACK
	};

	enum class FillMode
	{
		Solid = 3,      // D3D11_FILL_SOLID
		Wireframe = 2   // D3D11_FILL_WIREFRAME
	};

	/// <summary>Settings used for describing a blend state</summary>
	///
	///     The blend operation takes the form:
	///         out colour = Operation(Param1 * (Source colour), Param2 * (Destination colour))
	///         out alpha = Operation(Param1 * (Source alpha), Param2 * (Destination alpha))
	///
	///     Where "Operation" is typically addition.
	///
	///     This enum is used for "Param1" and "Param2"
	///     <seealso cref="BlendOp::Enum"/>
	///     <seealso cref="BlendState"/>
	enum class Blend
	{
		Zero = 1, // D3D11_BLEND_ZERO,
		One = 2, // D3D11_BLEND_ONE,

		SrcColor = 3, // D3D11_BLEND_SRC_COLOR,
		InvSrcColor = 4, // D3D11_BLEND_INV_SRC_COLOR,
		DestColor = 9, // D3D11_BLEND_DEST_COLOR,
		InvDestColor = 10, // D3D11_BLEND_INV_DEST_COLOR,

		SrcAlpha = 5, // D3D11_BLEND_SRC_ALPHA,
		InvSrcAlpha = 6, // D3D11_BLEND_INV_SRC_ALPHA,
		DestAlpha = 7, // D3D11_BLEND_DEST_ALPHA,
		InvDestAlpha = 8 // D3D11_BLEND_INV_DEST_ALPHA
	};

	/// <summary>Settings used for describing a blend state</summary>
	///
	///     The blend operation takes the form:
	///         out colour = Operation(Param1 * (Source colour), Param2 * (Destination colour))
	///         out alpha = Operation(Param1 * (Source alpha), Param2 * (Destination alpha))
	///
	///     This enum is used for "Operation"
	///     <seealso cref="BlendOp::Enum"/>
	///     <seealso cref="BlendState"/>
	enum class BlendOp
	{
		NoBlending,
		Add = 1, // D3D11_BLEND_OP_ADD,
		Subtract = 2, // D3D11_BLEND_OP_SUBTRACT,
		RevSubtract = 3, // D3D11_BLEND_OP_REV_SUBTRACT,
		Min = 4, // D3D11_BLEND_OP_MIN,
		Max = 5 // D3D11_BLEND_OP_MAX
	};

	enum class StencilOp
	{
        Keep = 1,           // D3D11_STENCIL_OP_KEEP
		DontWrite = 1,      // D3D11_STENCIL_OP_KEEP

		Zero = 2,           // D3D11_STENCIL_OP_ZERO
		Replace = 3,        // D3D11_STENCIL_OP_REPLACE
		IncreaseSat = 4,    // D3D11_STENCIL_OP_INCR_SAT
		DecreaseSat = 5,    // D3D11_STENCIL_OP_DECR_SAT
		Invert = 6,         // D3D11_STENCIL_OP_INVERT
		Increase = 7,       // D3D11_STENCIL_OP_INCR
		Decrease = 8        // D3D11_STENCIL_OP_DECR
	};

	/// Equivalent to MTLStencilDescriptor or D3D12_DEPTH_STENCILOP_DESC or VkStencilOpState
    /// Note that OpenGLES2 & Vulkan allow for separate readmask/writemask/reference values per
    /// face, but DX & Metal do not.
    class StencilDesc
    {
    public:
        StencilOp       _passOp = StencilOp::Keep;        ///< pass stencil & depth tests
        StencilOp       _failOp = StencilOp::Keep;        ///< fail stencil test
        StencilOp       _depthFailOp = StencilOp::Keep;   ///< pass stencil but fail depth tests
        CompareOp       _comparisonOp = CompareOp::Always;
    };

    /// Equivalent to MTLDepthStencilDescriptor or D3D12_DEPTH_STENCIL_DESC or VkPipelineDepthStencilStateCreateInfo
    class DepthStencilDesc
    {
    public:
        CompareOp       _depthTest = CompareOp::LessEqual;
        bool            _depthWrite = true;
        bool            _stencilEnable = false;
        uint8_t         _stencilReadMask = 0x0;
        uint8_t         _stencilWriteMask = 0x0;
        uint8_t         _stencilReference = 0x0;
        StencilDesc     _frontFaceStencil;
        StencilDesc     _backFaceStencil;

		uint64_t Hash() const;
    };

    /// Similar to VkPipelineRasterizationStateCreateInfo or D3D12_RASTERIZER_DESC
    /// (Metal just has separate function calls)
    class RasterizationDesc
    {
    public:
        CullMode        _cullMode = CullMode::Back;
        FaceWinding     _frontFaceWinding = FaceWinding::CCW;

		uint64_t Hash() const;
    };

    /// Similar to ?
    class SamplerStateDesc
    {
    public:
        RenderCore::FilterMode _filter = RenderCore::FilterMode::Trilinear;
        RenderCore::AddressMode _addressU = RenderCore::AddressMode::Wrap;
        RenderCore::AddressMode _addressV = RenderCore::AddressMode::Wrap;
        RenderCore::CompareOp _comparison = RenderCore::CompareOp::Never;
        bool _enableMipmaps = true;
    };

    namespace ColorWriteMask
    {
        enum Channels
        {
            Red     = (1<<0),
            Green   = (1<<1),
            Blue    = (1<<2),
            Alpha   = (1<<3)
        };
        using BitField = unsigned;

        const BitField All = (Red | Green | Blue | Alpha);
        const BitField None = 0;
    };

    /**
     * Similar to MTLRenderPipelineColorAttachmentDescriptor or D3D12_RENDER_TARGET_BLEND_DESC or VkPipelineColorBlendAttachmentState
     */
    class AttachmentBlendDesc
    {
    public:
        bool _blendEnable = false;
        Blend _srcColorBlendFactor = Blend::One;
        Blend _dstColorBlendFactor = Blend::Zero;
        BlendOp _colorBlendOp = BlendOp::Add;
        Blend _srcAlphaBlendFactor = Blend::One;
        Blend _dstAlphaBlendFactor = Blend::Zero;
        BlendOp _alphaBlendOp = BlendOp::Add;
        ColorWriteMask::BitField _writeMask = ColorWriteMask::All;

		uint64_t Hash() const;
    };
}

