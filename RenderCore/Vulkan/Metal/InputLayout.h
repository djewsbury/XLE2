// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "VulkanCore.h"
#include "../../UniformsStream.h"
#include "../../../Utility/IteratorUtils.h"
#include <memory>
#include <vector>

namespace RenderCore
{
	class UniformsStream;
	class UniformsStreamInterface;
	class VertexBufferView;
	class InputElementDesc;
	class MiniInputElementDesc;
	class CompiledShaderByteCode;
	template <typename Type, int Count> class ResourceList;
}

namespace RenderCore { namespace Metal_Vulkan
{
	class ShaderProgram;
	class DeviceContext;
	class ComputeShader;
	class PipelineLayoutConfig
	{
	public:
	};

        ////////////////////////////////////////////////////////////////////////////////////////////////

    class BoundInputLayout
    {
    public:
		void Apply(DeviceContext& context, IteratorRange<const VertexBufferView*> vertexBuffers) const never_throws;

        BoundInputLayout(IteratorRange<const InputElementDesc*> layout, const CompiledShaderByteCode& shader);
        BoundInputLayout(IteratorRange<const InputElementDesc*> layout, const ShaderProgram& shader);
		
		struct SlotBinding
        {
            IteratorRange<const MiniInputElementDesc*> _elements;
            unsigned _instanceStepDataRate;     // set to 0 for per vertex, otherwise a per-instance rate
        };
		BoundInputLayout(
            IteratorRange<const SlotBinding*> layouts,
            const CompiledShaderByteCode& program);
        BoundInputLayout(
            IteratorRange<const SlotBinding*> layouts,
            const ShaderProgram& shader);

        BoundInputLayout();
        ~BoundInputLayout();

		BoundInputLayout(BoundInputLayout&& moveFrom) never_throws;
		BoundInputLayout& operator=(BoundInputLayout&& moveFrom) never_throws;

        const IteratorRange<const VkVertexInputAttributeDescription*> GetAttributes() const { return MakeIteratorRange(_attributes); }
		const IteratorRange<const VkVertexInputBindingDescription*> GetVBBindings() const { return MakeIteratorRange(_vbBindingDescriptions); }
    private:
        std::vector<VkVertexInputAttributeDescription>	_attributes;
		std::vector<VkVertexInputBindingDescription>	_vbBindingDescriptions;
    };

        ////////////////////////////////////////////////////////////////////////////////////////////////

	class BoundUniformsHelper;
    class BoundUniforms
    {
    public:
		void Apply(
            DeviceContext& context,
            unsigned streamIdx,
            const UniformsStream& stream) const;

		void UnbindShaderResources(DeviceContext& context, unsigned streamIdx);

		uint64_t _boundUniformBufferSlots[4];
        uint64_t _boundResourceSlots[4];

        BoundUniforms(
            const ShaderProgram& shader,
            const PipelineLayoutConfig& pipelineLayout,
            const UniformsStreamInterface& interface0 = {},
            const UniformsStreamInterface& interface1 = {},
            const UniformsStreamInterface& interface2 = {},
            const UniformsStreamInterface& interface3 = {});
		BoundUniforms(
            const ComputeShader& shader,
            const PipelineLayoutConfig& pipelineLayout,
            const UniformsStreamInterface& interface0 = {},
            const UniformsStreamInterface& interface1 = {},
            const UniformsStreamInterface& interface2 = {},
            const UniformsStreamInterface& interface3 = {});
        BoundUniforms();
        ~BoundUniforms();
        BoundUniforms(const BoundUniforms&) = default;
		BoundUniforms& operator=(const BoundUniforms&) = default;
        BoundUniforms(BoundUniforms&&) = default;
        BoundUniforms& operator=(BoundUniforms&&) = default;

    private:
		bool _isComputeShader;
		static const unsigned s_streamCount = 4;
        std::vector<uint32_t> _cbBindingIndices[s_streamCount];
        std::vector<uint32_t> _srvBindingIndices[s_streamCount];
		uint64_t _shaderBindingMask[s_streamCount];

		void SetupBindings(
			BoundUniformsHelper& helper,
            const UniformsStreamInterface* interfaces[], 
			size_t interfaceCount);
    };

