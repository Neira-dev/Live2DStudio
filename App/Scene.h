#pragma once
#include <d3d12.h>
#include <chrono>
#include <memory>

class DX12Device;
class DX12CommandQueue;
class DX12Renderer;
class Stage;
class OutputWindow;
class StageView;
class PerformanceMonitor;
class FaceTrackingService;
class TrackingMapper;
namespace Rml { class Context; }

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
	void Init(DX12Device* device, DX12CommandQueue* commandQueue, DX12Renderer* renderer, Rml::Context* uiContext);
	void Update();
	void Draw(ID3D12GraphicsCommandList* commandList);

	/**
	 * @brief 出力ウィンドウを表示する（DX12Renderer::EndFrame の後に呼ぶ）
	 */
	void PresentOutput();

	void UnInit();

private:
	void UpdateTracking(float deltaSeconds);

private:
	std::unique_ptr<Stage> _stage;
	std::unique_ptr<OutputWindow> _output;
	std::unique_ptr<StageView> _view;
	std::unique_ptr<PerformanceMonitor> _monitor;

	// フェイストラッキング（自分 = 先頭の参加者を動かす）
	std::unique_ptr<FaceTrackingService> _tracking;
	std::unique_ptr<TrackingMapper> _trackingMapper;
	unsigned long long _trackingFrame = 0;
	float _trackingDelta = 0.0f;
	bool _trackingApplied = false;
	std::chrono::steady_clock::time_point _prevTime;
	bool _live2dStarted = false;

	DX12CommandQueue* _commandQueue = nullptr;
	DX12Renderer* _renderer = nullptr;
};
