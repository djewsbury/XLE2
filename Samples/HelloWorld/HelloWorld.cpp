// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "HelloWorld.h"
#include "BasicScene.h"
#include "../Shared/SampleRig.h"
#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../RenderCore/Techniques/RenderPassUtils.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../SceneEngine/LightingParserContext.h"
#include "../../ConsoleRig/Console.h"
#include <optional>

namespace Sample
{
	void RenderPostScene(RenderCore::IThreadContext& context);

	class HelloWorldOverlay::InputListener : public RenderOverlays::DebuggingDisplay::IInputListener
	{
	public:
		bool    OnInputEvent(const RenderOverlays::DebuggingDisplay::InputSnapshot& evnt)
		{
			_zoomFactor += evnt._wheelDelta / (16.f * 180.f);
			_zoomFactor = std::max(0.0f, _zoomFactor);

			if (!(evnt._mouseButtonsDown & 1)) {
				_mouseAnchor = {};
				return false;
			}

			if (_mouseAnchor.has_value()) {
				static float rotationSpeed = -gPI / 1000.0f;
				_rotations[1] += (evnt._mousePosition[0] - _mouseAnchor.value()[0]) * rotationSpeed;
				_rotations[0] += (evnt._mousePosition[1] - _mouseAnchor.value()[1]) * rotationSpeed;
				_rotations[0] = std::max(0.01f * gPI, _rotations[0]);
				_rotations[0] = std::min(0.99f * gPI, _rotations[0]);
			}
			_mouseAnchor = evnt._mousePosition;
			return true;
		}
		~InputListener() {}

		Float2 _rotations = { 0.5f * gPI, 0.5f * gPI };
		float _zoomFactor = 0.f;
		std::optional<RenderOverlays::DebuggingDisplay::Coord2> _mouseAnchor;
	};

	void HelloWorldOverlay::Render(
        RenderCore::IThreadContext& threadContext,
		const RenderCore::IResourcePtr& renderTarget,
		RenderCore::Techniques::ParsingContext& parsingContext)
	{
        SceneEngine::LightingParserContext lightingParserContext;
		if (_scene) {
			auto samples = RenderCore::TextureSamples::Create((uint8)Tweakable("SamplingCount", 1), (uint8)Tweakable("SamplingQuality", 0));
			// auto stdPlugin = std::make_shared<SceneEngine::LightingParserStandardPlugin>();
            lightingParserContext = LightingParser_ExecuteScene(
                threadContext, renderTarget, parsingContext, *_scene, 
				CalculateCameraDesc(_inputListener->_rotations, _inputListener->_zoomFactor, _lightingDelegate->GetTimeValue()),
                SceneEngine::RenderSceneSettings {
                    (Tweakable("LightingModel", 0) == 0) ? SceneEngine::RenderSceneSettings::LightingModel::Deferred : SceneEngine::RenderSceneSettings::LightingModel::Forward,
					_lightingDelegate.get(),
					{},
					samples._sampleCount, samples._samplingQuality } );
        }

		{
			auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(threadContext, renderTarget, parsingContext);
			RenderPostScene(threadContext);
			SceneEngine::LightingParser_Overlays(threadContext, parsingContext, lightingParserContext);
		}
	}

	auto HelloWorldOverlay::GetInputListener() -> std::shared_ptr<IInputListener>
	{
		return _inputListener;
	}

	void HelloWorldOverlay::OnUpdate(float deltaTime)
	{
		_lightingDelegate->Update(deltaTime);
	}

	void HelloWorldOverlay::OnStartup(const SampleGlobals& globals)
	{
		_scene = std::make_shared<BasicSceneParser>();
		_lightingDelegate = std::make_shared<SampleLightingDelegate>();
		_inputListener = std::make_shared<InputListener>();
	}
    
}

