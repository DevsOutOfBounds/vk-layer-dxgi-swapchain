#pragma once

#include <d3d11.h>
#include <dxgi1_6.h>
#include "D3D11FactoryInfo.h"

struct DOOB_DxgiSwapchain {
	DOOB_D3D11FactoryInfo* dxgi_factory_info;

	IDXGISwapChain1* swapchain;
	ID3D11Texture2D* swapchain_backbuffer;
	ID3D11RenderTargetView* swapchain_rtv;
	size_t swapchain_image_count;
};