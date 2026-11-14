#pragma once
/*
* StageLook
* ルーム単位のルック（ライティングと色調）
*
*   基本：プリセット3種 ＋ スライダー3本（明るさ・暖かさ・ムード）
*   詳細：光源の向き・擬似ライト・影・リムライト・ブルーム・LUT・ビネット
*
* ルックは2つに分けてかける（二重にかからないように）。
*   Actor Pass … モデルごと（色味・彩度・擬似ライト・リム）。CubismRenderer_D3D12 のシェーダーで行う
*   Stage Pass … ステージ全体（影・ブルーム・コントラスト・LUT・ビネット）。StageRenderer で行う
*/

#include <cstdint>
#include <vector>

#include "../Cubism/Live2DLook.h"

enum class StagePreset
{
	Natural,  // ナチュラル
	Sunset,   // 夕暮れ
	Night,    // ナイト
	Count
};

enum class StageLut
{
	Neutral,  // なし
	Film,     // フィルム
	Cool,     // クール
	Warm,     // ウォーム
	Faded,    // フェード
	Count
};

struct StageLook
{
	// ---- 基本 ----
	StagePreset preset = StagePreset::Natural;
	float brightness = 0.0f;  // 明るさ（-1〜1）
	float warmth = 0.0f;      // 暖かさ（-1〜1）
	float mood = 0.0f;        // ムード（0〜1。上げるほど暗く、コントラストと縁の光が強くなる）

	// ---- 詳細 ----
	float lightAngle = 135.0f;      // 光の来る方向（度。0 = 右、90 = 上）
	float lightStrength = 0.3f;     // 擬似ライト（0〜1）
	float shadowStrength = 0.3f;    // 影の濃さ（0〜1）
	float shadowDistance = 0.015f;  // 影の距離（画面の高さに対する割合）
	float rimColor[3] = { 1.0f, 1.0f, 1.0f };
	float rimStrength = 0.25f;      // リムライト（0〜1）
	float rimWidth = 3.0f;          // リムの幅（ステージ解像度でのピクセル）
	float bloomThreshold = 0.8f;    // ブルームがかかり始める明るさ
	float bloomIntensity = 0.2f;    // ブルームの強さ
	StageLut lut = StageLut::Neutral;
	float lutStrength = 1.0f;
	float vignette = 0.15f;         // ビネット（0〜1）

	/**
	 * @brief プリセットの値で上書きする
	 */
	void ApplyPreset(StagePreset preset);

	/**
	 * @brief モデルごとの Actor Pass の値を作る
	 * @param normalize 「見た目を揃える」の補正（色ごとの倍率）
	 */
	Live2DActorLook MakeActorLook(const float normalize[3]) const;

	/**
	 * @brief Stage Pass で使うコントラスト（ムードから決まる）
	 */
	float GetContrast() const { return 0.25f * mood; }

	/**
	 * @brief Stage Pass で使うビネット（ムードで強くなる）
	 */
	float GetVignette() const;

	/**
	 * @brief 光の来る方向（画面、Y 上向き、正規化済み）
	 */
	void GetLightDirection(float& x, float& y) const;
};

/**
 * @brief 3D LUT（size^3、RGBA8）を作る
 */
std::vector<uint8_t> BuildStageLut(StageLut lut, int size);

const char* GetStagePresetName(StagePreset preset);
const char* GetStageLutName(StageLut lut);
