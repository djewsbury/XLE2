// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../ConsoleRig/GlobalServices.h"
#include "../IDevice_Forward.h"
#include "../Init.h"
#include "../../Assets/AssetUtils.h"
#include <memory>

namespace RenderCore { class ShaderService; }
namespace Assets { class IAssetCompiler; }

namespace RenderCore { namespace Assets
{
    class Services
    {
    public:
        static RenderCore::IDevice& GetDevice() { return *s_instance->_device; }
        static bool HasInstance() { return s_instance != nullptr; }
        static const ::Assets::DirectorySearchRules& GetTechniqueConfigDirs() { return s_instance->_techConfDirs; }

        void InitModelCompilers();
		void ShutdownModelCompilers();

        Services(const std::shared_ptr<RenderCore::IDevice>& device);
        ~Services();

        void AttachCurrentModule();
        void DetachCurrentModule();

        Services(const Services&) = delete;
        const Services& operator=(const Services&) = delete;
    protected:
        std::shared_ptr<RenderCore::IDevice> _device;
        std::unique_ptr<ShaderService> _shaderService;
        ::Assets::DirectorySearchRules _techConfDirs;
        static Services* s_instance;

		std::shared_ptr<::Assets::IAssetCompiler> _modelCompilers;
    };
}}

