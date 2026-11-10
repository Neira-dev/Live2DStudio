#include "FrameContext.h"
#include "DX12Device.h"
#include "DX12CommandQueue.h"
#pragma comment(lib, "d3d12.lib")

HRESULT FrameContext::Initialize(DX12Device* device, DX12CommandQueue* commandQueue, D3D12_COMMAND_LIST_TYPE type)
{
	if(device == nullptr || commandQueue == nullptr)
	{
		return E_INVALIDARG;
	}
	_device = device;
	_commandQueue = commandQueue;


	return _device->GetDevice()->CreateCommandAllocator(
		type,
		IID_PPV_ARGS(&_commandAllocator));
}

bool FrameContext::IsAvailable() const
{
	return _commandQueue->GetFence()->GetCompletedValue() >= _fenceValue;
}
