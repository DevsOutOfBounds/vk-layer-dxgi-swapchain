#pragma once

#include "Core.h"
#include "DxgiSwapchain.h"
#include "D3D11FactoryInfo.h"
#include "generated/include/vk_doob_dxgi.h"

#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define HR(hr) do { HRESULT _hr = (hr); assert(SUCCEEDED(_hr)); } while (0)
#define MUTEX_KEY_VULKAN 0
#define MUTEX_KEY_D3D11  1
#define ARRAY_SIZE(a) ((sizeof(a)) / (sizeof(*a)))

std::mutex global_lock;
typedef std::lock_guard<std::mutex> scoped_lock;

// TODO: TEMPORARY LOCATION
#include <d3d11.h>
#include <dxgi1_6.h>
#include "D3D11FactoryInfo.h"

struct DOOB_DxgiSwapchain {
    DOOB_D3D11FactoryInfo dxgi_factory_info;

    IDXGISwapChain1* swapchain;
    ID3D11Texture2D* swapchain_backbuffer;
    ID3D11RenderTargetView* swapchain_rtv;
    uint32_t swapchain_image_count;
    uint32_t image_index;

    ID3D11Texture2D* shared_intermediate_tex;
    HANDLE shared_intermediate_handle;
    IDXGIKeyedMutex* shared_keyed_mutex;

    UINT sync_interval;
    UINT present_flags;

    VkImage vk_mirrored_shared_image;
    VkDeviceMemory vk_mirrored_shared_image_memory;

    std::vector<VkCommandPool> sync_command_pool;
    std::vector<VkCommandBuffer> sync_command_buffers;
};
//

struct DoobRequiredExt {
    const char* name;
    uint32_t promoted_to_vk; // ~0U if not promoted at all, works with if checks
};
static DoobRequiredExt REQUIRED_INSTANCE_EXTS[] = {
    { VK_KHR_SURFACE_EXTENSION_NAME, ~0U  },
    { VK_KHR_WIN32_SURFACE_EXTENSION_NAME, ~0U  },
    { VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME, VK_VERSION_1_1 },
    { VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, VK_VERSION_1_1 },
};

static DoobRequiredExt REQUIRED_DEVICE_EXTS[] = {
    { VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME, ~0U },
    { VK_KHR_WIN32_KEYED_MUTEX_EXTENSION_NAME, ~0U },
};

// ==== Dispatch table ====

struct DoobSettings {
    bool force_enable_dxgi;
    bool force_dxgi_version;
    VkDxgiDeviceVersionDOOB force_dxgi_version_value;
    std::string log_file;
};
struct DoobDeviceConfig {
    bool dxgi_swapchain_extension_enabled;
    bool dxgi_swapchain_feature_enabled;
    DOOB_D3D11FactoryInfo factory_info;
};
struct DoobInstanceConfig {
    std::unordered_set<VkPhysicalDevice> physical_devices;
    uint32_t vk_api_version;
    bool supports_dxgi_ext;
};
struct DoobWin32Surface {
    HINSTANCE hinstance;
    HWND hwnd;
};

std::unordered_map<VkInstance, VkLayerInstanceDispatchTable> g_instance_dispatch;
std::unordered_map<VkInstance, DoobInstanceConfig> g_instance_config;
std::unordered_map<VkDevice, VkLayerDispatchTable> g_device_dispatch;
std::unordered_map<VkDevice, DoobDeviceConfig> g_device_config;
std::unordered_map<VkDevice, VkPhysicalDevice> g_device_to_physical;
std::unordered_map<VkQueue, VkDevice> g_queue_ownership;
std::unordered_map<VkSurfaceKHR, DoobWin32Surface> g_win32_surfaces;

#define DOOB_CALL_DISPATCH_TABLE(table, object, retval, fn, params) do { PFN_vk##fn icd_function_call = NULL; \
{ scoped_lock l(global_lock); icd_function_call = table[object].fn; } retval = icd_function_call params; } while (false) 

#define DOOB_CALL_VOID_DISPATCH_TABLE(table, object, fn, params) do { PFN_vk##fn icd_function_call = NULL; \
{ scoped_lock l(global_lock); icd_function_call = table[object].fn; } (void)icd_function_call params; } while (false) 

//  ==== Handle table ====

std::vector<DOOB_DxgiSwapchain*> g_dxgi_swapchains = {};

#define DOOB_INVALID_COUNTER_HANDLE (~0U)
#define DOOB_SWAPCHAIN_HANDLE_ID (0x1020'0000)

#define DOOB_COMMAND_POOL_QUEUE_FAMILY 0

#define DOOB_print(...) printf(__VA_ARGS__)

template <typename TNonDispatchableHandle>
static TNonDispatchableHandle DOOB_MakeHandle(uint32_t id, uint32_t counter) {
    TNonDispatchableHandle handle;
    *(uint64_t*)(&handle) = ((uint64_t)(id) << 32) | (counter);
    return handle;
}
template <typename TNonDispatchableHandle>
static uint32_t DOOB_GetCounterAndVerify(uint32_t target_id, TNonDispatchableHandle handle) {
    uint64_t p = *(uint64_t*)(&handle);
    uint32_t id = (uint32_t)(p >> 32);
    uint32_t counter = (uint32_t)(p & 0xFFFFFFFF);
    if (id != target_id) {
        return DOOB_INVALID_COUNTER_HANDLE;
    }
    return counter;
}
template <typename TObject, typename TNonDispatchableHandle>
static TObject* DOOB_GetObjectIfExists(std::vector<TObject*>& data_array, uint32_t target_id, TNonDispatchableHandle handle) {
    uint32_t h = DOOB_GetCounterAndVerify<TNonDispatchableHandle>(target_id, handle);

    if (h == DOOB_INVALID_COUNTER_HANDLE) {
        return NULL;
    }
    scoped_lock l(global_lock);
    if (h >= data_array.size()) {
        return NULL;
    }
    return data_array[h];
}

template <typename TObject>
static TObject* DOOB_AllocCountedHandle(std::vector<TObject*>& data_array, uint32_t* out_counter, const VkAllocationCallbacks* alloc) {
    scoped_lock l(global_lock);
    *out_counter = (uint32_t)data_array.size();
    for (size_t i = 0; i < data_array.size(); ++i) {
        if (data_array[i] == NULL) {
            *out_counter = (uint32_t)i;
            break;
        }
    }
    if (*out_counter >= data_array.size()) {
        data_array.push_back({});
    }
    TObject* obj = (TObject*)alloc->pfnAllocation(NULL, sizeof(TObject), 1, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
    memset(obj, 0, sizeof(TObject));
    data_array[*out_counter] = obj;
    return obj;
}
template <typename TObject>
static void DOOB_ReleaseCountedHandle(std::vector<TObject*>& data_array, uint32_t handle, const VkAllocationCallbacks* alloc) {
    scoped_lock l(global_lock);
    if (handle >= data_array.size()) {
        return;
    }
    alloc->pfnFree(NULL, (void*)data_array[handle]);
    data_array[handle] = NULL;
}

static VkInstance DOOB_GetInstanceFromPhysicalDevice(VkPhysicalDevice physical_device) {
    scoped_lock l(global_lock);
    for (const auto& kv : g_instance_config) {
        if (kv.second.physical_devices.contains(physical_device)) {
            return kv.first;
        }
    }
    return VK_NULL_HANDLE;
}

static VkAllocationCallbacks DOOB_GetFilledAllocationCallbacks(const VkAllocationCallbacks* p_callbacks) {
    VkAllocationCallbacks callbacks = p_callbacks ? *p_callbacks : VkAllocationCallbacks{};
    if (!callbacks.pfnAllocation) {
        callbacks.pfnAllocation = [](
            void* pUserData,
            size_t                                      size,
            size_t                                      alignment,
            VkSystemAllocationScope                     allocationScope
            ) { return malloc(size); };
    }
    if (!callbacks.pfnReallocation) {
        callbacks.pfnReallocation = [](
            void* pUserData,
            void* pOriginal,
            size_t                                      size,
            size_t                                      alignment,
            VkSystemAllocationScope                     allocationScope
            ) { return realloc(pOriginal, size); };
    }
    if (!callbacks.pfnFree) {
        callbacks.pfnFree = [](
            void* pUserData,
            void* pMemory
            ) { return free(pMemory); };
    }
    return callbacks;
}

static DoobSettings DOOB_LoadSettings() {
    DoobSettings s;
    DOOB_print("[DOOB INFO]: loading settings\n");

    VkLayerSettingsCreateInfoEXT create_info{
        .sType = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT,
    };
    VkuLayerSettingSet setting_set;

    VkResult res;

    res = vkuCreateLayerSettingSet(VK_LAYER_DOOB_DXGI_SWAPCHAIN_NAME, &create_info, nullptr, NULL, &setting_set);
    if (res != VK_SUCCESS) {
        return {};
    }
    res = vkuGetLayerSettingValue(setting_set, "enable_dxgi_interop", s.force_enable_dxgi);
    DOOB_print("[DOOB INFO]: enable_dxgi_interop = %s\n", s.force_enable_dxgi ? "true" : "false");

    std::string dxgi_ver = {};
    vkuGetLayerSettingValue(setting_set, "force_dxgi_version", dxgi_ver);
    DOOB_print("[DOOB INFO]: force_dxgi_version = %s\n", dxgi_ver.c_str());
    if (dxgi_ver == "d3d11") {
        s.force_dxgi_version = true;
        s.force_dxgi_version_value = VK_DXGI_DEVICE_VERSION_D3D11_DOOB;
    }
    else if (dxgi_ver == "d3d12") {
        s.force_dxgi_version = true;
        s.force_dxgi_version_value = VK_DXGI_DEVICE_VERSION_D3D12_DOOB;
    }
    else {
        s.force_dxgi_version = false;
    }

    vkuGetLayerSettingValue(setting_set, "log_file_path", s.log_file);

    vkuDestroyLayerSettingSet(setting_set, nullptr);

    return s;
}

static constexpr DXGI_FORMAT DOOB_DXGIFormat_FromVkFormat(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8_UNORM:            return DXGI_FORMAT_R8_UNORM;
    case VK_FORMAT_R8_SNORM:            return DXGI_FORMAT_R8_SNORM;
    case VK_FORMAT_R8_UINT:             return DXGI_FORMAT_R8_UINT;
    case VK_FORMAT_R8_SINT:             return DXGI_FORMAT_R8_SINT;

    case VK_FORMAT_R8G8_UNORM:          return DXGI_FORMAT_R8G8_UNORM;
    case VK_FORMAT_R8G8_SNORM:          return DXGI_FORMAT_R8G8_SNORM;
    case VK_FORMAT_R8G8_UINT:           return DXGI_FORMAT_R8G8_UINT;
    case VK_FORMAT_R8G8_SINT:           return DXGI_FORMAT_R8G8_SINT;

    case VK_FORMAT_R8G8B8A8_UNORM:      return DXGI_FORMAT_R8G8B8A8_UNORM;
    case VK_FORMAT_R8G8B8A8_SNORM:      return DXGI_FORMAT_R8G8B8A8_SNORM;
    case VK_FORMAT_R8G8B8A8_UINT:       return DXGI_FORMAT_R8G8B8A8_UINT;
    case VK_FORMAT_R8G8B8A8_SINT:       return DXGI_FORMAT_R8G8B8A8_SINT;
    case VK_FORMAT_R8G8B8A8_SRGB:       return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

    case VK_FORMAT_B8G8R8A8_UNORM:      return DXGI_FORMAT_B8G8R8A8_UNORM;
    case VK_FORMAT_B8G8R8A8_SRGB:       return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;

    case VK_FORMAT_R16_UNORM:           return DXGI_FORMAT_R16_UNORM;
    case VK_FORMAT_R16_SNORM:           return DXGI_FORMAT_R16_SNORM;
    case VK_FORMAT_R16_UINT:            return DXGI_FORMAT_R16_UINT;
    case VK_FORMAT_R16_SINT:            return DXGI_FORMAT_R16_SINT;
    case VK_FORMAT_R16_SFLOAT:          return DXGI_FORMAT_R16_FLOAT;

    case VK_FORMAT_R16G16_UNORM:        return DXGI_FORMAT_R16G16_UNORM;
    case VK_FORMAT_R16G16_SNORM:        return DXGI_FORMAT_R16G16_SNORM;
    case VK_FORMAT_R16G16_UINT:         return DXGI_FORMAT_R16G16_UINT;
    case VK_FORMAT_R16G16_SINT:         return DXGI_FORMAT_R16G16_SINT;
    case VK_FORMAT_R16G16_SFLOAT:       return DXGI_FORMAT_R16G16_FLOAT;

    case VK_FORMAT_R16G16B16A16_UNORM:  return DXGI_FORMAT_R16G16B16A16_UNORM;
    case VK_FORMAT_R16G16B16A16_SNORM:  return DXGI_FORMAT_R16G16B16A16_SNORM;
    case VK_FORMAT_R16G16B16A16_UINT:   return DXGI_FORMAT_R16G16B16A16_UINT;
    case VK_FORMAT_R16G16B16A16_SINT:   return DXGI_FORMAT_R16G16B16A16_SINT;
    case VK_FORMAT_R16G16B16A16_SFLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;

    case VK_FORMAT_R32_UINT:            return DXGI_FORMAT_R32_UINT;
    case VK_FORMAT_R32_SINT:            return DXGI_FORMAT_R32_SINT;
    case VK_FORMAT_R32_SFLOAT:          return DXGI_FORMAT_R32_FLOAT;

    case VK_FORMAT_R32G32_UINT:         return DXGI_FORMAT_R32G32_UINT;
    case VK_FORMAT_R32G32_SINT:         return DXGI_FORMAT_R32G32_SINT;
    case VK_FORMAT_R32G32_SFLOAT:       return DXGI_FORMAT_R32G32_FLOAT;

    case VK_FORMAT_R32G32B32A32_UINT:   return DXGI_FORMAT_R32G32B32A32_UINT;
    case VK_FORMAT_R32G32B32A32_SINT:   return DXGI_FORMAT_R32G32B32A32_SINT;
    case VK_FORMAT_R32G32B32A32_SFLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;

    case VK_FORMAT_D16_UNORM:           return DXGI_FORMAT_D16_UNORM;
    case VK_FORMAT_D32_SFLOAT:          return DXGI_FORMAT_D32_FLOAT;
    case VK_FORMAT_D24_UNORM_S8_UINT:   return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case VK_FORMAT_D32_SFLOAT_S8_UINT:  return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;

    default:                            return DXGI_FORMAT_UNKNOWN;
    }
}

