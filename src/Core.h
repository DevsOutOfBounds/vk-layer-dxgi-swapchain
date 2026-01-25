#pragma once

#define VK_USE_PLATFORM_WIN32_KHR 1

#include <assert.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <vulkan/vulkan_win32.h> 
#include <vulkan/vk_icd.h>
#include <vulkan/vk_layer_dispatch_table.h>
#include <vulkan/layer/vk_layer_settings.hpp>


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