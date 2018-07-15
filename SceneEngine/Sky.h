// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "LightDesc.h"
#include "../Assets/AssetsCore.h"
#include "../RenderCore/Metal/Forward.h"

namespace RenderCore { namespace Assets { class DeferredShaderResource; } }
namespace RenderCore { namespace Techniques { class ParsingContext; } }
namespace RenderCore { class IThreadContext; }

namespace SceneEngine
{
    class SkyTextureParts
    {
    public:
        const RenderCore::Assets::DeferredShaderResource*      _faces12;
        const RenderCore::Assets::DeferredShaderResource*      _faces34;
        const RenderCore::Assets::DeferredShaderResource*      _face5;
        int _projectionType;

        bool IsGood() const { return _projectionType > 0; }
        unsigned BindPS(RenderCore::Metal::DeviceContext& context, int bindSlot) const;
		unsigned BindPS_G(RenderCore::Metal::DeviceContext& context, int bindSlot) const;

        SkyTextureParts(
            const ::Assets::ResChar assetName[], 
            GlobalLightingDesc::SkyTextureType assetType);
        SkyTextureParts(const GlobalLightingDesc& globalDesc);
    };

	void    Sky_Render(
        RenderCore::IThreadContext& threadContext, 
        RenderCore::Techniques::ParsingContext& parserContext,
		const GlobalLightingDesc& globalLightingDesc,
        bool blendFog);
    void    Sky_RenderPostFog(
        RenderCore::IThreadContext& threadContext, 
        RenderCore::Techniques::ParsingContext& parserContext,
		const SkyTextureParts& globalLightingDesc);

}

