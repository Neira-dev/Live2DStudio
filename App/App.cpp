#include "App.h"

#include "../Window/Window.h"
#include "../DirectX12/DX12Device.h"
#include "../DirectX12/DX12CommandQueue.h"
#include "../DirectX12/DX12SwapChain.h"
#include "../DirectX12/DX12Renderer.h"

#include "../DirectX12/Descriptor.h"
#include "../ImGui/ImGuiLayer.h"

#include "Scene.h"

App::App()
{
}

App::~App()
{
	_gui.reset();
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
	windowDesc.windowSettings.title[0] = L'Live2DGraphicStudio';
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

	// ImGuiの初期化
	_gui = std::make_unique<ImGuiLayer>();
	ImGuiLayer::ImGuiLayerDesc guiDesc;
	guiDesc.hwnd = _window->GetHwnd();
	guiDesc.device = _device->GetDevice();
	guiDesc.bufferCount = _swapChain->GetBufferCount();
	guiDesc.commandQueue = _commandQueue->GetCommandQueue();
	_gui->Initialize(guiDesc);

	// ImGuiのWndProcを登録
	_window->AddWndProc(ImGuiLayer::WndProcHandler);

	_scene = std::make_unique<Scene>();
	_scene->Init();

}

void App::Update()
{
	_scene->Update();
}

void App::Draw()
{
	_renderer->BeginFrame(Descriptor::Singleton()->GetDescriptorHeap());

	_scene->Draw(_renderer->GetCommandList());

	_gui->BeginFrame();

	_scene->DrawImGui();

	_gui->EndFrame(_renderer->GetCommandList());

	_renderer->EndFrame();
}

void App::UnInit()
{
}
