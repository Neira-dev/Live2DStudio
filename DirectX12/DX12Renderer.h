#pragma once
#include <d3d12.h>
#include <vector>
#include <wrl/client.h>

class DX12Device;
class DX12CommandQueue;
class DX12SwapChain;
class FrameContext;

class DX12Renderer
{
public:
	DX12Renderer() = default;
	~DX12Renderer() = default;
public:
	/**
	 * @brief 初期化
	 * @param commandQueue
	 * @param swapChain
	 * @return
	 */
	HRESULT Initialize(DX12Device* device, DX12CommandQueue* commandQueue, DX12SwapChain* swapChain);

	/**
	 * @brief フレームの開始
	 * @return
	 */
	HRESULT BeginFrame(ID3D12DescriptorHeap* descriptorHeap);

	/**
	 * @brief フレームの終了
	 * @return
	 */
	HRESULT EndFrame();

	/**
	 * @brief 解放処理
	 */
	void Release();

	/**
	 * @brief コマンドリストを取得する
	 * @return ID3D12GraphicsCommandList*
	 */
	ID3D12GraphicsCommandList* GetCommandList() const { return _commandList.Get(); }

	/**
	 * @brief 深度バッファをリサイズする
	 * @param width
	 * @param height
	 * @return
	 */
	HRESULT Resize(UINT width, UINT height);

	/**
	 * @brief 現在のフレームのバックバッファのRTVを取得する
	 * @details BeginFrame〜EndFrameの間のみ有効
	 */
	D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRTV() const;

	/**
	 * @brief 深度バッファのDSVを取得する
	 */
	D3D12_CPU_DESCRIPTOR_HANDLE GetDSV() const;

	/**
	 * @brief 現在のフレームのビューポートを取得する
	 */
	const D3D12_VIEWPORT& GetViewport() const { return _viewport; }

	/**
	 * @brief 現在のフレームのシザー矩形を取得する
	 */
	const D3D12_RECT& GetScissorRect() const { return _scissorRect; }

	/**
	 * @brief 同時に処理中になり得るフレーム数を取得する
	 */
	UINT GetFrameCount() const { return static_cast<UINT>(_frameContexts.size()); }

	/**
	 * @brief 深度バッファのフォーマットを取得する
	 */
	static constexpr DXGI_FORMAT GetDepthStencilFormat() { return DepthStencilFormat; }
protected:
	/**
	 * @brief 深度バッファを作成する
	 * @param width
	 * @param height
	 * @return
	 */
	HRESULT CreateDepthBuffer(UINT width, UINT height);
protected:
	DX12Device* _device = nullptr;
	DX12CommandQueue* _commandQueue = nullptr;
	DX12SwapChain* _swapChain = nullptr;
	std::vector < FrameContext* > _frameContexts;
	UINT _currentFrameIndex = 0;
	UINT _currentFrameContextIndex = 0;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> _commandList;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _depthStencilHeap;
	Microsoft::WRL::ComPtr<ID3D12Resource> _depthStencilBuffer;
	D3D12_VIEWPORT _viewport = {};
	D3D12_RECT _scissorRect = {};

	static constexpr DXGI_FORMAT DepthStencilFormat = DXGI_FORMAT_D32_FLOAT;
};
