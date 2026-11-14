#pragma once
/*
* CubismRenderer_D3D12
* Cubism SDK for Native の D3D12 用レンダラー
*
* SDK 同梱の D3D11 レンダラーを元に、以下に対応している
*  - 通常 / 加算 / 乗算 ブレンド
*  - クリッピングマスク（通常・反転・高精細マスク）
*  - カリング、乗算色 / スクリーン色、乗算済みアルファ
*
* Cubism 5.3 以降のオフスクリーン（パーツ単位のブレンド）と
* 拡張ブレンドモードには未対応。該当する Drawable は通常ブレンドで描画される。
*/

#include <d3d12.h>
#include <wrl/client.h>
#include <vector>

#include "Rendering/CubismRenderer.hpp"
#include "Rendering/CubismClippingManager.hpp"
#include "Type/csmMap.hpp"

namespace Live2D { namespace Cubism { namespace Framework { namespace Rendering {

class CubismRenderer_D3D12;
class CubismClippingContext_D3D12;

/**
 * @brief モデルの描画先（バックバッファ）の情報
 * @details マスク生成で描画先を切り替えた後、元に戻すために使う
 */
struct CubismRenderTargetBinding_D3D12
{
	D3D12_CPU_DESCRIPTOR_HANDLE rtv = {};
	D3D12_CPU_DESCRIPTOR_HANDLE dsv = {};
	bool hasDsv = false;
	D3D12_VIEWPORT viewport = {};
	D3D12_RECT scissor = {};
};

/**
 * @brief Actor Pass（モデルごとのライティング）の定数。HLSL の cbuffer と同じ並び
 */
struct CubismActorLook_D3D12
{
	float tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };    // rgb: 色の乗算, a: 彩度
	float light[4] = { 0.0f, 1.0f, 0.0f, 1.0f };   // xy: 光の来る方向（画面、Y 上向き）, z: 擬似ライトの強さ, w: 不透明度
	float rim[4] = { 1.0f, 1.0f, 1.0f, 0.0f };     // rgb: リムの色, a: 強さ
	float extra[4] = { 3.0f, 0.0f, 0.0f, 0.0f };   // x: リムの幅（ピクセル）
	float bounds[4] = { 0.0f, 0.0f, 1.0f, 1.0f };  // xy: モデルの中心（NDC）, zw: 半分の幅・高さ（NDC）
};

/**
 * @brief クリッピングマスク用のレンダーターゲット
 */
class CubismMaskTarget_D3D12
{
public:
	CubismMaskTarget_D3D12() = default;
	~CubismMaskTarget_D3D12();
	CubismMaskTarget_D3D12(const CubismMaskTarget_D3D12&) = delete;
	CubismMaskTarget_D3D12& operator=(const CubismMaskTarget_D3D12&) = delete;

	bool Create(ID3D12Device* device, UINT width, UINT height);
	void Release();

	/**
	 * @brief 描画先をこのマスクに切り替える
	 */
	void BeginDraw(ID3D12GraphicsCommandList* commandList);

	/**
	 * @brief シェーダーから読める状態に戻す
	 */
	void EndDraw(ID3D12GraphicsCommandList* commandList);

	/**
	 * @brief 指定色でクリアする（BeginDraw 中のみ）
	 */
	void Clear(ID3D12GraphicsCommandList* commandList, float r, float g, float b, float a);

	UINT GetWidth() const { return _width; }
	UINT GetHeight() const { return _height; }
	D3D12_GPU_DESCRIPTOR_HANDLE GetSrv() const;

private:
	Microsoft::WRL::ComPtr<ID3D12Resource> _texture;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _rtvHeap;
	UINT _srvIndex = UINT_MAX;
	UINT _width = 0;
	UINT _height = 0;
	D3D12_RESOURCE_STATES _state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
};

/**
 * @brief クリッピングマスクの管理
 */
class CubismClippingManager_D3D12 : public CubismClippingManager<CubismClippingContext_D3D12, CubismMaskTarget_D3D12>
{
public:
	/**
	 * @brief 使用するマスクをまとめてマスクテクスチャに描画する
	 */
	void SetupClippingContext(CubismModel& model, CubismRenderer_D3D12* renderer);

	/**
	 * @brief マスクの種類数（同じ組み合わせのマスクは1つにまとめられる）
	 */
	csmUint32 GetClippingContextCount() const { return _clippingContextListForMask.GetSize(); }
};

/**
 * @brief 1つのクリッピングマスクの情報
 */
class CubismClippingContext_D3D12 : public CubismClippingContext
{
public:
	using ManagerType = CubismClippingManager<CubismClippingContext_D3D12, CubismMaskTarget_D3D12>;

	CubismClippingContext_D3D12(ManagerType* manager, CubismModel& model, const csmInt32* clippingDrawableIndices, csmInt32 clipCount);
	~CubismClippingContext_D3D12() = default;

