#include "Stage.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "../Cubism/Live2D.h"

#define U8(text) reinterpret_cast<const char*>(u8##text)

namespace
{
	constexpr float Aspect = static_cast<float>(StageRenderer::Width) / StageRenderer::Height;

	float Approach(float current, float target, float rate, float deltaSeconds)
	{
		const float step = rate * deltaSeconds;
		return (current < target) ? (std::min)(current + step, target) : (std::max)(current - step, target);
	}

	struct Placement
	{
		float x, y, scale;
	};

	// 配置テンプレ（人数ごと）
	std::vector<Placement> GetPlacements(StageLayout layout, size_t count)
	{
		std::vector<Placement> result;
		if (count == 0)
		{
			return result;
		}
		if (count == 1)
		{
			result.push_back({ 0.0f, -0.05f, 0.95f });
			return result;
		}

		switch (layout)
		{
		case StageLayout::Tiered:
			// ひな壇：後ろの段は高く小さく、前の段は低く大きく
			if (count == 2)
			{
				result.push_back({ -0.45f, 0.10f, 0.80f });
				result.push_back({ 0.45f, -0.12f, 0.95f });
			}
			else
			{
				result.push_back({ 0.0f, 0.14f, 0.74f });
				result.push_back({ -0.80f, -0.12f, 0.92f });
				result.push_back({ 0.80f, -0.12f, 0.92f });
			}
			break;

		case StageLayout::RoundTable:
		{
			// 円卓：楕円の周りに並べ、奥ほど高く小さくする
			const float pi = 3.14159265f;
			for (size_t i = 0; i < count; ++i)
			{
				const float angle = (count == 2)
					? (i == 0 ? -0.5f * pi : 0.5f * pi)
					: (2.0f * pi * static_cast<float>(i) / static_cast<float>(count));
				const float depth = std::cos(angle);  // 1 = 奥、-1 = 手前
				result.push_back({ 1.05f * std::sin(angle), 0.12f * depth - 0.04f, 0.84f - 0.10f * depth });
			}
			break;
		}

		default:
		{
			// 横並び
			const float spread = (count == 2) ? 0.60f : 1.05f;
			for (size_t i = 0; i < count; ++i)
			{
				const float t = static_cast<float>(i) / static_cast<float>(count - 1);
				result.push_back({ -spread + 2.0f * spread * t, -0.05f, 0.92f });
			}
			break;
		}
		}
		return result;
	}
}

Stage::Stage() = default;

Stage::~Stage()
{
	Release();
}

bool Stage::Initialize(ID3D12Device* device, DX12CommandQueue* commandQueue)
{
	_look.ApplyPreset(StagePreset::Natural);
	return _renderer.Initialize(device, commandQueue);
}

void Stage::Release()
{
	_actors.clear();
	_renderer.Release();
}

bool Stage::AddActor(const std::filesystem::path& modelPath, const std::string& name)
{
	if (_actors.size() >= MaxActors)
	{
		return false;
	}

	auto actor = std::make_unique<StageActor>();
	actor->live2d = std::make_unique<Live2D>();
	if (!actor->live2d->Load(modelPath))
	{
		return false;
	}

	// model3.json に登録されていない待機モーションがあればループ再生する
	std::error_code ec;
	const std::filesystem::path idle = modelPath / "Animation" / "IDLE.motion3.json";
	if (std::filesystem::exists(idle, ec))
	{
		actor->live2d->PlayMotion(idle, true);
	}
	actor->name = name;
	actor->modelPath = modelPath;
	actor->tier = _actors.empty() ? ActorTier::Self : ActorTier::S;
	_actors.push_back(std::move(actor));

	if (_selected < 0)
	{
		_selected = 0;
	}
	ApplyLayout(_layout);
	if (_normalized)
	{
		NormalizeLooks();
	}
	return true;
}

void Stage::RemoveActor(size_t index)
{
	if (index >= _actors.size())
	{
		return;
	}
	_actors.erase(_actors.begin() + static_cast<std::ptrdiff_t>(index));
	if (_selected >= static_cast<int>(_actors.size()))
	{
		_selected = static_cast<int>(_actors.size()) - 1;
	}
	ApplyLayout(_layout);
	if (_normalized)
	{
		NormalizeLooks();
	}
}

