// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../DebuggingDisplay.h"

namespace SceneEngine { class MaterialOverride; }

namespace Overlays
{
    using namespace RenderOverlays;
    using namespace RenderOverlays::DebuggingDisplay;

    class TestMaterialSettings : public IWidget ///////////////////////////////////////////////////////////
    {
    public:
        TestMaterialSettings(SceneEngine::MaterialOverride& materialSettings);
        ~TestMaterialSettings();
        void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState);
        bool    ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputSnapshot& input);

    private:
        ScrollBar   _scrollers[11];
        SceneEngine::MaterialOverride* _materialSettings;
    };


}

