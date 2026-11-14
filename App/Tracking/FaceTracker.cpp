#include "FaceTracker.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.AI.MachineLearning.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

#pragma comment(lib, "windowsapp")

using namespace winrt::Windows::AI::MachineLearning;

namespace
{
	constexpr float Pi = 3.14159265358979f;
	constexpr int DetectorSize = 128;
	constexpr int LandmarkSize = 256;
	constexpr float DetectionThreshold = 0.5f;
	constexpr float PresenceThreshold = 0.5f;
	constexpr float RoiScale = 1.5f;

	// MediaPipe のブレンドシェイプモデルに入れるランドマークの番号（146 点）
	constexpr int BlendshapeLandmarks[] =
	{
		0, 1, 4, 5, 6, 7, 8, 10, 13, 14, 17, 21, 33, 37, 39, 40, 46, 52, 53, 54, 55, 58, 61, 63, 65, 66, 67, 70, 78, 80,
		81, 82, 84, 87, 88, 91, 93, 95, 103, 105, 107, 109, 127, 132, 133, 136, 144, 145, 146, 148, 149, 150, 152, 153,
		154, 155, 157, 158, 159, 160, 161, 162, 163, 168, 172, 173, 176, 178, 181, 185, 191, 195, 197, 234, 246, 249,
		251, 263, 267, 269, 270, 276, 282, 283, 284, 285, 288, 291, 293, 295, 296, 297, 300, 308, 310, 311, 312, 314,
		317, 318, 321, 323, 324, 332, 334, 336, 338, 356, 361, 362, 365, 373, 374, 375, 377, 378, 379, 380, 381, 382,
		384, 385, 386, 387, 388, 389, 390, 397, 398, 400, 402, 405, 409, 415, 454, 466, 468, 469, 470, 471, 472, 473,
		474, 475, 476, 477,
	};
	static_assert(sizeof(BlendshapeLandmarks) / sizeof(BlendshapeLandmarks[0]) == 146, "blendshape landmark subset");

	const char* BlendshapeNames[FaceTrackingResult::BlendshapeCount] =
	{
		"_neutral", "browDownLeft", "browDownRight", "browInnerUp", "browOuterUpLeft", "browOuterUpRight", "cheekPuff",
		"cheekSquintLeft", "cheekSquintRight", "eyeBlinkLeft", "eyeBlinkRight", "eyeLookDownLeft", "eyeLookDownRight",
		"eyeLookInLeft", "eyeLookInRight", "eyeLookOutLeft", "eyeLookOutRight", "eyeLookUpLeft", "eyeLookUpRight",
		"eyeSquintLeft", "eyeSquintRight", "eyeWideLeft", "eyeWideRight", "jawForward", "jawLeft", "jawOpen", "jawRight",
		"mouthClose", "mouthDimpleLeft", "mouthDimpleRight", "mouthFrownLeft", "mouthFrownRight", "mouthFunnel",
		"mouthLeft", "mouthLowerDownLeft", "mouthLowerDownRight", "mouthPressLeft", "mouthPressRight", "mouthPucker",
		"mouthRight", "mouthRollLower", "mouthRollUpper", "mouthShrugLower", "mouthShrugUpper", "mouthSmileLeft",
		"mouthSmileRight", "mouthStretchLeft", "mouthStretchRight", "mouthUpperUpLeft", "mouthUpperUpRight",
		"noseSneerLeft", "noseSneerRight",
	};

	struct Anchor
	{
		float x, y;
	};

	// 回転した正方形の範囲（ピクセル）
	struct Roi
	{
		float cx = 0.0f, cy = 0.0f;
		float size = 0.0f;
		float angle = 0.0f;  // ラジアン（画像の Y 下向きで時計回りが正）
	};

	float Sigmoid(float x)
	{
		x = std::clamp(x, -100.0f, 100.0f);
		return 1.0f / (1.0f + std::exp(-x));
	}

	float NormalizeRadians(float angle)
	{
		return angle - 2.0f * Pi * std::floor((angle + Pi) / (2.0f * Pi));
	}

	// BlazeFace（短距離）のアンカー：stride 8 の 16x16 に 2 個、stride 16 の 8x8 に 6 個
	std::vector<Anchor> CreateAnchors()
	{
		std::vector<Anchor> anchors;
		const int strides[] = { 8, 16, 16, 16 };
		int layer = 0;
		while (layer < 4)
		{
			int count = 0;
			int last = layer;
			while (last < 4 && strides[last] == strides[layer])
			{
				count += 2;
				++last;
			}
			const int grid = DetectorSize / strides[layer];
			for (int y = 0; y < grid; ++y)
			{
				for (int x = 0; x < grid; ++x)
				{
					for (int i = 0; i < count; ++i)
					{
						anchors.push_back({ (x + 0.5f) / grid, (y + 0.5f) / grid });
					}
				}
			}
			layer = last;
		}
		return anchors;
	}

