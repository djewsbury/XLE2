
#pragma once

#include "ShaderPatcher.h"
#include "../Utility/StringUtils.h"
#include <unordered_map>

namespace ShaderSourceParser { class FunctionSignature; }

namespace ShaderPatcher
{
    class BasicSignatureProvider : public ISignatureProvider
    {
    public:
        Result FindSignature(StringSection<> name);

        BasicSignatureProvider(const ::Assets::DirectorySearchRules& searchRules);
        ~BasicSignatureProvider();
    protected:
        ::Assets::DirectorySearchRules _searchRules;
		struct Entry
		{
			std::string _name;
			NodeGraphSignature _sig;
			std::string _sourceFile;
		};
        std::unordered_map<uint64, Entry> _cache;        
    };

    NodeGraphSignature AsNodeGraphSignature(const ShaderSourceParser::FunctionSignature& sig);
}

