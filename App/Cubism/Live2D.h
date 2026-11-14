#pragma once
/*
* Live2D
* Live2D モデルを1体読み込んで表示するクラス
*
* 使い方：
*   Live2D::StartUp(device, commandQueue, renderer);   // 最初に1回
*   auto model = std::make_unique<Live2D>();
*   model->Load(L"Live2D/DeepSeek");                    // フォルダー or .model3.json
*   model->Update(deltaSeconds);                        // 毎フレーム
*   model->Draw(commandList);                           // 毎フレーム（BeginFrame〜EndFrame の間）
*   model->SetCamera(&camera);                          // 3D 空間に置く（nullptr で 2D に戻る）
*   model->SetWorldPosition(0, 0, 0);                   // 足元の位置
*   model->SetBillboard(true);                          // カメラの方を向く（false で SetWorldRotationY の向き）
*   model.reset();
*   Live2D::Shutdown();                                 // 最後に1回（モデルを全て破棄した後）
*/

#include <d3d12.h>
#include <filesystem>
#include <memory>

#include "Live2DModel.h"  // Live2DRenderTarget, Live2DActorLook（Cubism SDK には依存しない）

class DX12Device;
class DX12CommandQueue;
class DX12Renderer;
class Live2DModel;
class Camera;

class Live2D
{
public:
	/**
	 * @brief Cubism Framework と描画用の共通リソースを初期化する
	 * @param device DX12 デバイス
	 * @param commandQueue テクスチャ転送に使うコマンドキュー
	 * @param renderer 描画先（バックバッファ）の取得に使うレンダラー
	 * @return 成功したら true
	 */
	static bool StartUp(DX12Device* device, DX12CommandQueue* commandQueue, DX12Renderer* renderer);

	/**
	 * @brief 終了処理（全ての Live2D インスタンスを破棄した後に呼ぶ）
	 */
	static void Shutdown();

public:
	Live2D();
	~Live2D();
	Live2D(const Live2D&) = delete;
	Live2D& operator=(const Live2D&) = delete;

	/**
	 * @brief モデルを読み込む
	 * @param path .model3.json のパス、またはそれが入ったフォルダー
	 * @return 成功したら true
	 */
	bool Load(const std::filesystem::path& path);

	/**
	 * @brief .motion3.json を再生する
	 * @param path モーションファイルのパス
	 * @param loop ループ再生するなら true
	 */
	bool PlayMotion(const std::filesystem::path& path, bool loop = false);

	/**
	 * @brief 再生中のモーションを停止する
	 */
	void StopMotion();

	/**
	 * @brief モーション・目パチ・呼吸・物理演算を更新する
	 * @param deltaSeconds 前フレームからの経過秒数
	 */
	void Update(float deltaSeconds);

	/**
	 * @brief 現在の描画先に描画する
	 * @param commandList DX12Renderer::GetCommandList() のコマンドリスト
	 */
	void Draw(ID3D12GraphicsCommandList* commandList);

	/**
	 * @brief 指定した描画先（オフスクリーンなど）に描画する
	 * @param target 描画先。フォーマットは R8G8B8A8_UNORM、深度は D32_FLOAT（同じ大きさ）
	 */
	void Draw(ID3D12GraphicsCommandList* commandList, const Live2DRenderTarget& target);

	/**
	 * @brief Actor Pass（モデルごとのライティング）を設定する
	 */
	void SetActorLook(const Live2DActorLook& look);

	/**
	 * @brief 最後に描いたときの、モデルを囲む範囲（NDC、Y は上向き）
	 * @return まだ描いていなければ false
	 */
	bool GetScreenBounds(float& left, float& right, float& bottom, float& top) const;

	/**
	 * @brief テクスチャの平均色（0〜1）
	 */
	void GetAverageColor(float rgb[3]) const;

	/**
	 * @brief 表示位置（画面中央が 0,0。上下の端が ±1）
	 */
	void SetPosition(float x, float y);

	/**
	 * @brief 拡大率（1.0 で画面の高さに収まる大きさ）
	 */
	void SetScale(float scale);

	/**
	 * @brief 読み込み済みか
	 */
	bool IsLoaded() const;

	// ---- 3D 空間への配置 ----

	/**
	 * @brief 3D 表示に使うカメラ（nullptr なら従来の 2D 表示）
	 * @details カメラは Live2D より長く生存させること
	 */
	void SetCamera(const Camera* camera) { _camera = camera; }

	/**
	 * @brief 足元の位置（m、Y = 0 が床）
	 */
	void SetWorldPosition(float x, float y, float z);

	/**
	 * @brief 向き（Y 軸回りの角度、度）。ビルボードが無効のときに使う
	 */
	void SetWorldRotationY(float degrees) { _worldRotationY = degrees; }

	/**
	 * @brief 3D 空間での身長（m）
	 */
	void SetWorldHeight(float height) { _worldHeight = height > 0.01f ? height : 0.01f; }

	/**
	 * @brief 常にカメラの方を向くか（Y 軸回りだけ回転するので、立ったまま向きが変わる）
	 */
	void SetBillboard(bool enable) { _billboard = enable; }

private:
	std::unique_ptr<Live2DModel> _model;

	// 3D
	const Camera* _camera = nullptr;
	float _worldPosition[3] = { 0.0f, 0.0f, 0.0f };
	float _worldRotationY = 0.0f;
	float _worldHeight = 1.6f;
	bool _billboard = true;
};