DXGI_ALPHA_MODE DOOB_DXGIAlphaMode_FromVkCompositeAlpha(VkCompositeAlphaFlagBitsKHR alpha) {
    switch (alpha) {
    case VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR:
        return DXGI_ALPHA_MODE_IGNORE;
    case VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR:
        return DXGI_ALPHA_MODE_PREMULTIPLIED;
    case VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR:
        return DXGI_ALPHA_MODE_STRAIGHT;
    case VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR:
        return DXGI_ALPHA_MODE_UNSPECIFIED;
    default:
        return DXGI_ALPHA_MODE_UNSPECIFIED;
    }
}


VkResult VKAPI_CALL DOOB_EnumerateInstanceLayerProperties(
    uint32_t* pPropertyCount,
    VkLayerProperties* pProperties) {
    if (pPropertyCount == NULL) {
        return VK_INCOMPLETE;
    }
    if (pProperties == NULL) {
        *pPropertyCount = 1;
        return VK_SUCCESS;
    }
    if (*pPropertyCount == 0) {
        return VK_INCOMPLETE;
    }
    if (pProperties) {
        VkLayerProperties& properties = pProperties[0];
        memset(properties.layerName, 0, sizeof(properties.layerName));
        memset(properties.description, 0, sizeof(properties.layerName));
        strncpy(properties.layerName, VK_LAYER_DOOB_DXGI_SWAPCHAIN_NAME, sizeof(VK_LAYER_DOOB_DXGI_SWAPCHAIN_NAME));
        strncpy(properties.description, VK_LAYER_DOOB_DXGI_SWAPCHAIN_DESCRIPTION, sizeof(VK_LAYER_DOOB_DXGI_SWAPCHAIN_DESCRIPTION));
        properties.implementationVersion = VK_MAKE_API_VERSION(0, VK_LAYER_DOOB_API_VERSION_MAJOR, VK_LAYER_DOOB_API_VERSION_MINOR, VK_LAYER_DOOB_API_VERSION_PATCH);
        properties.specVersion = VK_DOOB_DXGI_SWAPCHAIN_SPEC_VERSION;
    }
    return VK_SUCCESS;
}

VkResult VKAPI_CALL DOOB_EnumerateDeviceLayerProperties(
    VkPhysicalDevice                            physicalDevice,
    uint32_t* pPropertyCount,
    VkLayerProperties* pProperties) {
    return DOOB_EnumerateInstanceLayerProperties(pPropertyCount, pProperties);
}

VkResult VKAPI_CALL DOOB_CreateInstance(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance)
{
    uint32_t enabled_api_version = VK_API_VERSION_1_0; // Default fallback
    if (pCreateInfo->pApplicationInfo) {
        enabled_api_version = pCreateInfo->pApplicationInfo->apiVersion;
    }
    VkLayerInstanceCreateInfo* layerCreateInfo = (VkLayerInstanceCreateInfo*)pCreateInfo->pNext;

    // step through the chain of pNext until we get to the link info
    while (layerCreateInfo && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO ||
        layerCreateInfo->function != VK_LAYER_LINK_INFO))
    {
        layerCreateInfo = (VkLayerInstanceCreateInfo*)layerCreateInfo->pNext;
    }

    if (layerCreateInfo == NULL)
    {
        // No loader instance create info
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PFN_vkGetInstanceProcAddr gpa = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    // move chain on for next layer
    layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;

    PFN_vkCreateInstance createFunc = (PFN_vkCreateInstance)gpa(VK_NULL_HANDLE, "vkCreateInstance");

    // Enable instance extensions!

    VkInstanceCreateInfo instance_create_info_copy = *pCreateInfo;

    DoobSettings global_dxgi_layer_settings = DOOB_LoadSettings();
    bool supports_dxgi_extension = true;

    std::vector<const char*> enabled_extensions(pCreateInfo->enabledExtensionCount);
    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i) {
        enabled_extensions[i] = pCreateInfo->ppEnabledExtensionNames[i];
    }
    // Is the layer enabling it and the application didnt? Enable all required extensions!
    if (global_dxgi_layer_settings.force_enable_dxgi) {
        DOOB_print("[DOOB INFO]: VK_DOOB_dxgi_swapchain has been enabled by configurator\n");
        for (const auto& ext : REQUIRED_INSTANCE_EXTS) {
            if (enabled_api_version >= ext.promoted_to_vk) continue; // No enabling needed
            bool already_enabled = false;
            for (const char* enabled_ext : enabled_extensions) {
                if (strcmp(enabled_ext, ext.name) == 0) {
                    already_enabled = true;
                    break;
                }
            }
            if (!already_enabled) {
                DOOB_print("[DOOB WARNING]: Implicitly enabling extension %s\n", ext.name);
                enabled_extensions.push_back(ext.name);
            }
        }
    }
    // Update extensions!
    instance_create_info_copy.ppEnabledExtensionNames = enabled_extensions.data();
    instance_create_info_copy.enabledExtensionCount = (uint32_t)enabled_extensions.size();

    for (const auto& ext : REQUIRED_INSTANCE_EXTS) {
        if (enabled_api_version >= ext.promoted_to_vk) continue; // No enabling needed
        bool found = false;
        for (const char* enabled_ext : enabled_extensions) {
            if (strcmp(enabled_ext, ext.name) == 0) {
                found = true;
                break;
            }
        }

        if (!found) {
            DOOB_print("[DOOB INFO] Missing extension %s!\n", ext.name);

            supports_dxgi_extension = false;
            break;
        }
    }

    VkResult ret = createFunc(&instance_create_info_copy, pAllocator, pInstance);
    if (ret != VK_SUCCESS) {
        return ret;
    }
#define ASSIGN_DISPATCH(fn) dispatchTable.fn = (PFN_vk##fn)gpa(*pInstance, "vk"#fn) 

    // fetch our own dispatch table for the functions we need, into the next layer
    VkLayerInstanceDispatchTable dispatchTable;
    ASSIGN_DISPATCH(GetInstanceProcAddr);
    ASSIGN_DISPATCH(EnumerateInstanceLayerProperties);
    ASSIGN_DISPATCH(EnumerateDeviceExtensionProperties);
    ASSIGN_DISPATCH(EnumeratePhysicalDevices);
    ASSIGN_DISPATCH(EnumeratePhysicalDeviceGroupsKHR);
    ASSIGN_DISPATCH(EnumeratePhysicalDeviceGroups);
    ASSIGN_DISPATCH(GetPhysicalDeviceProperties2KHR);
    ASSIGN_DISPATCH(GetPhysicalDeviceProperties2);
    ASSIGN_DISPATCH(GetPhysicalDeviceFeatures2KHR);
    ASSIGN_DISPATCH(GetPhysicalDeviceFeatures2);
    ASSIGN_DISPATCH(GetPhysicalDeviceMemoryProperties);
    ASSIGN_DISPATCH(GetPhysicalDeviceSurfaceCapabilitiesKHR);
    ASSIGN_DISPATCH(GetPhysicalDeviceSurfaceCapabilities2KHR);
    ASSIGN_DISPATCH(GetPhysicalDeviceSurfaceCapabilities2EXT);
    ASSIGN_DISPATCH(GetPhysicalDeviceSurfacePresentModesKHR);
    ASSIGN_DISPATCH(GetPhysicalDeviceSurfaceFormatsKHR);
    ASSIGN_DISPATCH(GetPhysicalDeviceSurfaceSupportKHR);
    ASSIGN_DISPATCH(CreateInstance);
    ASSIGN_DISPATCH(CreateDevice);
    ASSIGN_DISPATCH(DestroyInstance);
    ASSIGN_DISPATCH(GetPhysicalDevicePresentRectanglesKHR);
    ASSIGN_DISPATCH(CreateWin32SurfaceKHR);

#undef ASSIGN_DISPATCH

    // fetch our own dispatch table for the functions we need, into the next layer

    {
        scoped_lock l(global_lock);
        g_instance_dispatch[*pInstance] = dispatchTable;
        g_instance_config[*pInstance].vk_api_version = pCreateInfo->pApplicationInfo->apiVersion;
        g_instance_config[*pInstance].supports_dxgi_ext = supports_dxgi_extension;
    }
    return VK_SUCCESS;
}

void VKAPI_CALL DOOB_DestroyInstance(
    VkInstance                                  instance,
    const VkAllocationCallbacks* pAllocator) {
    PFN_vkDestroyInstance call = NULL;
    {
        scoped_lock l(global_lock);
        auto it = g_instance_dispatch.find(instance);
        if (it != g_instance_dispatch.end()) {
            call = it->second.DestroyInstance;
            g_instance_dispatch.erase(it);
        }
        auto it2 = g_instance_config.find(instance);
        if (it2 != g_instance_config.end()) {
            g_instance_config.erase(it2);
        }
    }
    if (call != NULL) {
        call(instance, pAllocator);
    }
}

VkResult VKAPI_CALL DOOB_EnumeratePhysicalDevices(
    VkInstance                                  instance,
    uint32_t* pPhysicalDeviceCount,
    VkPhysicalDevice* pPhysicalDevices) {
    VkResult result;
    DOOB_CALL_DISPATCH_TABLE(g_instance_dispatch, instance, result, EnumeratePhysicalDevices, (instance, pPhysicalDeviceCount, pPhysicalDevices));
    if (result != VK_SUCCESS) {
        return result;
    }
    if (pPhysicalDeviceCount && pPhysicalDevices) {
        scoped_lock l(global_lock);
        auto& physical_device_set = g_instance_config[instance].physical_devices;
        for (uint32_t i = 0; i < *pPhysicalDeviceCount; ++i) {
            physical_device_set.emplace(pPhysicalDevices[i]);
        }
    }
    return VK_SUCCESS;
}

