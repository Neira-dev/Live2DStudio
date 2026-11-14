#pragma once
/*
* Stage
* CoStage のステージ（ローカル版）
*
*   ・モデル（参加者）を最大3体まで並べる。配置テンプレ＋ドラッグで微調整
*   ・ルーム単位のルック（Actor Pass はモデルごと、Stage Pass は全体）
*   ・離席検知（状態機械）と離席演出。回線断は映像を止めて半透明にする
*   ・演出（暗転・スポットライト）
*   ・描画結果をプレビュー（メイン画面）と出力ウィンドウ（クロマキー背景）に出す
*
* 通信とトラッキングは未実装。参加者の「顔ロスト」「回線断」は UI のテスト用スイッチで入れる。
*/

#include <d3d12.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "PresenceStateMachine.h"
#include "StageLook.h"
#include "StageRenderer.h"

class Live2D;
class DX12CommandQueue;

enum class StageLayout
{
	SideBySide,  // 横並び
	Tiered,      // ひな壇
	RoundTable,  // 円卓
	Count
};

enum class ActorTier
{
	Self,  // 自分（ローカルで描画）
	S,     // Tier S：送信側でルック適用済みの映像（通信実装後）
	P,     // Tier P：パラメータ同期で受信側が描画（通信実装後）
};

struct StageActor
{
	std::string name;                      // 表示名（UTF-8）
	std::filesystem::path modelPath;
	std::unique_ptr<Live2D> live2d;
	ActorTier tier = ActorTier::Self;

	// 配置（x: 画面の高さ = 2 の単位で左右 ±1.78、y: 上下 ±1、scale: 1 で画面の高さ）
	float x = 0.0f;
	float y = 0.0f;
	float scale = 0.9f;

	PresenceStateMachine presence;
	bool testFaceLost = false;     // テスト用：顔ロスト
	bool testDisconnected = false; // テスト用：回線断

	// フェイストラッキング（動作中は顔が映っているかを離席検知に使う）
	bool trackingActive = false;
	bool trackingFace = false;

	float normalize[3] = { 1.0f, 1.0f, 1.0f };  // 「見た目を揃える」の補正
};

class Stage
{
public:
	static constexpr size_t MaxActors = 3;

public:
	Stage();
	~Stage();

	bool Initialize(ID3D12Device* device, DX12CommandQueue* commandQueue);
	void Release();

	// ---- 参加者 ----
	bool AddActor(const std::filesystem::path& modelPath, const std::string& name);
	void RemoveActor(size_t index);
	size_t GetActorCount() const { return _actors.size(); }
	StageActor& GetActor(size_t index) { return *_actors[index]; }

	int GetSelected() const { return _selected; }
	void SetSelected(int index) { _selected = index; }

	// ---- 配置 ----
	void ApplyLayout(StageLayout layout);
	StageLayout GetLayout() const { return _layout; }

	/**
	 * @brief 画面上の点（NDC）にいる参加者（手前を優先）。いなければ -1
	 */
	int HitTest(float ndcX, float ndcY) const;

	/**
	 * @brief ドラッグで動かす（NDC での移動量）
	 */
	void MoveActor(size_t index, float deltaNdcX, float deltaNdcY);

	/**
	 * @brief 大きさを変える
	 */
	void ScaleActor(size_t index, float factor);

	// ---- ルック ----
	StageLook& GetLook() { return _look; }
	bool IsHostLocked() const { return _hostLock; }
	void SetHostLocked(bool locked) { _hostLock = locked; }

	/**
	 * @brief 「見た目を揃える」：各モデルの平均色から明るさと色味の補正を求める
	 */
	void NormalizeLooks();
	void ResetNormalize();
	bool IsNormalized() const { return _normalized; }

	// ---- 演出 ----
	void ToggleBlackout() { _blackoutOn = !_blackoutOn; }
	void ToggleSpotlight() { _spotlightOn = !_spotlightOn; }
	bool IsBlackout() const { return _blackoutOn; }
	bool IsSpotlight() const { return _spotlightOn; }

	/**
	 * @brief 自分（先頭の参加者）の手動離席を切り替える
	 */
	void ToggleManualAway();

	// ---- 更新・描画 ----
	void Update(float deltaSeconds);

	/**
	 * @brief 参加者をステージ用テクスチャに描く（毎フレーム、Composite より前に1回）
	 */
	void Render(ID3D12GraphicsCommandList* commandList);

	/**
	 * @brief 仕上げて描画先に描く
	 * @param background 背景色（rgb）
	 */
	void Composite(ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
		const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor, const float background[3]);

private:
	std::vector<size_t> GetDrawOrder() const;

private:
	StageRenderer _renderer;
	std::vector<std::unique_ptr<StageActor>> _actors;
	StageLayout _layout = StageLayout::SideBySide;
	int _selected = -1;

	StageLook _look;
	bool _hostLock = false;
	bool _normalized = false;

	bool _blackoutOn = false;
	bool _spotlightOn = false;
	float _blackout = 0.0f;   // 演出の進み具合（なめらかに変化させる）
	float _spotlight = 0.0f;
	float _spotCenter[2] = { 0.0f, 0.0f };
	float _spotRadius = 0.6f;
};

const char* GetStageLayoutName(StageLayout layout);
const char* GetActorTierName(ActorTier tier);
