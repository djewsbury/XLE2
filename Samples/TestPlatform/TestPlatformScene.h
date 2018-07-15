// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../RenderCore/IThreadContext_Forward.h"
#include "../../SceneEngine/SceneParser.h"
#include "../../PlatformRig/BasicSceneParser.h"
#include <memory>

namespace ToolsRig { class VisCameraSettings; }

namespace Sample
{
    class TestPlatformSceneParser : public PlatformRig::BasicSceneParser
    {
    public:
        void PrepareFrame(RenderCore::IThreadContext& context);
        void Update(float deltaTime);

        typedef SceneEngine::ShadowProjectionDesc   ShadowProjectionDesc;
        typedef SceneEngine::LightingParserContext  LightingParserContext;
        typedef SceneEngine::SceneParseSettings     SceneParseSettings;
        typedef SceneEngine::LightDesc              LightDesc;
        typedef SceneEngine::GlobalLightingDesc     GlobalLightingDesc;
        typedef SceneEngine::ToneMapSettings        ToneMapSettings;

        RenderCore::Techniques::CameraDesc GetCameraDesc() const;
        std::shared_ptr<ToolsRig::VisCameraSettings> GetCameraPtr();

        void ExecuteScene(   
            RenderCore::IThreadContext& context, 
            LightingParserContext& parserContext, 
            const SceneParseSettings& parseSettings,
            SceneEngine::PreparedScene& preparedPackets,
            unsigned techniqueIndex) const;
        void PrepareScene(
            RenderCore::IThreadContext& context, 
            RenderCore::Techniques::ParsingContext& parserContext,
            SceneEngine::PreparedScene& preparedPackets) const;
        virtual bool HasContent(const SceneParseSettings& parseSettings) const;

        float GetTimeValue() const;

        TestPlatformSceneParser();
        ~TestPlatformSceneParser();

    protected:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;

        const PlatformRig::EnvironmentSettings& GetEnvSettings() const;
    };

}

