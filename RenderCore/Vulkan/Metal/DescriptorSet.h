// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "VulkanCore.h"
#include "../../../Utility/StringUtils.h"
#include "../../../Utility/IteratorUtils.h"

#include "IncludeVulkan.h"
#include <iosfwd>
#include <string>
#include <vector>

namespace RenderCore { class CompiledShaderByteCode; }

namespace RenderCore { namespace Metal_Vulkan
{
	class TextureView;
	class GlobalPools;
	class DescriptorSetSignature;
	class DescriptorSetSignatureFile;

	#if defined(VULKAN_VERBOSE_DESCRIPTIONS)
		class DescriptorSetVerboseDescription
		{
		public:
			struct BindingDescription
			{
				VkDescriptorType _descriptorType = (VkDescriptorType)~0u;
				std::string _description;
			};
			std::vector<BindingDescription> _bindingDescriptions;
			std::string _descriptorSetInfo;
		};		
	#endif

	class DescriptorSetBuilder
    {
    public:
		void    BindSRV(unsigned descriptorSetBindPoint, const TextureView* resource);
		void    BindUAV(unsigned descriptorSetBindPoint, const TextureView* resource);
		void    BindCB(unsigned descriptorSetBindPoint, VkDescriptorBufferInfo uniformBuffer, StringSection<> description = {});
		void    BindSampler(unsigned descriptorSetBindPoint, VkSampler sampler, StringSection<> description = {});

		uint64_t	BindDummyDescriptors(const DescriptorSetSignature& sig, uint64_t dummyDescWriteMask);

		bool		HasChanges() const;
		void		Reset();
		uint64_t	PendingWriteMask() const { return _pendingWrites; }

		uint64_t	FlushChanges(
			VkDevice device,
			VkDescriptorSet destination,
			VkDescriptorSet copyPrevDescriptors, uint64_t prevDescriptorMask
			VULKAN_VERBOSE_DESCRIPTIONS_ONLY(, DescriptorSetVerboseDescription& description));

		void		ValidatePendingWrites(const DescriptorSetSignature& sig);

		DescriptorSetBuilder(GlobalPools& globalPools);
		~DescriptorSetBuilder();
		DescriptorSetBuilder(const DescriptorSetBuilder&) = delete;
		DescriptorSetBuilder& operator=(const DescriptorSetBuilder&) = delete;

	private:
        static const unsigned s_pendingBufferLength = 32;

        VkDescriptorBufferInfo  _bufferInfo[s_pendingBufferLength];
        VkDescriptorImageInfo   _imageInfo[s_pendingBufferLength];
        VkWriteDescriptorSet    _writes[s_pendingBufferLength];

        unsigned	_pendingWrites = 0;
        unsigned	_pendingImageInfos = 0;
        unsigned	_pendingBufferInfos = 0;

        uint64_t		_sinceLastFlush;
		GlobalPools*	_globalPools;

		#if defined(VULKAN_VERBOSE_DESCRIPTIONS)
			DescriptorSetVerboseDescription _verboseDescription;
		#endif

		template<typename BindingInfo> 
			void WriteBinding(
				unsigned bindingPoint, VkDescriptorType type, const BindingInfo& bindingInfo, bool reallocateBufferInfo
				VULKAN_VERBOSE_DESCRIPTIONS_ONLY(, const std::string& description));
		template<typename BindingInfo>
			BindingInfo& AllocateInfo(const BindingInfo& init);
	};
}}
