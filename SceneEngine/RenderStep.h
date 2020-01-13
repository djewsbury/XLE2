// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "SceneParser.h"
#include "LightDesc.h"
#include "Tonemap.h"		// (for LuminanceResult)
#include "../RenderCore/Techniques/Drawables.h"
#include "../RenderCore/Techniques/RenderPass.h"
#include <memory>

namespace RenderCore { namespace Techniques 
{
	class FrameBufferDescFragment;
	class RenderPassFragment;
	class ITechniqueDelegate;
	class SequencerConfig;
}}

namespace SceneEngine
{
	class IViewDelegate
	{
	public:
		virtual RenderCore::Techniques::DrawablesPacket* GetDrawablesPacket(RenderCore::Techniques::BatchFilter batch) = 0;
		virtual ~IViewDelegate();
	};

	class LightingParserContext;

	class RenderStepFragmentInterface
	{
	public:
		RenderCore::AttachmentName DefineAttachment(uint64_t semantic, const RenderCore::AttachmentDesc& request = {});
        RenderCore::AttachmentName DefineTemporaryAttachment(const RenderCore::AttachmentDesc& request);
        void AddSubpass(
			RenderCore::SubpassDesc&& subpass,
			const std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate>& techniqueDelegate = nullptr,
			ParameterBox&& sequencerSelectors = {});

		RenderStepFragmentInterface(RenderCore::PipelineType);
		~RenderStepFragmentInterface();

		const RenderCore::Techniques::FrameBufferDescFragment& GetFrameBufferDescFragment() const { return _frameBufferDescFragment; }
	private:
		RenderCore::Techniques::FrameBufferDescFragment _frameBufferDescFragment;

		struct SubpassExtension
		{
			std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate> _techniqueDelegate;
			ParameterBox _sequencerSelectors;
		};
		std::vector<SubpassExtension> _subpassExtensions;

		IteratorRange<const SubpassExtension*> GetSubpassAddendums() const { return MakeIteratorRange(_subpassExtensions); }
		friend class CompiledSceneTechnique;
	};

	class RenderStepFragmentInstance : public RenderCore::Techniques::RenderPassFragment
	{
	public:
		const RenderCore::Techniques::SequencerConfig* GetSequencerConfig() const;

		RenderStepFragmentInstance(
			RenderCore::Techniques::RenderPassInstance& rpi,
            const RenderCore::Techniques::FrameBufferFragmentMapping& mapping,
			IteratorRange<const std::shared_ptr<RenderCore::Techniques::SequencerConfig>*> sequencerConfigs);
		RenderStepFragmentInstance();
	private:
		IteratorRange<const std::shared_ptr<RenderCore::Techniques::SequencerConfig>*> _sequencerConfigs;
	};

	class IRenderStep
	{
	public:
		virtual std::shared_ptr<IViewDelegate> CreateViewDelegate();
		virtual const RenderStepFragmentInterface& GetInterface() const = 0;
		virtual void Execute(
			RenderCore::IThreadContext& threadContext,
			RenderCore::Techniques::ParsingContext& parsingContext,
			LightingParserContext& lightingParserContext,
			RenderStepFragmentInstance& rpi,
			IViewDelegate* viewDelegate) = 0;
		virtual ~IRenderStep();
	};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////

	std::shared_ptr<IRenderStep> CreateRenderStep_Forward(bool precisionTargets);
	std::shared_ptr<IRenderStep> CreateRenderStep_Direct();
	std::shared_ptr<IRenderStep> CreateRenderStep_GBuffer(unsigned gbufferType, bool precisionTargets);
	std::shared_ptr<IRenderStep> CreateRenderStep_LightingResolve(bool precisionTargets);
	
}
