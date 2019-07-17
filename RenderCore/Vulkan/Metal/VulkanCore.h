// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "VulkanForward.h"
#include "../../../Core/Exceptions.h"
#include <type_traits>
#include <utility>
#include <memory>
#include <functional>

namespace RenderCore { namespace Metal_Vulkan
{
    namespace Internal
    {
        template<typename Type>
            struct VulkanTypeTraits
            { 
                typedef std::shared_ptr<typename std::decay<decltype(*std::declval<Type>())>::type> SharedPtr; 
                typedef std::unique_ptr<typename std::decay<decltype(*std::declval<Type>())>::type, std::function<void(Type)>> UniquePtr; 
            };
    }

    template<typename Type> using VulkanSharedPtr = typename Internal::VulkanTypeTraits<Type>::SharedPtr;
    template<typename Type> using VulkanUniquePtr = typename Internal::VulkanTypeTraits<Type>::UniquePtr;

    class VulkanAPIFailure : public ::Exceptions::BasicLabel
    {
    public:
        VulkanAPIFailure(VkResult res, const char message[]);
    };
}}

#if defined(_DEBUG)
	#define VULKAN_VERBOSE_DESCRIPTIONS
#endif

#if defined(VULKAN_VERBOSE_DESCRIPTIONS)
	#define VULKAN_VERBOSE_DESCRIPTIONS_ONLY(...) __VA_ARGS__
#else
	#define VULKAN_VERBOSE_DESCRIPTIONS_ONLY(...)
#endif

