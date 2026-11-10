#pragma once
#include <d3d12.h>
#include <wrl/client.h>

class DX12CommandQueue;
class DX12Device;

class FrameContext
{
public:
	FrameContext() = default;
	~FrameContext() = default;

	/**
	 * @brief 初期化
	 * @param device
	 * @param commandQueue 
	 * @param type 
	 * @return 
	 */
	HRESULT Initialize(DX12Device* device, DX12CommandQueue* commandQueue, D3D12_COMMAND_LIST_TYPE type);


	/**
	 * @brief 使用可能か確認 
	 * @return 
	 */
	bool IsAvailable() const;

	/**
	 * @brief コマンドアロケータを取得する
	 * @return 
	 */
	ID3D12CommandAllocator* GetCommandAllocator() const { return _commandAllocator.Get(); }

	/**
	 * @brief フェンスの値を設定する
	 * @param fenceValue 
	 */
	void SetFenceValue(UINT64 fenceValue) { _fenceValue = fenceValue; }

	/**
	 * @brief フェンスの値を取得する
	 * @return  
	 */
	UINT64 GetFenceValue() const { return _fenceValue; }
private:
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> _commandAllocator;
	DX12Device* _device = nullptr;
	DX12CommandQueue* _commandQueue = nullptr;
	UINT64 _fenceValue = 0;
};