	// 回転した正方形を切り出して size x size の RGB（NHWC）にする
	void CropToTensor(const uint8_t* bgra, int width, int height, int stride, const Roi& roi, int size,
		float scale, float offset, std::vector<float>& out)
	{
		out.assign(static_cast<size_t>(size) * size * 3, offset);
		const float c = std::cos(roi.angle);
		const float s = std::sin(roi.angle);
		for (int v = 0; v < size; ++v)
		{
			for (int u = 0; u < size; ++u)
			{
				const float lx = ((u + 0.5f) / size - 0.5f) * roi.size;
				const float ly = ((v + 0.5f) / size - 0.5f) * roi.size;
				const float sx = roi.cx + lx * c - ly * s - 0.5f;
				const float sy = roi.cy + lx * s + ly * c - 0.5f;
				const int x0 = static_cast<int>(std::floor(sx));
				const int y0 = static_cast<int>(std::floor(sy));
				if (x0 < 0 || y0 < 0 || x0 + 1 >= width || y0 + 1 >= height)
				{
					continue;  // 画像の外は黒
				}
				const float fx = sx - x0;
				const float fy = sy - y0;
				const uint8_t* p00 = bgra + static_cast<size_t>(y0) * stride + x0 * 4;
				const uint8_t* p10 = p00 + 4;
				const uint8_t* p01 = p00 + stride;
				const uint8_t* p11 = p01 + 4;
				float* dst = &out[(static_cast<size_t>(v) * size + u) * 3];
				for (int ch = 0; ch < 3; ++ch)
				{
					const int src = 2 - ch;  // BGRA → RGB
					const float top = p00[src] + (p10[src] - p00[src]) * fx;
					const float bottom = p01[src] + (p11[src] - p01[src]) * fx;
					dst[ch] = (top + (bottom - top) * fy) * scale + offset;
				}
			}
		}
	}

	std::vector<float> ReadOutput(const LearningModelEvaluationResult& result, const winrt::hstring& name)
	{
		const TensorFloat tensor = result.Outputs().Lookup(name).as<TensorFloat>();
		const auto view = tensor.GetAsVectorView();
		std::vector<float> data(view.Size());
		view.GetMany(0, data);
		return data;
	}

	struct Model
	{
		LearningModel model{ nullptr };
		LearningModelSession session{ nullptr };
		winrt::hstring input;
		std::vector<winrt::hstring> outputs;

		void Load(const std::filesystem::path& path)
		{
			model = LearningModel::LoadFromFilePath(path.wstring());
			session = LearningModelSession(model, LearningModelDevice(LearningModelDeviceKind::Cpu));
			input = model.InputFeatures().GetAt(0).Name();
			outputs.clear();
			for (const auto& feature : model.OutputFeatures())
			{
				outputs.push_back(feature.Name());
			}
		}

		LearningModelEvaluationResult Run(const std::vector<int64_t>& shape, const std::vector<float>& data)
		{
			LearningModelBinding binding(session);
			binding.Bind(input, TensorFloat::CreateFromArray(shape, data));
			return session.Evaluate(binding, L"");
		}
	};
}

struct FaceTracker::Impl
{
	Model detector;
	Model landmarker;
	Model blendshapes;
	std::vector<Anchor> anchors = CreateAnchors();
	std::vector<float> buffer;
	bool tracking = false;
	Roi roi;

