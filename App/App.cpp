#include "App.h"

#include "../Window/Window.h"
#include "../DirectX12/DX12Device.h"
#include "../DirectX12/DX12CommandQueue.h"
#include "../DirectX12/DX12SwapChain.h"
#include "../DirectX12/DX12Renderer.h"

#include "../DirectX12/Descriptor.h"
#include "UI/RmlUiLayer.h"

#include "Scene.h"

App::App()
{
}

App::~App()
{
	if (_scene)
	{
		_scene->UnInit();
		_scene.reset();
	}
	_ui.reset();
	if(_renderer)
	{
		_renderer->Release();
		_renderer.reset();
	}
	_swapChain.reset();
	_commandQueue.reset();
	Descriptor::Singleton()->Release();
	_device.reset();
	_window.reset();
}

void App::Init(HINSTANCE hInstance, int nCmdShow)
{
	// ウィンドウの作成
	Window::WindowDesc windowDesc;
	windowDesc.hInstance = hInstance;
	windowDesc.nCmdShow = nCmdShow;
	windowDesc.width = 1280;
	windowDesc.height = 720;
	windowDesc.windowClass.className = L"Live2DGraphicStudio";
	WCHAR title[256] = L"Live2DGraphicStudio";
	for(int i = 0; i < 256; ++i)
	{
		windowDesc.windowSettings.title[i] = title[i];
		if(title[i] == L'\0')
		{
			break;
		}
	}
	_window = std::make_unique<Window>(windowDesc);

	// DX12デバイスの作成
	_device = std::make_unique<DX12Device>();
	DX12Device::DeviceDesc deviceDesc;
	_device->Initialize(deviceDesc);

	// ディスクリプタの作成
	Descriptor::Singleton()->Initialize(_device.get());

	// コマンドキューの作成
	_commandQueue = std::make_unique<DX12CommandQueue>();
	_commandQueue->Initialize(_device->GetDevice(), D3D12_COMMAND_LIST_TYPE_DIRECT);

	// スワップチェインの作成
	_swapChain = std::make_unique<DX12SwapChain>();
	DX12SwapChain::SwapChainDesc swapChainDesc;
	swapChainDesc.width = _window->GetDesc().width;
	swapChainDesc.height = _window->GetDesc().height;
	swapChainDesc.hwnd = _window->GetHwnd();
	swapChainDesc.device = _device.get();
	swapChainDesc.commandQueue = _commandQueue.get();
	_swapChain->Initialize(swapChainDesc);

	// レンダラーの作成
	_renderer = std::make_unique<DX12Renderer>();
	_renderer->Initialize(_device.get(), _commandQueue.get(), _swapChain.get());

	// UI（RmlUi）の初期化
	_ui = std::make_unique<RmlUiLayer>();
	RmlUiLayer::Desc uiDesc;
	uiDesc.hwnd = _window->GetHwnd();
	uiDesc.device = _device->GetDevice();
	uiDesc.frameCount = _swapChain->GetBufferCount();
	uiDesc.width = _window->GetDesc().width;
	uiDesc.height = _window->GetDesc().height;
	_ui->Initialize(uiDesc);

	// UI に Win32 のメッセージを渡す
	_window->AddWndProc([this](HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result)
	{
		return _ui->HandleMessage(hwnd, message, wParam, lParam, result);
	});

	_scene = std::make_unique<Scene>();
	_scene->Init(_device.get(), _commandQueue.get(), _renderer.get(), _ui->GetContext());

}

void App::Update()
{
	_scene->Update();
	_ui->Update();
}

void App::Draw()
{
	_renderer->BeginFrame(Descriptor::Singleton()->GetDescriptorHeap());

	_scene->Draw(_renderer->GetCommandList());

	// UI を一番上に重ねる
	_ui->Render(_renderer->GetCommandList(), _renderer->GetCurrentRTV());

	_renderer->EndFrame();

	// 出力ウィンドウ（OBS 取り込み用）を表示
	_scene->PresentOutput();
}

void App::UnInit()
{
}
