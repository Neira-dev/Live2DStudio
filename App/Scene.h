#pragma once
#include <d3d12.h>
#include <chrono>
#include <memory>

class DX12Device;
class DX12CommandQueue;
class DX12Renderer;
class Live2D;

class Scene
{
public:
	Scene();
	~Scene();
public:
	void Init(DX12Device* device, DX12CommandQueue* commandQueue, DX12Renderer* renderer);
	void Update();
	void Draw(ID3D12GraphicsCommandList* commandList);
	void DrawImGui();
	void UnInit();

private:
	std::unique_ptr<Live2D> _live2d;
	std::chrono::steady_clock::time_point _prevTime;
	bool _live2dStarted = false;
};