	// 顔検出：見つかったら顔の範囲（ランドマーク用）を返す
	bool Detect(const uint8_t* bgra, int width, int height, int stride, Roi& out)
	{
		// 画像全体を正方形（足りない所は黒）にして 128x128 へ
		Roi full;
		full.cx = width * 0.5f;
		full.cy = height * 0.5f;
		full.size = static_cast<float>((std::max)(width, height));
		CropToTensor(bgra, width, height, stride, full, DetectorSize, 2.0f / 255.0f, -1.0f, buffer);

		const auto result = detector.Run({ 1, DetectorSize, DetectorSize, 3 }, buffer);
		std::vector<float> regressors, scores;
		for (const auto& name : detector.outputs)
		{
			std::vector<float> data = ReadOutput(result, name);
			(data.size() == anchors.size() * 16 ? regressors : scores) = std::move(data);
		}
		if (regressors.size() != anchors.size() * 16 || scores.size() != anchors.size())
		{
			return false;
		}

		int best = -1;
		float bestScore = DetectionThreshold;
		for (size_t i = 0; i < anchors.size(); ++i)
		{
			const float score = Sigmoid(scores[i]);
			if (score > bestScore)
			{
				bestScore = score;
				best = static_cast<int>(i);
			}
		}
		if (best < 0)
		{
			return false;
		}

		// 128x128 での値 → 画像のピクセル
		const float* r = &regressors[static_cast<size_t>(best) * 16];
		const Anchor& a = anchors[best];
		auto toPixel = [&](float nx, float ny, float& px, float& py)
		{
			px = full.cx + (nx - 0.5f) * full.size;
			py = full.cy + (ny - 0.5f) * full.size;
		};
		float cx, cy, rightEyeX, rightEyeY, leftEyeX, leftEyeY;
		toPixel(r[0] / DetectorSize + a.x, r[1] / DetectorSize + a.y, cx, cy);
		toPixel(r[4] / DetectorSize + a.x, r[5] / DetectorSize + a.y, rightEyeX, rightEyeY);
		toPixel(r[6] / DetectorSize + a.x, r[7] / DetectorSize + a.y, leftEyeX, leftEyeY);
		const float boxWidth = r[2] / DetectorSize * full.size;
		const float boxHeight = r[3] / DetectorSize * full.size;

		out.cx = cx;
		out.cy = cy;
		out.size = (std::max)(boxWidth, boxHeight) * RoiScale;
		out.angle = NormalizeRadians(std::atan2(leftEyeY - rightEyeY, leftEyeX - rightEyeX));
		return out.size > 8.0f;
	}

	// ランドマークから次のフレームの顔の範囲を作る
	static Roi RoiFromLandmarks(const FaceTrackingResult& result)
	{
		const float* p = result.landmarks.data();
		const float angle = NormalizeRadians(std::atan2(p[263 * 3 + 1] - p[33 * 3 + 1], p[263 * 3] - p[33 * 3]));
		const float c = std::cos(angle);
		const float s = std::sin(angle);
		float minU = 1e9f, maxU = -1e9f, minV = 1e9f, maxV = -1e9f;
		for (int i = 0; i < FaceTrackingResult::LandmarkCount; ++i)
		{
			const float x = p[i * 3];
			const float y = p[i * 3 + 1];
			const float u = x * c + y * s;
			const float v = -x * s + y * c;
			minU = (std::min)(minU, u);
			maxU = (std::max)(maxU, u);
			minV = (std::min)(minV, v);
			maxV = (std::max)(maxV, v);
		}
		const float uc = (minU + maxU) * 0.5f;
		const float vc = (minV + maxV) * 0.5f;
		Roi roi;
		roi.cx = uc * c - vc * s;
		roi.cy = uc * s + vc * c;
		roi.size = (std::max)(maxU - minU, maxV - minV) * RoiScale;
		roi.angle = angle;
		return roi;
	}

	bool Landmarks(const uint8_t* bgra, int width, int height, int stride, const Roi& area, FaceTrackingResult& result)
	{
		CropToTensor(bgra, width, height, stride, area, LandmarkSize, 1.0f / 255.0f, 0.0f, buffer);
		const auto output = landmarker.Run({ 1, LandmarkSize, LandmarkSize, 3 }, buffer);

		std::vector<float> points;
		float presence = 0.0f;
		for (const auto& name : landmarker.outputs)
		{
			std::vector<float> data = ReadOutput(output, name);
			if (data.size() == static_cast<size_t>(FaceTrackingResult::LandmarkCount) * 3)
			{
				points = std::move(data);
			}
			else if (data.size() == 1 && presence == 0.0f)
			{
				presence = (data[0] < 0.0f || data[0] > 1.0f) ? Sigmoid(data[0]) : data[0];
			}
		}
		if (points.empty())
		{
			return false;
		}
		result.score = presence;
		if (presence < PresenceThreshold)
		{
			return false;
		}

		const float c = std::cos(area.angle);
		const float s = std::sin(area.angle);
		for (int i = 0; i < FaceTrackingResult::LandmarkCount; ++i)
		{
			const float lx = (points[i * 3] / LandmarkSize - 0.5f) * area.size;
			const float ly = (points[i * 3 + 1] / LandmarkSize - 0.5f) * area.size;
			result.landmarks[i * 3] = area.cx + lx * c - ly * s;
			result.landmarks[i * 3 + 1] = area.cy + lx * s + ly * c;
			result.landmarks[i * 3 + 2] = points[i * 3 + 2] / LandmarkSize * area.size;
		}
		return true;
	}

