#include "Scene.h"
#include "Cubism/Live2D.h"
#include "imgui.h"
#include "Camera.h"
#include "FloorGrid.h"
#include "../DirectX12/DX12Device.h"
#include "../DirectX12/DX12CommandQueue.h"
#include "../DirectX12/DX12Renderer.h"

#include <cmath>

Scene::Scene() = default;

Scene::~Scene()
{
	UnInit();
}

void Scene::Init(DX12Device* device, DX12CommandQueue* commandQueue, DX12Renderer* renderer)
{
	// シーンの初期化処理
	_commandQueue = commandQueue;
	_renderer = renderer;

	// 3D 表示用のカメラと床のグリッド
	_camera = std::make_unique<Camera>();
	_grid = std::make_unique<FloorGrid>();
	FloorGrid::Desc gridDesc;
	gridDesc.device = device->GetDevice();
	gridDesc.depthStencilFormat = DX12Renderer::GetDepthStencilFormat();
	if (!_grid->Initialize(gridDesc))
	{
		_grid.reset();
	}

	// Live2D の初期化（Cubism Framework と描画用の共通リソース）
	_live2dStarted = Live2D::StartUp(device, commandQueue, renderer);
	if (_live2dStarted)
	{
		// モデルの読み込み（作業ディレクトリ = App フォルダーからの相対パス）
		_live2d = std::make_unique<Live2D>();
		_live2d->SetCamera(_use3D ? _camera.get() : nullptr);
		if (_live2d->Load(L"Live2D/vampire_white"))
		{
			// 待機モーションをループ再生
			_live2d->PlayMotion(L"Live2D/vampire_white/animation/key01.exp3.json", true);

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
	if (_use3D && _grid && _camera && _renderer)
	{
		const D3D12_VIEWPORT& viewport = _renderer->GetViewport();
		const float aspect = viewport.Height > 0.0f ? viewport.Width / viewport.Height : 1.0f;
		_grid->Draw(commandList, _camera->GetViewProjection(aspect), _camera->GetPosition());
	}

	if (_live2d)
	{
		_live2d->Draw(commandList);
	}
}

void Scene::DrawImGui()
{
	// ImGuiの描画処理
	ImGui::Begin("Live2D");
	static float scale = 1.0f;
	static float PosX = 0.0f;
	static float PosY = 0.0f;

	if (ImGui::DragFloat("Scale", &scale, 0.1f, 3.0f))
	{
		if (_live2d)
		{
			_live2d->SetScale(scale);
		}
	}

	if (ImGui::DragFloat("PosX", &PosX, -1.0f, 1.0f))
	{
		if (_live2d)
		{
			_live2d->SetPosition(PosX, PosY);
		}
	}

	if (ImGui::DragFloat("PosY", &PosY))
	{
		if (_live2d)
		{
			_live2d->SetPosition(PosX, PosY);
		}
	}


	// 3D 空間への配置
	ImGui::Separator();
	if (ImGui::Checkbox("3D", &_use3D) && _live2d)
	{
		_live2d->SetCamera(_use3D ? _camera.get() : nullptr);
	}
	if (_use3D && _camera)
	{
		static bool billboard = true;
		static float rotationY = 0.0f;
		static float worldPosition[3] = { 0.0f, 0.0f, 0.0f };
		static float worldHeight = 1.6f;
		if (ImGui::Checkbox("Billboard", &billboard) && _live2d)
		{
			_live2d->SetBillboard(billboard);
		}
		if (!billboard && ImGui::SliderFloat("Rotation Y", &rotationY, -180.0f, 180.0f) && _live2d)
		{
			_live2d->SetWorldRotationY(rotationY);
		}
		if (ImGui::DragFloat3("World Pos", worldPosition, 0.01f) && _live2d)
		{
			_live2d->SetWorldPosition(worldPosition[0], worldPosition[1], worldPosition[2]);
		}
		if (ImGui::DragFloat("Height", &worldHeight, 0.01f, 0.1f, 10.0f) && _live2d)
		{
			_live2d->SetWorldHeight(worldHeight);
		}
		if (ImGui::Button("Reset Camera"))
		{
			_camera->Reset();
		}
		ImGui::TextUnformatted("L-drag: rotate  R-drag: move  Wheel: zoom");
	}

	ImGui::End();

	// カメラ操作（ImGui のウィンドウ上でないときだけ）
	if (_use3D && _camera)
	{
		ImGuiIO& io = ImGui::GetIO();
		if (!io.WantCaptureMouse)
		{
			if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
			{
				_camera->Orbit(-io.MouseDelta.x * 0.005f, io.MouseDelta.y * 0.005f);
			}
			if (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f) && io.DisplaySize.y > 0.0f)
			{
				// 1 ピクセルの移動が、注視点の距離で何 m に当たるか
				const float perPixel = 2.0f * std::tan(_camera->GetFovY() * 0.5f) / io.DisplaySize.y;
				_camera->Pan(-io.MouseDelta.x * perPixel, io.MouseDelta.y * perPixel);
			}
			if (io.MouseWheel != 0.0f)
			{
				_camera->Zoom(std::pow(0.9f, io.MouseWheel));
			}
		}
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
	_live2d.reset();
	_grid.reset();
	_camera.reset();
	if (_live2dStarted)
	{
		Live2D::Shutdown();
		_live2dStarted = false;
	}
}
