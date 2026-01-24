#pragma once

#include "Core.h"
#include "DxgiSwapchain.h"
#include "D3D11FactoryInfo.h"
#include "generated/include/vk_doob_dxgi.h"

#include <mutex>
#include <unordered_map>
#include <vector>

std::mutex global_lock;
typedef std::lock_guard<std::mutex> scoped_lock;

// ==== Dispatch table ====

struct DoobSettings {
    bool force_enable_dxgi;
    bool force_dxgi_version;
    VkDxgiDeviceVersionDOOB force_dxgi_version_value;
    std::string log_file;
};
struct DoobDeviceConfig {
    bool enabled_dxgi_swapchain;
    DOOB_D3D11FactoryInfo factory_info;
};
struct DoobInstanceConfig {
    std::vector<VkPhysicalDevice> physical_devices;
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


template <typename TNonDispatchableHandle>
static TNonDispatchableHandle DOOB_MakeHandle(uint32_t id, uint32_t counter) {
    TNonDispatchableHandle handle;
    *(uint64_t*)(&handle) = ((uint64_t)(id) << 32) | (counter);
    return handle;
}
template <typename TNonDispatchableHandle>
static uint32_t DOOB_GetCounterAndVerify(uint32_t target_id, TNonDispatchableHandle handle) {
    uint64_t p = *(uint64_t*)(handle);
    uint32_t id = (uint32_t)(p >> 32);
    uint32_t counter = (uint32_t)(p & 0xFFFFFFFF);
    if (id != target_id) {
        return DOOB_INVALID_COUNTER_HANDLE;
    }
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
    if (*out_counter > data_array.size()) {
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
        const auto& physical_device_list = kv.second.physical_devices;
        if (std::find(physical_device_list.begin(), physical_device_list.end(), physical_device) != physical_device_list.end()) {
            return kv.first;
        }
    }
    return VK_NULL_HANDLE;
}

static VkAllocationCallbacks DOOB_GetFilledAllocationCallbacks(VkAllocationCallbacks callbacks) {
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
    if (env_ver && strcmp(env_ver, "d3d11")) {
        s.force_dxgi_version = true;
        s.force_dxgi_version_value = VK_DXGI_DEVICE_VERSION_D3D11_DOOB;
    }
    else if (env_ver && strcmp(env_ver, "d3d12")) {
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


VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_EnumerateInstanceLayerProperties(
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

VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_EnumerateDeviceLayerProperties(
    VkPhysicalDevice                            physicalDevice,
    uint32_t* pPropertyCount,
    VkLayerProperties* pProperties) {
    return DOOB_EnumerateInstanceLayerProperties(pPropertyCount, pProperties);
}

VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_CreateInstance(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance)
{
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

    VkResult ret = createFunc(pCreateInfo, pAllocator, pInstance);
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
    ASSIGN_DISPATCH(CreateInstance);
    ASSIGN_DISPATCH(CreateDevice);
    ASSIGN_DISPATCH(DestroyInstance);
    ASSIGN_DISPATCH(GetPhysicalDevicePresentRectanglesKHR);

#undef ASSIGN_DISPATCH

    // fetch our own dispatch table for the functions we need, into the next layer
    {
        scoped_lock l(global_lock);
        g_instance_dispatch[*pInstance] = dispatchTable;
    }
    return VK_SUCCESS;
}

VK_LAYER_EXPORT void VKAPI_CALL DOOB_DestroyInstance(
    VkInstance                                  instance,
    const VkAllocationCallbacks* pAllocator) {
    PFN_vkDestroyInstance call;
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
    call(instance, pAllocator);

}

VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_EnumeratePhysicalDevices(
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
        std::vector<VkPhysicalDevice>& physical_device_array = g_instance_config[instance].physical_devices;
        physical_device_array.resize(*pPhysicalDeviceCount);
        for (uint32_t i = 0; i < *pPhysicalDeviceCount; ++i) {
            physical_device_array[i] = pPhysicalDevices[i];
        }
    }
}

static const VkExtensionProperties g_doob_extension_info = {
    VK_DOOB_DXGI_SWAPCHAIN_EXTENSION_NAME,
    VK_DOOB_DXGI_SWAPCHAIN_SPEC_VERSION
};

VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_EnumerateDeviceExtensionProperties(
    VkPhysicalDevice                            physicalDevice,
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties)
{
    if (pLayerName && strcmp(pLayerName, VK_LAYER_DOOB_DXGI_SWAPCHAIN_NAME) == 0) {
        if (pPropertyCount && !pProperties) {
            *pPropertyCount = 1;
            return VK_SUCCESS;
        }
        if (pProperties) {
            if (*pPropertyCount < 1) return VK_INCOMPLETE;
            pProperties[0] = g_doob_extension_info;
            *pPropertyCount = 1;
        }
        return VK_SUCCESS;
    }

    VkInstance instance = DOOB_GetInstanceFromPhysicalDevice(physicalDevice);
    if (instance == VK_NULL_HANDLE) {
        return VK_ERROR_UNKNOWN;
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
    uint32_t count = 0;
    VkResult res;
    DOOB_CALL_DISPATCH_TABLE(g_instance_dispatch, instance, res, EnumerateDeviceExtensionProperties, (physicalDevice, NULL, &count, NULL));
    if (res != VK_SUCCESS) return res;

    // Calculate Total (Driver + Us)
    uint32_t total_count = count + 1;

    // If App is just querying count
    if (!pProperties) {
        *pPropertyCount = total_count;
        return VK_SUCCESS;
    }

    // If App provided buffer, calculate how many to write
    uint32_t copy_count = *pPropertyCount;
    if (copy_count > count) copy_count = count; // Copy max what the driver has

    // Fetch Driver Extensions
    DOOB_CALL_DISPATCH_TABLE(g_instance_dispatch, instance, res, EnumerateDeviceExtensionProperties, (physicalDevice, NULL, &copy_count, pProperties));
    if (res != VK_SUCCESS && res != VK_INCOMPLETE) return res;

    // Append Our Extension (if there is space)
    if (*pPropertyCount >= total_count) {
        pProperties[count] = g_doob_extension_info;
        *pPropertyCount = total_count;
        return VK_SUCCESS;
    }
    else {
        // The buffer was too small to hold ours + driver's
        return VK_INCOMPLETE;
    }
}

VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_CreateDevice(
    VkPhysicalDevice                            physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice)
{
    VkLayerDeviceCreateInfo* layerCreateInfo = (VkLayerDeviceCreateInfo*)pCreateInfo->pNext;

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

    bool is_doob_enabled = false;
    if (pCreateInfo->ppEnabledExtensionNames) {
        for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
            if (strcmp(pCreateInfo->ppEnabledExtensionNames[i], VK_DOOB_DXGI_SWAPCHAIN_EXTENSION_NAME) == 0) {
                is_doob_enabled = true;
                break;
            }
        }
    }
    {
        scoped_lock l(global_lock);
        g_device_config[*pDevice].enabled_dxgi_swapchain = is_doob_enabled;
    }


    PFN_vkGetInstanceProcAddr gipa = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr gdpa = layerCreateInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    // move chain on for next layer
    layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;

    PFN_vkCreateDevice createFunc = (PFN_vkCreateDevice)gipa(VK_NULL_HANDLE, "vkCreateDevice");

    VkResult ret = createFunc(physicalDevice, pCreateInfo, pAllocator, pDevice);
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

    if (!is_doob_enabled) {
        return VK_SUCCESS; // Not enabled!
    }
    DoobSettings global_dxgi_layer_settings = DOOB_LoadSettings();

    VkDxgiDeviceFeaturesDOOB* dxgi_device_features = (VkDxgiDeviceFeaturesDOOB*)pCreateInfo->pNext;
    while (dxgi_device_features && (dxgi_device_features->sType != VK_STRUCTURE_TYPE_DXGI_DEVICE_FEATURES_DOOB))
    {
        dxgi_device_features = (VkDxgiDeviceFeaturesDOOB*)dxgi_device_features->pNext;
    }

    if (dxgi_device_features || global_dxgi_layer_settings.force_enable_dxgi) {
        VkDxgiDeviceFeaturesDOOB local_dxgi_features = {
            .sType = VK_STRUCTURE_TYPE_DXGI_DEVICE_FEATURES_DOOB,
            .dxgiVersion = VK_DXGI_DEVICE_VERSION_AUTO_DOOB,
        };
        if (dxgi_device_features) {
            local_dxgi_features = *dxgi_device_features;
        }
        if (global_dxgi_layer_settings.force_dxgi_version) {
            local_dxgi_features.dxgiVersion = global_dxgi_layer_settings.force_dxgi_version_value;
        }

        // use local_dxgi_features to create the dxgi context
    }


    return VK_SUCCESS;
}

VK_LAYER_EXPORT void VKAPI_CALL DOOB_DestroyDevice(
    VkDevice                                    device,
    const VkAllocationCallbacks* pAllocator) {
    PFN_vkDestroyDevice call;
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
    call(device, pAllocator);
}


VK_LAYER_EXPORT void VKAPI_CALL DOOB_GetDeviceQueue(
    VkDevice                                    device,
    uint32_t                                    queueFamilyIndex,
    uint32_t                                    queueIndex,
    VkQueue* pQueue) {

    printf("TEST REMOVE ME Intercept GetDeviceQueue\n");

    g_device_dispatch[device].GetDeviceQueue(device, queueFamilyIndex, queueIndex, pQueue);
    if (pQueue) {
        scoped_lock l(global_lock);
        g_queue_ownership[*pQueue] = device;
    }
}

VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_GetDeviceGroupPresentCapabilitiesKHR(
    VkDevice                                    device,
    VkDeviceGroupPresentCapabilitiesKHR* pDeviceGroupPresentCapabilities) {

    // TODO

    return VK_INCOMPLETE;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_GetPhysicalDevicePresentRectanglesKHR(
    VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    uint32_t* pRectCount,
    VkRect2D* pRects) {

    // TODO

    return VK_INCOMPLETE;
}
VK_LAYER_EXPORT void VKAPI_CALL DOOB_GetDeviceQueue2(
    VkDevice                                    device,
    const VkDeviceQueueInfo2* pQueueInfo,
    VkQueue* pQueue) {

    g_device_dispatch[device].GetDeviceQueue2(device, pQueueInfo, pQueue);
    {
        scoped_lock l(global_lock);
        g_queue_ownership[*pQueue] = device;
    }
}

VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_QueuePresentKHR(
    VkQueue                                     queue,
    const VkPresentInfoKHR* pPresentInfo) {

    VkDevice device;
    {
        scoped_lock l(global_lock);
        device = g_queue_ownership[queue];
    }
    assert(device != VK_NULL_HANDLE);

    bool has_doob_swapchain = false;
    for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++) {
        if (DOOB_GetCounterAndVerify(DOOB_SWAPCHAIN_HANDLE_ID, pPresentInfo->pSwapchains[i]) != DOOB_INVALID_COUNTER_HANDLE) {
            has_doob_swapchain = true;
            break;
        }
    }

    if (!has_doob_swapchain) {
        VkResult ret;
        DOOB_CALL_DISPATCH_TABLE(g_device_dispatch, device, ret, QueuePresentKHR, (queue, pPresentInfo));
        return ret;
    }


    std::vector<VkSwapchainKHR> non_doob_swapchains = {};
    std::vector<uint32_t> non_doob_swapchain_img_indices = {};
    std::vector<uint32_t> non_doob_swapchain_reverse_indices = {}; // maps array index back to input index (important for writing VK_RESULT)
    for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++) {
        VkSwapchainKHR sc = pPresentInfo->pSwapchains[i];
        DOOB_DxgiSwapchain* doob_sc = DOOB_GetObjectIfExists(g_dxgi_swapchains, DOOB_SWAPCHAIN_HANDLE_ID, sc);

        if (doob_sc) {
            // DXGI PRESENT HERE
            // NOTE: how to deal with the pWaitSemaphores?
            // pPresentInfo->pResults[i] = ...
        }
        else {
            non_doob_swapchains.push_back(sc);
            non_doob_swapchain_img_indices.push_back(pPresentInfo->pImageIndices[i]);
            non_doob_swapchain_reverse_indices.push_back(i);
        }
    }

    if (non_doob_swapchains.size() > 0) {
        std::vector<VkResult> results{};

        VkPresentInfoKHR temp_present_info{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        // FIXME: for certain pNext chains the indices might not match up anymore!!!
        temp_present_info.pNext = pPresentInfo->pNext;
        temp_present_info.waitSemaphoreCount = pPresentInfo->waitSemaphoreCount;
        temp_present_info.pWaitSemaphores = pPresentInfo->pWaitSemaphores;
        temp_present_info.swapchainCount = (uint32_t)non_doob_swapchains.size();
        temp_present_info.pSwapchains = non_doob_swapchains.data();
        temp_present_info.pImageIndices = non_doob_swapchain_img_indices.data();
        if (pPresentInfo->pResults) {
            results.resize(non_doob_swapchains.size());
            temp_present_info.pResults = results.data();
        }
        VkResult ret;
        DOOB_CALL_DISPATCH_TABLE(g_device_dispatch, device, ret, QueuePresentKHR, (queue, &temp_present_info));
        if (pPresentInfo->pResults) {
            // remap results
            for (uint32_t i = 0; i < temp_present_info.swapchainCount; ++i) {
                pPresentInfo->pResults[non_doob_swapchain_reverse_indices[i]] = results[i];
            }
        }

        return ret;
    }

    return VK_SUCCESS;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_CreateSwapchainKHR(
    VkDevice                                    device,
    const VkSwapchainCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSwapchainKHR* pSwapchain) {

    printf("[DOOB] Your swapchain is out of bounds\n");

    VkDxgiSwapchainCreateInfoDOOB* dxgi_info = (VkDxgiSwapchainCreateInfoDOOB*)pCreateInfo->pNext;
    while (dxgi_info && (dxgi_info->sType != VK_STRUCTURE_TYPE_DXGI_SWAPCHAIN_CREATE_INFO_DOOB))
    {
        dxgi_info = (VkDxgiSwapchainCreateInfoDOOB*)dxgi_info->pNext;
    }
    if (!dxgi_info) {
        VkResult ret;
        DOOB_CALL_DISPATCH_TABLE(g_device_dispatch, device, ret, CreateSwapchainKHR, (device, pCreateInfo, pAllocator, pSwapchain));
        return ret;
    }

    VkAllocationCallbacks alloc = DOOB_GetFilledAllocationCallbacks(*pAllocator);

    uint32_t swapchain_handle;
    DOOB_DxgiSwapchain* swapchain_obj = DOOB_AllocCountedHandle(g_dxgi_swapchains, &swapchain_handle, &alloc);
    if (!swapchain_obj) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    // create swapchain here

    *pSwapchain = DOOB_MakeHandle<VkSwapchainKHR>(DOOB_SWAPCHAIN_HANDLE_ID, swapchain_handle);

    return VK_SUCCESS;
}
VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_CreateSharedSwapchainsKHR(
    VkDevice                                    device,
    uint32_t                                    swapchainCount,
    const VkSwapchainCreateInfoKHR* pCreateInfos,
    const VkAllocationCallbacks* pAllocator,
    VkSwapchainKHR* pSwapchains) {

    // TODO

    return VK_INCOMPLETE;
}

VK_LAYER_EXPORT void VKAPI_CALL DOOB_DestroySwapchainKHR(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    const VkAllocationCallbacks* pAllocator) {

    VkAllocationCallbacks alloc = DOOB_GetFilledAllocationCallbacks(*pAllocator);

    uint32_t swapchain_handle = DOOB_GetCounterAndVerify(DOOB_SWAPCHAIN_HANDLE_ID, swapchain);
    if (swapchain_handle == DOOB_INVALID_COUNTER_HANDLE) {
        return;
    }
    DOOB_ReleaseCountedHandle(g_dxgi_swapchains, swapchain_handle, &alloc);
}
VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_GetSwapchainImagesKHR(
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

    return VK_INCOMPLETE;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_GetSwapchainStatusKHR(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain) {

    // TODO

    return VK_INCOMPLETE;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_AcquireFullScreenExclusiveModeEXT(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain) {

    // TODO

    return VK_INCOMPLETE;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_AcquireNextImageKHR(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint64_t                                    timeout,
    VkSemaphore                                 semaphore,
    VkFence                                     fence,
    uint32_t* pImageIndex) {

    // TODO

    return VK_INCOMPLETE;
}
VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_AcquireNextImage2KHR(
    VkDevice                                    device,
    const VkAcquireNextImageInfoKHR* pAcquireInfo,
    uint32_t* pImageIndex) {

    // TODO

    return VK_INCOMPLETE;
}
VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_GetPastPresentationTimingGOOGLE(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint32_t* pPresentationTimingCount,
    VkPastPresentationTimingGOOGLE* pPresentationTimings) {

    // TODO

    return VK_INCOMPLETE;
}
VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_GetRefreshCycleDurationGOOGLE(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    VkRefreshCycleDurationGOOGLE* pDisplayTimingProperties) {

    // TODO

    return VK_INCOMPLETE;
}

VKAPI_ATTR VkResult VKAPI_CALL DOOB_GetSwapchainCounterEXT(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    VkSurfaceCounterFlagBitsEXT                 counter,
    uint64_t* pCounterValue) {

    // TODO

    return VK_INCOMPLETE;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_GetSwapchainTimeDomainPropertiesEXT(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    VkSwapchainTimeDomainPropertiesEXT* pSwapchainTimeDomainProperties,
    uint64_t* pTimeDomainsCounter) {

    // TODO

    return VK_INCOMPLETE;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_GetSwapchainTimingPropertiesEXT(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    VkSwapchainTimingPropertiesEXT* pSwapchainTimingProperties,
    uint64_t* pSwapchainTimingPropertiesCounter) {

    // TODO

    return VK_INCOMPLETE;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_LatencySleepNV(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    const VkLatencySleepInfoNV* pSleepInfo) {

    // TODO

    return VK_INCOMPLETE;
}
VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_ReleaseFullScreenExclusiveModeEXT(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain) {

    // TODO

    return VK_INCOMPLETE;
}
VK_LAYER_EXPORT void VKAPI_CALL DOOB_SetHdrMetadataEXT(
    VkDevice                                    device,
    uint32_t                                    swapchainCount,
    const VkSwapchainKHR* pSwapchains,
    const VkHdrMetadataEXT* pMetadata) {

    // TODO

}
VK_LAYER_EXPORT void VKAPI_CALL DOOB_SetLatencyMarkerNV(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    const VkSetLatencyMarkerInfoNV* pLatencyMarkerInfo) {

    // TODO

}
VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_SetLatencySleepModeNV(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    const VkLatencySleepModeInfoNV* pSleepModeInfo) {

    // TODO

    return VK_INCOMPLETE;
}

VK_LAYER_EXPORT void VKAPI_CALL DOOB_SetLocalDimmingAMD(
    VkDevice                                    device,
    VkSwapchainKHR                              swapChain,
    VkBool32                                    localDimmingEnable) {

    // TODO

}
VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_SetSwapchainPresentTimingQueueSizeEXT(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint32_t                                    size) {

    // TODO

    return VK_INCOMPLETE;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_WaitForPresentKHR(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint64_t                                    presentId,
    uint64_t                                    timeout) {

    // TODO

    return VK_INCOMPLETE;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_WaitForPresent2KHR(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    const VkPresentWait2InfoKHR* pPresentWait2Info) {

    // TODO

    return VK_INCOMPLETE;
}

// ==== OUR CUSTOM FUNCTIONS ====

VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_GetDxgiSwapchainHandleDOOB(
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

    bool enabled_extension = false;
    {
        scoped_lock l(global_lock);
        enabled_extension = g_device_config[device].enabled_dxgi_swapchain;
    }
    if (enabled_extension) {
        if (strcmp(pName, "vkGetDxgiSwapchainHandleDOOB") == 0) {
            return (PFN_vkVoidFunction)DOOB_GetDxgiSwapchainHandleDOOB;
        }
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

