#include "StageUI.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "imgui.h"

#include "Stage.h"
#include "OutputWindow.h"
#include "StageRenderer.h"
#include "../Cubism/Live2D.h"

#define U8(text) reinterpret_cast<const char*>(u8##text)

namespace fs = std::filesystem;

namespace
{
	constexpr float TopBarHeight = 64.0f;
	constexpr float LeftWidth = 270.0f;
	constexpr float RightWidth = 330.0f;
	constexpr ImGuiWindowFlags PanelFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

	ImVec4 PresenceColor(PresenceState state, bool connectionLost)
	{
		if (connectionLost)
		{
			return ImVec4(1.0f, 0.55f, 0.2f, 1.0f);
		}
		switch (state)
		{
		case PresenceState::Present: return ImVec4(0.45f, 0.9f, 0.45f, 1.0f);
		case PresenceState::Unstable: return ImVec4(1.0f, 0.85f, 0.3f, 1.0f);
		case PresenceState::Away: return ImVec4(0.65f, 0.65f, 0.7f, 1.0f);
		case PresenceState::Returning: return ImVec4(0.5f, 0.8f, 1.0f, 1.0f);
		default: return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		}
	}

	// NDC → プレビューのピクセル座標
	ImVec2 NdcToScreen(const D3D12_VIEWPORT& viewport, float x, float y)
	{
		return ImVec2(viewport.TopLeftX + (x * 0.5f + 0.5f) * viewport.Width,
			viewport.TopLeftY + (0.5f - y * 0.5f) * viewport.Height);
	}
}

void StageUI::Initialize()
{
	ImGuiIO& io = ImGui::GetIO();
	const char* candidates[] =
	{
		"C:/Windows/Fonts/meiryo.ttc",
		"C:/Windows/Fonts/YuGothM.ttc",
		"C:/Windows/Fonts/msgothic.ttc",
	};
	for (const char* path : candidates)
	{
		std::error_code ec;
		if (fs::exists(path, ec))
		{
			if (ImFont* font = io.Fonts->AddFontFromFileTTF(path, 17.0f))
			{
				io.FontDefault = font;
				break;
			}
		}
	}

	_models = FindModels("Live2D");
}

