#include "DX12CommandQueue.h"
#pragma comment(lib, "d3d12.lib")

DX12CommandQueue::~DX12CommandQueue()
{
	if (_commandQueue && _fence && _fenceEvent) Flush();
	if (_fenceEvent)
	{
		CloseHandle(_fenceEvent);
		_fenceEvent = nullptr;
	}
}

HRESULT DX12CommandQueue::Initialize(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type)
{
	HRESULT hr = S_OK;
	D3D12_COMMAND_QUEUE_DESC desc = {};
	desc.Type = type;
	desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	desc.NodeMask = 0;

	if (device == nullptr)return E_INVALIDARG;

	 hr = device->CreateCommandQueue(&desc, IID_PPV_ARGS(&_commandQueue));
	 if (FAILED(hr)) return hr;

	 hr = device->CreateFence(
		 0,
		 D3D12_FENCE_FLAG_NONE,
		 IID_PPV_ARGS(&_fence)
	 );
	 if (FAILED(hr)) return hr;

	 _fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	 if (_fenceEvent == nullptr) return HRESULT_FROM_WIN32(GetLastError());

	return hr;
}

HRESULT DX12CommandQueue::Execute(UINT count, ID3D12CommandList* const* lists)
{
	if (_commandQueue == nullptr) return E_FAIL;
	_commandQueue->ExecuteCommandLists(count,lists);
	return S_OK;
}

UINT64 DX12CommandQueue::Signal()
{
	if (_commandQueue == nullptr) return 0;

	const UINT64 fenceValue = _fenceValue + 1;
	if (FAILED(_commandQueue->Signal(_fence.Get(), fenceValue)))return 0;

	_fenceValue = fenceValue;
	return fenceValue;
}

HRESULT DX12CommandQueue::CpuWait(UINT64 fenceValue)
{
	if (fenceValue == 0)return S_OK;
	if (IsFenceComplete(fenceValue))return S_OK;
	if (_fence == nullptr)return E_FAIL;
	if (_fenceEvent == nullptr)return E_FAIL;

	HRESULT hr;
	hr = _fence->SetEventOnCompletion(fenceValue, _fenceEvent);
	if (FAILED(hr))return hr;
	WaitForSingleObject(_fenceEvent, INFINITE);
	return S_OK;
}

HRESULT DX12CommandQueue::QueueWait(ID3D12Fence* fence, UINT64 fenceValue)
{
	if(fenceValue == 0)return S_OK;
	if (_commandQueue == nullptr)return E_FAIL;
	HRESULT hr;
	hr = _commandQueue->Wait(fence, fenceValue);
	if (FAILED(hr))return hr;
	return S_OK;
}

bool DX12CommandQueue::IsFenceComplete(UINT64 fenceValue)
{
	if (_fence == nullptr)return false;
	if (_fence->GetCompletedValue() >= fenceValue)return true;
	return false;

}

HRESULT DX12CommandQueue::Flush()
{
	const UINT64 v = Signal();
	if (v == 0)return E_FAIL;
	HRESULT hr = CpuWait(v);
	return hr;
}