// Use KHR to ensure it absolutely exists if using an old version with extensions enabled!
VkResult VKAPI_CALL DOOB_EnumeratePhysicalDeviceGroupsKHR(
    VkInstance instance,
    uint32_t* pPhysicalDeviceGroupCount,
    VkPhysicalDeviceGroupProperties* pPhysicalDeviceGroupProperties)
{
    VkResult res;
    DOOB_CALL_DISPATCH_TABLE(g_instance_dispatch, instance, res, EnumeratePhysicalDeviceGroupsKHR, (instance, pPhysicalDeviceGroupCount, pPhysicalDeviceGroupProperties));

    if (res == VK_SUCCESS && pPhysicalDeviceGroupProperties) {
        scoped_lock l(global_lock);
        std::unordered_set<VkPhysicalDevice>& physical_device_array = g_instance_config[instance].physical_devices;

        // Add all found devices to our map
        for (uint32_t i = 0; i < *pPhysicalDeviceGroupCount; i++) {
            for (uint32_t j = 0; j < pPhysicalDeviceGroupProperties[i].physicalDeviceCount; j++) {
                VkPhysicalDevice handle = pPhysicalDeviceGroupProperties[i].physicalDevices[j];
                physical_device_array.emplace(handle);
            }
        }
    }
    return res;
}
#define DOOB_EnumeratePhysicalDeviceGroups DOOB_EnumeratePhysicalDeviceGroupsKHR

// Use KHR to ensure it absolutely exists if using an old version with extensions enabled!
void VKAPI_CALL DOOB_GetPhysicalDeviceProperties2KHR(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceProperties2* pProperties) {

    VkInstance instance = DOOB_GetInstanceFromPhysicalDevice(physicalDevice);
    if (instance == VK_NULL_HANDLE) {
        return;
    }
    bool supports_dxgi_doob = false;
    {
        scoped_lock l(global_lock);
        supports_dxgi_doob = g_instance_config[instance].supports_dxgi_ext;
    }

    if (supports_dxgi_doob) {
        VkPhysicalDeviceDxgiPropertiesDOOB* dxgi_device_properties = (VkPhysicalDeviceDxgiPropertiesDOOB*)pProperties->pNext;
        while (dxgi_device_properties && (dxgi_device_properties->sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DXGI_PROPERTIES_DOOB)) {
            dxgi_device_properties = (VkPhysicalDeviceDxgiPropertiesDOOB*)dxgi_device_properties->pNext;
        }
        if (dxgi_device_properties) {
            // TODO: do we need this?

            //VkPhysicalDeviceIDProperties id_properties{
            //    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES,
            //};
            //VkPhysicalDeviceProperties2 properties2{
            //    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            //    .pNext = &id_properties,
            //};
            //DOOB_CALL_VOID_DISPATCH_TABLE(g_instance_dispatch, instance, GetPhysicalDeviceProperties2KHR, (physicalDevice, &properties2));
        }
    }
    DOOB_CALL_VOID_DISPATCH_TABLE(g_instance_dispatch, instance, GetPhysicalDeviceProperties2KHR, (physicalDevice, pProperties));
}
#define DOOB_GetPhysicalDeviceProperties2 DOOB_GetPhysicalDeviceProperties2KHR


// Use KHR to ensure it absolutely exists if using an old version with extensions enabled!
void VKAPI_CALL DOOB_GetPhysicalDeviceFeatures2KHR(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceFeatures2* pFeatures) {

    VkInstance instance = DOOB_GetInstanceFromPhysicalDevice(physicalDevice);
    if (instance == VK_NULL_HANDLE) {
        return;
    }
    bool supports_dxgi_doob = false;
    {
        scoped_lock l(global_lock);
        supports_dxgi_doob = g_instance_config[instance].supports_dxgi_ext;
    }

    if (supports_dxgi_doob) {
        VkPhysicalDeviceDxgiFeaturesDOOB* dxgi_device_features = (VkPhysicalDeviceDxgiFeaturesDOOB*)pFeatures->pNext;
        while (dxgi_device_features && (dxgi_device_features->sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DXGI_FEATURES_DOOB)) {
            dxgi_device_features = (VkPhysicalDeviceDxgiFeaturesDOOB*)dxgi_device_features->pNext;
        }
        if (dxgi_device_features) {
            VkPhysicalDeviceIDProperties id_properties{
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES,
            };
            VkPhysicalDeviceProperties2 properties2{
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                .pNext = &id_properties,
            };
            DOOB_CALL_VOID_DISPATCH_TABLE(g_instance_dispatch, instance, GetPhysicalDeviceProperties2KHR, (physicalDevice, &properties2));
            if (id_properties.deviceLUIDValid == VK_TRUE || properties2.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) {
                // maybe check with DXGI if the device exists
                // However if CPU device, then just default to WARP
                dxgi_device_features->dxgiSwapchain = VK_TRUE;
                // check what D3D device is supported, tbh should be d3d12 always
                dxgi_device_features->dxgiVersion = VK_DXGI_DEVICE_VERSION_D3D12_DOOB;
            }
            else {
                dxgi_device_features->dxgiSwapchain = VK_FALSE;
            }
        }
    }
    DOOB_CALL_VOID_DISPATCH_TABLE(g_instance_dispatch, instance, GetPhysicalDeviceFeatures2KHR, (physicalDevice, pFeatures));
}
#define DOOB_GetPhysicalDeviceFeatures2 DOOB_GetPhysicalDeviceFeatures2KHR


static const VkExtensionProperties g_doob_extension_info = {
    VK_DOOB_DXGI_SWAPCHAIN_EXTENSION_NAME,
    VK_DOOB_DXGI_SWAPCHAIN_SPEC_VERSION
};

VkResult VKAPI_CALL DOOB_EnumerateDeviceExtensionProperties(
    VkPhysicalDevice                            physicalDevice,
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties)
{
    VkInstance instance = DOOB_GetInstanceFromPhysicalDevice(physicalDevice);
    bool extension_supported_on_inst_level = false;
    uint32_t using_api_version = 0;
    if (instance == VK_NULL_HANDLE) {
        return VK_ERROR_UNKNOWN;
    }
    {
        scoped_lock l(global_lock);
        extension_supported_on_inst_level = g_instance_config[instance].supports_dxgi_ext;
        using_api_version = g_instance_config[instance].vk_api_version;
    }
    if (pLayerName != NULL && strcmp(pLayerName, VK_LAYER_DOOB_DXGI_SWAPCHAIN_NAME) == 0) {
        bool has_dependencies = extension_supported_on_inst_level;
        if (has_dependencies) {
            uint32_t count;
            VkResult res;
            DOOB_CALL_DISPATCH_TABLE(g_instance_dispatch, instance, res, EnumerateDeviceExtensionProperties, (physicalDevice, NULL, &count, NULL));
            if (res != VK_SUCCESS) return res;
            std::vector<VkExtensionProperties> driver_exts(count);
            DOOB_CALL_DISPATCH_TABLE(g_instance_dispatch, instance, res, EnumerateDeviceExtensionProperties, (physicalDevice, NULL, &count, driver_exts.data()));
            if (res != VK_SUCCESS) return res;

            // CHECK DEPENDENCIES

            for (const auto& ext : REQUIRED_DEVICE_EXTS) {
                if (using_api_version >= ext.promoted_to_vk) continue;
                bool found = false;
                for (const auto& d_ext : driver_exts) {
                    if (strcmp(d_ext.extensionName, ext.name) == 0) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    DOOB_print("[DOOB INFO] Missing extension %s!\n", ext.name);
                    has_dependencies = false;
                    break;
                }
            }
        }
        if (pPropertyCount && pProperties == NULL) {
            *pPropertyCount = has_dependencies ? 1 : 0;
            return VK_SUCCESS;
        }
        if (pProperties && has_dependencies) {
            if (pPropertyCount == NULL || *pPropertyCount < 1) return VK_INCOMPLETE;
            pProperties[0] = g_doob_extension_info;
            *pPropertyCount = 1;
        }
        return VK_SUCCESS;
    }

    // If pLayerName is NOT NULL and NOT us, pass it down (it's for another layer)
    if (pLayerName) {
        VkResult ret;
        DOOB_CALL_DISPATCH_TABLE(g_instance_dispatch, instance, ret, EnumerateDeviceExtensionProperties, (physicalDevice, pLayerName, pPropertyCount, pProperties));
        return ret;
    }

    // The App is asking for "All Extensions" (pLayerName == NULL)
    // We must fetch the driver's list, then add ours to it.

    // Get Driver Count
    uint32_t driver_count = 0;
    VkResult res;
    DOOB_CALL_DISPATCH_TABLE(g_instance_dispatch, instance, res, EnumerateDeviceExtensionProperties, (physicalDevice, NULL, &driver_count, NULL));
    if (res != VK_SUCCESS) return res;

    std::vector<VkExtensionProperties> driver_exts(driver_count);
    DOOB_CALL_DISPATCH_TABLE(g_instance_dispatch, instance, res, EnumerateDeviceExtensionProperties, (physicalDevice, NULL, &driver_count, driver_exts.data()));
    if (res != VK_SUCCESS) return res;

    // CHECK DEPENDENCIES

    bool has_dependencies = extension_supported_on_inst_level;
    if (has_dependencies) {
        for (const auto& ext : REQUIRED_DEVICE_EXTS) {
            if (using_api_version >= ext.promoted_to_vk) continue;
            bool found = false;
            for (const auto& d_ext : driver_exts) {
                if (strcmp(d_ext.extensionName, ext.name) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                has_dependencies = false;
                break;
            }
        }
    }
    // Only show OUR extension if dependency exists
    uint32_t our_count = has_dependencies ? 1 : 0;
    uint32_t total_count = driver_count + our_count;

    // If App is just querying count
    if (!pProperties) {
        *pPropertyCount = total_count;
        return VK_SUCCESS;
    }

    // If App provided buffer, calculate how many to write
    uint32_t copy_count = *pPropertyCount;
    if (copy_count > driver_count) copy_count = driver_count; // Copy max what the driver has

    // Fetch Driver Extensions (we already queried them so we can just store it now)
    for (uint32_t i = 0; i < copy_count; ++i) {
        pProperties[i] = driver_exts[i];
    }

    // If we dont have the required dependencies, exit out, no more extensions to write
    if (!has_dependencies) {
        return VK_SUCCESS;
    }
    // Append Our Extension (if there is space)
    if (*pPropertyCount >= total_count) {
        pProperties[driver_count] = g_doob_extension_info;
        *pPropertyCount = total_count;
        return VK_SUCCESS;
    }
    else {
        // The buffer was too small to hold ours + driver's
        return VK_INCOMPLETE;
    }
}

VkResult VKAPI_CALL DOOB_GetPhysicalDevicePresentRectanglesKHR(
    VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    uint32_t* pRectCount,
    VkRect2D* pRects) {

    VkInstance instance = DOOB_GetInstanceFromPhysicalDevice(physicalDevice);
    bool extension_supported_on_inst_level = false;
    uint32_t using_api_version = 0;
    if (instance == VK_NULL_HANDLE) {
        return VK_ERROR_UNKNOWN;
    }
    {
        scoped_lock l(global_lock);
        extension_supported_on_inst_level = g_instance_config[instance].supports_dxgi_ext;
        using_api_version = g_instance_config[instance].vk_api_version;
    }
    if (!extension_supported_on_inst_level) {
        VkResult res;
        DOOB_CALL_DISPATCH_TABLE(g_instance_dispatch, instance, res, GetPhysicalDevicePresentRectanglesKHR, (physicalDevice, surface, pRectCount, pRects));
        return res;
    }
    if (!pRectCount) {
        return VK_INCOMPLETE;
    }

    VkIcdSurfaceWin32* win32_surface = (VkIcdSurfaceWin32*)surface;
    if (win32_surface->base.platform != VK_ICD_WSI_PLATFORM_WIN32) {
        return VK_ERROR_UNKNOWN;
    }
    // TODO

    return VK_ERROR_UNKNOWN;
}
VkResult VKAPI_CALL DOOB_GetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    VkSurfaceCapabilitiesKHR* pSurfaceCapabilities) {

    VkInstance instance = DOOB_GetInstanceFromPhysicalDevice(physicalDevice);
    bool extension_supported_on_inst_level = false;
    uint32_t using_api_version = 0;
    if (instance == VK_NULL_HANDLE) {
        return VK_ERROR_UNKNOWN;
    }
    {
        scoped_lock l(global_lock);
        extension_supported_on_inst_level = g_instance_config[instance].supports_dxgi_ext;
        using_api_version = g_instance_config[instance].vk_api_version;
    }
    if (!extension_supported_on_inst_level) {
        VkResult res;
        DOOB_CALL_DISPATCH_TABLE(g_instance_dispatch, instance, res, GetPhysicalDeviceSurfaceCapabilitiesKHR, (physicalDevice, surface, pSurfaceCapabilities));
        return res;
    }
    if (!pSurfaceCapabilities) {
        return VK_INCOMPLETE;
    }
    VkIcdSurfaceWin32* win32_surface = (VkIcdSurfaceWin32*)surface;
    if (win32_surface->base.platform != VK_ICD_WSI_PLATFORM_WIN32) {
        return VK_ERROR_UNKNOWN;
    }
    RECT rect;
    if (GetClientRect(win32_surface->hwnd, &rect) == FALSE) {
        return VK_ERROR_SURFACE_LOST_KHR;
    }
    pSurfaceCapabilities->minImageCount = 2;
    pSurfaceCapabilities->maxImageCount = DXGI_MAX_SWAP_CHAIN_BUFFERS;
    pSurfaceCapabilities->currentExtent = { (uint32_t)(rect.right - rect.left), (uint32_t)(rect.bottom - rect.top) };
    pSurfaceCapabilities->minImageExtent = pSurfaceCapabilities->maxImageExtent = pSurfaceCapabilities->currentExtent;
    pSurfaceCapabilities->maxImageArrayLayers = 1;
    pSurfaceCapabilities->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    pSurfaceCapabilities->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    pSurfaceCapabilities->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR |
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR /*DXGI_ALPHA_MODE_PREMULTIPLIED*/;

    // FIXME: query for the supported formats, and their respective supported usage flags, but these should be safe 99.9% of the time
    pSurfaceCapabilities->supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    return VK_SUCCESS;
}
VkResult VKAPI_CALL DOOB_GetPhysicalDeviceSurfaceCapabilities2KHR(
    VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo,
    VkSurfaceCapabilities2KHR* pSurfaceCapabilities) {

    VkInstance instance = DOOB_GetInstanceFromPhysicalDevice(physicalDevice);
    bool extension_supported_on_inst_level = false;
    uint32_t using_api_version = 0;
    if (instance == VK_NULL_HANDLE) {
        return VK_ERROR_UNKNOWN;
    }
    {
        scoped_lock l(global_lock);
        extension_supported_on_inst_level = g_instance_config[instance].supports_dxgi_ext;
        using_api_version = g_instance_config[instance].vk_api_version;
    }
    VkResult res;
    DOOB_CALL_DISPATCH_TABLE(g_instance_dispatch, instance, res, GetPhysicalDeviceSurfaceCapabilities2KHR, (physicalDevice, pSurfaceInfo, pSurfaceCapabilities));
    if (!extension_supported_on_inst_level || res != VK_SUCCESS) {
        return res;
    }
    if (!pSurfaceInfo || !pSurfaceCapabilities) {
        return VK_INCOMPLETE;
    }
    VkIcdSurfaceWin32* win32_surface = (VkIcdSurfaceWin32*)pSurfaceInfo->surface;
    if (win32_surface->base.platform != VK_ICD_WSI_PLATFORM_WIN32) {
        return VK_ERROR_UNKNOWN;
    }
    return DOOB_GetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, pSurfaceInfo->surface, &pSurfaceCapabilities->surfaceCapabilities);
}
VkResult VKAPI_CALL DOOB_GetPhysicalDeviceSurfaceCapabilities2EXT(
    VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR surface,
    VkSurfaceCapabilities2EXT* pSurfaceCapabilities) {

    VkInstance instance = DOOB_GetInstanceFromPhysicalDevice(physicalDevice);
    bool extension_supported_on_inst_level = false;
    uint32_t using_api_version = 0;
    if (instance == VK_NULL_HANDLE) {
        return VK_ERROR_UNKNOWN;
    }
    {
        scoped_lock l(global_lock);
        extension_supported_on_inst_level = g_instance_config[instance].supports_dxgi_ext;
        using_api_version = g_instance_config[instance].vk_api_version;
    }
    VkResult res;
    DOOB_CALL_DISPATCH_TABLE(g_instance_dispatch, instance, res, GetPhysicalDeviceSurfaceCapabilities2EXT, (physicalDevice, surface, pSurfaceCapabilities));
    if (!extension_supported_on_inst_level || res != VK_SUCCESS) {
        return res;
    }
    if (!pSurfaceCapabilities) {
        return VK_INCOMPLETE;
    }
    VkIcdSurfaceWin32* win32_surface = (VkIcdSurfaceWin32*)surface;
    if (win32_surface->base.platform != VK_ICD_WSI_PLATFORM_WIN32) {
        return VK_ERROR_UNKNOWN;
    }
    pSurfaceCapabilities->supportedSurfaceCounters = VK_SURFACE_COUNTER_VBLANK_BIT_EXT;
    return DOOB_GetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, (VkSurfaceCapabilitiesKHR*)&pSurfaceCapabilities);
}

