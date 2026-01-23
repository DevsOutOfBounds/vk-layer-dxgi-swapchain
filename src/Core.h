#pragma once

#include <string.h>
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <vulkan/vk_icd.h>
#include <vulkan/utility/vk_dispatch_table.h>
#include <vulkan/vulkan_win32.h>
#include "vk/vk_layer_dispatch_table.h"


#if !defined(VK_LAYER_EXPORT)
#ifdef _MSC_VER
#ifdef __cplusplus
#define VK_LAYER_EXPORT extern "C" __declspec(dllexport)
#else
#define VK_LAYER_EXPORT __declspec(dllexport)
#endif
#else
#define VK_LAYER_EXPORT extern "C"
#endif
#endif

// If the string matches the proc address, return OUR implementation
#define DOOB_GETPROCADDR(func) do { if (strcmp(pName, "vk" #func) == 0) return (PFN_vkVoidFunction)&DOOB_##func; } while (false)

#define DOOB_LAYER_NAME "VK_LAYER_DOOB_dxgi_swapchain"
#define DOOB_LAYER_DESCRIPTION "Replaces the Vulkan swapchain with the DXGI swapchain - https://www.devsoutofbounds.com/"