std::string StageUI::ToUtf8(const fs::path& path)
{
	const std::u8string text = path.u8string();
	return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::vector<fs::path> StageUI::FindModels(const fs::path& root)
{
	std::vector<fs::path> result;
	std::error_code ec;
	for (const auto& entry : fs::directory_iterator(root, ec))
	{
		if (!entry.is_directory(ec))
		{
			continue;
		}
		for (const auto& file : fs::directory_iterator(entry.path(), ec))
		{
			const std::string name = ToUtf8(file.path().filename());
			if (name.size() > 12 && name.compare(name.size() - 12, 12, ".model3.json") == 0)
			{
				result.push_back(entry.path());
				break;
			}
		}
	}
	return result;
}

void StageUI::GetPreviewBackground(float rgb[3]) const
{
	if (_previewChroma)
	{
		rgb[0] = 0.0f; rgb[1] = 1.0f; rgb[2] = 0.0f;
	}
	else
	{
		rgb[0] = 0.10f; rgb[1] = 0.11f; rgb[2] = 0.13f;
	}
}

void StageUI::Draw(Stage& stage, OutputWindow& output)
{
	_output = &output;
	const ImVec2 display = ImGui::GetIO().DisplaySize;
	const float bodyHeight = display.y - TopBarHeight;

	DrawTopBar(stage, output, display.x, TopBarHeight);
	DrawParticipants(stage, 0.0f, TopBarHeight, LeftWidth, bodyHeight);
	DrawLookPanel(stage, display.x - RightWidth, TopBarHeight, RightWidth, bodyHeight);
	DrawPreview(stage, LeftWidth, TopBarHeight, display.x - LeftWidth - RightWidth, bodyHeight);
}

// ============================================================================
// 上部：配置テンプレ・招待コード・演出・出力
// ============================================================================
void StageUI::DrawTopBar(Stage& stage, OutputWindow& output, float width, float height)
{
	ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
	ImGui::Begin("##TopBar", nullptr, PanelFlags | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);

	// 配置テンプレ
	ImGui::TextUnformatted(U8("配置"));
	for (int i = 0; i < static_cast<int>(StageLayout::Count); ++i)
	{
		ImGui::SameLine();
		const StageLayout layout = static_cast<StageLayout>(i);
		const bool current = stage.GetLayout() == layout;
		if (current)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
		}
		if (ImGui::Button(GetStageLayoutName(layout)))
		{
			stage.ApplyLayout(layout);
		}
		if (current)
		{
			ImGui::PopStyleColor();
		}
	}

	// 招待コード（通信は未実装なので、コードを作るところまで）
	ImGui::SameLine(0.0f, 30.0f);
	ImGui::TextUnformatted(U8("招待コード"));
	ImGui::SameLine();
	if (_roomCode.empty())
	{
		if (ImGui::Button(U8("ルームを作る")))
		{
			std::random_device device;
			std::uniform_int_distribution<int> digit(0, 9);
			_roomCode.clear();
			for (int i = 0; i < 6; ++i)
			{
				if (i == 3)
				{
					_roomCode += ' ';
				}
				_roomCode += static_cast<char>('0' + digit(device));
			}
		}
	}
	else
	{
		ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f), "%s", _roomCode.c_str());
		ImGui::SameLine();
		ImGui::TextDisabled(U8("（通信は未実装）"));
	}

	// 2 行目：演出と出力
	auto toggleButton = [](const char* label, bool on) -> bool
	{
		if (on)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.35f, 0.2f, 1.0f));
		}
		const bool pressed = ImGui::Button(label);
		if (on)
		{
			ImGui::PopStyleColor();
		}
		return pressed;
	};

	ImGui::TextUnformatted(U8("演出"));
	ImGui::SameLine();
	std::string label = std::string(U8("暗転 (")) + OutputWindow::GetHotkeyLabel(StageHotkey::Blackout) + ")";
	if (toggleButton(label.c_str(), stage.IsBlackout()))
	{
		stage.ToggleBlackout();
	}
	ImGui::SameLine();
	label = std::string(U8("スポットライト (")) + OutputWindow::GetHotkeyLabel(StageHotkey::Spotlight) + ")";
	if (toggleButton(label.c_str(), stage.IsSpotlight()))
	{
		stage.ToggleSpotlight();
	}
	ImGui::SameLine();
	label = std::string(U8("自分を離席 (")) + OutputWindow::GetHotkeyLabel(StageHotkey::ManualAway) + ")";
	const bool selfAway = stage.GetActorCount() > 0 && stage.GetActor(0).presence.IsManualAway();
	if (toggleButton(label.c_str(), selfAway))
	{
		stage.ToggleManualAway();
	}

	ImGui::SameLine(0.0f, 30.0f);
	bool outputVisible = output.IsVisible();
	if (ImGui::Checkbox(U8("出力ウィンドウ"), &outputVisible))
	{
		output.SetVisible(outputVisible);
	}
	ImGui::SameLine();
	ImGui::Checkbox(U8("プレビューをクロマキー背景に"), &_previewChroma);

	ImGui::End();
}

