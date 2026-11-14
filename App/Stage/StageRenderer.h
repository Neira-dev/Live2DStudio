#pragma once
/*
* StageRenderer
* ステージの描画先と Stage Pass（ステージ全体にかける処理）
*
*   1. BeginActors … 1920x1080 のステージ用テクスチャ（透過・乗算済みアルファ）に切り替える
*                    ここに各モデル（Actor Pass 込み）を描く
*   2. EndActors   … ブルーム用に明るい所を取り出して縮小・ぼかす
*   3. Composite   … 影・コントラスト・LUT・ビネット・スポットライト・ブルーム・暗転をかけて、
*                    指定した背景色の上に描く（プレビューと出力ウィンドウで背景色を変えて2回呼ぶ）
*
* 注意：Cubism SDK のヘッダーは include しないこと
*/

#include <climits>
#include <d3d12.h>
#include <wrl/client.h>

#include "../Cubism/Live2DModel.h"  // Live2DRenderTarget
#include "StageLook.h"

class DX12CommandQueue;

struct StageCompositeParams
{
	float background[3] = { 0.0f, 0.0f, 0.0f };  // 背景色（プレビューは暗い灰色、出力はクロマキーの緑）
	float shadowOffset[2] = { 0.0f, 0.0f };      // 影のずれ（UV）
	float shadowStrength = 0.0f;
	float shadowSoftness = 0.003f;               // 影のぼけ（UV）
	float contrast = 0.0f;
	float lutStrength = 0.0f;
	float vignette = 0.0f;
	float bloomIntensity = 0.0f;
	float blackout = 0.0f;                       // 暗転（0〜1）
	float spotCenter[2] = { 0.0f, 0.0f };        // スポットライトの中心（NDC）
	float spotRadius = 0.5f;                     // スポットライトの半径（画面の高さ = 2 の単位）
	float spotAmount = 0.0f;                     // スポットライト（0〜1）
};

class StageRenderer
{
public:
	static constexpr UINT Width = 1920;
	static constexpr UINT Height = 1080;
	static constexpr UINT BloomDivisor = 4;
	static constexpr UINT LutSize = 32;

public:
	StageRenderer() = default;
	~StageRenderer();
	StageRenderer(const StageRenderer&) = delete;
	StageRenderer& operator=(const StageRenderer&) = delete;

	bool Initialize(ID3D12Device* device, DX12CommandQueue* commandQueue);
	void Release();

	/**
	 * @brief LUT を作り直す（変わったときだけ。GPU を待つので頻繁に呼ばない）
	 */
	void UpdateLut(ID3D12GraphicsCommandList* commandList, StageLut lut);

	/**
	 * @brief ステージ用テクスチャを描画先にしてクリアする
	 * @return モデルを描くときに Live2D::Draw に渡す描画先
	 */
	Live2DRenderTarget BeginActors(ID3D12GraphicsCommandList* commandList);

	/**
	 * @brief 半透明にする参加者を描くためのレイヤーに切り替える
	 * @details パーツごとに薄くすると奥のパーツが透けるため、1体分を描いてからまとめて薄くする
	 */
	Live2DRenderTarget BeginLayer(ID3D12GraphicsCommandList* commandList);

	/**
	 * @brief レイヤーを不透明度を掛けてステージに重ねる
	 */
	void EndLayer(ID3D12GraphicsCommandList* commandList, float opacity);

	/**
	 * @brief モデルを描き終えたら呼ぶ（ブルームの下準備）
	 */
	void EndActors(ID3D12GraphicsCommandList* commandList, float bloomThreshold);

	/**
	 * @brief Stage Pass をかけて描画先に描く
	 */
	void Composite(ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
		const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor, const StageCompositeParams& params);

private:
	struct Target
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> texture;
		UINT srvIndex = UINT_MAX;
		D3D12_CPU_DESCRIPTOR_HANDLE rtv = {};
		D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		UINT width = 0;
		UINT height = 0;
	};

	bool CreateTarget(Target& target, UINT width, UINT height, UINT rtvSlot);
	void ReleaseTarget(Target& target);
	void Transition(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource, D3D12_RESOURCE_STATES& state, D3D12_RESOURCE_STATES after);
	void DrawPass(ID3D12GraphicsCommandList* commandList, ID3D12PipelineState* pso, Target& output,
		const Target& source, const void* constants);

private:
	ID3D12Device* _device = nullptr;
	DX12CommandQueue* _commandQueue = nullptr;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _brightPso;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _blurHPso;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _blurVPso;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _compositePso;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _layerPso;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _rtvHeap;  // stage, bloomA, bloomB, layer
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _dsvHeap;
	Microsoft::WRL::ComPtr<ID3D12Resource> _depth;

	Target _stage;
	Target _bloomA;
	Target _bloomB;
	Target _layer;

	Microsoft::WRL::ComPtr<ID3D12Resource> _lut;
	Microsoft::WRL::ComPtr<ID3D12Resource> _lutUpload;
	UINT _lutSrvIndex = UINT_MAX;
	D3D12_RESOURCE_STATES _lutState = D3D12_RESOURCE_STATE_COPY_DEST;
	int _currentLut = -1;
};
