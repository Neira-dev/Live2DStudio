#include "CubismRenderer_D3D12.hpp"

namespace Live2D { namespace Cubism { namespace Framework { namespace Rendering {

// CubismUserModel::CreateRenderer() から呼ばれる生成関数
CubismRenderer* CubismRenderer::Create(csmUint32 width, csmUint32 height)
{
	return CSM_NEW CubismRenderer_D3D12(width, height);
}

// CubismFramework::Dispose() から呼ばれる共通リソースの解放
void CubismRenderer::StaticRelease()
{
}

CubismRenderer_D3D12::CubismRenderer_D3D12(csmUint32 width, csmUint32 height)
	: CubismRenderer(width, height)
{
}

void CubismRenderer_D3D12::DoDrawModel()
{
	// TODO: D3D12 での描画処理
}

void CubismRenderer_D3D12::SaveProfile()
{
}

void CubismRenderer_D3D12::RestoreProfile()
{
}

void CubismRenderer_D3D12::BeforeDrawModelRenderTarget()
{
}

void CubismRenderer_D3D12::AfterDrawModelRenderTarget()
{
}

}}}}
