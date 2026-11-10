#pragma once
/*
* DX12Device
* DirectX12 のデバイスを管理するクラス
*/

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

class DX12Device
{
public:
	struct DeviceDesc
	{
		/*
		* Factoryのフラグを指定する
		*/
		UINT factoryFlags = 0;
		/*
		* GPUの優先度を指定する
		* DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE: xGPU→dGPU→iGPUの順で優先度が高い
		* DXGI_GPU_PREFERENCE_MINIMUM_POWER  : iGPU→dGPU→xGPUの順で優先度が高い
		* DXGI_GPU_PREFERENCE_UNSPECIFIED    : デフォルトの優先度
		*/
		DXGI_GPU_PREFERENCE gpuPreference = DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;
		/*
		* Dx12の推奨レベル別の機能
		* 11_0 : 幅広いGPUでサポートされている基本的な機能
		* 12_0 : 現代のGPUを前提にした機能
		* 12_2 : レイトレーシング・Mesh Shaderなどの最新機能をサポートするGPU向け
		*/
		D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_12_0;

		bool enableDebugLayer = false;

	};
public:
	DX12Device() = default;
	~DX12Device() = default;
	DX12Device(const DX12Device&) = delete;
	DX12Device& operator=(const DX12Device&) = delete;
public:
	HRESULT Initialize(DeviceDesc Desc);
public:
	// デバイスの取得
	ID3D12Device* GetDevice() const { return _device.Get(); }
	// アダプターの取得
	IDXGIAdapter4* GetAdapter() const { return _adapter.Get(); }
	// ファクトリーの取得
	IDXGIFactory6* GetFactory() const { return _factory.Get(); }
private:
	Microsoft::WRL::ComPtr<IDXGIFactory6> _factory;
	Microsoft::WRL::ComPtr<IDXGIAdapter4> _adapter;
	Microsoft::WRL::ComPtr<ID3D12Device> _device;
};

