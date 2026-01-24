#pragma once

#include "Core.h"
#include "DxgiSwapchain.h"
#include "D3D11FactoryInfo.h"
#include "generated/include/vk_doob_dxgi.h"

#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

std::mutex global_lock;
typedef std::lock_guard<std::mutex> scoped_lock;


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
std::unordered_map<VkInstance, VkLayerInstanceDispatchTable> g_instance_dispatch;
std::unordered_map<VkInstance, DoobInstanceConfig> g_instance_config;
std::unordered_map<VkDevice, VkLayerDispatchTable> g_device_dispatch;
std::unordered_map<VkDevice, DoobDeviceConfig> g_device_config;
std::unordered_map<VkQueue, VkDevice> g_queue_ownership;

#define DOOB_CALL_DISPATCH_TABLE(table, object, retval, fn, params) do { PFN_vk##fn icd_function_call = NULL; \
{ scoped_lock l(global_lock); icd_function_call = table[object].fn; } retval = icd_function_call params; } while (false) 

#define DOOB_CALL_VOID_DISPATCH_TABLE(table, object, fn, params) do { PFN_vk##fn icd_function_call = NULL; \
{ scoped_lock l(global_lock); icd_function_call = table[object].fn; } (void)icd_function_call params; } while (false) 

//  ==== Handle table ====

std::vector<DOOB_DxgiSwapchain*> g_dxgi_swapchains = {};

