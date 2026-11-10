#pragma once
#include <memory>
#include <Windows.h>

class Window;
class DX12Device;
class DX12CommandQueue;
class DX12SwapChain;
class DX12Renderer;
class ImGuiLayer;

class App
{
public:
	App();
	~App();
public:
	void Init(HINSTANCE hInstance,int nCmdShow);
	void Update();
	void Draw();
	void UnInit();

private:
	std::unique_ptr<Window> _window;
	std::unique_ptr<DX12Device> _device;
	std::unique_ptr<DX12CommandQueue> _commandQueue;
	std::unique_ptr<DX12SwapChain> _swapChain;
	std::unique_ptr<DX12Renderer> _renderer;
	std::unique_ptr<ImGuiLayer> _gui;
};

