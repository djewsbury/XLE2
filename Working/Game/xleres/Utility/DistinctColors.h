// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

// This is a random but distinct set of colours generated from
// 		http://phrogz.net/css/distinct-colors.html

#if !defined(DISTINCT_COLORS)
#define DISTINCT_COLORS

#include "../Binding.h"

#if 0
    static const uint ColourListCount = 47;
    static const uint3 ColourList[47] =
    {
        uint3(217,0,79), uint3(0,81,255), uint3(234,255,0), uint3(127,28,0), uint3(239,61,242),
        uint3(48,155,191), uint3(29,115,43), uint3(76,44,19), uint3(77,101,153), uint3(115,57,57),

        uint3(191,225,255), uint3(217,198,163), uint3(89,0,71), uint3(0,77,62), uint3(255,166,0),
        uint3(89,0,0), uint3(35,46,140), uint3(19,62,77), uint3(31,51,13), uint3(153,77,105),

        uint3(195,204,102), uint3(191,143,151), uint3(86,114,115), uint3(64,55,48), uint3(42,0,77),
        uint3(0,179,30), uint3(89,58,0), uint3(51,13,27), uint3(16,31,64), uint3(54,211,217),

        uint3(178,132,45), uint3(146,83,166), uint3(86,89,45), uint3(240,191,255), uint3(172,230,219),
        uint3(204,164,153), uint3(64,0,191), uint3(13,255,0), uint3(229,50,0), uint3(230,57,158),

        uint3(64,166,255), uint3(51,204,143), uint3(191,110,48), uint3(121,133,242), uint3(255,155,128),
        uint3(79,67,89), uint3(135,166,124)
    };

    float3 GetDistinctFloatColour(uint index)
    {
        return ColourList[index % ColourListCount] * (1.0f/255.f).xxx;
    }
#else

    Texture1D DistinctColors BIND_SEQ_T5;
    float3 GetDistinctFloatColour(uint index)
    {
        uint dims;
        DistinctColors.GetDimensions(dims);
        return DistinctColors[index % dims];
    }

#endif

#endif