static const VkPresentModeKHR SUPPORTED_PRESENT_MODES[] = {
    VK_PRESENT_MODE_IMMEDIATE_KHR,
    VK_PRESENT_MODE_MAILBOX_KHR,
    VK_PRESENT_MODE_FIFO_KHR,
};
VkResult VKAPI_CALL DOOB_GetPhysicalDeviceSurfacePresentModesKHR(
    VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    uint32_t* pPresentModeCount,
    VkPresentModeKHR* pPresentModes) {
    VkInstance instance = DOOB_GetInstanceFromPhysicalDevice(physicalDevice);
    bool extension_supported_on_inst_level = false;
    uint32_t using_api_version = 0;
    if (instance == VK_NULL_HANDLE) {
        return VK_ERROR_UNKNOWN;
    }
    {
        scoped_lock l(global_lock);
        extension_supported_on_inst_level = g_instance_config[instance].supports_dxgi_ext;
        using_api_version = g_instance_config[instance].vk_api_version;
    }
    if (!extension_supported_on_inst_level) {
        VkResult res;
        DOOB_CALL_DISPATCH_TABLE(g_instance_dispatch, instance, res, GetPhysicalDeviceSurfacePresentModesKHR, (physicalDevice, surface, pPresentModeCount, pPresentModes));
        return res;
    }

    if (!pPresentModeCount) {
        return VK_INCOMPLETE;
    }
    if (pPresentModes) {
        for (uint32_t i = 0; i < *pPresentModeCount && i < (uint32_t)(sizeof(SUPPORTED_PRESENT_MODES) / sizeof(*SUPPORTED_PRESENT_MODES)); ++i) {
            pPresentModes[i] = SUPPORTED_PRESENT_MODES[i];
        }
        if (*pPresentModeCount != (uint32_t)(sizeof(SUPPORTED_PRESENT_MODES) / sizeof(*SUPPORTED_PRESENT_MODES))) {
            return VK_INCOMPLETE;
        }
        else {
            return VK_SUCCESS;
        }
    }
    else {
        *pPresentModeCount = sizeof(SUPPORTED_PRESENT_MODES) / sizeof(*SUPPORTED_PRESENT_MODES);
        return VK_SUCCESS;
    }
}

static const VkSurfaceFormatKHR SURFACE_FORMATS[] = {
    { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR }
};

VkResult VKAPI_CALL DOOB_GetPhysicalDeviceSurfaceFormatsKHR(
    VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    uint32_t* pSurfaceFormatCount,
    VkSurfaceFormatKHR* pSurfaceFormats) {
    VkInstance instance = DOOB_GetInstanceFromPhysicalDevice(physicalDevice);
    bool extension_supported_on_inst_level = false;
    uint32_t using_api_version = 0;
    if (instance == VK_NULL_HANDLE) {
        return VK_ERROR_UNKNOWN;
    }
    {
        scoped_lock l(global_lock);
        extension_supported_on_inst_level = g_instance_config[instance].supports_dxgi_ext;
        using_api_version = g_instance_config[instance].vk_api_version;
    }
    if (!extension_supported_on_inst_level) {
        VkResult res;
        DOOB_CALL_DISPATCH_TABLE(g_instance_dispatch, instance, res, GetPhysicalDeviceSurfaceFormatsKHR, (physicalDevice, surface, pSurfaceFormatCount, pSurfaceFormats));
        return res;
    }

    // TODO: somehow query the DXGI supported formats
    if (!pSurfaceFormatCount) {
        return VK_INCOMPLETE;
    }
    if (pSurfaceFormats == NULL) {
        *pSurfaceFormatCount = ARRAY_SIZE(SURFACE_FORMATS);
        return VK_SUCCESS;
    }
    else {
        for (uint32_t i = 0; i < *pSurfaceFormatCount; ++i) {
            pSurfaceFormats[i] = SURFACE_FORMATS[i];
        }
        if (*pSurfaceFormatCount < ARRAY_SIZE(SURFACE_FORMATS)) {
            return VK_INCOMPLETE;
        }
        return VK_SUCCESS;
    }

}

VkResult VKAPI_CALL DOOB_GetPhysicalDeviceSurfaceSupportKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t                                    queueFamilyIndex,
    VkSurfaceKHR                                surface,
    VkBool32* pSupported) {
    VkInstance instance = DOOB_GetInstanceFromPhysicalDevice(physicalDevice);
    bool extension_supported_on_inst_level = false;
    uint32_t using_api_version = 0;
    if (instance == VK_NULL_HANDLE) {
        return VK_ERROR_UNKNOWN;
    }
    {
        scoped_lock l(global_lock);
        extension_supported_on_inst_level = g_instance_config[instance].supports_dxgi_ext;
        using_api_version = g_instance_config[instance].vk_api_version;
    }
    if (!extension_supported_on_inst_level) {
        VkResult res;
        DOOB_CALL_DISPATCH_TABLE(g_instance_dispatch, instance, res, GetPhysicalDeviceSurfaceSupportKHR, (physicalDevice, queueFamilyIndex, surface, pSupported));
        return res;
    }

    //// Any queue is supported, as long as we can release and acquire the queued mutex
    //*pSupported = VK_TRUE;

    // FIXME: make command pools for all families if necessary
    *pSupported = queueFamilyIndex == DOOB_COMMAND_POOL_QUEUE_FAMILY ? VK_TRUE : VK_FALSE;
    return VK_SUCCESS;
}

