#pragma once
/*
* StageView
* ステージ画面の UI（RmlUi）。見た目は App/UI/stage.rml・stage.rcss
*
*   ・Stage の状態を RmlUi のデータモデル "stage" に映し、UI の操作を Stage に反映する
*   ・中央の #preview の位置からプレビューを描く範囲（16:9）を決める
*   ・プレビュー上のドラッグで参加者を移動、ホイールで拡大縮小
*/

#include <d3d12.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Rml
{
	class Context;
	class ElementDocument;
}
class Stage;
class OutputWindow;

class StageView
{
public:
	StageView();
	~StageView();
	StageView(const StageView&) = delete;
	StageView& operator=(const StageView&) = delete;

	/**
	 * @brief データモデルを作り、stage.rml を読み込む
	 */
	bool Initialize(Rml::Context* context, Stage& stage, OutputWindow* output);
	void Shutdown();

	/**
	 * @brief Stage の状態を UI に映す（毎フレーム、RmlUi の Update の前）
	 */
	void Update();

	/**
	 * @brief プレビューを描く範囲（前のフレームのレイアウトから決まる）
	 */
	const D3D12_VIEWPORT& GetPreviewViewport() const { return _previewViewport; }
	const D3D12_RECT& GetPreviewScissor() const { return _previewScissor; }
	void GetPreviewBackground(float rgb[3]) const;

	/**
	 * @brief プレビュー欄全体（16:9 の外の余白を含む）
	 */
	const D3D12_RECT& GetPreviewArea() const { return _previewArea; }

	/**
	 * @brief Live2D フォルダーの中のモデル（.model3.json を含むフォルダー）を探す
	 */
	static std::vector<std::filesystem::path> FindModels(const std::filesystem::path& root);

	/**
	 * @brief パスの表示名（UTF-8）
	 */
	static std::string ToUtf8(const std::filesystem::path& path);

private:
	struct Impl;
	std::unique_ptr<Impl> _impl;
	D3D12_VIEWPORT _previewViewport = { 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };
	D3D12_RECT _previewScissor = { 0, 0, 1, 1 };
	D3D12_RECT _previewArea = { 0, 0, 1, 1 };
};
