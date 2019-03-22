// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "FrameBuffer.h"
#include "Format.h"
#include "Resource.h"
#include "TextureView.h"
#include "DeviceContext.h"
#include "ObjectFactory.h"
#include "../../ResourceUtils.h"
#include "../../Format.h"
#include "../../../ConsoleRig/Log.h"
#include "../../../Utility/MemoryUtils.h"

namespace RenderCore { namespace Metal_Vulkan
{
    static VkAttachmentLoadOp AsLoadOp(LoadStore loadStore)
    {
        switch (loadStore)
        {
        default:
        case LoadStore::DontCare: 
        case LoadStore::DontCare_RetainStencil: 
        case LoadStore::DontCare_ClearStencil: 
            return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        case LoadStore::Retain: 
        case LoadStore::Retain_RetainStencil: 
        case LoadStore::Retain_ClearStencil: 
            return VK_ATTACHMENT_LOAD_OP_LOAD;
        case LoadStore::Clear: 
        case LoadStore::Clear_RetainStencil: 
        case LoadStore::Clear_ClearStencil: 
            return VK_ATTACHMENT_LOAD_OP_CLEAR;
        }
    }

    static VkAttachmentStoreOp AsStoreOp(LoadStore loadStore)
    {
        switch (loadStore)
        {
        default:
        case LoadStore::Clear: 
        case LoadStore::Clear_RetainStencil: 
        case LoadStore::Clear_ClearStencil: 
        case LoadStore::DontCare: 
        case LoadStore::DontCare_RetainStencil: 
        case LoadStore::DontCare_ClearStencil: 
            return VK_ATTACHMENT_STORE_OP_DONT_CARE;
        case LoadStore::Retain: 
        case LoadStore::Retain_RetainStencil: 
        case LoadStore::Retain_ClearStencil: 
            return VK_ATTACHMENT_STORE_OP_STORE;
        }
    }

    static VkAttachmentLoadOp AsLoadOpStencil(LoadStore loadStore)
    {
        switch (loadStore)
        {
        default:
        case LoadStore::Clear: 
        case LoadStore::DontCare: 
        case LoadStore::Retain: 
            return VK_ATTACHMENT_LOAD_OP_DONT_CARE;

        case LoadStore::Clear_ClearStencil: 
        case LoadStore::DontCare_ClearStencil: 
        case LoadStore::Retain_ClearStencil: 
            return VK_ATTACHMENT_LOAD_OP_CLEAR;

        case LoadStore::Clear_RetainStencil: 
        case LoadStore::DontCare_RetainStencil: 
        case LoadStore::Retain_RetainStencil: 
            return VK_ATTACHMENT_LOAD_OP_LOAD;
        }
    }

    static VkAttachmentStoreOp AsStoreOpStencil(LoadStore loadStore)
    {
        switch (loadStore)
        {
        default:
        case LoadStore::Clear: 
        case LoadStore::DontCare: 
        case LoadStore::Retain: 
        case LoadStore::Clear_ClearStencil: 
        case LoadStore::DontCare_ClearStencil: 
        case LoadStore::Retain_ClearStencil: 
            return VK_ATTACHMENT_STORE_OP_DONT_CARE;

        case LoadStore::Clear_RetainStencil: 
        case LoadStore::DontCare_RetainStencil: 
        case LoadStore::Retain_RetainStencil: 
            return VK_ATTACHMENT_STORE_OP_STORE;
        }
    }

	namespace Internal
	{
		namespace AttachmentUsageType
		{
			enum Flags
			{
				Input = 1<<0, Output = 1<<1, DepthStencil = 1<<2
			};
			using BitField = unsigned;
		}
	}

