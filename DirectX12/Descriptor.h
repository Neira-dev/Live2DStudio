#pragma once
#include <wrl/client.h>
#include <d3d12.h>
#include <queue>
#include <vector>

class DX12Device;

class Descriptor
{
public:
	/**
	 * @brief シングルトンのインスタンスを取得する
	 * @return
	 */
	static Descriptor* Singleton();

	/**
	 * @brief シングルトンのインスタンスを解放する
	 */
	static void Release();

	/**
	 * @brief 初期化する
	 * @param device 
	 * @param capacity 
	 * @return 
	 */
	HRESULT Initialize(DX12Device* device, UINT capacity = 4096);
	
	/**
	 * @brief ディスクリプタを割り当てる 
	 * @return 割り当てられたディスクリプタのインデックス
	 */
	UINT Allocate();

	/**
	 * @brief ディスクリプタを解放する
	 * @param index 解放するディスクリプタのインデックス
	 */
	void Free(UINT index);

	/**
	 * @brief CPUハンドルを取得する
	 * @param index 
	 * @return 
	 */
	D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(UINT index) const;

	/**
	 * @brief GPUハンドルを取得する
	 * @param index 
	 * @return 
	 */
	D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(UINT index) const;

	/**
	 * @brief ディスクリプタヒープを取得する
	 * @return 
	 */
	ID3D12DescriptorHeap* GetDescriptorHeap() const { return _descriptorHeap.Get(); }

	/**
	 * @brief ディスクリプタの容量を取得する
	 * @return 
	 */
	UINT GetDescriptorSize() const { return _descriptorSize; }
protected:
	UINT _capacity = 0;
	UINT _descriptorSize = 0;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _descriptorHeap;
	std::queue<UINT> _freeQueue;
	std::vector<bool> _allocated;
private:
	static Descriptor* _instance;
};