	void Blendshapes(FaceTrackingResult& result)
	{
		std::vector<float> input(146 * 2);
		for (int i = 0; i < 146; ++i)
		{
			input[i * 2] = result.landmarks[BlendshapeLandmarks[i] * 3];
			input[i * 2 + 1] = result.landmarks[BlendshapeLandmarks[i] * 3 + 1];
		}
		const auto output = blendshapes.Run({ 1, 146, 2 }, input);
		const std::vector<float> values = ReadOutput(output, blendshapes.outputs[0]);
		for (int i = 0; i < FaceTrackingResult::BlendshapeCount && i < static_cast<int>(values.size()); ++i)
		{
			result.blendshapes[i] = std::isfinite(values[i]) ? std::clamp(values[i], 0.0f, 1.0f) : 0.0f;
		}
	}

	// 頭の向き：頬の左右（234 → 454）と、あごから額（152 → 10）で顔の座標軸を作る
	static void HeadPose(FaceTrackingResult& result)
	{
		const float* p = result.landmarks.data();
		auto point = [&](int i, float out[3])
		{
			out[0] = p[i * 3];
			out[1] = -p[i * 3 + 1];  // 上向きを正に
			out[2] = -p[i * 3 + 2];  // カメラ側を正に
		};
		float a[3], b[3], chin[3], forehead[3];
		point(234, a);
		point(454, b);
		point(152, chin);
		point(10, forehead);

		float right[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
		float up[3] = { forehead[0] - chin[0], forehead[1] - chin[1], forehead[2] - chin[2] };
		auto normalize = [](float v[3])
		{
			const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
			if (length > 1e-6f)
			{
				v[0] /= length; v[1] /= length; v[2] /= length;
			}
		};
		normalize(right);
		const float d = up[0] * right[0] + up[1] * right[1] + up[2] * right[2];
		up[0] -= right[0] * d; up[1] -= right[1] * d; up[2] -= right[2] * d;
		normalize(up);
		const float forward[3] =
		{
			right[1] * up[2] - right[2] * up[1],
			right[2] * up[0] - right[0] * up[2],
			right[0] * up[1] - right[1] * up[0],
		};

		const float toDegrees = 180.0f / Pi;
		result.yaw = std::atan2(forward[0], forward[2]) * toDegrees;
		result.pitch = std::asin(std::clamp(forward[1], -1.0f, 1.0f)) * toDegrees;
		result.roll = std::atan2(right[1], right[0]) * toDegrees;
	}
};

FaceTracker::FaceTracker() = default;
FaceTracker::~FaceTracker() = default;

bool FaceTracker::Initialize(const std::filesystem::path& modelDirectory, std::string& error)
{
	try
	{
		auto impl = std::make_unique<Impl>();
		impl->detector.Load(modelDirectory / "face_detector.onnx");
		impl->landmarker.Load(modelDirectory / "face_landmarks_detector.onnx");
		impl->blendshapes.Load(modelDirectory / "face_blendshapes.onnx");
		_impl = std::move(impl);
		return true;
	}
	catch (const winrt::hresult_error& e)
	{
		error = winrt::to_string(e.message());
		if (error.empty())
		{
			error = "HRESULT " + std::to_string(static_cast<int32_t>(e.code()));
		}
		return false;
	}
}

void FaceTracker::Reset()
{
	if (_impl)
	{
		_impl->tracking = false;
	}
}

FaceTrackingResult FaceTracker::Process(const uint8_t* bgra, int width, int height, int stride)
{
	FaceTrackingResult result;
	if (!_impl || bgra == nullptr || width <= 0 || height <= 0)
	{
		return result;
	}
	const auto start = std::chrono::steady_clock::now();

	try
	{
		Impl& impl = *_impl;
		// 前のフレームの顔の位置で試し、駄目なら顔検出からやり直す
		bool ok = impl.tracking && impl.Landmarks(bgra, width, height, stride, impl.roi, result);
		if (!ok)
		{
			Roi detected;
			ok = impl.Detect(bgra, width, height, stride, detected) && impl.Landmarks(bgra, width, height, stride, detected, result);
		}
		if (ok)
		{
			impl.Blendshapes(result);
			Impl::HeadPose(result);
			impl.roi = Impl::RoiFromLandmarks(result);
			impl.tracking = true;
			result.found = true;
		}
		else
		{
			impl.tracking = false;
		}
	}
	catch (const winrt::hresult_error&)
	{
		_impl->tracking = false;
		result.found = false;
	}

	result.processMilliseconds = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
	return result;
}

const char* FaceTracker::GetBlendshapeName(int index)
{
	return (index >= 0 && index < FaceTrackingResult::BlendshapeCount) ? BlendshapeNames[index] : "";
}

int FaceTracker::FindBlendshape(const char* name)
{
	for (int i = 0; i < FaceTrackingResult::BlendshapeCount; ++i)
	{
		if (std::strcmp(BlendshapeNames[i], name) == 0)
		{
			return i;
		}
	}
	return -1;
}