void Stage::ApplyLayout(StageLayout layout)
{
	_layout = layout;
	const std::vector<Placement> placements = GetPlacements(layout, _actors.size());
	for (size_t i = 0; i < _actors.size(); ++i)
	{
		_actors[i]->x = placements[i].x;
		_actors[i]->y = placements[i].y;
		_actors[i]->scale = placements[i].scale;
	}
}

std::vector<size_t> Stage::GetDrawOrder() const
{
	// 小さい（奥にいる）ものから描く
	std::vector<size_t> order(_actors.size());
	for (size_t i = 0; i < order.size(); ++i)
	{
		order[i] = i;
	}
	std::stable_sort(order.begin(), order.end(), [this](size_t a, size_t b)
	{
		return _actors[a]->scale < _actors[b]->scale;
	});
	return order;
}

int Stage::HitTest(float ndcX, float ndcY) const
{
	const std::vector<size_t> order = GetDrawOrder();
	for (auto it = order.rbegin(); it != order.rend(); ++it)
	{
		float left, right, bottom, top;
		if (_actors[*it]->live2d->GetScreenBounds(left, right, bottom, top) &&
			ndcX >= left && ndcX <= right && ndcY >= bottom && ndcY <= top)
		{
			return static_cast<int>(*it);
		}
	}
	return -1;
}

void Stage::MoveActor(size_t index, float deltaNdcX, float deltaNdcY)
{
	if (index >= _actors.size())
	{
		return;
	}
	StageActor& actor = *_actors[index];
	actor.x = std::clamp(actor.x + deltaNdcX * Aspect, -Aspect, Aspect);
	actor.y = std::clamp(actor.y + deltaNdcY, -1.0f, 1.0f);
}

void Stage::ScaleActor(size_t index, float factor)
{
	if (index >= _actors.size())
	{
		return;
	}
	_actors[index]->scale = std::clamp(_actors[index]->scale * factor, 0.2f, 3.0f);
}

void Stage::NormalizeLooks()
{
	_normalized = true;
	if (_actors.empty())
	{
		return;
	}

	// 各モデルの平均の明るさと色味（明るさに対する各色の比）
	std::vector<float> luminance(_actors.size());
	std::vector<std::array<float, 3>> ratio(_actors.size());
	float meanLuminance = 0.0f;
	float meanRatio[3] = {};
	for (size_t i = 0; i < _actors.size(); ++i)
	{
		float rgb[3];
		_actors[i]->live2d->GetAverageColor(rgb);
		const float l = (std::max)(rgb[0] * 0.299f + rgb[1] * 0.587f + rgb[2] * 0.114f, 0.05f);
		luminance[i] = l;
		meanLuminance += l;
		for (int c = 0; c < 3; ++c)
		{
			ratio[i][c] = rgb[c] / l;
			meanRatio[c] += ratio[i][c];
		}
	}
	const float count = static_cast<float>(_actors.size());
	meanLuminance /= count;
	for (int c = 0; c < 3; ++c)
	{
		meanRatio[c] /= count;
	}

	// 明るさは平均に寄せ、色味は半分だけ寄せる（キャラの色を壊さないように）
	for (size_t i = 0; i < _actors.size(); ++i)
	{
		const float gain = std::clamp(meanLuminance / luminance[i], 0.75f, 1.3f);
		for (int c = 0; c < 3; ++c)
		{
			const float balance = (ratio[i][c] > 1e-3f) ? meanRatio[c] / ratio[i][c] : 1.0f;
			const float partial = std::clamp(1.0f + (balance - 1.0f) * 0.5f, 0.85f, 1.15f);
			_actors[i]->normalize[c] = gain * partial;
		}
	}
}

void Stage::ResetNormalize()
{
	_normalized = false;
	for (auto& actor : _actors)
	{
		actor->normalize[0] = actor->normalize[1] = actor->normalize[2] = 1.0f;
	}
}

void Stage::ToggleManualAway()
{
	if (!_actors.empty())
	{
		PresenceStateMachine& presence = _actors[0]->presence;
		presence.SetManualAway(!presence.IsManualAway());
	}
}

