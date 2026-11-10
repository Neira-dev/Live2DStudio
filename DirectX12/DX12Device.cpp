#include "DX12Device.h"
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")


HRESULT DX12Device::Initialize(DeviceDesc Desc)
{
	HRESULT hr = S_OK;

	// デバッグレイヤーの有効化
	if (Desc.enableDebugLayer)
	{
		Microsoft::WRL::ComPtr<ID3D12Debug> debug;
		if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
		{
			debug->EnableDebugLayer();
			Desc.factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}

	/*
	* Factoryの役割
	* GPUの列挙と選択を行う
	*/

	hr = CreateDXGIFactory2(Desc.factoryFlags, IID_PPV_ARGS(&_factory));
	if(FAILED(hr))return hr;

	/*
	* アダプターの列挙と選択
	*/
	Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter;
	for(UINT i = 0 ; SUCCEEDED(_factory->EnumAdapterByGpuPreference(i, Desc.gpuPreference, IID_PPV_ARGS(&adapter))); ++i)
	{
		DXGI_ADAPTER_DESC3 ad{};
		adapter->GetDesc3(&ad);
		if(ad.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)continue;

		HRESULT check;
		check = D3D12CreateDevice(
			adapter.Get(),
			Desc.featureLevel,
			__uuidof(ID3D12Device),
			nullptr
		);
		if(SUCCEEDED(check))
		{	
			_adapter = adapter;
			break;
		}
	}

	if(!_adapter)return DXGI_ERROR_NOT_FOUND;

	/*
	* デバイスの作成
	*/
	hr = D3D12CreateDevice(
		_adapter.Get(),
		Desc.featureLevel,
		IID_PPV_ARGS(&_device)
	);

	if (FAILED(hr))return hr;

	// デバッグレイヤーの設定
	if (Desc.enableDebugLayer)
	{
		Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
		if(SUCCEEDED(_device.As(&infoQueue)))
		{
			infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
			infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
		}
	}

	return S_OK;
}
