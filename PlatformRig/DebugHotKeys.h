// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/StringUtils.h"
#include <memory>

namespace PlatformRig
{
	class IInputListener;
    std::unique_ptr<IInputListener> MakeHotKeysHandler(StringSection<> filename);
}