VkResult VKAPI_CALL DOOB_CreateDevice(
    VkPhysicalDevice                            physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice)
{
    VkLayerDeviceCreateInfo* layerCreateInfo = (VkLayerDeviceCreateInfo*)pCreateInfo->pNext;

    VkInstance instance;
    uint32_t enabled_api_version = 0;
    bool supports_dxgi_ext = false;
    {
        instance = DOOB_GetInstanceFromPhysicalDevice(physicalDevice);
        if (instance == VK_NULL_HANDLE) {
            return VK_ERROR_UNKNOWN;
        }
        scoped_lock l(global_lock);
        enabled_api_version = g_instance_config[instance].vk_api_version;
        supports_dxgi_ext = g_instance_config[instance].supports_dxgi_ext;
    }

    // step through the chain of pNext until we get to the link info
    while (layerCreateInfo && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO ||
        layerCreateInfo->function != VK_LAYER_LINK_INFO))
    {
        layerCreateInfo = (VkLayerDeviceCreateInfo*)layerCreateInfo->pNext;
    }

    if (layerCreateInfo == NULL)
    {
        // No loader instance create info
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PFN_vkGetInstanceProcAddr gipa = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr gdpa = layerCreateInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    // move chain on for next layer
    layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;

    PFN_vkCreateDevice createFunc = (PFN_vkCreateDevice)gipa(VK_NULL_HANDLE, "vkCreateDevice");

    // Inject DOOB DXGI if the layer is overriding !
    VkDeviceCreateInfo device_create_info_copy = *pCreateInfo;

    bool is_dxgi_enabled = false;
    bool is_dxgi_supported = supports_dxgi_ext;

    // Check if the extension is supported!
    if (is_dxgi_supported) {
        uint32_t pcount = 0;
        VkResult result = DOOB_EnumerateDeviceExtensionProperties(physicalDevice, VK_LAYER_DOOB_DXGI_SWAPCHAIN_NAME, &pcount, NULL);
        if (result != VK_SUCCESS) {
            DOOB_print("something went very wrong");
            return VK_ERROR_UNKNOWN;
        }
        if (pcount == 0) {
            is_dxgi_supported = false;
        }
    }
    DoobSettings global_dxgi_layer_settings = DOOB_LoadSettings();

    VkPhysicalDeviceDxgiFeaturesDOOB* dxgi_device_features = (VkPhysicalDeviceDxgiFeaturesDOOB*)pCreateInfo->pNext;
    while (dxgi_device_features && (dxgi_device_features->sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DXGI_FEATURES_DOOB)) {
        dxgi_device_features = (VkPhysicalDeviceDxgiFeaturesDOOB*)dxgi_device_features->pNext;
    }

    std::vector<const char*> enabled_extensions(pCreateInfo->enabledExtensionCount);
    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i) {
        enabled_extensions[i] = pCreateInfo->ppEnabledExtensionNames[i];
    }
    // Is the layer enabling it and the application didnt? Enable all required extensions!
    if (global_dxgi_layer_settings.force_enable_dxgi) {
        is_dxgi_enabled = true;
        for (const auto& ext : REQUIRED_DEVICE_EXTS) {
            if (enabled_api_version >= ext.promoted_to_vk) continue; // No enabling needed
            bool already_enabled = false;
            for (const char* enabled_ext : enabled_extensions) {
                if (strcmp(enabled_ext, ext.name) == 0) {
                    already_enabled = true;
                    break;
                }
            }
            if (!already_enabled) {
                DOOB_print("[DOOB WARNING]: Implicitly enabling extension %s\n", ext.name);
                enabled_extensions.push_back(ext.name);
            }
        }
    }
    // Update extensions!
    device_create_info_copy.ppEnabledExtensionNames = enabled_extensions.data();
    device_create_info_copy.enabledExtensionCount = (uint32_t)enabled_extensions.size();

    // Check for dependencies!
    bool dependency_enabled = supports_dxgi_ext;
    if (device_create_info_copy.ppEnabledExtensionNames) {
        for (uint32_t i = 0; i < device_create_info_copy.enabledExtensionCount; i++) {
            if (strcmp(device_create_info_copy.ppEnabledExtensionNames[i], VK_DOOB_DXGI_SWAPCHAIN_EXTENSION_NAME) == 0) {
                is_dxgi_enabled = true;
                break;
            }
        }
        if (dependency_enabled) {
            for (const auto& ext : REQUIRED_DEVICE_EXTS) {
                if (enabled_api_version >= ext.promoted_to_vk) continue; // No enabling needed
                bool found = false;
                for (uint32_t i = 0; i < device_create_info_copy.enabledExtensionCount; ++i) {
                    if (strcmp(device_create_info_copy.ppEnabledExtensionNames[i], ext.name) == 0) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    dependency_enabled = false;
                    break;
                }
            }
        }
    }

    // ENFORCE DEPENDENCY
    if (is_dxgi_enabled && !dependency_enabled) {
        DOOB_print("[DOOB ERROR] " VK_DOOB_DXGI_SWAPCHAIN_EXTENSION_NAME " requires the following device extensions:");
        for (const auto& ext : REQUIRED_DEVICE_EXTS) {
            if (enabled_api_version >= ext.promoted_to_vk) continue; // No enabling needed
            DOOB_print(" %s", ext.name);
        }
        for (const auto& ext : REQUIRED_INSTANCE_EXTS) {
            if (enabled_api_version >= ext.promoted_to_vk) continue; // No enabling needed
            DOOB_print(" %s", ext.name);
        }
        DOOB_print("\n");
        return VK_ERROR_EXTENSION_NOT_PRESENT; // Fail the creation
    }
    // ENFORCE SUPPORT
    if (!is_dxgi_supported && is_dxgi_enabled) {
        DOOB_print("[DOOB ERROR] " VK_DOOB_DXGI_SWAPCHAIN_EXTENSION_NAME " is not supported!\n");
        return VK_ERROR_EXTENSION_NOT_PRESENT; // Fail the creation
    }

    bool dxgi_swapchain_enabled = false;
    // Set the DXGI features
    VkPhysicalDeviceDxgiFeaturesDOOB local_dxgi_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DXGI_FEATURES_DOOB,
        .dxgiSwapchain = VK_FALSE,
        .dxgiVersion = VK_DXGI_DEVICE_VERSION_AUTO_DOOB,
    };

    if (is_dxgi_enabled) {
        if (dxgi_device_features) {
            local_dxgi_features = *dxgi_device_features;
        }

        if (global_dxgi_layer_settings.force_enable_dxgi) {
            local_dxgi_features.dxgiSwapchain = VK_TRUE;
            if (global_dxgi_layer_settings.force_dxgi_version) {
                local_dxgi_features.dxgiVersion = global_dxgi_layer_settings.force_dxgi_version_value;
            }
        }
        dxgi_swapchain_enabled = local_dxgi_features.dxgiSwapchain;

        if (dxgi_swapchain_enabled) {
            // vkGetPhysicalDeviceFeatures2 is supported on this level
            VkPhysicalDeviceDxgiFeaturesDOOB dxgi_features{
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DXGI_FEATURES_DOOB
            };

            VkPhysicalDeviceFeatures2 properties2{
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                .pNext = &dxgi_features,
            };
            DOOB_GetPhysicalDeviceFeatures2(physicalDevice, &properties2);

            if (dxgi_features.dxgiSwapchain == VK_FALSE) {
                // DXGI swapchain somehow not supported
                return VK_ERROR_FEATURE_NOT_PRESENT;
            }
        }
    }

    VkResult ret = createFunc(physicalDevice, &device_create_info_copy, pAllocator, pDevice);
    if (ret != VK_SUCCESS) {
        return ret;
    }
    {
        scoped_lock l(global_lock);
        g_device_to_physical[*pDevice] = physicalDevice;
    }

#define ASSIGN_DISPATCH(fn) dispatchTable.fn = (PFN_vk##fn)gdpa(*pDevice, "vk"#fn) 

    // fetch our own dispatch table for the functions we need, into the next layer
    VkLayerDispatchTable dispatchTable;
    // overriding functions
    ASSIGN_DISPATCH(GetDeviceProcAddr);
    ASSIGN_DISPATCH(DestroyDevice);
    ASSIGN_DISPATCH(GetDeviceQueue);
    ASSIGN_DISPATCH(GetDeviceQueue2);
    ASSIGN_DISPATCH(GetDeviceGroupPresentCapabilitiesKHR);
    ASSIGN_DISPATCH(QueuePresentKHR);
    ASSIGN_DISPATCH(CreateSwapchainKHR);
    ASSIGN_DISPATCH(CreateSharedSwapchainsKHR);
    ASSIGN_DISPATCH(DestroySwapchainKHR);
    ASSIGN_DISPATCH(AcquireNextImageKHR);
    ASSIGN_DISPATCH(AcquireNextImage2KHR);
    ASSIGN_DISPATCH(GetSwapchainImagesKHR);
    ASSIGN_DISPATCH(GetSwapchainStatusKHR);
    ASSIGN_DISPATCH(AcquireFullScreenExclusiveModeEXT);
    ASSIGN_DISPATCH(GetPastPresentationTimingGOOGLE);
    ASSIGN_DISPATCH(GetRefreshCycleDurationGOOGLE);
    ASSIGN_DISPATCH(GetSwapchainCounterEXT);
    ASSIGN_DISPATCH(GetSwapchainTimeDomainPropertiesEXT);
    ASSIGN_DISPATCH(GetSwapchainTimingPropertiesEXT);
    ASSIGN_DISPATCH(LatencySleepNV);
    ASSIGN_DISPATCH(ReleaseFullScreenExclusiveModeEXT);
    ASSIGN_DISPATCH(SetHdrMetadataEXT);
    ASSIGN_DISPATCH(SetLatencyMarkerNV);
    ASSIGN_DISPATCH(SetLatencySleepModeNV);
    ASSIGN_DISPATCH(SetLocalDimmingAMD);
    ASSIGN_DISPATCH(SetSwapchainPresentTimingQueueSizeEXT);
    ASSIGN_DISPATCH(WaitForPresentKHR);
    ASSIGN_DISPATCH(WaitForPresent2KHR);

    // vulkan functios we rely on!
    ASSIGN_DISPATCH(CreateImage);
    ASSIGN_DISPATCH(GetImageMemoryRequirements2);
    ASSIGN_DISPATCH(AllocateMemory);
    ASSIGN_DISPATCH(BindImageMemory);
    ASSIGN_DISPATCH(CreateFence);
    ASSIGN_DISPATCH(DestroyFence);
    ASSIGN_DISPATCH(QueueSubmit);
    ASSIGN_DISPATCH(WaitForFences);
    ASSIGN_DISPATCH(AllocateCommandBuffers);
    ASSIGN_DISPATCH(CreateCommandPool);
    ASSIGN_DISPATCH(DestroyCommandPool);
    ASSIGN_DISPATCH(ResetCommandBuffer);
    ASSIGN_DISPATCH(BeginCommandBuffer);
    ASSIGN_DISPATCH(EndCommandBuffer);
    ASSIGN_DISPATCH(CmdPipelineBarrier);
#undef ASSIGN_DISPATCH
    // store the table by key
    {
        scoped_lock l(global_lock);
        g_device_dispatch[*pDevice] = dispatchTable;
    }

    DOOB_D3D11FactoryInfo factory_info = {};
    if (dxgi_swapchain_enabled) {
        VkPhysicalDeviceIDProperties id_properties = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES
        };
        VkPhysicalDeviceProperties2 properties2{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            .pNext = &id_properties,
        };
        DOOB_GetPhysicalDeviceProperties2(physicalDevice, &properties2);

        if (id_properties.deviceLUIDValid == VK_FALSE) {
            return VK_ERROR_FEATURE_NOT_PRESENT;
        }
        LUID vk_win32_luid;
        memcpy(&vk_win32_luid, id_properties.deviceLUID, sizeof(LUID));


        // Factory
        UINT factory_flags = 0;
#ifdef _DEBUG
        factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
        HR(CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&factory_info.dxgi_factory)));

        // Device
        // TODO: Support WARP driver
        bool has_found_adapter = false;
        for (UINT i = 0;; ++i) {
            IDXGIAdapter1* adapter = nullptr;
            if (FAILED(factory_info.dxgi_factory->EnumAdapters1(i, &adapter))) {
                break;
            }

            DXGI_ADAPTER_DESC1 adapter_desc;
            adapter->GetDesc1(&adapter_desc);

            if (adapter_desc.AdapterLuid.LowPart != vk_win32_luid.LowPart ||
                adapter_desc.AdapterLuid.HighPart != vk_win32_luid.HighPart) {
                adapter->Release();
                continue;
            }

            if (adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                adapter->Release();
                continue;
            }

            UINT flags = 0;
#ifdef _DEBUG
            flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
            D3D_FEATURE_LEVEL feature_levels[] = { D3D_FEATURE_LEVEL_11_0 };
            D3D_FEATURE_LEVEL picked_feature_level;

            if (FAILED(D3D11CreateDevice(
                adapter,
                D3D_DRIVER_TYPE_UNKNOWN,
                nullptr,
                flags,
                feature_levels,
                _countof(feature_levels),
                D3D11_SDK_VERSION,
                &factory_info.device,
                &picked_feature_level,
                &factory_info.device_context
            ))) {
                adapter->Release();
                continue;
            }
            assert(picked_feature_level >= D3D_FEATURE_LEVEL_11_0);

            has_found_adapter = true;
            adapter->Release();
            break;
        }

        if (!has_found_adapter) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }
    {
        scoped_lock l(global_lock);
        g_device_config[*pDevice] = {
            .dxgi_swapchain_extension_enabled = is_dxgi_enabled,
            .dxgi_swapchain_feature_enabled = dxgi_swapchain_enabled,
            .factory_info = factory_info
        };
    }
    return VK_SUCCESS;
}