// ============================================================================
// 左：参加者一覧
// ============================================================================
void StageUI::DrawParticipants(Stage& stage, float x, float y, float width, float height)
{
	ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
	ImGui::Begin(U8("参加者"), nullptr, PanelFlags);

	int removeIndex = -1;
	for (size_t i = 0; i < stage.GetActorCount(); ++i)
	{
		StageActor& actor = stage.GetActor(i);
		ImGui::PushID(static_cast<int>(i));

		const bool selected = stage.GetSelected() == static_cast<int>(i);
		if (ImGui::Selectable(actor.name.c_str(), selected))
		{
			stage.SetSelected(static_cast<int>(i));
		}

		const PresenceState state = actor.presence.GetState();
		const bool connectionLost = actor.presence.IsConnectionLost();
		ImGui::TextDisabled("%s", GetActorTierName(actor.tier));
		ImGui::SameLine();
		ImGui::TextColored(PresenceColor(state, connectionLost), "%s",
			connectionLost ? U8("接続不安定") : GetPresenceStateName(state));
		ImGui::SameLine();
		ImGui::TextDisabled(U8("回線: %s"), actor.testDisconnected ? U8("切断") : U8("良好"));

		bool manualAway = actor.presence.IsManualAway();
		if (ImGui::Checkbox(U8("手動離席"), &manualAway))
		{
			actor.presence.SetManualAway(manualAway);
		}
		ImGui::SameLine();
		if (ImGui::SmallButton(U8("外す")))
		{
			removeIndex = static_cast<int>(i);
		}

		// トラッキングと通信が入るまでのテスト用
		ImGui::TextDisabled(U8("テスト:"));
		ImGui::SameLine();
		ImGui::Checkbox(U8("顔ロスト"), &actor.testFaceLost);
		ImGui::SameLine();
		ImGui::Checkbox(U8("回線断"), &actor.testDisconnected);

		ImGui::Separator();
		ImGui::PopID();
	}
	if (removeIndex >= 0)
	{
		stage.RemoveActor(static_cast<size_t>(removeIndex));
	}

	// 参加者を追加（通信が入るまではローカルのモデルを並べる）
	if (stage.GetActorCount() < Stage::MaxActors && !_models.empty())
	{
		_modelChoice = std::clamp(_modelChoice, 0, static_cast<int>(_models.size()) - 1);
		const std::string preview = ToUtf8(_models[_modelChoice].filename());
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo("##Model", preview.c_str()))
		{
			for (int i = 0; i < static_cast<int>(_models.size()); ++i)
			{
				const std::string name = ToUtf8(_models[i].filename());
				if (ImGui::Selectable(name.c_str(), i == _modelChoice))
				{
					_modelChoice = i;
				}
			}
			ImGui::EndCombo();
		}
		if (ImGui::Button(U8("参加者を追加"), ImVec2(-1.0f, 0.0f)))
		{
			stage.AddActor(_models[_modelChoice], ToUtf8(_models[_modelChoice].filename()));
		}
	}
	else if (stage.GetActorCount() >= Stage::MaxActors)
	{
		ImGui::TextDisabled(U8("ルームは最大 %d 人です"), static_cast<int>(Stage::MaxActors));
	}

	// 離席の設定（選んでいる参加者）
	const int selected = stage.GetSelected();
	if (selected >= 0 && selected < static_cast<int>(stage.GetActorCount()))
	{
		ImGui::Spacing();
		if (ImGui::CollapsingHeader(U8("離席の設定"), ImGuiTreeNodeFlags_DefaultOpen))
		{
			PresenceStateMachine::Settings& settings = stage.GetActor(selected).presence.GetSettings();
			ImGui::SliderFloat(U8("離席まで"), &settings.awayDelay, 1.0f, 10.0f, U8("%.1f 秒"));
			ImGui::SliderFloat(U8("復帰まで"), &settings.returnDelay, 0.1f, 3.0f, U8("%.1f 秒"));
		}
	}

	ImGui::End();
}

