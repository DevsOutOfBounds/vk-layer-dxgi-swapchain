#pragma once

#include "Core.h"

#include <mutex>
#include <unordered_map>

std::mutex global_lock;
typedef std::lock_guard<std::mutex> scoped_lock;

std::unordered_map<VkInstance, VkLayerInstanceDispatchTable> g_instance_dispatch;
std::unordered_map<VkDevice, VkLayerDispatchTable> g_device_dispatch;
std::unordered_map<VkQueue, VkDevice> g_queue_ownership;

struct DoobSwapchainHandle {
	int unused_todo = 0;
};

#define DOOB_CALL_DISPATCH_TABLE(table, object, retval, fn, params) do { PFN_vk##fn icd_function_call = nullptr; \
{ scoped_lock l(global_lock); icd_function_call = table[object].fn; } retval = icd_function_call params; } while (false) 

#define DOOB_CALL_VOID_DISPATCH_TABLE(table, object, fn, params) do { PFN_vk##fn icd_function_call = nullptr; \
{ scoped_lock l(global_lock); icd_function_call = table[object].fn; } (void)icd_function_call params; } while (false) 

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL DOOB_GetInstanceProcAddr(VkInstance instance, const char* pName)
{
	// instance chain functions we intercept
	DOOB_GETPROCADDR(GetInstanceProcAddr);
	DOOB_GETPROCADDR(EnumerateInstanceLayerProperties);
	DOOB_GETPROCADDR(CreateInstance);
	DOOB_GETPROCADDR(CreateDevice);
	DOOB_GETPROCADDR(DestroyInstance);

	PFN_vkVoidFunction result;
	DOOB_CALL_DISPATCH_TABLE(g_instance_dispatch, instance, result, GetInstanceProcAddr, (instance, pName));
	return result;
}

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL DOOB_GetDeviceProcAddr(VkDevice device, const char* pName)
{
	// device chain functions we intercept
	DOOB_GETPROCADDR(GetDeviceProcAddr);
	DOOB_GETPROCADDR(EnumerateDeviceLayerProperties);
	DOOB_GETPROCADDR(CreateDevice);
	DOOB_GETPROCADDR(DestroyDevice);
	DOOB_GETPROCADDR(GetDeviceQueue);
	DOOB_GETPROCADDR(GetDeviceQueue2);
	DOOB_GETPROCADDR(CreateSwapchainKHR);
	DOOB_GETPROCADDR(GetSwapchainImagesKHR);
	DOOB_GETPROCADDR(AcquireNextImage2KHR);
	DOOB_GETPROCADDR(AcquireNextImageKHR);
	DOOB_GETPROCADDR(DestroySwapchainKHR);
	DOOB_GETPROCADDR(GetDeviceGroupPresentCapabilitiesKHR);
	// DOOB_GETPROCADDR(GetPhysicalDevicePresentRectanglesKHR);
	DOOB_GETPROCADDR(GetDeviceGroupSurfacePresentModesKHR);

	PFN_vkVoidFunction result;
	DOOB_CALL_DISPATCH_TABLE(g_device_dispatch, device, result, GetDeviceProcAddr, (device, pName));
	return result;
}

