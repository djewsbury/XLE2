// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

namespace RenderCore { class IThreadContext; }
namespace RenderCore { namespace Techniques
{
	class RenderPassInstance;
	class ParsingContext;

	RenderPassInstance RenderPassToPresentationTarget(
		IThreadContext& context,
        ParsingContext& parserContext);
}}
