#pragma once
/*
* DX12CommandQueue
* DirectX12 のコマンドキューを管理するクラス
*/

#include <d3d12.h>
#include <wrl/client.h>

class DX12CommandQueue
{
public:
	DX12CommandQueue() = default;
	~DX12CommandQueue();
	DX12CommandQueue(const DX12CommandQueue&) = delete;
	DX12CommandQueue& operator=(const DX12CommandQueue&) = delete;
public:
	/**
	 * @brief 初期化
	 * @param device Dx12 デバイス
	 * @param type コマンドリストのタイプ
	 * @return HRESULT
	 */
	HRESULT Initialize(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type);

	/**
	 * @brief コマンドリストを詰む処理
	 * @param count　コマンドリストの数
	 * @param lists　コマンドリストの配列
	 */
	HRESULT Execute(UINT count, ID3D12CommandList* const* lists);

	/**
	 * @brief コマンドキューのシグナルを送る
	 * @return UINT64　シグナルの値
	 */
	UINT64 Signal();

	/**
	 * @brief GPUの処理が完了するまでCPUを待機する
	 * @param fenceValue シグナルの値
	 */
	HRESULT CpuWait(UINT64 fenceValue);

	/**
	 * @brief 他のコマンドキューのシグナルが完了するまで待機する
	 * @param fenceValue　シグナルの値
	 * @return　HRESULT
	 */
	HRESULT QueueWait(ID3D12Fence* fence, UINT64 fenceValue);

	/**
	 * @brief フェンスの値が完了しているか確認する
	 * @param fenceValue シグナルの値
	 * @return true 完了している false 完了していない
	 */
	bool IsFenceComplete(UINT64 fenceValue);

	/**
	 * @brief コマンドキューを取得する
	 * @return ID3D12CommandQueue*
	 */
	ID3D12CommandQueue* GetCommandQueue() const { return _commandQueue.Get(); }

	/**
	 * @brief フェンスを取得する
	 * @return ID3D12Fence*
	 */
	ID3D12Fence* GetFence() const { return _fence.Get(); }


	/**
	 * @brief コマンドキューの処理が完了するまで待機する
	 * @return HRESULT
	 */
	HRESULT Flush();

	/**
	 * @brief 現在のフェンスの値を取得する
	 * @return UINT64 現在のフェンスの値
	 */
	UINT64 GetCurrentFenceValue() const { return _fenceValue; }

private:
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> _commandQueue;
	Microsoft::WRL::ComPtr<ID3D12Fence> _fence;
	HANDLE _fenceEvent = nullptr;
	UINT64 _fenceValue = 0;
};
