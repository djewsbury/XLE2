// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/FunctionUtils.h"
#include <string>
#include <memory>

namespace Utility { class CompletionThreadPool; }

namespace ConsoleRig
{
    class StartupConfig
    {
    public:
        std::string _applicationName;
        std::string _logConfigFile;
        bool _setWorkingDir;
        bool _redirectCout;
        unsigned _longTaskThreadPoolCount;
        unsigned _shortTaskThreadPoolCount;

        StartupConfig();
        StartupConfig(const char applicationName[]);
    };

    class LogCentralConfiguration;

    class GlobalServices
    {
    public:
        CompletionThreadPool& GetShortTaskThreadPool();
        CompletionThreadPool& GetLongTaskThreadPool();

        static GlobalServices& GetInstance() { assert(s_instance); return *s_instance; }

        GlobalServices(const StartupConfig& cfg = StartupConfig());
        ~GlobalServices();

        GlobalServices(const GlobalServices&) = delete;
        GlobalServices& operator=(const GlobalServices&) = delete;

        void AttachCurrentModule();
        void DetachCurrentModule();
    protected:
        static GlobalServices* s_instance;

        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };

	class LibVersionDesc
    {
    public:
        const char* _versionString;
        const char* _buildDateString;
    };

	LibVersionDesc GetLibVersionDesc();
}
