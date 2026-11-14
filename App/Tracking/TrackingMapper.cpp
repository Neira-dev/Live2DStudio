#include "TrackingMapper.h"

#include <algorithm>
#include <cmath>

namespace
{
	float Shape(const FaceTrackingResult& result, const char* name)
	{
		static std::map<std::string, int> cache;
		auto it = cache.find(name);
		if (it == cache.end())
		{
			it = cache.emplace(name, FaceTracker::FindBlendshape(name)).first;
		}
		return (it->second >= 0) ? result.blendshapes[it->second] : 0.0f;
	}

	float Clamp(float v, float lo, float hi) { return std::clamp(v, lo, hi); }
}

void TrackingMapper::Reset()
{
	_smoothed.clear();
}

float TrackingMapper::Smooth(const char* id, float target, float speed, float deltaSeconds)
{
	auto it = _smoothed.find(id);
	if (it == _smoothed.end())
	{
		_smoothed[id] = target;
		return target;
	}
	// フレームの間隔に関係なく同じ速さで追いかける
	const float alpha = 1.0f - std::exp(-speed * (std::max)(deltaSeconds, 0.0f));
	it->second += (target - it->second) * alpha;
	return it->second;
}

TrackingParameters TrackingMapper::Map(const FaceTrackingResult& result, float deltaSeconds)
{
	TrackingParameters parameters;
	if (!result.found)
	{
		return parameters;
	}

	if (_calibrateRequested || !_calibrated)
	{
		_neutralYaw = result.yaw;
		_neutralPitch = result.pitch;
		_neutralRoll = result.roll;
		_blinkBase[0] = Shape(result, "eyeBlinkLeft");
		_blinkBase[1] = Shape(result, "eyeBlinkRight");
		_calibrated = true;
		_calibrateRequested = false;
	}

	// ---- 頭の向き（鏡のように動く）----
	const float angleX = Clamp(result.yaw - _neutralYaw, -30.0f, 30.0f);
	const float angleY = Clamp(result.pitch - _neutralPitch, -30.0f, 30.0f);
	const float angleZ = Clamp(-(result.roll - _neutralRoll), -30.0f, 30.0f);
	const float x = Smooth("ParamAngleX", angleX, 18.0f, deltaSeconds);
	const float y = Smooth("ParamAngleY", angleY, 18.0f, deltaSeconds);
	const float z = Smooth("ParamAngleZ", angleZ, 18.0f, deltaSeconds);
	parameters.emplace_back("ParamAngleX", x);
	parameters.emplace_back("ParamAngleY", y);
	parameters.emplace_back("ParamAngleZ", z);
	parameters.emplace_back("ParamBodyAngleX", x * 0.35f);
	parameters.emplace_back("ParamBodyAngleY", y * 0.2f);
	parameters.emplace_back("ParamBodyAngleZ", z * 0.35f);

	// ---- 目（普段の開きを 1 にする）----
	auto eyeOpen = [&](const char* blink, const char* wide, float base)
	{
		const float closed = (Shape(result, blink) - base) / (std::max)(1.0f - base, 0.2f);
		return Clamp(1.0f - closed * 1.3f + Shape(result, wide) * 0.3f, 0.0f, 1.0f);
	};
	parameters.emplace_back("ParamEyeLOpen", Smooth("ParamEyeLOpen", eyeOpen("eyeBlinkLeft", "eyeWideLeft", _blinkBase[0]), 35.0f, deltaSeconds));
	parameters.emplace_back("ParamEyeROpen", Smooth("ParamEyeROpen", eyeOpen("eyeBlinkRight", "eyeWideRight", _blinkBase[1]), 35.0f, deltaSeconds));

	// ---- 視線 ----
	const float lookX = ((Shape(result, "eyeLookOutLeft") - Shape(result, "eyeLookInLeft")) +
		(Shape(result, "eyeLookInRight") - Shape(result, "eyeLookOutRight"))) * 0.5f;
	const float lookY = ((Shape(result, "eyeLookUpLeft") + Shape(result, "eyeLookUpRight")) -
		(Shape(result, "eyeLookDownLeft") + Shape(result, "eyeLookDownRight"))) * 0.5f;
	parameters.emplace_back("ParamEyeBallX", Smooth("ParamEyeBallX", Clamp(lookX * 1.5f, -1.0f, 1.0f), 25.0f, deltaSeconds));
	parameters.emplace_back("ParamEyeBallY", Smooth("ParamEyeBallY", Clamp(lookY * 1.5f, -1.0f, 1.0f), 25.0f, deltaSeconds));

	// ---- 眉 ----
	const float innerUp = Shape(result, "browInnerUp");
	parameters.emplace_back("ParamBrowLY", Smooth("ParamBrowLY",
		Clamp((Shape(result, "browOuterUpLeft") + innerUp * 0.5f - Shape(result, "browDownLeft")) * 1.5f, -1.0f, 1.0f), 20.0f, deltaSeconds));
	parameters.emplace_back("ParamBrowRY", Smooth("ParamBrowRY",
		Clamp((Shape(result, "browOuterUpRight") + innerUp * 0.5f - Shape(result, "browDownRight")) * 1.5f, -1.0f, 1.0f), 20.0f, deltaSeconds));

	// ---- 口 ----
	const float open = Clamp(Shape(result, "jawOpen") * 1.8f - Shape(result, "mouthClose") * 0.5f, 0.0f, 1.0f);
	const float smile = (Shape(result, "mouthSmileLeft") + Shape(result, "mouthSmileRight")) * 0.5f;
	const float frown = (Shape(result, "mouthFrownLeft") + Shape(result, "mouthFrownRight")) * 0.5f;
	const float form = Clamp((smile - frown - Shape(result, "mouthPucker") * 0.3f) * 2.0f, -1.0f, 1.0f);
	parameters.emplace_back("ParamMouthOpenY", Smooth("ParamMouthOpenY", open, 30.0f, deltaSeconds));
	parameters.emplace_back("ParamMouthForm", Smooth("ParamMouthForm", form, 15.0f, deltaSeconds));

	return parameters;
}
