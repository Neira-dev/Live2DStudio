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
*   model.reset();
*   Live2D::Shutdown();                                 // 最後に1回（モデルを全て破棄した後）
*/

#include <d3d12.h>
#include <filesystem>
#include <memory>

class DX12Device;
class DX12CommandQueue;
class DX12Renderer;
class Live2DModel;

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

private:
	std::unique_ptr<Live2DModel> _model;
};
