#include "Descriptor.h"
#include "DX12Device.h"

Descriptor* Descriptor::_instance = nullptr;

Descriptor* Descriptor::Singleton()
{
	if (_instance == nullptr)
	{
		_instance = new Descriptor();
	}
	return _instance;
}

void Descriptor::Release()
{
	if (_instance != nullptr)
	{
		delete _instance;
		_instance = nullptr;
	}
}

HRESULT Descriptor::Initialize(DX12Device* device, UINT capacity)
{
	_capacity = capacity;

	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	desc.NumDescriptors = _capacity;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	/*
	* CBV シェーダーへの定数バッファ
	* SRV シェーダーが読み込むリソース
	* UAV シェーダーが読み書きできるリソース
	*/

	if (device == nullptr) return E_FAIL;

	HRESULT hr;
	hr = device->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&_descriptorHeap));
	if(FAILED(hr))
	{
		return hr;
	}
	_descriptorSize = device->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	for(UINT i = 0; i < _capacity; ++i)
	{
		_freeQueue.push(i);
	}
	_allocated.resize(_capacity, false);

	return S_OK;
}

UINT Descriptor::Allocate()
{
	if (_freeQueue.empty())return UINT_MAX; // エラー値

	UINT index = _freeQueue.front();
	_freeQueue.pop();
	_allocated[index] = true;
	return index;
}

void Descriptor::Free(UINT index)
{
	if (index >= _capacity || !_allocated[index]) return; // 無効なインデックスまたは未割り当てのインデックス
	_allocated[index] = false;
	_freeQueue.push(index);
}

D3D12_CPU_DESCRIPTOR_HANDLE Descriptor::GetCpuHandle(UINT index) const
{
	if (index >= _capacity) return { 0 }; // 無効なインデックス
	auto handle = _descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handle.ptr += (SIZE_T)index * _descriptorSize;
	return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE Descriptor::GetGpuHandle(UINT index) const
{
	if (index >= _capacity) return { 0 }; // 無効なインデックス
	auto handle = _descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handle.ptr += (SIZE_T)index * _descriptorSize;
	return handle;
}
