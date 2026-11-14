#include "Live2D.h"
#include "Live2DModel.h"

#include "../../DirectX12/DX12Device.h"
#include "../../DirectX12/DX12CommandQueue.h"
#include "../../DirectX12/DX12Renderer.h"

#include "../Camera.h"

#include <DirectXMath.h>
#include <cmath>

namespace
{
	DX12Renderer* g_renderer = nullptr;
}

bool Live2D::StartUp(DX12Device* device, DX12CommandQueue* commandQueue, DX12Renderer* renderer)
{
	if (device == nullptr || commandQueue == nullptr || renderer == nullptr)
	{
		return false;
	}

	Live2DSystem::Desc desc;
	desc.device = device->GetDevice();
	desc.commandQueue = commandQueue->GetCommandQueue();
	desc.renderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.depthStencilFormat = DX12Renderer::GetDepthStencilFormat();
	desc.frameCount = renderer->GetFrameCount();

	if (!Live2DSystem::StartUp(desc))
	{
		return false;
	}
	g_renderer = renderer;
	return true;
}

void Live2D::Shutdown()
{
	Live2DSystem::Shutdown();
	g_renderer = nullptr;
}

Live2D::Live2D()
	: _model(Live2DModel::Create())
{
}

Live2D::~Live2D() = default;

bool Live2D::Load(const std::filesystem::path& path)
{
	return _model->Load(path);
}

bool Live2D::PlayMotion(const std::filesystem::path& path, bool loop)
{
	return _model->PlayMotion(path, loop);
}

void Live2D::StopMotion()
{
	if (_model) { _model->StopMotion(); }
}

void Live2D::Update(float deltaSeconds)
{
	_model->Update(deltaSeconds);
}

void Live2D::Draw(ID3D12GraphicsCommandList* commandList)
{
	if (g_renderer == nullptr)
	{
		return;
	}

	// マスク生成で描画先を切り替えた後に戻すため、現在の描画先を渡す
	Live2DRenderTarget target;
	target.rtv = g_renderer->GetCurrentRTV();
	target.dsv = g_renderer->GetDSV();
	target.hasDsv = true;
	target.viewport = g_renderer->GetViewport();
	target.scissor = g_renderer->GetScissorRect();
	Draw(commandList, target);
}

void Live2D::Draw(ID3D12GraphicsCommandList* commandList, const Live2DRenderTarget& target)
{
	if (_camera != nullptr && target.viewport.Height > 0.0f)
	{
		using namespace DirectX;

		// ローカル：足元の中心を原点にし、高さを _worldHeight（m）にする
		float left, right, bottom, top;
		_model->GetModelBounds(left, right, bottom, top);
		const float unit = _worldHeight / (top - bottom);
		const XMMATRIX local = XMMatrixTranslation(-(left + right) * 0.5f, -bottom, 0.0f) * XMMatrixScaling(unit, unit, unit);

		// 向き：ビルボードならカメラの方を向くよう Y 軸回りに回す（モデルの正面は -Z）
		const XMFLOAT3 cameraPosition = _camera->GetPosition();
		float yaw = XMConvertToRadians(_worldRotationY);
		if (_billboard)
		{
			const float dx = cameraPosition.x - _worldPosition[0];
			const float dz = cameraPosition.z - _worldPosition[2];
			if (dx * dx + dz * dz > 1e-6f)
			{
				yaw = std::atan2(-dx, -dz);
			}
		}
		const XMMATRIX world = local * XMMatrixRotationY(yaw)
			* XMMatrixTranslation(_worldPosition[0], _worldPosition[1], _worldPosition[2]);

		const float aspect = target.viewport.Width / target.viewport.Height;
		const XMMATRIX worldViewProjection = world * _camera->GetViewProjection(aspect);

		// DirectXMath（行ベクトル用・行優先）の並びは、列ベクトル用・列優先の行列と同じ並びになる
		XMFLOAT4X4 matrix;
		XMStoreFloat4x4(&matrix, worldViewProjection);
		_model->SetWorldViewProjection(&matrix._11);
	}
	else
	{
		_model->SetWorldViewProjection(nullptr);
	}

	_model->Draw(commandList, target);
}

void Live2D::SetActorLook(const Live2DActorLook& look)
{
	_model->SetActorLook(look);
}

bool Live2D::GetScreenBounds(float& left, float& right, float& bottom, float& top) const
{
	return _model->GetScreenBounds(left, right, bottom, top);
}

void Live2D::GetAverageColor(float rgb[3]) const
{
	_model->GetAverageColor(rgb);
}

void Live2D::SetTrackingParameters(const std::vector<std::pair<std::string, float>>& values)
{
	_model->SetTrackingParameters(values);
}

void Live2D::SetWorldPosition(float x, float y, float z)
{
	_worldPosition[0] = x;
	_worldPosition[1] = y;
	_worldPosition[2] = z;
}

void Live2D::SetPosition(float x, float y)
{
	_model->SetPosition(x, y);
}

void Live2D::SetScale(float scale)
{
	_model->SetScale(scale);
}

bool Live2D::IsLoaded() const
{
	return _model->IsLoaded();
}