// ============================================================================
// 右：ルックパネル
// ============================================================================
void StageUI::DrawLookPanel(Stage& stage, float x, float y, float width, float height)
{
	ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
	ImGui::Begin(U8("ルック"), nullptr, PanelFlags);

	bool hostLock = stage.IsHostLocked();
	if (ImGui::Checkbox(U8("ホストロック"), &hostLock))
	{
		stage.SetHostLocked(hostLock);
	}
	ImGui::SameLine();
	ImGui::TextDisabled(U8("（参加者はルックを変更できない）"));

	StageLook& look = stage.GetLook();
	ImGui::PushItemWidth(-110.0f);

	// プリセット
	ImGui::SeparatorText(U8("プリセット"));
	for (int i = 0; i < static_cast<int>(StagePreset::Count); ++i)
	{
		if (i > 0)
		{
			ImGui::SameLine();
		}
		const StagePreset preset = static_cast<StagePreset>(i);
		const bool current = look.preset == preset;
		if (current)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
		}
		if (ImGui::Button(GetStagePresetName(preset), ImVec2((width - 40.0f) / 3.0f, 0.0f)))
		{
			look.ApplyPreset(preset);
		}
		if (current)
		{
			ImGui::PopStyleColor();
		}
	}

	ImGui::SeparatorText(U8("基本"));
	ImGui::SliderFloat(U8("明るさ"), &look.brightness, -1.0f, 1.0f, "%.2f");
	ImGui::SliderFloat(U8("暖かさ"), &look.warmth, -1.0f, 1.0f, "%.2f");
	ImGui::SliderFloat(U8("ムード"), &look.mood, 0.0f, 1.0f, "%.2f");

	if (ImGui::Button(U8("見た目を揃える")))
	{
		stage.NormalizeLooks();
	}
	if (stage.IsNormalized())
	{
		ImGui::SameLine();
		if (ImGui::Button(U8("解除")))
		{
			stage.ResetNormalize();
		}
		ImGui::SameLine();
		ImGui::TextDisabled(U8("補正中"));
	}

	if (ImGui::CollapsingHeader(U8("詳細")))
	{
		ImGui::SeparatorText(U8("光源"));
		ImGui::SliderFloat(U8("向き"), &look.lightAngle, 0.0f, 360.0f, U8("%.0f 度"));
		ImGui::SliderFloat(U8("擬似ライト"), &look.lightStrength, 0.0f, 1.0f, "%.2f");

		ImGui::SeparatorText(U8("影"));
		ImGui::SliderFloat(U8("濃さ##影"), &look.shadowStrength, 0.0f, 1.0f, "%.2f");
		ImGui::SliderFloat(U8("距離##影"), &look.shadowDistance, 0.0f, 0.06f, "%.3f");

		ImGui::SeparatorText(U8("リムライト"));
		ImGui::ColorEdit3(U8("色##リム"), look.rimColor);
		ImGui::SliderFloat(U8("強さ##リム"), &look.rimStrength, 0.0f, 1.0f, "%.2f");
		ImGui::SliderFloat(U8("幅##リム"), &look.rimWidth, 1.0f, 10.0f, "%.1f px");

		ImGui::SeparatorText(U8("ブルーム"));
		ImGui::SliderFloat(U8("しきい値"), &look.bloomThreshold, 0.3f, 1.0f, "%.2f");
		ImGui::SliderFloat(U8("強さ##ブルーム"), &look.bloomIntensity, 0.0f, 1.5f, "%.2f");

		ImGui::SeparatorText("LUT");
		if (ImGui::BeginCombo(U8("種類##LUT"), GetStageLutName(look.lut)))
		{
			for (int i = 0; i < static_cast<int>(StageLut::Count); ++i)
			{
				const StageLut lut = static_cast<StageLut>(i);
				if (ImGui::Selectable(GetStageLutName(lut), look.lut == lut))
				{
					look.lut = lut;
				}
			}
			ImGui::EndCombo();
		}
		ImGui::SliderFloat(U8("強さ##LUT"), &look.lutStrength, 0.0f, 1.0f, "%.2f");

		ImGui::SeparatorText(U8("ビネット"));
		ImGui::SliderFloat(U8("強さ##ビネット"), &look.vignette, 0.0f, 1.0f, "%.2f");
	}

	ImGui::PopItemWidth();

	// ホットキー
	ImGui::Spacing();
	ImGui::SeparatorText(U8("ホットキー（配信中も有効）"));
	const char* names[] = { U8("暗転"), U8("スポットライト"), U8("自分を離席") };
	for (int i = 0; i < static_cast<int>(StageHotkey::Count); ++i)
	{
		const StageHotkey hotkey = static_cast<StageHotkey>(i);
		ImGui::Text("%s  %s", OutputWindow::GetHotkeyLabel(hotkey), names[i]);
		if (_output != nullptr && !_output->IsHotkeyRegistered(hotkey))
		{
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), U8("（他のアプリが使用中）"));
		}
	}

	ImGui::End();
}

