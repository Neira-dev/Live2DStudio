#pragma once
/*
* FloorGrid
* 3D 空間の床（Y = 0）にグリッド線を描く
*
*   1m ごとの細い線、5m ごとの太めの線、X 軸（赤）と Z 軸（青）。
*   カメラから離れるほど薄くなる。深度を書き込むので、床より下にある物は隠れる。
*/

#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl/client.h>

class FloorGrid
{
public:
	struct Desc
	{
		ID3D12Device* device = nullptr;
		DXGI_FORMAT renderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_D32_FLOAT;
		int halfLineCount = 20;     // 中心から片側に引く線の本数
		float spacing = 1.0f;       // 線の間隔（m）
		float fadeDistance = 18.0f; // この距離で見えなくなる
	};

public:
	FloorGrid() = default;
	~FloorGrid() = default;
	FloorGrid(const FloorGrid&) = delete;
	FloorGrid& operator=(const FloorGrid&) = delete;

	bool Initialize(const Desc& desc);

	/**
	 * @brief 現在の描画先に描く
	 * @param viewProjection カメラのビュー × プロジェクション
	 * @param cameraPosition カメラの位置（距離で薄くするのに使う）
	 */
	void Draw(ID3D12GraphicsCommandList* commandList, const DirectX::XMMATRIX& viewProjection, const DirectX::XMFLOAT3& cameraPosition);

	void Release();

private:
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _pso;
	Microsoft::WRL::ComPtr<ID3D12Resource> _vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW _vertexBufferView = {};
	UINT _vertexCount = 0;
	float _fadeDistance = 18.0f;
};
