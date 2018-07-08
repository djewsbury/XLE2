// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "EngineControl.h"
#include <memory>

namespace RenderCore { namespace Techniques { class  TechniqueContext; class AttachmentPool; class FrameBufferPool; }}
namespace SceneEngine { class LightingParserStandardPlugin; }

namespace GUILayer 
{
    class LayerControlPimpl;

    ref class ModelVisSettings;
    ref class IOverlaySystem;
    ref class VisCameraSettings;
    ref class VisMouseOver;
    ref class TechniqueContextWrapper;
    ref class VisResources;

    public ref class LayerControl : public EngineControl
    {
    public:
        void SetupDefaultVis(ModelVisSettings^ settings, VisMouseOver^ mouseOver, VisResources^ resources);
        VisMouseOver^ CreateVisMouseOver(ModelVisSettings^ settings, VisResources^ resources);
        VisResources^ CreateVisResources();

        void AddDefaultCameraHandler(VisCameraSettings^);
        void AddSystem(IOverlaySystem^ overlay);
        void SetUpdateAsyncMan(bool updateAsyncMan);

        TechniqueContextWrapper^ GetTechniqueContext();

        LayerControl(System::Windows::Forms::Control^ control);
        ~LayerControl();
        !LayerControl();
    protected:
        clix::auto_ptr<LayerControlPimpl> _pimpl;
        TechniqueContextWrapper^ _techContextWrapper;

        virtual bool Render(RenderCore::IThreadContext&, IWindowRig&) override;
    };

    class LayerControlPimpl 
    {
    public:
        std::shared_ptr<SceneEngine::LightingParserStandardPlugin> _stdPlugin;
        std::shared_ptr<RenderCore::Techniques::TechniqueContext> _globalTechniqueContext;
        std::shared_ptr<RenderCore::Techniques::AttachmentPool> _namedResources;
		std::shared_ptr<RenderCore::Techniques::FrameBufferPool> _frameBufferPool;
        bool _activePaint;
    };

}


