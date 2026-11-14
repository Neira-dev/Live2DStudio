#include "Scene.h"
#include "Cubism/Live2D.h"
#include "Stage/Stage.h"
#include "Stage/OutputWindow.h"
#include "Stage/StageView.h"
#include "../DirectX12/DX12Device.h"
#include "../DirectX12/DX12CommandQueue.h"
#include "../DirectX12/DX12Renderer.h"

#include <algorithm>

Scene::Scene() = default;

Scene::~Scene()
{
	UnInit();
}

void Scene::Init(DX12Device* device, DX12CommandQueue* commandQueue, DX12Renderer* renderer, Rml::Context* uiContext)
{
	// シーンの初期化処理
	_commandQueue = commandQueue;
	_renderer = renderer;

	// Live2D の初期化（Cubism Framework と描画用の共通リソース）
	_live2dStarted = Live2D::StartUp(device, commandQueue, renderer);
	if (_live2dStarted)
	{
		_stage = std::make_unique<Stage>();
		if (_stage->Initialize(device->GetDevice(), commandQueue))
		{
			// 最初は Live2D フォルダーのモデルを2体まで並べる（作業ディレクトリ = App フォルダー）
			const auto models = StageView::FindModels("Live2D");
			for (size_t i = 0; i < (std::min)(models.size(), static_cast<size_t>(2)); ++i)
			{
				_stage->AddActor(models[i], StageView::ToUtf8(models[i].filename()));
			}
		}
		else
		{
	_stage.reset();
		}
	}

	// 出力ウィンドウ（OBS で取り込む）とホットキー
	_output = std::make_unique<OutputWindow>();
	if (!_output->Initialize(device, commandQueue, [this](StageHotkey hotkey)
	{
		if (!_stage)
		{
			return;
		}
		switch (hotkey)
		{
		case StageHotkey::Blackout: _stage->ToggleBlackout(); break;
		case StageHotkey::Spotlight: _stage->ToggleSpotlight(); break;
		case StageHotkey::ManualAway: _stage->ToggleManualAway(); break;
		default: break;
		}
	}))
	{
	_output.reset();
	}

	// ステージ画面の UI（RmlUi）
	if (_stage && uiContext != nullptr)
	{
		_view = std::make_unique<StageView>();
		if (!_view->Initialize(uiContext, *_stage, _output.get()))
		{
			_view.reset();
		}
	}

	_prevTime = std::chrono::steady_clock::now();
}

void Scene::Update()
{
	// シーンの更新処理
	const auto now = std::chrono::steady_clock::now();
	float deltaSeconds = std::chrono::duration<float>(now - _prevTime).count();
	_prevTime = now;

	// ウィンドウ移動などで止まった後に大きく飛ばないようにする
	if (deltaSeconds > 0.1f)
	{
		deltaSeconds = 0.1f;
	}

	if (_stage)
	{
		_stage->Update(deltaSeconds);
	}
	if (_view)
	{
		_view->Update();
	}
}

void Scene::Draw(ID3D12GraphicsCommandList* commandList)
{
	// シーンの描画処理
	if (!_stage || !_view)
	{
		return;
	}

	// 1. 参加者をステージ用テクスチャに描く
	_stage->Render(commandList);

	// 2. 出力ウィンドウ（クロマキーの緑背景、1920x1080）
	if (_output && _output->IsVisible())
	{
		const float chroma[3] = { 0.0f, 1.0f, 0.0f };
		const D3D12_CPU_DESCRIPTOR_HANDLE rtv = _output->BeginFrame(commandList);
		const D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(OutputWindow::Width), static_cast<float>(OutputWindow::Height), 0.0f, 1.0f };
		const D3D12_RECT scissor = { 0, 0, static_cast<LONG>(OutputWindow::Width), static_cast<LONG>(OutputWindow::Height) };
		_stage->Composite(commandList, rtv, viewport, scissor, chroma);
		_output->EndFrame(commandList);
	}

	// 3. メイン画面のプレビュー（欄全体を背景色で塗ってから、16:9 の範囲に描く）
	float background[3];
	_view->GetPreviewBackground(background);
	const float areaColor[4] = { 0.07f, 0.08f, 0.10f, 1.0f };
	commandList->ClearRenderTargetView(_renderer->GetCurrentRTV(), areaColor, 1, &_view->GetPreviewArea());
	_stage->Composite(commandList, _renderer->GetCurrentRTV(), _view->GetPreviewViewport(), _view->GetPreviewScissor(), background);

	// ImGui 用に描画先を戻す
	const D3D12_CPU_DESCRIPTOR_HANDLE rtv = _renderer->GetCurrentRTV();
	const D3D12_CPU_DESCRIPTOR_HANDLE dsv = _renderer->GetDSV();
	commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
	commandList->RSSetViewports(1, &_renderer->GetViewport());
	commandList->RSSetScissorRects(1, &_renderer->GetScissorRect());
}

void Scene::PresentOutput()
{
	if (_output)
	{
		_output->Present();
	}
}

void Scene::UnInit()
{
	// シーンの終了処理
	// GPU が使い終わるのを待ってから解放する
	if (_commandQueue)
	{
		_commandQueue->Flush();
	}
	_view.reset();
	_stage.reset();
	_output.reset();
	if (_live2dStarted)
	{
		Live2D::Shutdown();
		_live2dStarted = false;
	}
	_commandQueue = nullptr;
}
