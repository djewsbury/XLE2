// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "MainInputHandler.h"
#include "../RenderOverlays/DebuggingDisplay.h"
#include "../Utility/StringUtils.h"
#include <assert.h>

namespace PlatformRig
{
    bool    MainInputHandler::OnInputEvent(const InputContext& context, const InputSnapshot& evnt)
    {
        bool consumed = false;
        for (auto i=_listeners.cbegin(); i!=_listeners.cend() && !consumed; ++i) {
            consumed |= (*i)->OnInputEvent(context, evnt);
        }
        return consumed;
    }

    void    MainInputHandler::AddListener(std::shared_ptr<IInputListener> listener)
    {
        assert(listener);
        _listeners.push_back(std::move(listener));
    }

    MainInputHandler::MainInputHandler()
    {}

    MainInputHandler::~MainInputHandler() {}



    bool    DebugScreensInputHandler::OnInputEvent(const InputContext& context, const InputSnapshot& evnt)
    {
        static const KeyId escape = KeyId_Make("escape");
        if (evnt.IsPress(escape)) {
            if (_debugScreens->CurrentScreen(0)) {
                _debugScreens->SwitchToScreen(0, nullptr);
                return true;
            }
        }

        return _debugScreens && _debugScreens->OnInputEvent(context, evnt);
    }

    DebugScreensInputHandler::DebugScreensInputHandler(std::shared_ptr<RenderOverlays::DebuggingDisplay::DebugScreensSystem> debugScreens)
    : _debugScreens(std::move(debugScreens)) 
    {}

    DebugScreensInputHandler::~DebugScreensInputHandler() {}
}

