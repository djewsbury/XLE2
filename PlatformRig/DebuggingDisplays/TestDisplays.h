// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../RenderOverlays/DebuggingDisplay.h"
#include <memory>

namespace PlatformRig { namespace Overlays
{
    using namespace RenderOverlays;
    using namespace RenderOverlays::DebuggingDisplay;

    class GridIteratorDisplay : public IWidget ///////////////////////////////////////////////////////////
    {
    public:
        void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState);
        bool    ProcessInput(InterfaceState& interfaceState, const InputContext& inputContext, const InputSnapshot& input);

        GridIteratorDisplay();
        ~GridIteratorDisplay();

    private:
        Coord2 _currentMousePosition;
    };

    class DualContouringTest : public IWidget ///////////////////////////////////////////////////////////
    {
    public:
        void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState);
        bool    ProcessInput(InterfaceState& interfaceState, const InputContext& inputContext, const InputSnapshot& input);

        DualContouringTest();
        ~DualContouringTest();
    };

    class ConservativeRasterTest : public IWidget ///////////////////////////////////////////////////////////
    {
    public:
        void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState);
        bool    ProcessInput(InterfaceState& interfaceState, const InputContext& inputContext, const InputSnapshot& input);

        ConservativeRasterTest();
        ~ConservativeRasterTest();
    };

    class RectanglePackerTest : public IWidget ///////////////////////////////////////////////////////////
    {
    public:
        void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState);
        bool    ProcessInput(InterfaceState& interfaceState, const InputContext& inputContext, const InputSnapshot& input);

        RectanglePackerTest();
        ~RectanglePackerTest();
    protected:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };
}}

