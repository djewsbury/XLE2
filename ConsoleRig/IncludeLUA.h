// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#undef new
    #include <../../../Foreign/Lua/src/lua.hpp>
    #include <../../../Foreign/LuaBridge/Source/LuaBridge/LuaBridge.h>
#if defined(DEBUG_NEW)
    #define new DEBUG_NEW
#endif

