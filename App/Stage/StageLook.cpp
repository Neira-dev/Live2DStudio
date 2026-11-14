#include "StageLook.h"

#include <algorithm>
#include <cmath>

#define U8(text) reinterpret_cast<const char*>(u8##text)

namespace
{
	constexpr float Pi = 3.14159265f;

	float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

	float Luminance(const float rgb[3]) { return rgb[0] * 0.299f + rgb[1] * 0.587f + rgb[2] * 0.114f; }

	float SmoothStep(float edge0, float edge1, float x)
	{
		const float t = Clamp01((x - edge0) / (edge1 - edge0));
		return t * t * (3.0f - 2.0f * t);
	}

	// LUT の色変換（入力・出力とも 0〜1）
	void Grade(StageLut lut, const float in[3], float out[3])
	{
		out[0] = in[0];
		out[1] = in[1];
		out[2] = in[2];
		switch (lut)
		{
		case StageLut::Film:
		{
			// ゆるい S 字カーブ＋影は青緑、明るい所は橙に寄せる
			const float l = Luminance(in);
			for (int c = 0; c < 3; ++c)
			{
				out[c] = in[c] + (SmoothStep(0.0f, 1.0f, in[c]) - in[c]) * 0.6f;
			}
			out[0] += (0.04f * l) - 0.01f * (1.0f - l);
			out[1] += 0.01f * (1.0f - l);
			out[2] += 0.04f * (1.0f - l) - 0.03f * l;
			break;
		}
		case StageLut::Cool:
			out[0] = in[0] * 0.92f + 0.01f;
			out[1] = in[1] * 0.98f + 0.015f;
			out[2] = in[2] * 1.06f + 0.03f;
			break;
		case StageLut::Warm:
			out[0] = in[0] * 1.06f + 0.02f;
			out[1] = in[1] * 1.0f + 0.01f;
			out[2] = in[2] * 0.86f;
			break;
		case StageLut::Faded:
		{
			const float l = Luminance(in);
			for (int c = 0; c < 3; ++c)
			{
				const float desaturated = l + (in[c] - l) * 0.75f;
				out[c] = 0.07f + desaturated * 0.88f;
			}
			break;
		}
		default:
			break;
		}
		for (int c = 0; c < 3; ++c)
		{
			out[c] = Clamp01(out[c]);
		}
	}
}

void StageLook::ApplyPreset(StagePreset value)
{
	*this = StageLook();
	preset = value;
	switch (value)
	{
	case StagePreset::Sunset:
		brightness = 0.05f;
		warmth = 0.7f;
		mood = 0.3f;
		lightAngle = 160.0f;
		lightStrength = 0.5f;
		shadowStrength = 0.4f;
		rimColor[0] = 1.0f; rimColor[1] = 0.62f; rimColor[2] = 0.32f;
		rimStrength = 0.6f;
		bloomThreshold = 0.7f;
		bloomIntensity = 0.45f;
		lut = StageLut::Warm;
		lutStrength = 0.6f;
		vignette = 0.3f;
		break;
	case StagePreset::Night:
		brightness = -0.15f;
		warmth = -0.5f;
		mood = 0.7f;
		lightAngle = 60.0f;
		lightStrength = 0.55f;
		shadowStrength = 0.5f;
		rimColor[0] = 0.45f; rimColor[1] = 0.62f; rimColor[2] = 1.0f;
		rimStrength = 0.8f;
		bloomThreshold = 0.65f;
		bloomIntensity = 0.55f;
		lut = StageLut::Cool;
		lutStrength = 0.7f;
		vignette = 0.45f;
		break;
	default:
		break;
	}
}

Live2DActorLook StageLook::MakeActorLook(const float normalize[3]) const
{
	Live2DActorLook look;

	// 明るさ（ムードを上げると少し暗くなる）
	const float bright = (1.0f + 0.35f * brightness) * (1.0f - 0.15f * mood);
	// 暖かさ
	const float warm[3] = { 1.0f + 0.12f * warmth, 1.0f + 0.02f * warmth, 1.0f - 0.15f * warmth };
	for (int c = 0; c < 3; ++c)
	{
		look.tint[c] = bright * warm[c] * normalize[c];
	}
	look.saturation = 1.0f - 0.25f * mood;

	GetLightDirection(look.lightDirection[0], look.lightDirection[1]);
	look.lightStrength = Clamp01(lightStrength + 0.3f * mood);

	for (int c = 0; c < 3; ++c)
	{
		look.rimColor[c] = rimColor[c];
	}
	look.rimStrength = Clamp01(rimStrength + 0.3f * mood);
	look.rimWidth = rimWidth;
	return look;
}

float StageLook::GetVignette() const
{
	return Clamp01(vignette + 0.35f * mood);
}

void StageLook::GetLightDirection(float& x, float& y) const
{
	const float radians = lightAngle * Pi / 180.0f;
	x = std::cos(radians);
	y = std::sin(radians);
}

std::vector<uint8_t> BuildStageLut(StageLut lut, int size)
{
	std::vector<uint8_t> data(static_cast<size_t>(size) * size * size * 4);
	const float scale = 1.0f / static_cast<float>(size - 1);
	for (int b = 0; b < size; ++b)
	{
		for (int g = 0; g < size; ++g)
		{
			for (int r = 0; r < size; ++r)
			{
				const float in[3] = { r * scale, g * scale, b * scale };
				float out[3];
				Grade(lut, in, out);
				uint8_t* p = &data[((static_cast<size_t>(b) * size + g) * size + r) * 4];
				p[0] = static_cast<uint8_t>(out[0] * 255.0f + 0.5f);
				p[1] = static_cast<uint8_t>(out[1] * 255.0f + 0.5f);
				p[2] = static_cast<uint8_t>(out[2] * 255.0f + 0.5f);
				p[3] = 255;
			}
		}
	}
	return data;
}

const char* GetStagePresetName(StagePreset preset)
{
	switch (preset)
	{
	case StagePreset::Natural: return U8("ナチュラル");
	case StagePreset::Sunset: return U8("夕暮れ");
	case StagePreset::Night: return U8("ナイト");
	default: return "";
	}
}

const char* GetStageLutName(StageLut lut)
{
	switch (lut)
	{
	case StageLut::Neutral: return U8("なし");
	case StageLut::Film: return U8("フィルム");
	case StageLut::Cool: return U8("クール");
	case StageLut::Warm: return U8("ウォーム");
	case StageLut::Faded: return U8("フェード");
	default: return "";
	}
}
