#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <vector>
#include <wrl/client.h>

class DX12Device;
class DX12CommandQueue;

class DX12SwapChain
{
public:
	struct SwapChainDesc
	{
		UINT width = 0;
		UINT height = 0;
		UINT BufferCount = 2;
		HWND hwnd = nullptr;
		DX12Device* device = nullptr;
		DX12CommandQueue* commandQueue = nullptr;
	};
public:
	DX12SwapChain() = default;
	~DX12SwapChain() = default;

	/**
	 * @brief 初期化
	 * @param desc  
	 * @return 
	 */
	HRESULT Initialize(SwapChainDesc desc);

	/**
	 * @brief スワップチェインを取得する
	 * @return 
	 */
	IDXGISwapChain3* GetSwapChain() const { return _swapChain.Get(); }

	/**
	 * @brief バッファ数を取得する
	 * @return バッファ数
	 */
	UINT GetBufferCount() const { return _desc.BufferCount; }

	/**
	 * @brief 現在のバックバッファのインデックスを取得する
	 * @return 現在のバックバッファのインデックス
	 */
	UINT GetCurrentBackBufferIndex() const { return _swapChain->GetCurrentBackBufferIndex(); }

	/**
	 * @brief 垂直同期の有効/無効を設定する
	 * @param enable 有効にする場合はtrue、無効にする場合はfalse
	 */
	void VSyncInterval(bool enable) { _vSyncEnabled = enable; }

	/**
	 * @brief RTVを取得する
	 * @param index RTVのインデックス
	 * @return ID3D12Resource* RTVのリソース
	 */
	ID3D12Resource* GetBackBuffer(UINT index) const { return _renderTargets[index].Get(); }

	/**
	 * @brief RTVのハンドルを取得する
	 * @param index RTVのインデックス
	 * @return D3D12_CPU_DESCRIPTOR_HANDLE RTVのハンドル
	 */
	D3D12_CPU_DESCRIPTOR_HANDLE GetRTV(UINT index) const;

	/**
	 * @brief RTVの切替、描画を行う 
	 */
	void Present();

	/**
	 * @brief  RTVのディスクリプタサイズを取得する
	 * @return UINT RTVのディスクリプタサイズ
	 */
	UINT GetRenderTargetViewDescriptorSize() const { return _renderTargetViewDescriptorSize; }

	/**
	 * @brief スワップチェインの情報を取得する
	 * @return SwapChainDesc スワップチェインの情報
	 */
	SwapChainDesc GetDesc() const { return _desc; }

	/**
	* @brief スワップチェインのリサイズ
	* @param width
	* @param height
	*/
	void Resize(UINT width, UINT height);

protected:

	/**
	 * @brief スワップチェインの初期化に必要な情報が揃っているか確認する
	 * @param desc  
	 * @return 
	 */
	bool CheckDescPtr(const SwapChainDesc* desc);

private:
	Microsoft::WRL::ComPtr<IDXGISwapChain3> _swapChain;
	SwapChainDesc _desc;
	// 書き込んだ結果を格納するレンダーターゲットの配列
	std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> _renderTargets;
	UINT _renderTargetViewDescriptorSize = 0;
	// レンダーターゲットビューのヒープ
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _rtvHeap;
	bool _vSyncEnabled = true;
};