#define DOOB_INVALID_COUNTER_HANDLE (~0U)
#define DOOB_SWAPCHAIN_HANDLE_ID (0x1020'0000)

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

    const char* env_enable = std::getenv("VK_DOOB_FORCE_ENABLE_DXGI_INTEROP");
    if (env_enable && strcmp(env_enable, "true") == 0) {
        s.force_enable_dxgi = true;
    }
    else {
        s.force_enable_dxgi = false; // Default
    }

    const char* env_ver = std::getenv("VK_DOOB_FORCE_DXGI_VERSION");
    if (env_ver && strcmp(env_ver, "d3d11") == 0) {
        s.force_dxgi_version = true;
        s.force_dxgi_version_value = VK_DXGI_DEVICE_VERSION_D3D11_DOOB;
    }
    else if (env_ver && strcmp(env_ver, "d3d12") == 0) {
        s.force_dxgi_version = true;
        s.force_dxgi_version_value = VK_DXGI_DEVICE_VERSION_D3D12_DOOB;
    }
    else {
        s.force_dxgi_version = false;
    }

    const char* env_log = std::getenv("VK_DOOB_LOG_FILE");
    s.log_file = env_log ? env_log : "";

    return s;
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
                DOOB_print("[DOOB] WARNING: Implicitly enabling extension %s\n", ext.name);
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
    ASSIGN_DISPATCH(CreateInstance);
    ASSIGN_DISPATCH(CreateDevice);
    ASSIGN_DISPATCH(DestroyInstance);
    ASSIGN_DISPATCH(GetPhysicalDevicePresentRectanglesKHR);

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
    if (global_dxgi_layer_settings.force_enable_dxgi && !dxgi_device_features) {
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
                DOOB_print("[DOOB] WARNING: Implicitly enabling extension %s\n", ext.name);
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
        if (global_dxgi_layer_settings.force_enable_dxgi) {
            if (dxgi_device_features) {
                local_dxgi_features = *dxgi_device_features;
            }
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

#define ASSIGN_DISPATCH(fn) dispatchTable.fn = (PFN_vk##fn)gdpa(*pDevice, "vk"#fn) 

    // fetch our own dispatch table for the functions we need, into the next layer
    VkLayerDispatchTable dispatchTable;
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

#undef ASSIGN_DISPATCH
    // store the table by key
    {
        scoped_lock l(global_lock);
        g_device_dispatch[*pDevice] = dispatchTable;
    }

    if (dxgi_swapchain_enabled) {
        VkPhysicalDeviceIDProperties id_properties = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES
        };

        VkPhysicalDeviceProperties2 properties2{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            .pNext = &id_properties,
        };
        DOOB_GetPhysicalDeviceProperties2(physicalDevice, &properties2);
        // If this is a CPU device, use the WARP driver!!
        // Otherwise, the LUID is valid, otherwise the dxgiSwapchain property is set to VK_FALSE!
        // 
        // TODO: Create DXGI factory and device
        // Make sure to return the appropriate errors if error occur!

    }
    {
        scoped_lock l(global_lock);
        g_device_config[*pDevice].dxgi_swapchain_feature_enabled = dxgi_swapchain_enabled;
        g_device_config[*pDevice].dxgi_swapchain_extension_enabled = is_dxgi_enabled;
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

VkResult VKAPI_CALL DOOB_GetPhysicalDevicePresentRectanglesKHR(
    VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    uint32_t* pRectCount,
    VkRect2D* pRects) {

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

    VkDevice device;
    {
        scoped_lock l(global_lock);
        device = g_queue_ownership[queue];
    }
    assert(device != VK_NULL_HANDLE);

    for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++) {
        VkSwapchainKHR sc = pPresentInfo->pSwapchains[i];
        DOOB_DxgiSwapchain* doob_sc = DOOB_GetObjectIfExists(g_dxgi_swapchains, DOOB_SWAPCHAIN_HANDLE_ID, sc);

        if (doob_sc) {
            // DXGI PRESENT HERE
            // NOTE: how to deal with the pWaitSemaphores?
            // pPresentInfo->pResults[i] = ...
        }
    }
    return VK_SUCCESS;
}

VkResult VKAPI_CALL DOOB_CreateSwapchainKHR(
    VkDevice                                    device,
    const VkSwapchainCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSwapchainKHR* pSwapchain) {

    printf("[DOOB] Your swapchain is out of bounds\n");

    VkIcdSurfaceWin32* win32_surface = (VkIcdSurfaceWin32*)pCreateInfo->surface;
    if (win32_surface->base.platform != VK_ICD_WSI_PLATFORM_WIN32) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

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

    // create swapchain here 
    //  
    DOOB_print("HINSTANCE handle = %p\n", win32_surface->hinstance);
    DOOB_print("HWND handle = %p\n", win32_surface->hwnd);

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

    // EXAMPLE HOW TO USE:

    DOOB_DxgiSwapchain* swapchain_obj = DOOB_GetObjectIfExists(g_dxgi_swapchains, DOOB_SWAPCHAIN_HANDLE_ID, swapchain);
    if (!swapchain_obj) {
        // not found in our database! probably normal swapchain, call normal function

        VkResult vkresult;
        DOOB_CALL_DISPATCH_TABLE(g_device_dispatch, device, vkresult, GetSwapchainImagesKHR, (device, swapchain, pSwapchainImageCount, pSwapchainImages));
        return vkresult;
    }


    // swapchain_obj->...

    return VK_ERROR_UNKNOWN;
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

    // TODO

    return VK_ERROR_UNKNOWN;
}
VkResult VKAPI_CALL DOOB_AcquireNextImage2KHR(
    VkDevice                                    device,
    const VkAcquireNextImageInfoKHR* pAcquireInfo,
    uint32_t* pImageIndex) {

    // TODO

    return VK_ERROR_UNKNOWN;
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

    // TODO

    return VK_ERROR_UNKNOWN;
}

VKAPI_ATTR VkResult VKAPI_CALL DOOB_GetSwapchainCounterEXT(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    VkSurfaceCounterFlagBitsEXT                 counter,
    uint64_t* pCounterValue) {

    // TODO

    return VK_ERROR_UNKNOWN;
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
    DOOB_GETPROCADDR(CreateInstance);
    DOOB_GETPROCADDR(CreateDevice);
    DOOB_GETPROCADDR(DestroyInstance);
    DOOB_GETPROCADDR(GetPhysicalDevicePresentRectanglesKHR);

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