        ////////////////////////////////////////////////////////////////////////////////////////////////

    class BoundClassInterfaces
    {
    public:
        void Bind(uint64_t hashName, unsigned bindingArrayIndex, const char instance[]) {}

        BoundClassInterfaces(const ShaderProgram& shader) {}
        BoundClassInterfaces() {}
        ~BoundClassInterfaces() {}

        BoundClassInterfaces(BoundClassInterfaces&& moveFrom) {}
        BoundClassInterfaces& operator=(BoundClassInterfaces&& moveFrom) { return *this; }
    };

        ////////////////////////////////////////////////////////////////////////////////////////////////

	class ShaderResourceView;
	class UnorderedAccessView;
	class SamplerState;
	class Buffer;
	using ConstantBuffer = Buffer;
	class TextureView;
	class ObjectFactory;
	class DescriptorPool;
	class DummyResources;
	class DescriptorSetSignature;

	/// <summary>Bind uniforms at numeric binding points</summary>
	class NumericUniformsInterface
	{
	public:
		void    BindSRV(unsigned startingPoint, IteratorRange<const TextureView*const*> resources);
        void    BindUAV(unsigned startingPoint, IteratorRange<const TextureView*const*> resources);
        void    BindCB(unsigned startingPoint, IteratorRange<const VkBuffer*> uniformBuffers);
        void    BindSampler(unsigned startingPoint, IteratorRange<const VkSampler*> samplers);

        void    GetDescriptorSets(IteratorRange<VkDescriptorSet*> dst);
        bool    HasChanges() const;
        void    Reset();
		
		template<int Count> void Bind(const ResourceList<ShaderResourceView, Count>&);
		template<int Count> void Bind(const ResourceList<SamplerState, Count>&);
		template<int Count> void Bind(const ResourceList<ConstantBuffer, Count>&);
		template<int Count> void Bind(const ResourceList<UnorderedAccessView, Count>&);

        NumericUniformsInterface(
            const ObjectFactory& factory, DescriptorPool& descPool, 
            DummyResources& dummyResources,
            VkDescriptorSetLayout layout,
            const DescriptorSetSignature& signature);
		NumericUniformsInterface();
        ~NumericUniformsInterface();

        NumericUniformsInterface(NumericUniformsInterface&&);
        NumericUniformsInterface& operator=(NumericUniformsInterface&&);
    protected:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };

        ////////////////////////////////////////////////////////////////////////////////////////////////

	template<int Count> 
	    void    NumericUniformsInterface::Bind(const ResourceList<ShaderResourceView, Count>& shaderResources) 
	    {
			auto r = MakeIteratorRange(shaderResources._buffers);
	        BindSRV(
	            shaderResources._startingPoint,
	            MakeIteratorRange((const TextureView*const*)r.begin(), (const TextureView*const*)r.end()));
	    }
	
	template<int Count> void    NumericUniformsInterface::Bind(const ResourceList<SamplerState, Count>& samplerStates) 
	    {
			VkSampler samplers[Count];
			for (unsigned c=0; c<Count; ++c)
				samplers[c] = samplerStates._buffers[c]->GetUnderlying();
	        BindSampler(
	            samplerStates._startingPoint,
	            MakeIteratorRange(samplers));
	    }
	
	template<int Count> 
	    void    NumericUniformsInterface::Bind(const ResourceList<ConstantBuffer, Count>& constantBuffers) 
	    {
			VkBuffer buffers[Count];
			for (unsigned c=0; c<Count; ++c)
				buffers[c] = constantBuffers._buffers[c]->GetBuffer();
	        BindCB(
	            constantBuffers._startingPoint,
	            MakeIteratorRange(buffers));
	    }

	template<int Count> 
	    void    NumericUniformsInterface::Bind(const ResourceList<UnorderedAccessView, Count>& uavs)
	    {
			auto r = MakeIteratorRange(uavs._buffers);
	        BindUAV(
	            uavs._startingPoint,
	            MakeIteratorRange((const TextureView*const*)r.begin(), (const TextureView*const*)r.end()));
	    }

}}

