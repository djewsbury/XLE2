// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "TextureView.h"
#include "FrameBuffer.h"        // for AttachmentPool
#include "DX11.h"
#include "../../IDevice_Forward.h"
#include "../../IThreadContext_Forward.h"
#include "../../ResourceList.h"
#include "../../Types_Forward.h"
#include "../../../Utility/Threading/ThreadingUtils.h"
#include "../../../Utility/IntrusivePtr.h"
#include "../../../Core/Prefix.h"

namespace RenderCore { enum class ShaderStage; class Viewport; class ScissorRect; }

namespace RenderCore { namespace Metal_DX11
{
    class Buffer;
    class ShaderResourceView;
    class SamplerState;
	class BoundInputLayout;
    class ComputeShader;
    class ShaderProgram;
    class RasterizerState;
    class BlendState;
    class DepthStencilState;
    class DepthStencilView;
    class RenderTargetView;
    class BoundClassInterfaces;
	class ObjectFactory;
	class Resource;
	class NumericUniformsInterface;

        //  todo ---    DeviceContext, ObjectFactory & CommandList -- maybe these
        //              should go into RenderCore (because it's impossible to do anything without them)

    class CommandList : public RefCountedObject
    {
    public:
        CommandList(ID3D::CommandList* underlying);
        CommandList(intrusive_ptr<ID3D::CommandList>&& underlying);
        ID3D::CommandList* GetUnderlying() { return _underlying.get(); }

		CommandList(const CommandList&) = delete;
		CommandList& operator=(const CommandList&) = delete;
    private:
        intrusive_ptr<ID3D::CommandList> _underlying;
    };

    using CommandListPtr = intrusive_ptr<CommandList>;

    class DeviceContext
    {
    public:
        template<int Count> void    Bind(const ResourceList<RenderTargetView, Count>& renderTargets, const DepthStencilView* depthStencil);
        template<int Count1, int Count2> void    Bind(const ResourceList<RenderTargetView, Count1>& renderTargets, const DepthStencilView* depthStencil, const ResourceList<UnorderedAccessView, Count2>& unorderedAccess);

        void        Bind(const Resource& ib, Format indexFormat, unsigned offset=0);
        void        Bind(Topology topology);
        void        Bind(const ComputeShader& computeShader);
        void        Bind(const ShaderProgram& shaderProgram);
        void        Bind(const RasterizerState& rasterizer);
        void        Bind(const BlendState& blender);
        void        Bind(const DepthStencilState& depthStencilState, unsigned stencilRef = 0x0);

        void        Bind(const ShaderProgram& shaderProgram, const BoundClassInterfaces& dynLinkage);

		void		UnbindVS();
		void		UnbindPS();
		void		UnbindGS();
		void		UnbindHS();
		void		UnbindDS();
		void		UnbindCS();
		void		UnbindInputLayout();

        void        Draw(unsigned vertexCount, unsigned startVertexLocation=0);
        void        DrawIndexed(unsigned indexCount, unsigned startIndexLocation=0, unsigned baseVertexLocation=0);
        void        DrawAuto();
        void        Dispatch(unsigned countX, unsigned countY=1, unsigned countZ=1);

        void        Clear(const RenderTargetView& renderTargets, const VectorPattern<float,4>& values);
        struct ClearFilter { enum Enum { Depth = 1<<0, Stencil = 1<<1 }; using BitField = unsigned; };
        void        Clear(const DepthStencilView& depthStencil, ClearFilter::BitField clearFilter, float depth, unsigned stencil);
        void        ClearUInt(const UnorderedAccessView& unorderedAccess, const VectorPattern<unsigned,4>& values);
        void        ClearFloat(const UnorderedAccessView& unorderedAccess, const VectorPattern<float,4>& values);

		bool		InRenderPass();

        void        BeginCommandList();
        auto        ResolveCommandList() -> CommandListPtr;
        void        ExecuteCommandList(CommandList& commandList, bool preserveRenderState);

		void SetViewportAndScissorRects(IteratorRange<const Viewport*> viewports, IteratorRange<const ScissorRect*> scissorRects);
		Viewport GetBoundViewport();

		DEPRECATED_ATTRIBUTE void        Bind(const Viewport& viewport);

		NumericUniformsInterface& GetNumericUniforms(ShaderStage stage);

        static std::shared_ptr<DeviceContext> Get(IThreadContext& threadContext);
        static void PrepareForDestruction(IDevice* device, IPresentationChain* presentationChain);

        ID3D::DeviceContext*            GetUnderlying() const           { return _underlying.get(); }
        ID3D::UserDefinedAnnotation*    GetAnnotationInterface() const  { return _annotations.get(); }
        bool                            IsImmediate() const;

        void        InvalidateCachedState();

        ID3D::Buffer*               _currentCBs[6][14];
        ID3D::ShaderResourceView*   _currentSRVs[6][32];
		ID3D::SamplerState*			_currentSSs[6][32];

		ObjectFactory&	GetFactory() { return *_factory; }

        std::shared_ptr<DeviceContext> Fork();

        DeviceContext(ID3D::DeviceContext* context);
        DeviceContext(intrusive_ptr<ID3D::DeviceContext>&& context);
        ~DeviceContext();

        #if COMPILER_DEFAULT_IMPLICIT_OPERATORS == 1
            DeviceContext(const DeviceContext&) = delete;
            DeviceContext& operator=(const DeviceContext&) = delete;
            DeviceContext(DeviceContext&&) = default;
            DeviceContext& operator=(DeviceContext&&) = default;
        #endif
    private:
        intrusive_ptr<ID3D::DeviceContext> _underlying;
        intrusive_ptr<ID3D::UserDefinedAnnotation> _annotations;
		ObjectFactory* _factory;
        std::vector<NumericUniformsInterface> _numericUniforms;

		// The following API functions are meant for use by the FrameBuffer implementation
		void    BeginRenderPass();
        void    EndRenderPass();
        void    OnEndRenderPass(std::function<void(void)> fn);
        void    BeginSubpass(unsigned renderTargetWidth, unsigned renderTargetHeight);
        void    EndSubpass();

		unsigned _renderTargetWidth;
        unsigned _renderTargetHeight;

        bool _inRenderPass;
        std::vector<std::function<void(void)>> _onEndRenderPassFunctions;

		friend class FrameBuffer;
		friend void BeginRenderPass(
			DeviceContext& context,
			FrameBuffer& frameBuffer,
			const FrameBufferDesc& layout,
			const FrameBufferProperties& props,
			IteratorRange<const ClearValue*> clearValues);
		friend void EndRenderPass(DeviceContext& context);
    };

        ////////////////////////////////////////////////////////////////////////////////////////////////

    ObjectFactory& GetObjectFactory(IDevice& device);
	ObjectFactory& GetObjectFactory(DeviceContext&);
	ObjectFactory& GetObjectFactory();
    ObjectFactory& GetObjectFactory(ID3D::Device& device);
	ObjectFactory& GetObjectFactory(ID3D::Resource& resource);

}}
