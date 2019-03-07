// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../DebuggingDisplay.h"
#include "../../RenderCore/Metal/Forward.h"
#include "../../RenderCore/IThreadContext_Forward.h"
#include <memory>

namespace Overlays
{
    using namespace RenderOverlays;
    using namespace RenderOverlays::DebuggingDisplay;

    class SharedBrowser : public IWidget
    {
    public:
        SharedBrowser(const char baseDirectory[], const std::string& headerName, unsigned itemDimensions, const std::string& fileFilter);
        ~SharedBrowser();
        void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState);
        bool    ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputContext& inputContext, const PlatformRig::InputSnapshot& input);

    private:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
        ScrollBar   _mainScrollBar;

        virtual std::pair<const RenderCore::Metal::ShaderResourceView*, uint64> GetSRV(RenderCore::IThreadContext& devContext, const std::basic_string<ucs2>&) = 0;
        virtual bool Filter(const std::basic_string<ucs2>&) = 0;
    };

    class ModelBrowser : public SharedBrowser
    {
    public:
        ModelBrowser(const char baseDirectory[]);
        ~ModelBrowser();

        class ProcessInputResult
        {
        public:
            std::string _selectedModel;
            bool _consumed;
            ProcessInputResult(bool consumed, const std::string& selected = std::string())
                : _consumed(consumed), _selectedModel(selected) {}
        };
        ProcessInputResult SpecialProcessInput(
            InterfaceState& interfaceState, const PlatformRig::InputContext& inputContext, const PlatformRig::InputSnapshot& input);

        Coord2  GetPreviewSize() const;
        auto    GetSRV(RenderCore::IThreadContext& context, const std::basic_string<ucs2>&) -> std::pair<const RenderCore::Metal::ShaderResourceView*, uint64>;

    private:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;

        bool Filter(const std::basic_string<ucs2>&);
    };

    class TextureBrowser : public SharedBrowser
    {
    public:
        TextureBrowser(const char baseDirectory[]);
        ~TextureBrowser();

    private:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;

        std::pair<const RenderCore::Metal::ShaderResourceView*, uint64> GetSRV(RenderCore::IThreadContext& devContext, const std::basic_string<ucs2>&);
        bool Filter(const std::basic_string<ucs2>&);
    };
}