	static VkImageLayout AsShaderReadLayout(unsigned attachmentUsage)
	{
		if (attachmentUsage & unsigned(Internal::AttachmentUsageType::DepthStencil))
			return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	static bool HasRetain(LoadStore ls)
	{
		return ls == LoadStore::Retain
			|| ls == LoadStore::DontCare_RetainStencil
			|| ls == LoadStore::Retain_RetainStencil
			|| ls == LoadStore::Clear_RetainStencil
			|| ls == LoadStore::Retain_ClearStencil;
	}

	static void MergeFormatFilter(TextureViewDesc::FormatFilter& dst, TextureViewDesc::FormatFilter src)
	{
		if (dst._aspect == TextureViewDesc::Aspect::Depth) {
			if (src._aspect == TextureViewDesc::Aspect::DepthStencil || src._aspect == TextureViewDesc::Aspect::Stencil) {
				dst = TextureViewDesc::Aspect::DepthStencil;
				return;
			}
		} else if (dst._aspect == TextureViewDesc::Aspect::Stencil) {
			if (src._aspect == TextureViewDesc::Aspect::Depth || src._aspect == TextureViewDesc::Aspect::DepthStencil) {
				dst = TextureViewDesc::Aspect::DepthStencil;
				return;
			}
		} else if (dst._aspect == TextureViewDesc::Aspect::DepthStencil) {
			if (src._aspect == TextureViewDesc::Aspect::Depth || src._aspect == TextureViewDesc::Aspect::Stencil || src._aspect == TextureViewDesc::Aspect::DepthStencil) {
				dst = TextureViewDesc::Aspect::DepthStencil;
				return;
			}
		}

		assert(dst._aspect == src._aspect
			|| dst._aspect == TextureViewDesc::Aspect::UndefinedAspect
			|| src._aspect == TextureViewDesc::Aspect::UndefinedAspect);
		if (src._aspect != TextureViewDesc::Aspect::UndefinedAspect)
			dst._aspect = src._aspect;
	}

	VulkanUniquePtr<VkRenderPass> CreateRenderPass(
        const Metal_Vulkan::ObjectFactory& factory,
        const FrameBufferDesc& layout,
        const INamedAttachments& namedResources,
        TextureSamples samples)
	{
		const auto subpasses = layout.GetSubpasses();

		struct AttachmentUsage
		{
			unsigned _subpassIdx = ~0u;
			Internal::AttachmentUsageType::BitField _usage = 0;
			LoadStore _loadStore = LoadStore::DontCare;
		};
		struct WorkingAttachment
		{
			AttachmentUsage _lastSubpassWrite;
			AttachmentUsage _lastSubpassRead;
			TextureViewDesc::FormatFilter _formatFilter;
			Internal::AttachmentUsageType::BitField _attachmentUsage = 0;
		};
		std::vector<std::pair<AttachmentName, WorkingAttachment>> workingAttachments;
		workingAttachments.reserve(subpasses.size()*2);	// approximate

		struct SubpassDependency
		{
			AttachmentName _resource;
			AttachmentUsage _first;
			AttachmentUsage _second;
		};
		std::vector<SubpassDependency> dependencies;
		dependencies.reserve(subpasses.size()*2);	// approximate

		////////////////////////////////////////////////////////////////////////////////////
		// Build up the list of subpass dependencies and the set of unique attachments
        for (unsigned spIdx=0; spIdx<subpasses.size(); spIdx++) {
			const auto& spDesc = subpasses[spIdx];

			std::vector<std::pair<const AttachmentViewDesc*, Internal::AttachmentUsageType::BitField>> subpassAttachments;
			for (const auto& r:spDesc._output) 
				subpassAttachments.push_back({&r, Internal::AttachmentUsageType::Output});
			if (spDesc._depthStencil._resourceName != SubpassDesc::Unused._resourceName)
				subpassAttachments.push_back({&spDesc._depthStencil, Internal::AttachmentUsageType::DepthStencil});
			for (const auto& r:spDesc._input) 
				subpassAttachments.push_back({&r, Internal::AttachmentUsageType::Input});

			//////////////////////////////////////////////////////////////////////////////////////////

			for (const auto& spa:subpassAttachments) {
				const auto&r = *spa.first;
				auto resource = r._resourceName;
				auto i = LowerBound(workingAttachments, resource);
				if (i == workingAttachments.end() || i->first != resource)
					i = workingAttachments.insert(i, {resource, WorkingAttachment{}});

				AttachmentUsage loadUsage { spIdx, spa.second, r._loadFromPreviousPhase };
				AttachmentUsage storeUsage { spIdx, spa.second, r._storeToNextPhase };

				// If we're loading data from a previous phase, we've got to find it in
				// the working attachments, and create a subpass dependency
				// Otherwise, if there are any previous contents, they 
				// will be destroyed.
				// We do this even if there's not an explicit retain on the load step
				//	-- we assume "retain" between subpasses, even if the views contradict that
				//	(as per Vulkan, where LoadStore is only for the input/output of the entire render pass)
				dependencies.push_back({resource, i->second._lastSubpassWrite, loadUsage});

				// We also need a dependency with the last subpass to read from this 
				// attachment. We can't write to it until the reading is finished
				if (spa.second & (Internal::AttachmentUsageType::Output | Internal::AttachmentUsageType::DepthStencil)) {
					if (i->second._lastSubpassRead._subpassIdx != ~0u)
						dependencies.push_back({resource, i->second._lastSubpassRead, loadUsage});

					i->second._lastSubpassWrite = storeUsage;
				} else {
					i->second._lastSubpassRead = loadUsage;

					// If the data isn't marked as "retained" after this read, we must clear out the 
					// last subpass write flags
					if (!HasRetain(r._storeToNextPhase))
						i->second._lastSubpassWrite = {};
				}

				MergeFormatFilter(i->second._formatFilter, r._window._format);
				i->second._attachmentUsage |= spa.second;
			}
		}

		////////////////////////////////////////////////////////////////////////////////////
		// Build the VkAttachmentDescription objects
		std::vector<VkAttachmentDescription> attachmentDesc;
        attachmentDesc.reserve(workingAttachments.size());
        for (auto&a:workingAttachments) {
            const auto* resourceDesc = namedResources.GetDesc(a.first);
            assert(resourceDesc);

            auto formatFilter = a.second._formatFilter;
            FormatUsage formatUsage = FormatUsage::SRV;
            if (a.second._attachmentUsage & Internal::AttachmentUsageType::Output) formatUsage = FormatUsage::RTV;
            if (a.second._attachmentUsage & Internal::AttachmentUsageType::DepthStencil) formatUsage = FormatUsage::DSV;
            auto resolvedFormat = ResolveFormat(resourceDesc->_format, formatFilter, formatUsage);

			// look through the subpass dependencies to find the load operation for the first reference to this resource
			LoadStore originalLoad = LoadStore::DontCare;
			for (const auto& d:dependencies) {
				if (d._resource == a.first && d._first._subpassIdx == ~0u) {
					// this is a load from pre-renderpass
					assert(originalLoad == LoadStore::DontCare || originalLoad == d._second._loadStore);
					originalLoad = d._second._loadStore;
				}
			}
			LoadStore finalStore = a.second._lastSubpassWrite._loadStore;

            VkAttachmentDescription desc;
            desc.flags = 0;
            desc.format = AsVkFormat(resolvedFormat);
            desc.samples = VK_SAMPLE_COUNT_1_BIT;
            desc.loadOp = AsLoadOp(originalLoad);
            desc.stencilLoadOp = AsLoadOpStencil(originalLoad);
			desc.storeOp = AsStoreOp(finalStore);
            desc.stencilStoreOp = AsStoreOpStencil(finalStore);

            desc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            desc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            bool isDepthStencil = !!(a.second._attachmentUsage & unsigned(Internal::AttachmentUsageType::DepthStencil));// !!(resourceDesc->_flags & AttachmentDesc::Flags::DepthStencil);
            if (isDepthStencil) {
                desc.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                desc.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            }

            // Assume that that attachments marked "ShaderResource" will begin and end in the 
            // shader read layout. This should be fine for cases where a single render pass
            // is used to write to a texture, and then it is read subsequentially. 
            //
            // However, if there are multiple writing render passes, followed by a shader
            // read at some later point, then this may switch to shader read layout redundantly
            if (resourceDesc->_flags & AttachmentDesc::Flags::ShaderResource) {
                desc.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                desc.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }

            // desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            // desc.finalLayout = VK_IMAGE_LAYOUT_UNDEFINED;

            if (resourceDesc->_flags & AttachmentDesc::Flags::PresentationSource) {
                desc.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                desc.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            } 
            
            if (resourceDesc->_flags & AttachmentDesc::Flags::Multisampled)
                desc.samples = AsSampleCountFlagBits(samples);

            // note --  do we need to set VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL or 
            //          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL as appropriate for input attachments
            //          (we should be able to tell this from the subpasses)?

            attachmentDesc.push_back(desc);
        }


		////////////////////////////////////////////////////////////////////////////////////
		// Build the actual VkSubpassDescription objects

		std::vector<VkAttachmentReference> attachReferences;
        std::vector<uint32_t> preserveAttachments;

        std::vector<VkSubpassDescription> subpassDesc;
        subpassDesc.reserve(subpasses.size());
        for (auto&p:subpasses) {
            VkSubpassDescription desc;
            desc.flags = 0;
            desc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

            // Input attachments are going to be difficult, because they must be bound both
            // by the sub passes and by the descriptor set! (and they must be explicitly listed as
            // input attachments in the shader). Holy cow, the render pass, frame buffer, pipeline
            // layout, descriptor set and shader must all agree!
            auto beforeInputs = attachReferences.size();
            for (auto& a:p._input) {
				auto resource = a._resourceName;
				auto i = LowerBound(workingAttachments, resource);
				assert(i != workingAttachments.end() && i->first == resource);
				auto internalName = std::distance(workingAttachments.begin(), i);
				attachReferences.push_back(VkAttachmentReference{(uint32_t)internalName, AsShaderReadLayout(i->second._attachmentUsage)});
            }
            desc.pInputAttachments = (const VkAttachmentReference*)(beforeInputs+1);
            desc.inputAttachmentCount = uint32_t(attachReferences.size() - beforeInputs);

            auto beforeOutputs = attachReferences.size();
            for (auto& a:p._output) {
				auto resource = a._resourceName;
				auto i = LowerBound(workingAttachments, resource);
				assert(i != workingAttachments.end() && i->first == resource);
				auto internalName = std::distance(workingAttachments.begin(), i);
				attachReferences.push_back(VkAttachmentReference{(uint32_t)internalName, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}); // AsShaderReadLayout(i->second._attachmentUsage)});
            }
            desc.pColorAttachments = (const VkAttachmentReference*)(beforeOutputs+1);
            desc.colorAttachmentCount = uint32_t(attachReferences.size() - beforeOutputs);
            desc.pResolveAttachments = nullptr; // not supported
			desc.pPreserveAttachments = nullptr;
			desc.preserveAttachmentCount = 0;

            if (p._depthStencil._resourceName != SubpassDesc::Unused._resourceName) {
				auto resource = p._depthStencil._resourceName;
				auto i = LowerBound(workingAttachments, resource);
				assert(i != workingAttachments.end() && i->first == resource);
				auto internalName = std::distance(workingAttachments.begin(), i);
				desc.pDepthStencilAttachment = (const VkAttachmentReference*)(attachReferences.size()+1);
				attachReferences.push_back(VkAttachmentReference{(uint32_t)internalName, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}); // AsShaderReadLayout(i->second._attachmentUsage)});
            } else {
                desc.pDepthStencilAttachment = nullptr;
            }

			// preserve & resolve attachments not supported currently

            subpassDesc.push_back(desc);
        }

        // we need to do a fixup pass over all of the subpasses to generate correct pointers
        for (auto&p:subpassDesc) {
            if (p.pInputAttachments)
                p.pInputAttachments = AsPointer(attachReferences.begin()) + size_t(p.pInputAttachments)-1;
            if (p.pColorAttachments)
                p.pColorAttachments = AsPointer(attachReferences.begin()) + size_t(p.pColorAttachments)-1;
            if (p.pResolveAttachments)
                p.pResolveAttachments = AsPointer(attachReferences.begin()) + size_t(p.pResolveAttachments)-1;
            if (p.pDepthStencilAttachment)
                p.pDepthStencilAttachment = AsPointer(attachReferences.begin()) + size_t(p.pDepthStencilAttachment)-1;
            if (p.pPreserveAttachments)
                p.pPreserveAttachments = AsPointer(preserveAttachments.begin()) + size_t(p.pPreserveAttachments)-1;
        }

		////////////////////////////////////////////////////////////////////////////////////
		// Build the actual VkSubpassDependency objects

        std::vector<VkSubpassDependency> vkDeps;
		for (unsigned c=0;c<unsigned(subpasses.size()); ++c) {
			// Find the list of SubPassDependency objects where _second is this subpass. We'll
			// then find the unique list of subpasses referenced by _first, and generate the
			// Vulkan object from them.
			//
			// Note that there are implicit dependencies to "VK_SUBPASS_EXTERNAL" which are defined
			// with a standard form. We'll rely on those implicit dependencies, rather than 
			// explicitly creating them here.

			std::vector<SubpassDependency> terminatingDependencies;
			for (const auto& d:dependencies)
				if (d._second._subpassIdx == c && d._first._subpassIdx != ~0u)
					terminatingDependencies.push_back(d);

			std::vector<VkSubpassDependency> deps;
			for (const auto& d:terminatingDependencies) {
				auto i = std::find_if(
					deps.begin(), deps.end(),
					[&d](const VkSubpassDependency& vkd) { return vkd.srcSubpass == d._first._subpassIdx; });
				if (i == deps.end())
					i = deps.insert(deps.end(), {
						d._first._subpassIdx, c, 
						// note -- making assumptions about attachments usage here -- (in particular, ignoring shader resources bound to shaders other than the fragment shader)
						VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        0, 0,	// access flags set below
						0});

				if (d._first._usage | Internal::AttachmentUsageType::Output)
					i->srcAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				if (d._first._usage | Internal::AttachmentUsageType::DepthStencil)
					i->srcAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				if (d._first._usage | Internal::AttachmentUsageType::Input)
					i->srcAccessMask |= VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;

				if (d._second._usage | Internal::AttachmentUsageType::Output)
					i->dstAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				if (d._second._usage | Internal::AttachmentUsageType::DepthStencil)
					i->dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				if (d._second._usage | Internal::AttachmentUsageType::Input)
					i->dstAccessMask |= VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
			}

			vkDeps.insert(vkDeps.end(), deps.begin(), deps.end());
        }

		// Also create subpass dependencies for every subpass. This is required currently, because we can sometimes
		// use vkCmdPipelineBarrier with a global memory barrier to push through dynamic constants data. However, this
		// might defeat some of the key goals of the render pass system!
		/*
			Note -- this currently crashes the VK_LAYER_LUNARG_draw_state validation code... But it seems like we need it!
		for (unsigned c = 0; c<unsigned(subpasses.size()); ++c) {
			vkDeps.push_back(VkSubpassDependency{
				c, c, 
				VK_PIPELINE_STAGE_HOST_BIT,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				VK_ACCESS_HOST_WRITE_BIT,
				VK_ACCESS_INDIRECT_COMMAND_READ_BIT
				| VK_ACCESS_INDEX_READ_BIT
				| VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT
				| VK_ACCESS_UNIFORM_READ_BIT
				| VK_ACCESS_INPUT_ATTACHMENT_READ_BIT
				| VK_ACCESS_SHADER_READ_BIT,
				0});
		}
		*/

		////////////////////////////////////////////////////////////////////////////////////
		// Build the final render pass object

        VkRenderPassCreateInfo rp_info = {};
        rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp_info.pNext = nullptr;
        rp_info.attachmentCount = (uint32_t)attachmentDesc.size();
        rp_info.pAttachments = attachmentDesc.data();
        rp_info.subpassCount = (uint32_t)subpassDesc.size();
        rp_info.pSubpasses = subpassDesc.data();
        rp_info.dependencyCount = (uint32_t)vkDeps.size();
        rp_info.pDependencies = vkDeps.data();

        return factory.CreateRenderPass(rp_info);
	}

	struct MaxDims
	{
		unsigned _width = 0, _height = 0;
		unsigned _layers = 0;
	};

	static void BuildMaxDims(MaxDims& result, AttachmentName attachment, const INamedAttachments& namedResources, const FrameBufferProperties& props)
	{
		// This is annoying -- we need to look for any resources with
        // array layers. Ideally all our resources should have the same
        // array layer count (if they don't, we just end up selecting the
        // largest value)
        auto* desc = namedResources.GetDesc(attachment);
        if (desc) {
            result._layers = std::max(result._layers, desc->_arrayLayerCount);
            unsigned resWidth = 0u, resHeight = 0u;
                
            if (desc->_dimsMode == AttachmentDesc::DimensionsMode::Absolute) {
                resWidth = unsigned(desc->_width);
                resHeight = unsigned(desc->_height);
            } else {
                resWidth = unsigned(std::floor(props._outputWidth * desc->_width + 0.5f));
                resHeight = unsigned(std::floor(props._outputHeight * desc->_height + 0.5f));
            }

            result._width = std::max(result._width, resWidth);
            result._height = std::max(result._height, resHeight);
        }
	}

    FrameBuffer::FrameBuffer(
        const ObjectFactory& factory,
        const FrameBufferDesc& fbDesc,
        const INamedAttachments& namedResources)
    {
		const auto& props = namedResources.GetFrameBufferProperties();
		// todo --	we shouldn't create the render pass every time.
		//			we need to be able to share equivalent vkRenderPasses, so that
		//			there are fewer pipeline objects required
		_layout = CreateRenderPass(factory, fbDesc, namedResources, props._samples);

        // We must create the frame buffer, including all views required.
        // We need to order the list of views in VkFramebufferCreateInfo in the
		// same order as the attachments were defined in the VkRenderPass object.
		auto subpasses = fbDesc.GetSubpasses();

        MaxDims maxDims;
		std::vector<std::pair<AttachmentName, Internal::AttachmentUsageType::BitField>> attachments;
		attachments.reserve(subpasses.size()*4);	// estimate
		for (unsigned c=0; c<(unsigned)subpasses.size(); ++c) {
			const auto& spDesc = subpasses[c];

			for (const auto& r:spDesc._output) {
				attachments.push_back({r._resourceName, Internal::AttachmentUsageType::Output});
				BuildMaxDims(maxDims, r._resourceName, namedResources, props);
			}

			if (spDesc._depthStencil._resourceName != SubpassDesc::Unused._resourceName) {
				attachments.push_back({spDesc._depthStencil._resourceName, Internal::AttachmentUsageType::DepthStencil});
				BuildMaxDims(maxDims, spDesc._depthStencil._resourceName, namedResources, props);
			}

			for (const auto& r:spDesc._input) {
				// todo -- these srvs also need to be exposed to the caller, so they can be bound to
				// the shader during the subpass
				attachments.push_back({r._resourceName, Internal::AttachmentUsageType::Input});
				BuildMaxDims(maxDims, r._resourceName, namedResources, props);
			}
        }

		// Sort by AttachmentName, and combine multiple references to the same resource into a single view
		std::sort(attachments.begin(), attachments.end(), CompareFirst<AttachmentName, Internal::AttachmentUsageType::BitField>());
		std::vector<std::pair<AttachmentName, Internal::AttachmentUsageType::BitField>> uniqueAttachments;
		uniqueAttachments.reserve(attachments.size());

		for (auto i=attachments.begin(); i!=attachments.end();) {
			auto i2 = i;
			Internal::AttachmentUsageType::BitField mergedUsage = 0;
			while (i2!=attachments.end() && i2->first == i->first) { 
				mergedUsage |= i2->second;
				++i2;
			}
			uniqueAttachments.push_back({i->first, mergedUsage});
			i = i2;
		}

		ViewPool<TextureView> viewPool;
        VkImageView rawViews[16];
		unsigned rawViewCount = 0;

        for (const auto&a:uniqueAttachments) {
			// Note that we can't support TextureViewDesc properly here, because we don't support 
			// the same resource being used with more than one view
			auto resource = namedResources.GetResource(a.first);
			auto* rtv = viewPool.GetView(resource, TextureViewDesc{});
			rawViews[rawViewCount++] = rtv->GetImageView();
        }

        VkFramebufferCreateInfo fb_info = {};
        fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_info.pNext = nullptr;
        fb_info.renderPass = _layout.get();
        fb_info.attachmentCount = rawViewCount;
        fb_info.pAttachments = rawViews;
        fb_info.width = maxDims._width;
        fb_info.height = maxDims._height;
        fb_info.layers = std::max(1u, maxDims._layers);
        _underlying = factory.CreateFramebuffer(fb_info);
		_subpassCount = (unsigned)fbDesc.GetSubpasses().size();

        // todo --  do we need to create a "patch up" command buffer to assign the starting image layouts
        //          for all of the images we created?
    }

	FrameBuffer::FrameBuffer() : _subpassCount(0) {}
    FrameBuffer::~FrameBuffer() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    void BeginRenderPass(
        DeviceContext& context,
        FrameBuffer& frameBuffer,
        const FrameBufferDesc& layout,
        const FrameBufferProperties& props,
        IteratorRange<const ClearValue*> clearValues)
    {
        context.BeginRenderPass(
            frameBuffer, TextureSamples::Create(),
            {0u, 0u}, {props._outputWidth, props._outputHeight},
            clearValues);
    }

    void BeginNextSubpass(DeviceContext& context, FrameBuffer& frameBuffer)
    {
		if ((context.RenderPassSubPassIndex()+1) < frameBuffer.GetSubpassCount())
			context.NextSubpass(VK_SUBPASS_CONTENTS_INLINE);
    }

	void EndSubpass(DeviceContext& context)
	{
	}

    void EndRenderPass(DeviceContext& context)
    {
        context.EndRenderPass();
    }

	unsigned GetCurrentSubpassIndex(DeviceContext& context)
	{
		return context.RenderPassSubPassIndex();
	}

}}