	ManagerType* GetClippingManager() { return _owner; }

private:
	ManagerType* _owner;
};

/**
 * @brief D3D12 用 Cubism レンダラー
 */
class CubismRenderer_D3D12 : public CubismRenderer
{
	friend class CubismRenderer;
	friend class CubismClippingManager_D3D12;

public:
	/**
	 * @brief 全モデル共通のリソース（ルートシグネチャ・PSO）を作成する
	 * @param device D3D12 デバイス
	 * @param renderTargetFormat 描画先のフォーマット
	 * @param depthStencilFormat 描画時にバインドされている DSV のフォーマット（無い場合は UNKNOWN）
	 * @param frameCount 同時に処理中になり得るフレーム数
	 */
	static bool InitializeShared(ID3D12Device* device, DXGI_FORMAT renderTargetFormat, DXGI_FORMAT depthStencilFormat, UINT frameCount);

	/**
	 * @brief 全モデル共通のリソースを解放する
	 */
	static void ReleaseShared();

	void Initialize(CubismModel* model) override;
	void Initialize(CubismModel* model, csmInt32 maskBufferCount) override;

	/**
	 * @brief モデルのテクスチャ番号に SRV を割り当てる
	 */
	void BindTexture(csmUint32 modelTextureIndex, D3D12_GPU_DESCRIPTOR_HANDLE srv);

	/**
	 * @brief 描画に使うコマンドリストと描画先を設定する（DrawModel の直前に呼ぶ）
	 */
	void SetDrawContext(ID3D12GraphicsCommandList* commandList, const CubismRenderTargetBinding_D3D12& target);

	/**
	 * @brief クリッピングマスクのバッファサイズを設定する
	 */
	void SetClippingMaskBufferSize(csmFloat32 width, csmFloat32 height);

	/**
	 * @brief Actor Pass（モデルごとのライティング）の値を設定する
	 */
	void SetActorLook(const CubismActorLook_D3D12& look) { _actorLook = look; }

protected:
	CubismRenderer_D3D12(csmUint32 width, csmUint32 height);
	~CubismRenderer_D3D12() override;

	void DoDrawModel() override;
	void SaveProfile() override {}
	void RestoreProfile() override {}
	void BeforeDrawModelRenderTarget() override {}
	void AfterDrawModelRenderTarget() override {}

private:
	// CPU から毎フレーム書き込むデータ（頂点・定数）用のアップロードバッファ
	struct UploadSlot
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
		std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> retired; // 容量不足で差し替えた古いバッファ
		UINT8* cpu = nullptr;
		D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
		UINT64 size = 0;
		UINT64 offset = 0;
	};

	bool CreateUploadBuffer(UploadSlot& slot, UINT64 size);
	bool AllocateUpload(UINT64 size, UINT64 alignment, void** cpu, D3D12_GPU_VIRTUAL_ADDRESS* gpu);
	bool CreateIndexBuffer();
	bool CreateMaskTargets();

	void DrawDrawable(CubismModel& model, csmInt32 drawableIndex);
	void DrawMesh(CubismModel& model, csmInt32 drawableIndex);
	bool UploadVertices(const CubismModel& model, csmInt32 drawableIndex, D3D12_VERTEX_BUFFER_VIEW* view);
	void RestoreMainTarget();

	D3D12_GPU_DESCRIPTOR_HANDLE GetTextureSrv(const CubismModel& model, csmInt32 drawableIndex) const;
	CubismMaskTarget_D3D12* GetMaskTarget(csmInt32 index);
	csmBool IsGeneratingMask() const { return _clippingContextBufferForMask != nullptr; }

	ID3D12GraphicsCommandList* _commandList = nullptr;
	CubismRenderTargetBinding_D3D12 _mainTarget;
	CubismActorLook_D3D12 _actorLook;

	std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> _textures;

	CubismClippingManager_D3D12* _clippingManager = nullptr;
	std::vector<CubismMaskTarget_D3D12*> _maskTargets;
	CubismClippingContext_D3D12* _clippingContextBufferForMask = nullptr;
	CubismClippingContext_D3D12* _clippingContextBufferForDraw = nullptr;

	// インデックスはモデル読み込み時に一度だけ作成する
	Microsoft::WRL::ComPtr<ID3D12Resource> _indexBuffer;
	std::vector<UINT64> _indexOffsets;

	std::vector<UploadSlot> _uploadSlots;
	UINT _currentSlot = 0;
	std::vector<D3D12_GPU_VIRTUAL_ADDRESS> _uploadedVertices; // 今フレームで転送済みの頂点（0 = 未転送）

	std::vector<csmInt32> _sortedDrawableIndices;
	bool _warnedOffscreen = false;
};

}}}}
