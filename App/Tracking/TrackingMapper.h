#pragma once
/*
* TrackingMapper
* 顔の推定結果を Live2D の標準パラメータに変換する
*
*   頭の向き → ParamAngleX/Y/Z（と体を少し）
*   まばたき → ParamEyeLOpen / ParamEyeROpen
*   視線     → ParamEyeBallX / ParamEyeBallY
*   眉       → ParamBrowLY / ParamBrowRY
*   口       → ParamMouthOpenY / ParamMouthForm
*
* 向きは鏡のように動く（自分が左を向くとモデルも自分の左を向く）。
* キャリブレーションで「正面」と「普段の目の開き」を覚える。
*/

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "FaceTracker.h"

using TrackingParameters = std::vector<std::pair<std::string, float>>;

class TrackingMapper
{
public:
	/**
	 * @brief 次の結果で正面を覚え直す
	 */
	void RequestCalibration() { _calibrateRequested = true; }
	bool IsCalibrated() const { return _calibrated; }

	/**
	 * @brief 結果をパラメータに変換する（顔が無ければ空）
	 */
	TrackingParameters Map(const FaceTrackingResult& result, float deltaSeconds);

	void Reset();

private:
	float Smooth(const char* id, float target, float speed, float deltaSeconds);

private:
	bool _calibrated = false;
	bool _calibrateRequested = false;
	float _neutralYaw = 0.0f;
	float _neutralPitch = 0.0f;
	float _neutralRoll = 0.0f;
	float _blinkBase[2] = {};  // 普段の目の開きでのまばたき値（左・右）
	std::map<std::string, float> _smoothed;
};
