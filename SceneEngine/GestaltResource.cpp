// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "GestaltResource.h"
#include "../RenderCore/Metal/TextureView.h"
#include "../RenderCore/Format.h"
#include "../RenderCore/Techniques/Services.h"
#include "../BufferUploads/IBufferUploads.h"
#include "../BufferUploads/ResourceLocator.h"
#include "../Utility/ParameterPackUtils.h"
#include <tuple>

namespace SceneEngine
{
    using namespace RenderCore;

    namespace Internal
    {
        template<typename Tuple, int Index>
            static void InitViews(Tuple& tuple, BufferUploads::ResourceLocator& loc) {}

        template<typename Tuple, int Index, typename Top, typename... V>
            static void InitViews(Tuple& tuple, BufferUploads::ResourceLocator& loc)
            {
                std::get<Index>(tuple) = Top(loc.GetUnderlying());
                InitViews<Tuple, Index+1, V...>(tuple, loc);
            }

        static bool NeedTypelessFormat(Format fmt)
        {
            return (fmt >= Format::R24G8_TYPELESS && fmt <= Format::X24_TYPELESS_G8_UINT)
                || (fmt >= Format::D32_FLOAT);
        }

        template<typename View>
            static Format SpecializeFormat(Format fmt) { return fmt; }

        template<>
            Format SpecializeFormat<Metal::ShaderResourceView>(Format fmt)
            { 
                if (    fmt >= Format::R24G8_TYPELESS
                    &&  fmt <= Format::X24_TYPELESS_G8_UINT) {
                    return Format::R24_UNORM_X8_TYPELESS;      // only the depth parts are accessible in this way
                }
                else if (   fmt == Format::D32_FLOAT) {
                    return Format::R32_FLOAT;
                }
                return fmt;
            }

        template<>
            Format SpecializeFormat<Metal::DepthStencilView>(Format fmt)
            { 
                if (    fmt >= Format::R24G8_TYPELESS
                    &&  fmt <= Format::X24_TYPELESS_G8_UINT) {
                    return Format::D24_UNORM_S8_UINT;
                }
                return fmt;
            }

        template<typename Tuple, int Index>
            static void InitViews(Tuple&, BufferUploads::ResourceLocator&, Format) {}

        template<typename Tuple, int Index, typename Top, typename... V>
            static void InitViews(Tuple& tuple, BufferUploads::ResourceLocator& loc, Format fmt)
            {
                std::get<Index>(tuple) = Top(loc.GetUnderlying(), {SpecializeFormat<Top>(fmt)});
                InitViews<Tuple, Index+1, V...>(tuple, loc, fmt);
            }

        using BindFlags = BufferUploads::BindFlag::BitField;
        template<typename... V>
            static BindFlags MakeBindFlags()
            {
                using namespace BufferUploads;
                return (HasType< Metal::ShaderResourceView, std::tuple<V...>>::value ? BindFlag::ShaderResource : 0)
                    |  (HasType<   Metal::RenderTargetView, std::tuple<V...>>::value ? BindFlag::RenderTarget : 0)
                    |  (HasType<Metal::UnorderedAccessView, std::tuple<V...>>::value ? BindFlag::UnorderedAccess : 0)
                    |  (HasType<   Metal::DepthStencilView, std::tuple<V...>>::value ? BindFlag::DepthStencil : 0)
                    ;
            }
    }

    template<typename... Views>
        GestaltResource<Views...>::GestaltResource(
            const BufferUploads::TextureDesc& tdesc,
            const char name[],
            BufferUploads::DataPacket* initialData)
    {
        auto& uploads = RenderCore::Techniques::Services::GetBufferUploads();

        auto tdescCopy = tdesc;

        const bool needTypelessFmt = Internal::NeedTypelessFormat(Format(tdescCopy._format));
        if (needTypelessFmt)
            tdescCopy._format = AsTypelessFormat(Format(tdescCopy._format));

        auto desc = CreateDesc(
            Internal::MakeBindFlags<Views...>(),
            0, GPUAccess::Read|GPUAccess::Write,
            tdescCopy, name);
        _locator = uploads.Transaction_Immediate(desc, initialData);

        if (needTypelessFmt) {
            Internal::InitViews<std::tuple<Views...>, 0, Views...>(_views, *_locator, {tdesc._format});
        } else {
            Internal::InitViews<std::tuple<Views...>, 0, Views...>(_views, *_locator);
        }
    }

    template<typename... Views>
        GestaltResource<Views...>::GestaltResource(
            const BufferUploads::LinearBufferDesc& lbDesc,
            const char name[], BufferUploads::DataPacket* initialData,
            BufferUploads::BindFlag::BitField extraBindFlags)
    {
        auto& uploads = RenderCore::Techniques::Services::GetBufferUploads();

        auto desc = CreateDesc(
            Internal::MakeBindFlags<Views...>() | extraBindFlags,
            0, GPUAccess::Read|GPUAccess::Write,
            lbDesc, name);
        _locator = uploads.Transaction_Immediate(desc, initialData);

        Internal::InitViews<std::tuple<Views...>, 0, Views...>(_views, *_locator);
    }

    template<typename... Views>
        GestaltResource<Views...>::GestaltResource(GestaltResource&& moveFrom) never_throws
        : _views(std::move(moveFrom._views))
        , _locator(std::move(moveFrom._locator))
    {}

    template<typename... Views>
        GestaltResource<Views...>& GestaltResource<Views...>::operator=(GestaltResource&& moveFrom) never_throws
    {
        _views = std::move(moveFrom._views);
        _locator = std::move(moveFrom._locator);
        return *this;
    }

    template<typename... Views> GestaltResource<Views...>::GestaltResource() {}
    template<typename... Views> GestaltResource<Views...>::~GestaltResource() {}

    template class GestaltResource<RenderCore::Metal::ShaderResourceView>;
    template class GestaltResource<RenderCore::Metal::RenderTargetView>;
    template class GestaltResource<RenderCore::Metal::RenderTargetView, RenderCore::Metal::ShaderResourceView>;
    template class GestaltResource<RenderCore::Metal::DepthStencilView>;
    template class GestaltResource<RenderCore::Metal::DepthStencilView, RenderCore::Metal::ShaderResourceView>;
    template class GestaltResource<RenderCore::Metal::UnorderedAccessView>;
    template class GestaltResource<RenderCore::Metal::UnorderedAccessView, RenderCore::Metal::ShaderResourceView>;
    template class GestaltResource<RenderCore::Metal::RenderTargetView, RenderCore::Metal::UnorderedAccessView, RenderCore::Metal::ShaderResourceView>;
}
