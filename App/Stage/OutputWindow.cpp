#include "OutputWindow.h"

#include "../../Window/Window.h"
#include "../../DirectX12/DX12Device.h"
#include "../../DirectX12/DX12CommandQueue.h"
#include "../../DirectX12/DX12SwapChain.h"

namespace
{
	constexpr int HotkeyIdBase = 0x4300;

	struct HotkeyDefinition
	{
		UINT modifiers;
		UINT key;
		const char* label;
	};

	const HotkeyDefinition Hotkeys[] =
	{
		{ MOD_CONTROL | MOD_ALT, 'B', "Ctrl+Alt+B" },  // 暗転
		{ MOD_CONTROL | MOD_ALT, 'S', "Ctrl+Alt+S" },  // スポットライト
		{ MOD_CONTROL | MOD_ALT, 'A', "Ctrl+Alt+A" },  // 手動離席
	};
	static_assert(sizeof(Hotkeys) / sizeof(Hotkeys[0]) == static_cast<size_t>(StageHotkey::Count), "hotkey table");

	// ウィンドウの見た目の大きさ（中身は 1920x1080 で描いて縮小表示する）
	constexpr UINT ClientWidth = 960;
	constexpr UINT ClientHeight = 540;
}

OutputWindow::OutputWindow() = default;

OutputWindow::~OutputWindow()
{
	Release();
}

bool OutputWindow::Initialize(DX12Device* device, DX12CommandQueue* commandQueue, HotkeyCallback onHotkey)
{
	_onHotkey = std::move(onHotkey);

	Window::WindowDesc desc;
	desc.hInstance = GetModuleHandle(nullptr);
	desc.nCmdShow = SW_SHOWNOACTIVATE;
	desc.width = ClientWidth;
	desc.height = ClientHeight;
	desc.windowClass.className = L"CoStageOutputWindow";
	const wchar_t title[] = L"CoStage Output (OBS: Window Capture)";
	for (size_t i = 0; i < sizeof(title) / sizeof(title[0]); ++i)
	{
		desc.windowSettings.title[i] = title[i];
	}
	_window = std::make_unique<Window>(desc);
	if (_window->GetHwnd() == nullptr)
	{
		_window.reset();
		return false;
	}
	_window->AddWndProc([this](HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result)
	{
		return HandleMessage(hwnd, message, wParam, lParam, result);
	});

	DX12SwapChain::SwapChainDesc swapDesc;
	swapDesc.width = Width;
	swapDesc.height = Height;
	swapDesc.hwnd = _window->GetHwnd();
	swapDesc.device = device;
	swapDesc.commandQueue = commandQueue;
	_swapChain = std::make_unique<DX12SwapChain>();
	if (FAILED(_swapChain->Initialize(swapDesc)))
	{
		_swapChain.reset();
		return false;
	}
	// メインウィンドウの垂直同期と二重に待たないようにする
	_swapChain->VSyncInterval(false);

	for (int i = 0; i < static_cast<int>(StageHotkey::Count); ++i)
	{
		_hotkeyRegistered[i] = RegisterHotKey(_window->GetHwnd(), HotkeyIdBase + i, Hotkeys[i].modifiers | MOD_NOREPEAT, Hotkeys[i].key) != FALSE;
	}
	return true;
}

void OutputWindow::Release()
{
	if (_window && _window->GetHwnd() != nullptr)
	{
		for (int i = 0; i < static_cast<int>(StageHotkey::Count); ++i)
		{
			if (_hotkeyRegistered[i])
			{
				UnregisterHotKey(_window->GetHwnd(), HotkeyIdBase + i);
				_hotkeyRegistered[i] = false;
			}
		}
	}
	_swapChain.reset();
	if (_window && _window->GetHwnd() != nullptr)
	{
		DestroyWindow(_window->GetHwnd());
	}
	_window.reset();
}

bool OutputWindow::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM, LRESULT& result)
{
	switch (message)
	{
	case WM_CLOSE:
		// 閉じても非表示にするだけ（メイン画面から再表示できる）
		ShowWindow(hwnd, SW_HIDE);
		result = 0;
		return true;
	case WM_DESTROY:
		// Window の既定処理はアプリを終了させるので止める
		result = 0;
		return true;
	case WM_HOTKEY:
	{
		const int index = static_cast<int>(wParam) - HotkeyIdBase;
		if (index >= 0 && index < static_cast<int>(StageHotkey::Count) && _onHotkey)
		{
			_onHotkey(static_cast<StageHotkey>(index));
		}
		result = 0;
		return true;
	}
	default:
		return false;
	}
}

bool OutputWindow::IsVisible() const
{
	return _window && _window->GetHwnd() != nullptr && IsWindowVisible(_window->GetHwnd()) && !IsIconic(_window->GetHwnd());
}

void OutputWindow::SetVisible(bool visible)
{
	if (_window && _window->GetHwnd() != nullptr)
	{
		ShowWindow(_window->GetHwnd(), visible ? SW_SHOWNOACTIVATE : SW_HIDE);
	}
}

const char* OutputWindow::GetHotkeyLabel(StageHotkey hotkey)
{
	return Hotkeys[static_cast<int>(hotkey)].label;
}

D3D12_CPU_DESCRIPTOR_HANDLE OutputWindow::BeginFrame(ID3D12GraphicsCommandList* commandList)
{
	_backBufferIndex = _swapChain->GetCurrentBackBufferIndex();

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = _swapChain->GetBackBuffer(_backBufferIndex);
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	commandList->ResourceBarrier(1, &barrier);
	_frameBegun = true;
	return _swapChain->GetRTV(_backBufferIndex);
}

void OutputWindow::EndFrame(ID3D12GraphicsCommandList* commandList)
{
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = _swapChain->GetBackBuffer(_backBufferIndex);
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	commandList->ResourceBarrier(1, &barrier);
}

void OutputWindow::Present()
{
	if (_frameBegun && _swapChain)
	{
		_swapChain->Present();
	}
	_frameBegun = false;
}