void VKAPI_CALL DOOB_DestroyDevice(
    VkDevice                                    device,
    const VkAllocationCallbacks* pAllocator) {
    PFN_vkDestroyDevice call = NULL;
    {
        scoped_lock l(global_lock);
        auto it = g_device_dispatch.find(device);
        if (it != g_device_dispatch.end()) {
            call = it->second.DestroyDevice;
            g_device_dispatch.erase(it);
            std::vector<VkQueue> to_erase = {};
            for (auto kv : g_queue_ownership) {
                if (kv.second == device) {
                    to_erase.push_back(kv.first);
                }
            }
            for (VkQueue queue : to_erase) {
                g_queue_ownership.erase(queue);
            }
        }
        auto it2 = g_device_config.find(device);
        if (it2 != g_device_config.end()) {
            g_device_config.erase(it2);
        }
    }
    if (call != NULL) {
        call(device, pAllocator);
    }
}


void VKAPI_CALL DOOB_GetDeviceQueue(
    VkDevice                                    device,
    uint32_t                                    queueFamilyIndex,
    uint32_t                                    queueIndex,
    VkQueue* pQueue) {

    g_device_dispatch[device].GetDeviceQueue(device, queueFamilyIndex, queueIndex, pQueue);
    if (pQueue) {
        scoped_lock l(global_lock);
        g_queue_ownership[*pQueue] = device;
    }
}

VkResult VKAPI_CALL DOOB_GetDeviceGroupPresentCapabilitiesKHR(
    VkDevice                                    device,
    VkDeviceGroupPresentCapabilitiesKHR* pDeviceGroupPresentCapabilities) {

    // TODO

    return VK_INCOMPLETE;
}
void VKAPI_CALL DOOB_GetDeviceQueue2(
    VkDevice                                    device,
    const VkDeviceQueueInfo2* pQueueInfo,
    VkQueue* pQueue) {

    g_device_dispatch[device].GetDeviceQueue2(device, pQueueInfo, pQueue);
    {
        scoped_lock l(global_lock);
        g_queue_ownership[*pQueue] = device;
    }
}

VkResult VKAPI_CALL DOOB_QueuePresentKHR(
    VkQueue                                     queue,
    const VkPresentInfoKHR* pPresentInfo) {
    DOOB_print("presentation\n");
    VkDevice device;
    {
        scoped_lock l(global_lock);
        device = g_queue_ownership[queue];
    }
    assert(device != VK_NULL_HANDLE);

    // accounts for all possible swapchain writes, via storage image, framebuffer attachment or copy command
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkAccessFlags waitAccess = VK_ACCESS_MEMORY_WRITE_BIT |
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    std::vector<VkPipelineStageFlags> wait_dst_mask(pPresentInfo->waitSemaphoreCount, waitStage);
    for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++) {
        VkSwapchainKHR sc = pPresentInfo->pSwapchains[i];
        DOOB_DxgiSwapchain* doob_sc = DOOB_GetObjectIfExists(g_dxgi_swapchains, DOOB_SWAPCHAIN_HANDLE_ID, sc);

        DOOB_print("swapchain %p\n", doob_sc);
        if (doob_sc) {
            static constexpr bool ENABLE_SYNC_VK = true; // debugging on intel
            static constexpr bool ENABLE_SYNC_DX = true; 
            if (ENABLE_SYNC_VK) {
                uint32_t image_idx = pPresentInfo->pImageIndices[i];
                if (image_idx >= doob_sc->sync_command_buffers.size()) return VK_ERROR_OUT_OF_DATE_KHR;
                DOOB_print("recording commands %i/%i\n", (int)image_idx, (int)doob_sc->sync_command_buffers.size());
                VkCommandBuffer sync_commands = doob_sc->sync_command_buffers[image_idx];
                {
                    // FIXME: why are validation layers crashing here????
                    VkCommandBufferBeginInfo begin_info = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
                    VkResult res;
                    DOOB_CALL_DISPATCH_TABLE(g_device_dispatch, device, res, ResetCommandBuffer, (sync_commands, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT));
                    DOOB_print("ResetCommandBuffer %i\n", res);
                    DOOB_CALL_DISPATCH_TABLE(g_device_dispatch, device, res, BeginCommandBuffer, (sync_commands, &begin_info));
                    DOOB_print("BeginCommandBuffer %i\n", res);
                    if (res != VK_SUCCESS) {
                        return res;
                    }
                    VkImageMemoryBarrier img_barrier = {
                        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                        .srcAccessMask = waitAccess,
                        .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
                        .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                        .image = doob_sc->vk_mirrored_shared_image,
                        .subresourceRange = {
                            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                            .levelCount = 1,
                            .layerCount = 1
                        },
                    };
                    VkMemoryBarrier barrier = {
                        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                        .srcAccessMask = waitAccess,
                        .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT
                    };
                    DOOB_print("pipeline barrier\n");
                    DOOB_CALL_VOID_DISPATCH_TABLE(g_device_dispatch, device, CmdPipelineBarrier, (
                        sync_commands,
                        waitStage,
                        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        0,
                        1, &barrier,
                        0, nullptr,
                        1, &img_barrier
                        ));

                    DOOB_CALL_DISPATCH_TABLE(g_device_dispatch, device, res, EndCommandBuffer, (sync_commands));
                    DOOB_print("EndCommandBuffer %i\n", res);
                    if (res != VK_SUCCESS) {
                        return res;
                    }
                }
                DOOB_print("submitting\n");
                uint64_t win32_acquire_key = MUTEX_KEY_VULKAN;
                uint64_t win32_release_key = MUTEX_KEY_D3D11;
                uint32_t win32_acquire_timeout = UINT32_MAX;
                VkWin32KeyedMutexAcquireReleaseInfoKHR win32_keyed_mutex_acquire_release_info = {
                    .sType = VK_STRUCTURE_TYPE_WIN32_KEYED_MUTEX_ACQUIRE_RELEASE_INFO_KHR,
                    .acquireCount = 1,
                    .pAcquireSyncs = &doob_sc->vk_mirrored_shared_image_memory,
                    .pAcquireKeys = &win32_acquire_key,
                    .pAcquireTimeouts = &win32_acquire_timeout,
                    .releaseCount = 1,
                    .pReleaseSyncs = &doob_sc->vk_mirrored_shared_image_memory,
                    .pReleaseKeys = &win32_release_key
                };
                VkSubmitInfo submit_info = {
                    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                    .pNext = &win32_keyed_mutex_acquire_release_info,
                    .waitSemaphoreCount = pPresentInfo->waitSemaphoreCount,
                    .pWaitSemaphores = pPresentInfo->pWaitSemaphores,
                    .pWaitDstStageMask = wait_dst_mask.data(), // See fix below
                    .commandBufferCount = 1,
                    .pCommandBuffers = &sync_commands,
                };

                // TEMPORARY FENCE
                VkFence fence;
                VkFenceCreateInfo fence_info = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
                VkResult res;
                DOOB_CALL_DISPATCH_TABLE(g_device_dispatch, device, res, CreateFence, (device, &fence_info, nullptr, &fence));
                if (res != VK_SUCCESS) {
                    return VK_ERROR_DEVICE_LOST;
                }
                DOOB_print("submission\n");
                DOOB_CALL_DISPATCH_TABLE(g_device_dispatch, device, res, QueueSubmit, (queue, 1, &submit_info, fence));
                if (res != VK_SUCCESS) {
                    return VK_ERROR_DEVICE_LOST;
                }
                DOOB_print("ready to to dxgi!\n");
                DOOB_CALL_DISPATCH_TABLE(g_device_dispatch, device, res, WaitForFences, (device, 1, &fence, VK_TRUE, UINT64_MAX));
                if (res != VK_SUCCESS) {
                    return VK_ERROR_DEVICE_LOST;
                }
                DOOB_CALL_VOID_DISPATCH_TABLE(g_device_dispatch, device, DestroyFence, (device, fence, nullptr));
            }
            // D3D11 Present
            DOOB_D3D11FactoryInfo dx11_factory_info = doob_sc->dxgi_factory_info;

            HRESULT hres = S_OK;
            DXGI_SWAP_CHAIN_DESC1 swapchain_desc;
            hres = doob_sc->swapchain->GetDesc1(&swapchain_desc);
            if (hres != S_OK) {
                return VK_ERROR_SURFACE_LOST_KHR;
            }
            D3D11_VIEWPORT viewport = { // TODO: Query the current Vulkan viewport?
                .TopLeftX = 0,
                .TopLeftY = 0,
                .Width = (FLOAT)swapchain_desc.Width,
                .Height = (FLOAT)swapchain_desc.Height,
                .MinDepth = 0.0f,
                .MaxDepth = 1.0f
            };
            dx11_factory_info.device_context->RSSetViewports(1, &viewport);
            DOOB_print("synchronising dxgi!\n");
            if (ENABLE_SYNC_DX) {
                hres = doob_sc->shared_keyed_mutex->AcquireSync(MUTEX_KEY_D3D11, INFINITE);
                if (hres != S_OK) {
                    return VK_ERROR_SURFACE_LOST_KHR;
                }
            }
            dx11_factory_info.device_context->CopyResource(doob_sc->swapchain_backbuffer, doob_sc->shared_intermediate_tex);
            DOOB_print("presenting dxgi!\n");
            hres = doob_sc->swapchain->Present(doob_sc->sync_interval, doob_sc->present_flags);
            if (pPresentInfo->pResults != NULL) {
                if (hres == S_OK) {
                    pPresentInfo->pResults[i] = VK_SUCCESS;
                }
                else if (hres == DXGI_STATUS_OCCLUDED) {
                    pPresentInfo->pResults[i] = VK_SUBOPTIMAL_KHR;
                }
                else if (hres == DXGI_ERROR_DEVICE_REMOVED || hres == DXGI_ERROR_DEVICE_RESET) {
                    pPresentInfo->pResults[i] = VK_ERROR_DEVICE_LOST;
                    return VK_ERROR_DEVICE_LOST;
                }
                else {
                    pPresentInfo->pResults[i] = VK_ERROR_SURFACE_LOST_KHR;
                }
            }
            if (ENABLE_SYNC_DX) {
                hres = doob_sc->shared_keyed_mutex->ReleaseSync(MUTEX_KEY_VULKAN);
                if (hres != S_OK) {
                    return VK_ERROR_SURFACE_LOST_KHR;
                }
            }
            DOOB_print("finished!\n");
        }
    }
    return VK_SUCCESS;
}

