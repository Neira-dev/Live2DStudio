#include "Live2D.h"
#include "Live2DModel.h"

#include "../../DirectX12/DX12Device.h"
#include "../../DirectX12/DX12CommandQueue.h"
#include "../../DirectX12/DX12Renderer.h"

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

	_model->Draw(commandList, target);
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