// ============================================================================
// 中央：プレビュー
// ============================================================================
void StageUI::DrawPreview(Stage& stage, float x, float y, float width, float height)
{
	ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
	ImGui::Begin("##Preview", nullptr, PanelFlags | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground |
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);

	// 16:9 で収まる大きさ（余白 12px）
	const float margin = 12.0f;
	const float aspect = static_cast<float>(StageRenderer::Width) / StageRenderer::Height;
	float previewWidth = (std::max)(width - margin * 2.0f, 16.0f);
	float previewHeight = previewWidth / aspect;
	if (previewHeight > height - margin * 2.0f)
	{
		previewHeight = (std::max)(height - margin * 2.0f, 9.0f);
		previewWidth = previewHeight * aspect;
	}
	const float left = x + (width - previewWidth) * 0.5f;
	const float top = y + (height - previewHeight) * 0.5f;

	_previewViewport = { left, top, previewWidth, previewHeight, 0.0f, 1.0f };
	_previewScissor = { static_cast<LONG>(left), static_cast<LONG>(top),
		static_cast<LONG>(left + previewWidth), static_cast<LONG>(top + previewHeight) };

	// ドラッグで移動、ホイールで拡大縮小
	ImGui::SetCursorScreenPos(ImVec2(left, top));
	ImGui::InvisibleButton("##PreviewArea", ImVec2(previewWidth, previewHeight));
	const ImGuiIO& io = ImGui::GetIO();
	auto toNdc = [&](const ImVec2& p)
	{
		return ImVec2((p.x - left) / previewWidth * 2.0f - 1.0f, 1.0f - (p.y - top) / previewHeight * 2.0f);
	};

	if (ImGui::IsItemActivated())
	{
		const ImVec2 ndc = toNdc(io.MousePos);
		const int hit = stage.HitTest(ndc.x, ndc.y);
		if (hit >= 0)
		{
			stage.SetSelected(hit);
		}
		_dragging = hit >= 0;
	}
	if (!ImGui::IsItemActive())
	{
		_dragging = false;
	}
	const int selected = stage.GetSelected();
	if (_dragging && selected >= 0 && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f))
	{
		stage.MoveActor(static_cast<size_t>(selected), io.MouseDelta.x / previewWidth * 2.0f, -io.MouseDelta.y / previewHeight * 2.0f);
	}
	if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f)
	{
		const ImVec2 ndc = toNdc(io.MousePos);
		const int hit = stage.HitTest(ndc.x, ndc.y);
		const int target = (hit >= 0) ? hit : selected;
		if (target >= 0)
		{
			stage.ScaleActor(static_cast<size_t>(target), std::pow(1.08f, io.MouseWheel));
		}
	}

	// 名前・状態・選択枠（プレビューだけ。出力ウィンドウには出さない）
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->PushClipRect(ImVec2(left, top), ImVec2(left + previewWidth, top + previewHeight), true);
	for (size_t i = 0; i < stage.GetActorCount(); ++i)
	{
		StageActor& actor = stage.GetActor(i);
		float l, r, b, t;
		if (!actor.live2d->GetScreenBounds(l, r, b, t))
		{
			continue;
		}
		const ImVec2 topLeft = NdcToScreen(_previewViewport, l, t);
		const ImVec2 bottomRight = NdcToScreen(_previewViewport, r, b);
		if (static_cast<int>(i) == selected)
		{
			drawList->AddRect(topLeft, bottomRight, IM_COL32(255, 200, 80, 200), 4.0f, 0, 2.0f);
		}

		const PresenceState state = actor.presence.GetState();
		const bool connectionLost = actor.presence.IsConnectionLost();
		std::string text = actor.name;
		if (connectionLost)
		{
			text += U8("  接続不安定");
		}
		else if (state != PresenceState::Present)
		{
			text += std::string("  ") + GetPresenceStateName(state);
		}
		const ImVec2 size = ImGui::CalcTextSize(text.c_str());
		const ImVec2 pos((topLeft.x + bottomRight.x - size.x) * 0.5f, (std::max)(topLeft.y + 4.0f, top + 4.0f));
		drawList->AddRectFilled(ImVec2(pos.x - 6.0f, pos.y - 2.0f), ImVec2(pos.x + size.x + 6.0f, pos.y + size.y + 2.0f), IM_COL32(0, 0, 0, 150), 6.0f);
		drawList->AddText(pos, ImGui::ColorConvertFloat4ToU32(PresenceColor(state, connectionLost)), text.c_str());
	}
	drawList->PopClipRect();

	ImGui::SetCursorScreenPos(ImVec2(left + 8.0f, top + previewHeight - 26.0f));
	ImGui::TextDisabled(U8("ドラッグ: 移動　ホイール: 拡大縮小"));

	ImGui::End();
}
