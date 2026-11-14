#pragma once
/*
* StageUI
* ステージ画面の UI（ImGui）
*
*   上部：配置テンプレ・招待コード・演出・出力ウィンドウ
*   左　：参加者一覧（Tier・在席状態・回線品質）と追加
*   右　：ルックパネル（プリセット・スライダー・詳細・ホストロック・見た目を揃える）
*   中央：プレビュー（参加者をドラッグで移動、ホイールで拡大縮小）
*/

#include <d3d12.h>
#include <filesystem>
#include <string>
#include <vector>

class Stage;
class OutputWindow;

class StageUI
{
public:
	/**
	 * @brief 日本語フォントを読み込む（最初の ImGui フレームより前に呼ぶ）
	 */
	void Initialize();

	/**
	 * @brief UI を描く（ImGui のフレーム内で呼ぶ）
	 */
	void Draw(Stage& stage, OutputWindow& output);

	/**
	 * @brief プレビューを描く範囲（前のフレームの UI の配置から決まる）
	 */
	const D3D12_VIEWPORT& GetPreviewViewport() const { return _previewViewport; }
	const D3D12_RECT& GetPreviewScissor() const { return _previewScissor; }

	/**
	 * @brief プレビューの背景色
	 */
	void GetPreviewBackground(float rgb[3]) const;

	/**
	 * @brief Live2D フォルダーの中のモデル（.model3.json を含むフォルダー）を探す
	 */
	static std::vector<std::filesystem::path> FindModels(const std::filesystem::path& root);

	/**
	 * @brief パスの表示名（UTF-8）
	 */
	static std::string ToUtf8(const std::filesystem::path& path);

private:
	void DrawTopBar(Stage& stage, OutputWindow& output, float width, float height);
	void DrawParticipants(Stage& stage, float x, float y, float width, float height);
	void DrawLookPanel(Stage& stage, float x, float y, float width, float height);
	void DrawPreview(Stage& stage, float x, float y, float width, float height);

private:
	D3D12_VIEWPORT _previewViewport = { 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };
	D3D12_RECT _previewScissor = { 0, 0, 1, 1 };
	bool _previewChroma = false;
	std::string _roomCode;
	std::vector<std::filesystem::path> _models;
	int _modelChoice = 0;
	bool _dragging = false;
	OutputWindow* _output = nullptr;
};
