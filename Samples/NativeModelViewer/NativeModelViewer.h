// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Shared/SampleRig.h"

namespace SceneEngine { class IScene; }

namespace Sample
{
	class SampleLightingDelegate;

	class NativeModelViewerOverlay : virtual public PlatformRig::OverlaySystemSet, virtual public ISampleOverlay
	{
	public:
		virtual void OnUpdate(float deltaTime) override;
		virtual void OnStartup(const SampleGlobals& globals) override;

		void Render(
            RenderCore::IThreadContext& threadContext,
			const RenderCore::IResourcePtr& renderTarget,
            RenderCore::Techniques::ParsingContext& parserContext) override;
		std::shared_ptr<IInputListener> GetInputListener() override;
        void SetActivationState(bool newState) override;

		NativeModelViewerOverlay();
		~NativeModelViewerOverlay();
	};
}