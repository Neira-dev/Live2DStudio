#include "../Window/Window.h"

#include "../DirectX12/DX12Device.h"
#include "../DirectX12/Descriptor.h"
#include "../DirectX12/DX12CommandQueue.h"
#include "../DirectX12/DX12SwapChain.h"
#include "../DirectX12/DX12Renderer.h"

#include "../ImGui/ImGuiLayer.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	Window::WindowDesc desc;
	desc.hInstance = hInstance;
	desc.nCmdShow = nCmdShow;
	Window window(desc);

	DX12Device::DeviceDesc deviceDesc;
	DX12Device device;
	device.Initialize(deviceDesc);

	Descriptor descriptor;
	descriptor.Initialize(&device);

	DX12CommandQueue commandQueue;
	commandQueue.Initialize(device.GetDevice(), D3D12_COMMAND_LIST_TYPE_DIRECT);

	DX12SwapChain::SwapChainDesc swapChainDesc;
	swapChainDesc.width = window.GetDesc().width;
	swapChainDesc.height = window.GetDesc().height;
	swapChainDesc.hwnd = window.GetHwnd();
	swapChainDesc.device = &device;
	swapChainDesc.commandQueue = &commandQueue;
	swapChainDesc.BufferCount = 2;

	DX12SwapChain swapChain;
	swapChain.Initialize(swapChainDesc);

	DX12Renderer renderer;
	renderer.Initialize(&device, &commandQueue, &swapChain);

	ImGuiLayer imGuiLayer;
	imGuiLayer.Initialize({ window.GetHwnd(), device.GetDevice(), commandQueue.GetCommandQueue(), swapChain.GetBufferCount(), DXGI_FORMAT_R8G8B8A8_UNORM, 64 });
	window.AddWndProc(ImGuiLayer::WndProcHandler);

	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);

		// ï`âÊèàóù
		renderer.BeginFrame(descriptor.GetDescriptorHeap());
		imGuiLayer.BeginFrame();
	
		ImGui::Begin("Hello, ImGui!");
		ImGui::Text("This is some useful text.");
		ImGui::End();
		

		imGuiLayer.EndFrame(renderer.GetCommandList());
		renderer.EndFrame();
	}

	imGuiLayer.Shutdown();
	renderer.Release();
	return (int)msg.wParam;
}