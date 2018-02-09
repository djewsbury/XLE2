// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../RenderCore/Metal/Forward.h"
#include "../../RenderCore/IThreadContext_Forward.h"
#include "../../Math/Vector.h"

namespace RenderCore { namespace Techniques { class NamedAttachments; } }

namespace RenderOverlays
{
    class HighlightByStencilSettings
    {
    public:
        Float3 _outlineColor; float _dummy;
        UInt4 _highlightedMarker;
        UInt4 _stencilToMarkerMap[256];

        static const UInt4 NoHighlight;

        HighlightByStencilSettings();
    };

    void ExecuteHighlightByStencil(
        RenderCore::IThreadContext& threadContext,
        RenderCore::Techniques::NamedAttachments& namedRes,
        const HighlightByStencilSettings& settings,
        bool onlyHighlighted);

    /// <summary>Utility class for rendering a highlight around some geometry</summary>
    /// Using BinaryHighlight, we can draw some geometry to an offscreen
    /// buffer, and then blend a outline or highlight over other geometry.
    /// Generally, it's used like this:
    /// <list>
    ///   <item>BinaryHighlight::BinaryHighlight() (constructor)
    ///   <item>Draw something... 
    ///         (BinaryHighlight constructor binds an offscreen buffer, so this render 
    ///         is just to provide the siholette of the thing we want to highlight
    ///   <item>BinaryHighlight::FinishWithOutline()
    ///         This rebinds the old render target, and blends in the highlight
    /// </list>
    class BinaryHighlight
    {
    public:
        void FinishWithOutline(RenderCore::IThreadContext& threadContext, Float3 outlineColor);
        void FinishWithOutlineAndOverlay(RenderCore::IThreadContext& threadContext, Float3 outlineColor, unsigned overlayColor);
        void FinishWithShadow(RenderCore::IThreadContext& threadContext, Float4 shadowColor);
        
        BinaryHighlight(RenderCore::IThreadContext& threadContext, RenderCore::Techniques::NamedAttachments& namedRes);
        ~BinaryHighlight();
    protected:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };
}