static void GetLayerProperties(VkLayerProperties& properties) {
	memset(properties.layerName, 0, sizeof(properties.layerName));
	memset(properties.description, 0, sizeof(properties.layerName));
	strncpy(properties.layerName, DOOB_LAYER_NAME, sizeof(DOOB_LAYER_NAME));
	strncpy(properties.description, DOOB_LAYER_DESCRIPTION, sizeof(DOOB_LAYER_DESCRIPTION));
	properties.implementationVersion = 0x1;
	properties.specVersion = VK_HEADER_VERSION_COMPLETE;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_EnumerateInstanceLayerProperties(
	uint32_t* pPropertyCount,
	VkLayerProperties* pProperties) {
	if (pPropertyCount == nullptr) {
		return VK_INCOMPLETE;
	}
	if (pProperties == nullptr) {
		*pPropertyCount = 1;
		return VK_SUCCESS;
	}
	if (*pPropertyCount == 0) {
		return VK_INCOMPLETE;
	}
	if (pProperties) {
		GetLayerProperties(pProperties[0]);
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
	ASSIGN_DISPATCH(EnumerateDeviceExtensionProperties);
	ASSIGN_DISPATCH(CreateInstance);
	ASSIGN_DISPATCH(CreateDevice);
	ASSIGN_DISPATCH(DestroyInstance);

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
		if (it == g_instance_dispatch.end()) {
			return;
		}
		else {
			call = it->second.DestroyInstance;
			g_instance_dispatch.erase(it);
		}
	}
	call(instance, pAllocator);

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
	ASSIGN_DISPATCH(CreateSwapchainKHR);
	ASSIGN_DISPATCH(DestroySwapchainKHR);
	ASSIGN_DISPATCH(AcquireNextImageKHR);
	ASSIGN_DISPATCH(AcquireNextImage2KHR);
	ASSIGN_DISPATCH(GetDeviceGroupPresentCapabilitiesKHR);
	ASSIGN_DISPATCH(GetSwapchainImagesKHR);
	// ASSIGN_DISPATCH(GetPhysicalDevicePresentRectanglesKHR);
	ASSIGN_DISPATCH(QueuePresentKHR);
	ASSIGN_DISPATCH(GetSwapchainImagesKHR);

#undef ASSIGN_DISPATCH
	// store the table by key
	{
		scoped_lock l(global_lock);
		g_device_dispatch[*pDevice] = dispatchTable;
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
		if (it == g_device_dispatch.end()) {
			return;
		}
		else {
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
	}
	call(device, pAllocator);
}


VK_LAYER_EXPORT void VKAPI_CALL DOOB_GetDeviceQueue(
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


VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_CreateSwapchainKHR(
	VkDevice                                    device,
	const VkSwapchainCreateInfoKHR* pCreateInfo,
	const VkAllocationCallbacks* pAllocator,
	VkSwapchainKHR* pSwapchain) {

	printf("Intercept CreateSwapchainKHR\n");

	VkResult ret;
	DOOB_CALL_DISPATCH_TABLE(g_device_dispatch, device, ret, CreateSwapchainKHR, (device, pCreateInfo, pAllocator, pSwapchain));
	return ret;
}


VK_LAYER_EXPORT void VKAPI_CALL DOOB_DestroySwapchainKHR(
	VkDevice                                    device,
	VkSwapchainKHR                              swapchain,
	const VkAllocationCallbacks* pAllocator) {

	DOOB_CALL_VOID_DISPATCH_TABLE(g_device_dispatch, device, DestroySwapchainKHR, (device, swapchain, pAllocator));
}
VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_GetSwapchainImagesKHR(
	VkDevice                                    device,
	VkSwapchainKHR                              swapchain,
	uint32_t* pSwapchainImageCount,
	VkImage* pSwapchainImages) {

	VkResult ret;
	DOOB_CALL_DISPATCH_TABLE(g_device_dispatch, device, ret, GetSwapchainImagesKHR, (device, swapchain, pSwapchainImageCount, pSwapchainImages));
	return ret;
}


VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_AcquireNextImageKHR(
	VkDevice                                    device,
	VkSwapchainKHR                              swapchain,
	uint64_t                                    timeout,
	VkSemaphore                                 semaphore,
	VkFence                                     fence,
	uint32_t* pImageIndex) {

	VkResult ret;
	DOOB_CALL_DISPATCH_TABLE(g_device_dispatch, device, ret, AcquireNextImageKHR, (device, swapchain, timeout, semaphore, fence, pImageIndex));
	return ret;
}


VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_QueuePresentKHR(
	VkQueue                                     queue,
	const VkPresentInfoKHR* pPresentInfo) {

	VkDevice device;
	{
		scoped_lock l(global_lock);
		device = g_queue_ownership[queue];
	}
	VkResult ret;
	DOOB_CALL_DISPATCH_TABLE(g_device_dispatch, device, ret, QueuePresentKHR, (queue, pPresentInfo));
	return ret;
}



VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_GetDeviceGroupPresentCapabilitiesKHR(
	VkDevice                                    device,
	VkDeviceGroupPresentCapabilitiesKHR* pDeviceGroupPresentCapabilities) {

	VkResult ret;
	DOOB_CALL_DISPATCH_TABLE(g_device_dispatch, device, ret, GetDeviceGroupPresentCapabilitiesKHR, (device, pDeviceGroupPresentCapabilities));
	return ret;
}


VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_GetDeviceGroupSurfacePresentModesKHR(
	VkDevice                                    device,
	VkSurfaceKHR                                surface,
	VkDeviceGroupPresentModeFlagsKHR* pModes) {

	VkResult ret;
	DOOB_CALL_DISPATCH_TABLE(g_device_dispatch, device, ret, GetDeviceGroupSurfacePresentModesKHR, (device, surface, pModes));
	return ret;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_GetPhysicalDevicePresentRectanglesKHR(
	VkPhysicalDevice                            physicalDevice,
	VkSurfaceKHR                                surface,
	uint32_t* pRectCount,
	VkRect2D* pRects) {

	return VK_ERROR_INCOMPATIBLE_DRIVER;
}


VK_LAYER_EXPORT VkResult VKAPI_CALL DOOB_AcquireNextImage2KHR(
	VkDevice                                    device,
	const VkAcquireNextImageInfoKHR* pAcquireInfo,
	uint32_t* pImageIndex) {

	VkResult ret;
	DOOB_CALL_DISPATCH_TABLE(g_device_dispatch, device, ret, AcquireNextImage2KHR, (device, pAcquireInfo, pImageIndex));
	return ret;
}

