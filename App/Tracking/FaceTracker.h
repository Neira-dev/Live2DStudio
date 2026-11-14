#pragma once
/*
* FaceTracker
* MediaPipe Face Landmarker（ONNX に変換したもの）を Windows ML で動かす
*
*   1. 顔検出（face_detector）       … 顔が見つかっていないときだけ
*   2. 顔のランドマーク 478 点        … 前のフレームの顔の位置から切り出して推定
*   3. 表情 52 種（ブレンドシェイプ） … ランドマークから推定
*   4. 頭の向き                       … ランドマークから計算
*
* 1 枚の画像を Process に渡すと結果が返る（呼び出しは同じスレッドから行うこと）。
*/

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

struct FaceTrackingResult
{
	static constexpr int LandmarkCount = 478;
	static constexpr int BlendshapeCount = 52;

	bool found = false;
	float score = 0.0f;
	std::array<float, LandmarkCount * 3> landmarks = {};   // 画像のピクセル座標（x, y, z）
	std::array<float, BlendshapeCount> blendshapes = {};   // 0〜1（並びは GetBlendshapeName）
	float yaw = 0.0f;    // 頭の向き（度）。画像の右を向くと正
	float pitch = 0.0f;  // 上を向くと正
	float roll = 0.0f;   // 画像上で反時計回りに傾くと正
	float processMilliseconds = 0.0f;
};

class FaceTracker
{
public:
	FaceTracker();
	~FaceTracker();

	/**
	 * @brief モデルを読み込む（face_detector / face_landmarks_detector / face_blendshapes の .onnx）
	 */
	bool Initialize(const std::filesystem::path& modelDirectory, std::string& error);

	/**
	 * @brief 1 枚の画像を処理する
	 * @param bgra 32bit BGRA の画像（上から下）
	 * @param stride 1 行のバイト数
	 */
	FaceTrackingResult Process(const uint8_t* bgra, int width, int height, int stride);

	/**
	 * @brief 次のフレームで顔検出からやり直す
	 */
	void Reset();

	static const char* GetBlendshapeName(int index);

	/**
	 * @brief 名前からブレンドシェイプの番号を引く（無ければ -1）
	 */
	static int FindBlendshape(const char* name);

private:
	struct Impl;
	std::unique_ptr<Impl> _impl;
};
