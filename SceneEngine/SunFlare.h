// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../RenderCore/Metal/Forward.h"

namespace RenderCore { class IThreadContext; }
namespace RenderCore { namespace Techniques { class ParsingContext; } }

namespace SceneEngine
{
	class LightDesc;

    void SunFlare_Execute(
        RenderCore::IThreadContext& context,
        RenderCore::Techniques::ParsingContext& parserContext,
        const RenderCore::Metal::ShaderResourceView& depthsSRV,
		const LightDesc& sunDesc);
}

