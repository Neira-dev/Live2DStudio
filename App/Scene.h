#pragma once
#include <d3d12.h>
#include <chrono>
#include <memory>

class DX12Device;
class DX12CommandQueue;
class DX12Renderer;
class Stage;
class OutputWindow;
class StageUI;

/*
* Scene
* CoStage のステージ画面（ステージ・出力ウィンドウ・UI をまとめる）
*/
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

	/**
	 * @brief 出力ウィンドウを表示する（DX12Renderer::EndFrame の後に呼ぶ）
	 */
	void PresentOutput();

	void UnInit();

private:
	std::unique_ptr<Stage> _stage;
	std::unique_ptr<OutputWindow> _output;
	std::unique_ptr<StageUI> _ui;
	std::chrono::steady_clock::time_point _prevTime;
	bool _live2dStarted = false;

	DX12CommandQueue* _commandQueue = nullptr;
	DX12Renderer* _renderer = nullptr;
};