VkResult VKAPI_CALL DOOB_CreateSwapchainKHR(
    VkDevice                                    device,
    const VkSwapchainCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSwapchainKHR* pSwapchain) {

    DOOB_print("[DOOB] Your swapchain is out of bounds\n");

    VkDxgiSwapchainCreateInfoDOOB local_dxgi_info = {
        .sType = VK_STRUCTURE_TYPE_DXGI_SWAPCHAIN_CREATE_INFO_DOOB,
        .swapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
    };

    VkDxgiSwapchainCreateInfoDOOB* dxgi_info = (VkDxgiSwapchainCreateInfoDOOB*)pCreateInfo->pNext;
    while (dxgi_info && (dxgi_info->sType != VK_STRUCTURE_TYPE_DXGI_SWAPCHAIN_CREATE_INFO_DOOB))
    {
        dxgi_info = (VkDxgiSwapchainCreateInfoDOOB*)dxgi_info->pNext;
    }
    if (dxgi_info) {
        local_dxgi_info = *dxgi_info;
    }

    VkAllocationCallbacks alloc = DOOB_GetFilledAllocationCallbacks(pAllocator);

    uint32_t swapchain_handle;
    DOOB_DxgiSwapchain* swapchain_obj = DOOB_AllocCountedHandle(g_dxgi_swapchains, &swapchain_handle, &alloc);
    if (!swapchain_obj) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    DoobDeviceConfig device_config;
    DoobWin32Surface win32_surface;
    {
        scoped_lock l(global_lock);
        device_config = g_device_config[device];
        win32_surface = g_win32_surfaces[pCreateInfo->surface];
    }
    DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {
        .Width = pCreateInfo->imageExtent.width,
        .Height = pCreateInfo->imageExtent.height,
        .Format = DOOB_DXGIFormat_FromVkFormat(pCreateInfo->imageFormat),
        .Stereo = FALSE,
        .SampleDesc = {.Count = 1, .Quality = 0 },
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = pCreateInfo->minImageCount,
        .Scaling = DXGI_SCALING_NONE,
        .SwapEffect = local_dxgi_info.swapEffect,
        .AlphaMode = DOOB_DXGIAlphaMode_FromVkCompositeAlpha(pCreateInfo->compositeAlpha),
        .Flags = 0
    };

    DOOB_D3D11FactoryInfo factory_info = device_config.factory_info;

    HR(device_config.factory_info.dxgi_factory->CreateSwapChainForHwnd(
        factory_info.device,
        win32_surface.hwnd,
        &swapchain_desc,
        nullptr,
        nullptr,
        &swapchain_obj->swapchain
    ));
    swapchain_obj->swapchain_image_count = swapchain_desc.BufferCount;
    swapchain_obj->image_index = 0;
    swapchain_obj->dxgi_factory_info = factory_info;
    swapchain_obj->present_flags = 0;
    swapchain_obj->sync_interval = pCreateInfo->presentMode == VK_PRESENT_MODE_FIFO_KHR ? 1 : 0;
    HR(swapchain_obj->swapchain->GetBuffer(0, IID_PPV_ARGS(&swapchain_obj->swapchain_backbuffer)));
    HR(factory_info.device->CreateRenderTargetView(
        swapchain_obj->swapchain_backbuffer,
        nullptr,
        &swapchain_obj->swapchain_rtv
    ));

    // Create shared texture handle
    // TODO: Not sure if this should be created elsewhere, but for now it will have to do
    // TODO: There's also a question of how this will work with multiple frames in flight? In such cases we must have multiple shared textures
    //       Perhaps it would be smart to have a num_frames_in_flight field in the pNext struct?
    // TODO: Sync the texture desc with the Vulkan desc
    D3D11_TEXTURE2D_DESC shared_tex_desc = {
        .Width = swapchain_desc.Width,
        .Height = swapchain_desc.Height,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = swapchain_desc.Format,
        .SampleDesc = {.Count = 1, .Quality = 0 },
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
        .CPUAccessFlags = 0,
        .MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX
    };

    HR(factory_info.device->CreateTexture2D(&shared_tex_desc, nullptr, &swapchain_obj->shared_intermediate_tex));

    IDXGIResource1* dxgi_resource = nullptr;
    HR(swapchain_obj->shared_intermediate_tex->QueryInterface(IID_PPV_ARGS(&dxgi_resource)));
    HR(dxgi_resource->CreateSharedHandle(
        nullptr,
        DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
        nullptr,
        &swapchain_obj->shared_intermediate_handle
    ));
    HR(swapchain_obj->shared_intermediate_tex->QueryInterface(IID_PPV_ARGS(&swapchain_obj->shared_keyed_mutex)));

    // TODO: We should probably move this to GetSwapchainImagesKHR
    // Create VkImage object from the shared tex
    VkExternalMemoryImageCreateInfo external_memory_image_info = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT
    };
    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = &external_memory_image_info,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = pCreateInfo->imageFormat, // TODO: Convert to VK format
        .extent = { pCreateInfo->imageExtent.width, pCreateInfo->imageExtent.width, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | pCreateInfo->imageUsage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    VkResult res;
    DOOB_CALL_DISPATCH_TABLE(g_device_dispatch, device, res, CreateImage, (device, &image_info, nullptr, &swapchain_obj->vk_mirrored_shared_image));
    assert(res == VK_SUCCESS);

    VkMemoryPropertyFlags mem_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkPhysicalDeviceMemoryProperties mem_props;

    VkImageMemoryRequirementsInfo2 mem_reqs_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
        .pNext = nullptr,
        .image = swapchain_obj->vk_mirrored_shared_image
    };
    VkMemoryDedicatedRequirements mem_dedicated_reqs = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS
    };
    VkMemoryRequirements2 mem_reqs2 = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
        .pNext = &mem_dedicated_reqs
    };

    VkPhysicalDevice physical_device;
    {
        scoped_lock l(global_lock);
        physical_device = g_device_to_physical[device];
    }

    DOOB_CALL_VOID_DISPATCH_TABLE(g_device_dispatch, device, GetImageMemoryRequirements2, (device, &mem_reqs_info, &mem_reqs2));
    VkInstance instance = DOOB_GetInstanceFromPhysicalDevice(physical_device);
    DOOB_CALL_VOID_DISPATCH_TABLE(g_instance_dispatch, instance, GetPhysicalDeviceMemoryProperties, (physical_device, &mem_props));

    bool found_mem_type = false;
    uint32_t mem_type_idx = 0;
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((mem_reqs2.memoryRequirements.memoryTypeBits & (1 << i)) && ((mem_props.memoryTypes[i].propertyFlags & mem_flags) == mem_flags)) {
            mem_type_idx = i;
            found_mem_type = true;
            break;
        }
    }
    assert(found_mem_type); // TODO: Fallback?
    VkMemoryDedicatedAllocateInfo dedicated_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .image = swapchain_obj->vk_mirrored_shared_image
    };
    VkImportMemoryWin32HandleInfoKHR import_win32_handle_info = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR,
        .pNext = &dedicated_alloc_info,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT,
        .handle = swapchain_obj->shared_intermediate_handle
    };
    VkMemoryAllocateInfo mem_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &import_win32_handle_info,
        .memoryTypeIndex = mem_type_idx
    };
    DOOB_CALL_DISPATCH_TABLE(g_device_dispatch, device, res, AllocateMemory, (device, &mem_alloc_info, nullptr, &swapchain_obj->vk_mirrored_shared_image_memory));
    assert(res == VK_SUCCESS);
    DOOB_CALL_DISPATCH_TABLE(g_device_dispatch, device, res, BindImageMemory, (device, swapchain_obj->vk_mirrored_shared_image, swapchain_obj->vk_mirrored_shared_image_memory, 0));
    assert(res == VK_SUCCESS);

    DOOB_print("[DOOB] making commands\n");
    // 1. Create ONE Command Pool for all buffers
    VkCommandPoolCreateInfo sync_pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = DOOB_COMMAND_POOL_QUEUE_FAMILY
    };
    DOOB_print("[DOOB] xxx\n");
    VkCommandPool swapchain_pool;
    DOOB_CALL_DISPATCH_TABLE(g_device_dispatch, device, res, CreateCommandPool, (device, &sync_pool_info, pAllocator, &swapchain_pool));
    if (res != VK_SUCCESS) {
        DOOB_print("[DOOB] FAIL pool, %i\n", res);
        return res;
    }
    DOOB_print("[DOOB] wait\n");

    new (&swapchain_obj->sync_command_buffers) std::vector<VkCommandBuffer>();
    new (&swapchain_obj->sync_command_pool) std::vector<VkCommandPool>();

    swapchain_obj->sync_command_pool.push_back(swapchain_pool);

    VkCommandBufferAllocateInfo sync_cmd_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = swapchain_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = swapchain_obj->swapchain_image_count
    };
    DOOB_print("[DOOB] one more\n");

    swapchain_obj->sync_command_buffers.resize(swapchain_obj->swapchain_image_count);

    DOOB_print("[DOOB] one allocate\n");
    DOOB_CALL_DISPATCH_TABLE(g_device_dispatch, device, res, AllocateCommandBuffers, (device, &sync_cmd_alloc_info, swapchain_obj->sync_command_buffers.data()));

    if (res != VK_SUCCESS) {
        DOOB_print("[DOOB] FAIL commands, %i\n", res);
        DOOB_CALL_VOID_DISPATCH_TABLE(g_device_dispatch, device, DestroyCommandPool, (device, swapchain_pool, pAllocator));
        return res;
    }
    DOOB_print("[DOOB] success commands\n");

    *pSwapchain = DOOB_MakeHandle<VkSwapchainKHR>(DOOB_SWAPCHAIN_HANDLE_ID, swapchain_handle);

    return VK_SUCCESS;
}

VkResult VKAPI_CALL DOOB_CreateSharedSwapchainsKHR(
    VkDevice                                    device,
    uint32_t                                    swapchainCount,
    const VkSwapchainCreateInfoKHR* pCreateInfos,
    const VkAllocationCallbacks* pAllocator,
    VkSwapchainKHR* pSwapchains) {

    // TODO

    return VK_ERROR_UNKNOWN;
}

void VKAPI_CALL DOOB_DestroySwapchainKHR(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    const VkAllocationCallbacks* pAllocator) {

    VkAllocationCallbacks alloc = DOOB_GetFilledAllocationCallbacks(pAllocator);

    uint32_t swapchain_handle = DOOB_GetCounterAndVerify(DOOB_SWAPCHAIN_HANDLE_ID, swapchain);
    if (swapchain_handle == DOOB_INVALID_COUNTER_HANDLE) {
        return;
    }
    DOOB_ReleaseCountedHandle(g_dxgi_swapchains, swapchain_handle, &alloc);
}
VkResult VKAPI_CALL DOOB_GetSwapchainImagesKHR(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint32_t* pSwapchainImageCount,
    VkImage* pSwapchainImages) {
    DOOB_print("Get images\n");

    // EXAMPLE HOW TO USE:

    DOOB_DxgiSwapchain* swapchain_obj = DOOB_GetObjectIfExists(g_dxgi_swapchains, DOOB_SWAPCHAIN_HANDLE_ID, swapchain);
    if (!swapchain_obj) {
        DOOB_print("Failed what\n");
        return VK_ERROR_UNKNOWN;
    }
    if (pSwapchainImageCount == NULL) {
        return VK_INCOMPLETE;
    }
    if (pSwapchainImages == nullptr) {
        *pSwapchainImageCount = (uint32_t)swapchain_obj->swapchain_image_count;
        return VK_SUCCESS;
    }

    for (uint32_t i = 0; i < std::min(*pSwapchainImageCount, swapchain_obj->swapchain_image_count); ++i) {
        pSwapchainImages[i] = swapchain_obj->vk_mirrored_shared_image;
    }
    if (*pSwapchainImageCount < swapchain_obj->swapchain_image_count) {
        return VK_INCOMPLETE;
    }
    return VK_SUCCESS;
}

VkResult VKAPI_CALL DOOB_GetSwapchainStatusKHR(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain) {

    // TODO

    return VK_ERROR_UNKNOWN;
}

VkResult VKAPI_CALL DOOB_AcquireFullScreenExclusiveModeEXT(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain) {

    // TODO

    return VK_ERROR_UNKNOWN;
}

