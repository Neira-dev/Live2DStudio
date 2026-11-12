#include "Scene.h"
#include "Cubism/Live2D.h"

Scene::Scene() = default;

Scene::~Scene()
{
	UnInit();
}

void Scene::Init(DX12Device* device, DX12CommandQueue* commandQueue, DX12Renderer* renderer)
{
	// シーンの初期化処理

	// Live2D の初期化（Cubism Framework と描画用の共通リソース）
	_live2dStarted = Live2D::StartUp(device, commandQueue, renderer);
	if (_live2dStarted)
	{
		// モデルの読み込み（作業ディレクトリ = App フォルダーからの相対パス）
		_live2d = std::make_unique<Live2D>();
		if (_live2d->Load(L"Live2D/DeepSeek"))
		{
			// 待機モーションをループ再生
			_live2d->PlayMotion(L"Live2D/DeepSeek/Animation/IDLE.motion3.json", true);
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

	if (_live2d)
	{
		_live2d->Update(deltaSeconds);
	}
}

void Scene::Draw(ID3D12GraphicsCommandList* commandList)
{
	// シーンの描画処理
	if (_live2d)
	{
		_live2d->Draw(commandList);
	}
}

void Scene::DrawImGui()
{
	// ImGuiの描画処理
}

void Scene::UnInit()
{
	// シーンの終了処理
	_live2d.reset();
	if (_live2dStarted)
	{
		Live2D::Shutdown();
		_live2dStarted = false;
	}
}
