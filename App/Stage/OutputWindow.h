#pragma once
/*
* OutputWindow
* OBS のウィンドウキャプチャで取り込むための出力ウィンドウ（UI なし、クロマキー背景、1920x1080）
*
*   閉じるボタンでは非表示になるだけ（アプリは終了しない）。
*   配信中に画面を開かずに操作できるよう、グローバルホットキーもこのウィンドウで受け取る。
*/

#include <d3d12.h>
#include <functional>
#include <memory>
#include <Windows.h>

class Window;
class DX12Device;
class DX12CommandQueue;
class DX12SwapChain;

enum class StageHotkey
{
	Blackout,    // 暗転
	Spotlight,   // スポットライト
	ManualAway,  // 手動離席
	Count
};

class OutputWindow
{
public:
	static constexpr UINT Width = 1920;
	static constexpr UINT Height = 1080;

	using HotkeyCallback = std::function<void(StageHotkey)>;

public:
	OutputWindow();
	~OutputWindow();
	OutputWindow(const OutputWindow&) = delete;
	OutputWindow& operator=(const OutputWindow&) = delete;

	bool Initialize(DX12Device* device, DX12CommandQueue* commandQueue, HotkeyCallback onHotkey);
	void Release();

	bool IsVisible() const;
	void SetVisible(bool visible);

	/**
	 * @brief ホットキーの登録に成功したか（他のアプリが使っていると失敗する）
	 */
	bool IsHotkeyRegistered(StageHotkey hotkey) const { return _hotkeyRegistered[static_cast<int>(hotkey)]; }

	/**
	 * @brief ホットキーの表示名（例：Ctrl+Alt+B）
	 */
	static const char* GetHotkeyLabel(StageHotkey hotkey);

	/**
	 * @brief 現在のバックバッファを描画先にする
	 * @return 描画先の RTV
	 */
	D3D12_CPU_DESCRIPTOR_HANDLE BeginFrame(ID3D12GraphicsCommandList* commandList);

	/**
	 * @brief バックバッファを表示できる状態に戻す
	 */
	void EndFrame(ID3D12GraphicsCommandList* commandList);

	/**
	 * @brief 表示する（コマンドリストを実行した後に呼ぶ）
	 */
	void Present();

private:
	bool HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result);

private:
	std::unique_ptr<Window> _window;
	std::unique_ptr<DX12SwapChain> _swapChain;
	HotkeyCallback _onHotkey;
	bool _hotkeyRegistered[static_cast<int>(StageHotkey::Count)] = {};
	bool _frameBegun = false;
	UINT _backBufferIndex = 0;
};
