// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../RenderOverlays/DebuggingDisplay.h"

namespace Utility { class IHierarchicalProfiler; }

namespace PlatformRig { namespace Overlays
{
    class HierarchicalSpikesDisplay : public RenderOverlays::DebuggingDisplay::IWidget ///////////////////////////////////////////////////////////
    {
    public:
        typedef RenderOverlays::IOverlayContext IOverlayContext;
        typedef RenderOverlays::DebuggingDisplay::Layout Layout;
        typedef RenderOverlays::DebuggingDisplay::Interactables Interactables;
        typedef RenderOverlays::DebuggingDisplay::InterfaceState InterfaceState;
        typedef RenderOverlays::DebuggingDisplay::InputSnapshot InputSnapshot;

        void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState);
        bool    ProcessInput(InterfaceState& interfaceState, const InputSnapshot& input);

        HierarchicalSpikesDisplay(IHierarchicalProfiler* profiler);
        ~HierarchicalSpikesDisplay();
    private:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };
}}

