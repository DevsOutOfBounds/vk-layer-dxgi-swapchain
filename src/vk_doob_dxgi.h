#ifndef VK_DOOB_DXGI_H
#define VK_DOOB_DXGI_H

#include <vulkan/vulkan_core.h>
#include <dxgi.h>

#define VK_STRUCTURE_TYPE_DXGI_DEVICE_FEATURES_DOOB		   ((VkStructureType)(2123157880))
#define VK_STRUCTURE_TYPE_DXGI_SWAPCHAIN_CREATE_INFO_DOOB  ((VkStructureType)(2123157881))
enum VkDxgiDeviceVersionDOOB {
	VK_DXGI_DEVICE_VERSION_D3D11 = 0,
	VK_DXGI_DEVICE_VERSION_D3D12 = 1,

	VK_DXGI_DEVICE_VERSION_MAX_ENUM = 0x7FFFFFFF
};

typedef struct VkDxgiDeviceFeaturesDOOB {
	VkStructureType			sType;
	const void* pNext;
	VkDxgiDeviceVersionDOOB dxgiVersion;
};

typedef struct VkDxgiSwapchainCreateInfoDOOB {
	VkStructureType  sType;
	const void* pNext;
	DXGI_MODE_DESC   bufferDesc;
	DXGI_SAMPLE_DESC sampleDesc;
	DXGI_USAGE       bufferUsage;
	UINT             bufferCount;
	HWND             outputWindow;
	BOOL             windowed;
	DXGI_SWAP_EFFECT swapEffect;
	UINT             flags;
} VkDxgiSwapchainCreateInfoDOOB;


#endif 
