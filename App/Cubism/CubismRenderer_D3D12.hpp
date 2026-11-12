#pragma once
#include "Rendering/CubismRenderer.hpp"

namespace Live2D { namespace Cubism { namespace Framework { namespace Rendering {

/**
 * @brief D3D12 用 Cubism レンダラー
 * @details 現在はビルドを通すための空実装。描画処理は今後実装する。
 */
class CubismRenderer_D3D12 : public CubismRenderer
{
	friend class CubismRenderer;

public:
	~CubismRenderer_D3D12() override = default;

protected:
	CubismRenderer_D3D12(csmUint32 width, csmUint32 height);

	void DoDrawModel() override;
	void SaveProfile() override;
	void RestoreProfile() override;
	void BeforeDrawModelRenderTarget() override;
	void AfterDrawModelRenderTarget() override;
};

}}}}
