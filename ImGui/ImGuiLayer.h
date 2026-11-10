#pragma once
/*
* ImGuiLayer
* ImGui (Win32 + DirectX12) の初期化・描画を管理するクラス
* 依存: d3d12 / imgui のみ
*/

#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
#include "imgui.h"

struct ImGui_ImplDX12_InitInfo;

class ImGuiLayer
{
public:
	struct ImGuiLayerDesc
	{
		HWND hwnd = nullptr;
		ID3D12Device* device = nullptr;
		ID3D12CommandQueue* commandQueue = nullptr;
		/*
		* フレーム数（スワップチェインのバッファ数）
		*/
		UINT bufferCount = 2;
		/*
		* バックバッファの RTV フォーマット
		*/
		DXGI_FORMAT rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		/*
		* ImGui 用 SRV ヒープのディスクリプタ数
		*/
		UINT srvCapacity = 64;
	};
public:
	ImGuiLayer() = default;
	~ImGuiLayer();
	ImGuiLayer(const ImGuiLayer&) = delete;
	ImGuiLayer& operator=(const ImGuiLayer&) = delete;
public:
	/**
	 * @brief 初期化処理
	 * @param desc 初期化情報
	 * @return HRESULT
	 */
	HRESULT Initialize(const ImGuiLayerDesc& desc);

	/**
	 * @brief 終了処理（GPU の処理が完了してから呼ぶこと）
	 */
	void Shutdown();

	/**
	 * @brief フレームの開始
	 */
	void BeginFrame();

	/**
	 * @brief フレームの終了と描画コマンドの記録
	 * @param commandList 描画先の RTV がセットされたコマンドリスト
	 */
	void EndFrame(ID3D12GraphicsCommandList* commandList);

	/**
	 * @brief Win32 のメッセージを ImGui に渡す
	 * @details Window::MessageHandler と同じシグネチャなので window.AddWndProc(ImGuiLayer::WndProcHandler) で登録できる
	 * @param result ImGui が処理した場合の WndProc の戻り値
	 * @return ImGui がメッセージを処理した場合は true
	 */
	static bool WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result);

private:
	HRESULT CreateSrvHeap(ID3D12Device* device, UINT capacity);

	// ImGui のバックエンドから呼ばれる SRV の確保・解放
	static void SrvAlloc(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu);
	static void SrvFree(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu);
private:
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _srvHeap;
	D3D12_CPU_DESCRIPTOR_HANDLE _srvCpuStart = {};
	D3D12_GPU_DESCRIPTOR_HANDLE _srvGpuStart = {};
	UINT _srvDescriptorSize = 0;
	std::vector<UINT> _srvFreeList;
	bool _initialized = false;
};
