#pragma once
/*
* Live2DModel
* Cubism SDK を使ったモデルの実装（Live2D クラスの内部実装）
*
* 注意：このヘッダーでは Cubism SDK のヘッダーを include しないこと。
*       SDK は namespace Live2D を使うため、class Live2D と同じ翻訳単位に入ると名前が衝突する。
*/

#include <d3d12.h>
#include <filesystem>
#include <memory>

/**
 * @brief モデルの描画先
 */
struct Live2DRenderTarget
{
	D3D12_CPU_DESCRIPTOR_HANDLE rtv = {};
	D3D12_CPU_DESCRIPTOR_HANDLE dsv = {};
	bool hasDsv = false;
	D3D12_VIEWPORT viewport = {};
	D3D12_RECT scissor = {};
};

/**
 * @brief Cubism Framework と GPU 共通リソースの初期化・終了
 */
class Live2DSystem
{
public:
	struct Desc
	{
		ID3D12Device* device = nullptr;
		ID3D12CommandQueue* commandQueue = nullptr;
		DXGI_FORMAT renderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_UNKNOWN;
		UINT frameCount = 2;
	};

	static bool StartUp(const Desc& desc);
	static void Shutdown();
	static bool IsStarted();
};

/**
 * @brief 1体のモデル
 */
class Live2DModel
{
public:
	static std::unique_ptr<Live2DModel> Create();
	virtual ~Live2DModel() = default;

	/**
	 * @brief モデルを読み込む
	 * @param path .model3.json のパス、またはそれが入ったフォルダー
	 */
	virtual bool Load(const std::filesystem::path& path) = 0;

	/**
	 * @brief .motion3.json を再生する
	 */
	virtual bool PlayMotion(const std::filesystem::path& path, bool loop) = 0;

	/**
	 * @brief 再生中のモーションを停止する
	 */
	virtual void StopMotion() = 0;

	/**
	 * @brief パラメータ・物理演算などを更新する
	 */
	virtual void Update(float deltaSeconds) = 0;

	/**
	 * @brief 描画コマンドを積む
	 */
	virtual void Draw(ID3D12GraphicsCommandList* commandList, const Live2DRenderTarget& target) = 0;

	virtual void SetPosition(float x, float y) = 0;
	virtual void SetScale(float scale) = 0;

	/**
	 * @brief 3D 表示用の行列を設定する
	 * @param matrix モデルの座標（GetModelBounds と同じ単位）からクリップ空間への行列。
	 *               列ベクトル用・列優先の 16 要素（DirectXMath の XMFLOAT4X4 をそのまま渡せる）。
	 *               nullptr なら 2D 表示（SetPosition / SetScale）に戻す。
	 */
	virtual void SetWorldViewProjection(const float* matrix) = 0;

	/**
	 * @brief 読み込み時のポーズでの、全パーツを囲む範囲（モデルの座標、Y は上向き）
	 */
	virtual void GetModelBounds(float& left, float& right, float& bottom, float& top) const = 0;
	virtual bool IsLoaded() const = 0;
};
