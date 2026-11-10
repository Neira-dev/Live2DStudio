#include "DX12SwapChain.h"
#include "DX12Device.h"
#include "DX12CommandQueue.h"
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

HRESULT DX12SwapChain::Initialize(SwapChainDesc desc)
{
	HRESULT hr = S_OK;

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.Width = desc.width;
	swapChainDesc.Height = desc.height;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = desc.BufferCount;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;

	if(CheckDescPtr(&desc) == false)
	{
		return E_INVALIDARG;
	}

	hr = desc.device->GetFactory()->CreateSwapChainForHwnd(
		desc.commandQueue->GetCommandQueue(),
		desc.hwnd,
		&swapChainDesc,
		nullptr,
		nullptr,
		&swapChain1
	);

	if(FAILED(hr))
	{
		return hr;
	}

	hr = swapChain1.As(&_swapChain);
	if(FAILED(hr))
	{
		return hr;
	}

	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = desc.BufferCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	hr = desc.device->GetDevice()->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&_rtvHeap));
	if(FAILED(hr))return hr;
	_renderTargetViewDescriptorSize = desc.device->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = _rtvHeap->GetCPUDescriptorHandleForHeapStart();

	_renderTargets.resize(desc.BufferCount);

	for (int i = 0; i < desc.BufferCount; ++i)
	{
		_swapChain->GetBuffer(i, IID_PPV_ARGS(&_renderTargets[i]));

		desc.device->GetDevice()->CreateRenderTargetView(_renderTargets[i].Get(), nullptr, rtvHandle);

		rtvHandle.ptr += _renderTargetViewDescriptorSize;
	}


	_desc = desc;
	return S_OK;
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12SwapChain::GetRTV(UINT index) const
{
	D3D12_CPU_DESCRIPTOR_HANDLE h = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
	h.ptr += SIZE_T(index) * _renderTargetViewDescriptorSize;
	return h;
}

void DX12SwapChain::Present()
{
	if (_vSyncEnabled)
	{
		_swapChain->Present(1, 0);
	}
	else
	{
		_swapChain->Present(0, 0);
	}
}

bool DX12SwapChain::CheckDescPtr(const SwapChainDesc* desc)
{
	if (desc == nullptr)return false;
	if (desc->device == nullptr)return false;
	if (desc->commandQueue == nullptr)return false;
	if (desc->hwnd == nullptr)return false;
	return true;
}

void DX12SwapChain::Resize(UINT width, UINT height)
{
	if (width == 0 || height == 0)return;

	_desc.width = width;
	_desc.height = height;

	for(int i = 0; i < _renderTargets.size(); ++i)
	{
		_renderTargets[i].Reset();
	}

	_swapChain->ResizeBuffers(
		_desc.BufferCount,
		width,
		height,
		DXGI_FORMAT_R8G8B8A8_UNORM,
		0
	);

	_renderTargets.resize(_desc.BufferCount);
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
	for (int i = 0; i < _desc.BufferCount; ++i)
	{
		_swapChain->GetBuffer(i, IID_PPV_ARGS(&_renderTargets[i]));

		_desc.device->GetDevice()->CreateRenderTargetView(_renderTargets[i].Get(), nullptr, rtvHandle);
		rtvHandle.ptr += _renderTargetViewDescriptorSize;
	}
}
