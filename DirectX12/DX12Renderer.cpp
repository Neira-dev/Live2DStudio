#include "DX12Renderer.h"
#include "DX12Device.h"
#include "DX12CommandQueue.h"
#include "DX12SwapChain.h"
#include "FrameContext.h"

HRESULT DX12Renderer::Initialize(DX12Device* device, DX12CommandQueue* commandQueue, DX12SwapChain* swapChain)
{
	if(commandQueue == nullptr || swapChain == nullptr || device == nullptr)
	{
		return E_INVALIDARG;
	}

	_device = device;
	_commandQueue = commandQueue;
	_swapChain = swapChain;

	UINT bufferCount = _swapChain->GetBufferCount();

	HRESULT hr = S_OK;
	for(int i = 0; i < bufferCount; ++i)
	{
		FrameContext* frameContext = new FrameContext();
		hr = frameContext->Initialize(device,commandQueue, D3D12_COMMAND_LIST_TYPE_DIRECT);
		if (FAILED(hr))
		{
			return hr;
		}
		_frameContexts.push_back(frameContext);
	}

	device->GetDevice()->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		_frameContexts[0]->GetCommandAllocator(),
		nullptr,
		IID_PPV_ARGS(&_commandList)
	);

	if (FAILED(hr))
	{
		return hr;
	}
	_commandList->Close();


	D3D12_DESCRIPTOR_HEAP_DESC DepthDesc = {};
	DepthDesc.NumDescriptors = 1;
	DepthDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	DepthDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	hr = device->GetDevice()->CreateDescriptorHeap(&DepthDesc, IID_PPV_ARGS(&_depthStencilHeap));
	if (FAILED(hr))
	{
		return hr;
	}

	hr = CreateDepthBuffer(_swapChain->GetDesc().width, _swapChain->GetDesc().height);
	if (FAILED(hr))
	{
		return hr;
	}

	return S_OK;
}

HRESULT DX12Renderer::Resize(UINT width, UINT height)
{
	return CreateDepthBuffer(width, height);
}

HRESULT DX12Renderer::CreateDepthBuffer(UINT width, UINT height)
{
	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_RESOURCE_DESC resourceDesc = {};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDesc.Width = width;
	resourceDesc.Height = height;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DepthStencilFormat;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.SampleDesc.Quality = 0;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = DepthStencilFormat;
	clearValue.DepthStencil.Depth = 1.0f;
	clearValue.DepthStencil.Stencil = 0;

	_depthStencilBuffer.Reset();

	HRESULT hr = _device->GetDevice()->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&clearValue,
		IID_PPV_ARGS(&_depthStencilBuffer)
	);
	if (FAILED(hr))
	{
		return hr;
	}

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DepthStencilFormat;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

	_device->GetDevice()->CreateDepthStencilView(
		_depthStencilBuffer.Get(),
		&dsvDesc,
		_depthStencilHeap->GetCPUDescriptorHandleForHeapStart()
	);

	return S_OK;
}

HRESULT DX12Renderer::BeginFrame(ID3D12DescriptorHeap* descriptorHeap)
{
	_currentFrameIndex = _swapChain->GetCurrentBackBufferIndex();
	 
	bool flag = false;
	for (int i = 0; i < _frameContexts.size(); ++i)
	{
		if (_frameContexts[i]->IsAvailable())
		{
			_currentFrameContextIndex = i;
			flag = true;
			break;
		}
	}
	if (!flag)
	{
		UINT64 minFenceValue = UINT64_MAX;
		int minIndex = 0;
		for (int i = 0; i < _frameContexts.size(); ++i)
		{
			UINT64 fenceValue = _frameContexts[i]->GetFenceValue();
			if (minFenceValue > fenceValue)
			{
				minFenceValue = fenceValue;
				minIndex = i;
			}
		}
		_currentFrameContextIndex = minIndex;
		_commandQueue->CpuWait(minFenceValue);
	}

	_frameContexts[_currentFrameContextIndex]->GetCommandAllocator()->Reset();
	_commandList->Reset(_frameContexts[_currentFrameContextIndex]->GetCommandAllocator(), nullptr);

	_commandList->SetDescriptorHeaps(1, &descriptorHeap);

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = _swapChain->GetBackBuffer(_currentFrameIndex);
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	_commandList->ResourceBarrier(1, &barrier);

	D3D12_CPU_DESCRIPTOR_HANDLE rtv = _swapChain->GetRTV(_currentFrameIndex);
	D3D12_CPU_DESCRIPTOR_HANDLE dsv = _depthStencilHeap->GetCPUDescriptorHandleForHeapStart();

	_commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

	const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
	_commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
	_commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	D3D12_VIEWPORT viewport = {};
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = _swapChain->GetDesc().width;
	viewport.Height = _swapChain->GetDesc().height;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	D3D12_RECT scissorRect = {};
	scissorRect.left = 0;
	scissorRect.top = 0;
	scissorRect.right = _swapChain->GetDesc().width;
	scissorRect.bottom = _swapChain->GetDesc().height;

	_commandList->RSSetViewports(1, &viewport);
	_commandList->RSSetScissorRects(1, &scissorRect);
	return S_OK;
}

HRESULT DX12Renderer::EndFrame()
{
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = _swapChain->GetBackBuffer(_currentFrameIndex);
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	_commandList->ResourceBarrier(1, &barrier);
	_commandList->Close();

	ID3D12CommandList* cmdList[] = { _commandList.Get() };
	_commandQueue->GetCommandQueue()->ExecuteCommandLists(1, cmdList);
	UINT64 fenceValue = _commandQueue->Signal();
	_frameContexts[_currentFrameContextIndex]->SetFenceValue(fenceValue);
	_swapChain->Present();

	return S_OK;
}

void DX12Renderer::Release()
{
	if (_commandQueue == nullptr || _swapChain == nullptr)return;
	UINT64 fenceValue = _commandQueue->GetCurrentFenceValue();
	_commandQueue->Flush();
	for(int i = 0; i < _frameContexts.size(); ++i)
	{
		if(_frameContexts[i]->GetFenceValue() > fenceValue)
		{
			_commandQueue->CpuWait(_frameContexts[i]->GetFenceValue());
		}
		delete _frameContexts[i];
		_frameContexts[i] = nullptr;
	}
	_commandQueue = nullptr;
	_swapChain = nullptr;
}
