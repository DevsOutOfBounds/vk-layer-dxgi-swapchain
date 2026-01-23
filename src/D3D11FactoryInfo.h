#pragma once

#include <d3d11.h>
#include <dxgi1_6.h>

struct DOOB_D3D11FactoryInfo {
	IDXGIFactory2* dxgi_factory;
	ID3D11Device* device;
	ID3D11DeviceContext* device_context;
};