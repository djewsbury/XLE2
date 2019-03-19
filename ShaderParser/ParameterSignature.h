// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Core/Prefix.h"
#include "../Core/Exceptions.h"
#include "../Utility/Mixins.h"
#include <vector>
#include <string>

namespace ShaderSourceParser
{
    class UniformBufferSignature
    {
    public:
        struct Source { enum Enum { Material, System }; };

        std::string     _name, _description;
        std::string     _min, _max;
        std::string     _type, _typeExtra;
        Source::Enum    _source;
        std::string     _semantic;
        std::string     _default;
    };

    UniformBufferSignature	LoadSignature(const char sourceCode[], size_t sourceCodeLength);
    std::string             StoreSignature(const UniformBufferSignature& signature);
}