VkResult VKAPI_CALL DOOB_AcquireNextImageKHR(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint64_t                                    timeout,
    VkSemaphore                                 semaphore,
    VkFence                                     fence,
    uint32_t* pImageIndex) {
    DOOB_print("Acquire image\n");
    if (!pImageIndex) {
        return VK_ERROR_UNKNOWN;
    }

    DOOB_DxgiSwapchain* swapchain_obj = DOOB_GetObjectIfExists(g_dxgi_swapchains, DOOB_SWAPCHAIN_HANDLE_ID, swapchain);

    if (!swapchain_obj) {
        return VK_ERROR_UNKNOWN;
    }
    *pImageIndex = (swapchain_obj->image_index + 1) % swapchain_obj->swapchain_image_count;
    return VK_SUCCESS;
}
VkResult VKAPI_CALL DOOB_AcquireNextImage2KHR(
    VkDevice                                    device,
    const VkAcquireNextImageInfoKHR* pAcquireInfo,
    uint32_t* pImageIndex) {
    if (!pAcquireInfo) {
        return VK_ERROR_UNKNOWN;
    }
    if (!pImageIndex) {
        return VK_ERROR_UNKNOWN;
    }
    DOOB_DxgiSwapchain* swapchain_obj = DOOB_GetObjectIfExists(g_dxgi_swapchains, DOOB_SWAPCHAIN_HANDLE_ID, pAcquireInfo->swapchain);

    if (!swapchain_obj) {
        return VK_ERROR_UNKNOWN;
    }
    *pImageIndex = (swapchain_obj->image_index + 1) % swapchain_obj->swapchain_image_count;
    return VK_SUCCESS;
}
VkResult VKAPI_CALL DOOB_GetPastPresentationTimingGOOGLE(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint32_t* pPresentationTimingCount,
    VkPastPresentationTimingGOOGLE* pPresentationTimings) {

    // TODO

    return VK_ERROR_UNKNOWN;
}
VkResult VKAPI_CALL DOOB_GetRefreshCycleDurationGOOGLE(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    VkRefreshCycleDurationGOOGLE* pDisplayTimingProperties) {

    DOOB_DxgiSwapchain* swapchain_obj = DOOB_GetObjectIfExists(g_dxgi_swapchains, DOOB_SWAPCHAIN_HANDLE_ID, swapchain);
    if (!swapchain_obj) {
        return VK_ERROR_UNKNOWN;
    }
    DXGI_FRAME_STATISTICS statistics;
    HRESULT hres = swapchain_obj->swapchain->GetFrameStatistics(&statistics);
    if (hres != S_OK) {
        return VK_ERROR_SURFACE_LOST_KHR;
    }
    if (!pDisplayTimingProperties) {
        return VK_SUCCESS; // according to spec, there is no VK_INCOMPLETE?
    }

    LARGE_INTEGER ElapsedMicroseconds;
    LARGE_INTEGER Frequency;

    QueryPerformanceFrequency(&Frequency);
    ElapsedMicroseconds.QuadPart = statistics.SyncQPCTime.QuadPart;

    //
    // We now have the elapsed number of ticks, along with the
    // number of ticks-per-second. We use these values
    // to convert to the number of elapsed microseconds.
    // To guard against loss-of-precision, we convert
    // to microseconds *before* dividing by ticks-per-second.
    //

    ElapsedMicroseconds.QuadPart *= 1000000;
    ElapsedMicroseconds.QuadPart /= Frequency.QuadPart;
    pDisplayTimingProperties->refreshDuration = ((uint64_t)ElapsedMicroseconds.QuadPart) * 1000 /*to nanoseconds*/;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL DOOB_GetSwapchainCounterEXT(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    VkSurfaceCounterFlagBitsEXT                 counter,
    uint64_t* pCounterValue) {

    DOOB_DxgiSwapchain* swapchain_obj = DOOB_GetObjectIfExists(g_dxgi_swapchains, DOOB_SWAPCHAIN_HANDLE_ID, swapchain);
    if (!swapchain_obj) {
        return VK_ERROR_UNKNOWN;
    }
    DXGI_FRAME_STATISTICS statistics;
    HRESULT hres = swapchain_obj->swapchain->GetFrameStatistics(&statistics);
    if (hres != S_OK) {
        return VK_ERROR_SURFACE_LOST_KHR;
    }
    if (!pCounterValue) {
        return VK_SUCCESS; // according to spec, there is no VK_INCOMPLETE?
    }
    if (counter & VK_SURFACE_COUNTER_VBLANK_BIT_EXT) {
        *pCounterValue = (uint64_t)statistics.PresentRefreshCount;
    }
    else {
        return VK_ERROR_UNKNOWN;
    }
    return VK_SUCCESS;
}

VkResult VKAPI_CALL DOOB_GetSwapchainTimeDomainPropertiesEXT(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    VkSwapchainTimeDomainPropertiesEXT* pSwapchainTimeDomainProperties,
    uint64_t* pTimeDomainsCounter) {

    // TODO

    return VK_ERROR_UNKNOWN;
}

VkResult VKAPI_CALL DOOB_GetSwapchainTimingPropertiesEXT(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    VkSwapchainTimingPropertiesEXT* pSwapchainTimingProperties,
    uint64_t* pSwapchainTimingPropertiesCounter) {

    // TODO

    return VK_ERROR_UNKNOWN;
}

VkResult VKAPI_CALL DOOB_LatencySleepNV(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    const VkLatencySleepInfoNV* pSleepInfo) {

    // TODO

    return VK_ERROR_UNKNOWN;
}

VkResult VKAPI_CALL DOOB_ReleaseFullScreenExclusiveModeEXT(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain) {

    // TODO

    return VK_ERROR_UNKNOWN;
}
void VKAPI_CALL DOOB_SetHdrMetadataEXT(
    VkDevice                                    device,
    uint32_t                                    swapchainCount,
    const VkSwapchainKHR* pSwapchains,
    const VkHdrMetadataEXT* pMetadata) {

    // TODO

}
void VKAPI_CALL DOOB_SetLatencyMarkerNV(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    const VkSetLatencyMarkerInfoNV* pLatencyMarkerInfo) {

    // TODO

}
VkResult VKAPI_CALL DOOB_SetLatencySleepModeNV(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    const VkLatencySleepModeInfoNV* pSleepModeInfo) {

    // TODO

    return VK_ERROR_UNKNOWN;
}

void VKAPI_CALL DOOB_SetLocalDimmingAMD(
    VkDevice                                    device,
    VkSwapchainKHR                              swapChain,
    VkBool32                                    localDimmingEnable) {

    // TODO

}
VkResult VKAPI_CALL DOOB_SetSwapchainPresentTimingQueueSizeEXT(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint32_t                                    size) {

    // TODO

    return VK_ERROR_UNKNOWN;
}

VkResult VKAPI_CALL DOOB_WaitForPresentKHR(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint64_t                                    presentId,
    uint64_t                                    timeout) {

    // TODO

    return VK_ERROR_UNKNOWN;
}

VkResult VKAPI_CALL DOOB_WaitForPresent2KHR(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    const VkPresentWait2InfoKHR* pPresentWait2Info) {

    // TODO

    return VK_ERROR_UNKNOWN;
}

VkResult VKAPI_CALL DOOB_CreateWin32SurfaceKHR(
    VkInstance                                  instance,
    const VkWin32SurfaceCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSurfaceKHR* pSurface) {

    VkResult res;
    DOOB_CALL_DISPATCH_TABLE(g_instance_dispatch, instance, res, CreateWin32SurfaceKHR, (instance, pCreateInfo, pAllocator, pSurface));

    if (res == VK_SUCCESS) {
        scoped_lock l(global_lock);
        g_win32_surfaces[*pSurface] = { pCreateInfo->hinstance, pCreateInfo->hwnd };
    }

    return res;
}

// ==== OUR CUSTOM FUNCTIONS ====

VkResult VKAPI_CALL DOOB_GetDxgiSwapchainHandleDOOB(
    VkDevice                                    device,
    VkSwapchainKHR swapchain,
    IDXGISwapChain** pDxgiSwapchain
) {

    DOOB_DxgiSwapchain* swapchain_obj = DOOB_GetObjectIfExists(g_dxgi_swapchains, DOOB_SWAPCHAIN_HANDLE_ID, swapchain);
    if (!swapchain_obj) {
        return VK_ERROR_UNKNOWN;
    }

    *pDxgiSwapchain = swapchain_obj->swapchain;

    return VK_SUCCESS;
}


// ==== INSTANCE/DEVICE STUFF ====


VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL DOOB_GetInstanceProcAddr(VkInstance instance, const char* pName)
{
    // instance chain functions we intercept
    DOOB_GETPROCADDR(GetInstanceProcAddr);
    DOOB_GETPROCADDR(EnumerateInstanceLayerProperties);
    DOOB_GETPROCADDR(EnumerateDeviceExtensionProperties);
    DOOB_GETPROCADDR(EnumeratePhysicalDevices);
    DOOB_GETPROCADDR(EnumeratePhysicalDeviceGroups);
    DOOB_GETPROCADDR(EnumeratePhysicalDeviceGroupsKHR);
    DOOB_GETPROCADDR(GetPhysicalDeviceProperties2);
    DOOB_GETPROCADDR(GetPhysicalDeviceProperties2KHR);
    DOOB_GETPROCADDR(GetPhysicalDeviceFeatures2KHR);
    DOOB_GETPROCADDR(GetPhysicalDeviceFeatures2);
    DOOB_GETPROCADDR(GetPhysicalDeviceSurfaceCapabilitiesKHR);
    DOOB_GETPROCADDR(GetPhysicalDeviceSurfaceCapabilities2KHR);
    DOOB_GETPROCADDR(GetPhysicalDeviceSurfaceCapabilities2EXT);
    DOOB_GETPROCADDR(GetPhysicalDeviceSurfacePresentModesKHR);
    DOOB_GETPROCADDR(GetPhysicalDeviceSurfaceFormatsKHR);
    DOOB_GETPROCADDR(CreateInstance);
    DOOB_GETPROCADDR(CreateDevice);
    DOOB_GETPROCADDR(DestroyInstance);
    DOOB_GETPROCADDR(GetPhysicalDevicePresentRectanglesKHR);
    DOOB_GETPROCADDR(CreateWin32SurfaceKHR);

    PFN_vkVoidFunction result;
    DOOB_CALL_DISPATCH_TABLE(g_instance_dispatch, instance, result, GetInstanceProcAddr, (instance, pName));
    return result;
}

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL DOOB_GetDeviceProcAddr(VkDevice device, const char* pName)
{
    // device chain functions we intercept
    DOOB_GETPROCADDR(GetDeviceProcAddr);
    DOOB_GETPROCADDR(DestroyDevice);
    DOOB_GETPROCADDR(GetDeviceQueue);
    DOOB_GETPROCADDR(GetDeviceQueue2);
    if (strcmp(pName, "vkGetDxgiSwapchainHandleDOOB") == 0) {
        return (PFN_vkVoidFunction)DOOB_GetDxgiSwapchainHandleDOOB;
    }
    bool enabled_extension = false;
    {
        scoped_lock l(global_lock);
        enabled_extension = g_device_config[device].dxgi_swapchain_extension_enabled;
    }
    if (enabled_extension) {
        DOOB_GETPROCADDR(GetDeviceGroupPresentCapabilitiesKHR);
        DOOB_GETPROCADDR(GetPhysicalDevicePresentRectanglesKHR);
        DOOB_GETPROCADDR(QueuePresentKHR);
        DOOB_GETPROCADDR(CreateSwapchainKHR);
        DOOB_GETPROCADDR(CreateSharedSwapchainsKHR);
        DOOB_GETPROCADDR(DestroySwapchainKHR);
        DOOB_GETPROCADDR(AcquireNextImageKHR);
        DOOB_GETPROCADDR(AcquireNextImage2KHR);
        DOOB_GETPROCADDR(GetSwapchainImagesKHR);
        DOOB_GETPROCADDR(GetSwapchainStatusKHR);
        DOOB_GETPROCADDR(AcquireFullScreenExclusiveModeEXT);
        DOOB_GETPROCADDR(GetPastPresentationTimingGOOGLE);
        DOOB_GETPROCADDR(GetRefreshCycleDurationGOOGLE);
        DOOB_GETPROCADDR(GetSwapchainCounterEXT);
        DOOB_GETPROCADDR(GetSwapchainTimeDomainPropertiesEXT);
        DOOB_GETPROCADDR(GetSwapchainTimingPropertiesEXT);
        DOOB_GETPROCADDR(LatencySleepNV);
        DOOB_GETPROCADDR(ReleaseFullScreenExclusiveModeEXT);
        DOOB_GETPROCADDR(SetHdrMetadataEXT);
        DOOB_GETPROCADDR(SetLatencyMarkerNV);
        DOOB_GETPROCADDR(SetLatencySleepModeNV);
        DOOB_GETPROCADDR(SetLocalDimmingAMD);
        DOOB_GETPROCADDR(SetSwapchainPresentTimingQueueSizeEXT);
        DOOB_GETPROCADDR(WaitForPresentKHR);
        DOOB_GETPROCADDR(WaitForPresent2KHR);
    }
    PFN_vkVoidFunction result;
    DOOB_CALL_DISPATCH_TABLE(g_device_dispatch, device, result, GetDeviceProcAddr, (device, pName));
    return result;
}