void Stage::Update(float deltaSeconds)
{
	_blackout = Approach(_blackout, _blackoutOn ? 1.0f : 0.0f, 2.5f, deltaSeconds);
	_spotlight = Approach(_spotlight, _spotlightOn ? 1.0f : 0.0f, 2.5f, deltaSeconds);

	for (auto& actor : _actors)
	{
		const bool faceVisible = !actor->testFaceLost && (!actor->trackingActive || actor->trackingFace);
		actor->presence.Update(deltaSeconds, faceVisible, !actor->testDisconnected);

		// 回線断の間は最後のフレームのまま止める
		if (!actor->presence.IsConnectionLost())
		{
			actor->live2d->Update(deltaSeconds);
		}
	}

	// スポットライトは選んでいる参加者を追いかける
	if (_selected >= 0 && _selected < static_cast<int>(_actors.size()))
	{
		float left, right, bottom, top;
		if (_actors[_selected]->live2d->GetScreenBounds(left, right, bottom, top))
		{
			const float centerX = (left + right) * 0.5f;
			const float centerY = (bottom + top) * 0.5f;
			const float radius = (top - bottom) * 0.42f;
			const float follow = (std::min)(1.0f, deltaSeconds * 8.0f);
			_spotCenter[0] += (centerX - _spotCenter[0]) * follow;
			_spotCenter[1] += (centerY - _spotCenter[1]) * follow;
			_spotRadius += (radius - _spotRadius) * follow;
		}
	}
}

void Stage::Render(ID3D12GraphicsCommandList* commandList)
{
	_renderer.UpdateLut(commandList, _look.lut);

	const Live2DRenderTarget target = _renderer.BeginActors(commandList);
	for (size_t index : GetDrawOrder())
	{
		StageActor& actor = *_actors[index];
		const float away = actor.presence.GetAwayBlend();
		const float connection = actor.presence.GetConnectionBlend();

		// 離席演出：少し沈みながら色が抜けて薄くなる。回線断は半透明にする
		Live2DActorLook look = _look.MakeActorLook(actor.normalize);
		look.opacity = (1.0f - 0.7f * away) * (1.0f - 0.45f * connection);
		look.saturation *= 1.0f - 0.8f * away;

		actor.live2d->SetCamera(nullptr);
		actor.live2d->SetPosition(actor.x, actor.y - 0.05f * away);
		actor.live2d->SetScale(actor.scale * (1.0f - 0.04f * away));
		if (look.opacity < 0.999f)
		{
			// 半透明は1体分をレイヤーに描いてから、まとめて薄くして重ねる
			const float opacity = look.opacity;
			look.opacity = 1.0f;
			actor.live2d->SetActorLook(look);
			actor.live2d->Draw(commandList, _renderer.BeginLayer(commandList));
			_renderer.EndLayer(commandList, opacity);
		}
		else
		{
			actor.live2d->SetActorLook(look);
			actor.live2d->Draw(commandList, target);
		}
	}
	_renderer.EndActors(commandList, _look.bloomThreshold);
}

void Stage::Composite(ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
	const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor, const float background[3])
{
	StageCompositeParams params;
	params.background[0] = background[0];
	params.background[1] = background[1];
	params.background[2] = background[2];

	// 影は光と反対側へ落とす（UV は Y 下向き）
	float lightX, lightY;
	_look.GetLightDirection(lightX, lightY);
	params.shadowOffset[0] = -lightX * _look.shadowDistance / Aspect;
	params.shadowOffset[1] = lightY * _look.shadowDistance;
	params.shadowStrength = _look.shadowStrength;

	params.contrast = _look.GetContrast();
	params.lutStrength = (_look.lut != StageLut::Neutral) ? _look.lutStrength : 0.0f;
	params.vignette = _look.GetVignette();
	params.bloomIntensity = _look.bloomIntensity;
	params.blackout = _blackout;
	params.spotCenter[0] = _spotCenter[0];
	params.spotCenter[1] = _spotCenter[1];
	params.spotRadius = _spotRadius;
	params.spotAmount = _spotlight;
	_renderer.Composite(commandList, rtv, viewport, scissor, params);
}

const char* GetStageLayoutName(StageLayout layout)
{
	switch (layout)
	{
	case StageLayout::SideBySide: return U8("横並び");
	case StageLayout::Tiered: return U8("ひな壇");
	case StageLayout::RoundTable: return U8("円卓");
	default: return "";
	}
}

const char* GetActorTierName(ActorTier tier)
{
	switch (tier)
	{
	case ActorTier::Self: return U8("自分");
	case ActorTier::S: return "Tier S";
	case ActorTier::P: return "Tier P";
	default: return "";
	}
}
