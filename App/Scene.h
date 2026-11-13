#pragma once
#include <d3d12.h>
#include <chrono>
#include <memory>

class DX12Device;
class DX12CommandQueue;
class DX12Renderer;
class Live2D;
class Camera;
class FloorGrid;

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

	// 3D •\Ž¦
	std::unique_ptr<Camera> _camera;
	std::unique_ptr<FloorGrid> _grid;
	bool _use3D = true;

	DX12CommandQueue* _commandQueue = nullptr;
	DX12Renderer* _renderer = nullptr;
	std::chrono::steady_clock::time_point _prevTime;
	bool _live2dStarted = false;
};
