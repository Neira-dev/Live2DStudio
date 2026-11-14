#include "StageView.h"

#include <RmlUi/Core.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>

#include "Stage.h"
#include "OutputWindow.h"
#include "StageRenderer.h"
#include "../Cubism/Live2D.h"

#define U8(text) reinterpret_cast<const char*>(u8##text)

namespace fs = std::filesystem;

namespace
{
	// 参加者 1 人分の表示用データ
	struct ActorView
	{
		Rml::String name;
		Rml::String tier;
		Rml::String state;
		Rml::String stateClass;
		Rml::String link;
		Rml::String label;
		bool manualAway = false;
		bool faceLost = false;
		bool disconnected = false;
		bool selected = false;
		Rml::String labelLeft = "0px";
		Rml::String labelTop = "0px";
		Rml::String boxLeft = "0px";
		Rml::String boxTop = "0px";
		Rml::String boxWidth = "0px";
		Rml::String boxHeight = "0px";

		bool operator==(const ActorView& o) const
		{
			return name == o.name && tier == o.tier && state == o.state && stateClass == o.stateClass && link == o.link &&
				label == o.label && manualAway == o.manualAway && faceLost == o.faceLost && disconnected == o.disconnected &&
				selected == o.selected && labelLeft == o.labelLeft && labelTop == o.labelTop && boxLeft == o.boxLeft &&
				boxTop == o.boxTop && boxWidth == o.boxWidth && boxHeight == o.boxHeight;
		}
	};

	Rml::String Px(float value)
	{
		return Rml::CreateString("%.0fpx", value);
	}

	const char* StateClass(PresenceState state, bool connectionLost)
	{
		if (connectionLost)
		{
			return "lost";
		}
		switch (state)
		{
		case PresenceState::Unstable: return "unstable";
		case PresenceState::Away: return "away";
		case PresenceState::Returning: return "returning";
		default: return "present";
		}
	}
}

// ============================================================================
// 実装（RmlUi の型をヘッダーに出さないため Impl にまとめる）
// ============================================================================
struct StageView::Impl : public Rml::EventListener
{
	Rml::Context* context = nullptr;
	Rml::ElementDocument* document = nullptr;
	Rml::DataModelHandle model;
	Stage* stage = nullptr;
	OutputWindow* output = nullptr;
	StageView* owner = nullptr;

	// ---- データモデルの変数 ----
	std::vector<Rml::String> layouts;
	std::vector<Rml::String> presets;
	std::vector<Rml::String> luts;
	std::vector<Rml::String> models;
	std::vector<fs::path> modelPaths;
	std::vector<ActorView> actors;

	int layout = 0;
	Rml::String roomCode;
	bool hasRoom = false;
	bool blackout = false;
	bool spotlight = false;
	bool selfAway = false;
	bool outputVisible = true;
	bool previewChroma = false;
	bool roomFull = false;
	int maxActors = static_cast<int>(Stage::MaxActors);
	bool hasSelection = false;
	Rml::String selectedName;
	float awayDelay = 3.0f;
	float returnDelay = 0.5f;

	bool hostLock = false;
	bool normalized = false;
	int preset = 0;
	float brightness = 0.0f, warmth = 0.0f, mood = 0.0f;
	bool detailOpen = false;
	float lightAngle = 0.0f, lightStrength = 0.0f;
	float shadowStrength = 0.0f, shadowDistance = 0.0f;
	float rimR = 1.0f, rimG = 1.0f, rimB = 1.0f, rimStrength = 0.0f, rimWidth = 3.0f;
	float bloomThreshold = 0.8f, bloomIntensity = 0.0f;
	int lut = 0;
	float lutStrength = 1.0f;
	float vignette = 0.0f;

	Rml::String hotkeyBlackout, hotkeySpotlight, hotkeyAway;
	bool hotkeyBlackoutOk = true, hotkeySpotlightOk = true, hotkeyAwayOk = true;

	// ---- プレビュー ----
	Rml::Element* previewElement = nullptr;
	Rml::Element* overlayElement = nullptr;
	bool dragging = false;
	Rml::Vector2f lastMouse;
	Rml::Vector2f hoverMouse;  // ホイール操作の位置（mousescroll にはマウス位置が無いため）
	float overlayRect[4] = {};

	// 値が変わったときだけ UI を更新する
	template <class T>
	void Sync(T& variable, const T& value, const char* name)
	{
		if (!(variable == value))
		{
			variable = value;
			model.DirtyVariable(name);
		}
	}

	bool Bind(Rml::Context* ctx)
	{
		Rml::DataModelConstructor constructor = ctx->CreateDataModel("stage");
		if (!constructor)
		{
			return false;
		}

		if (auto handle = constructor.RegisterStruct<ActorView>())
		{
			handle.RegisterMember("name", &ActorView::name);
			handle.RegisterMember("tier", &ActorView::tier);
			handle.RegisterMember("state", &ActorView::state);
			handle.RegisterMember("state_class", &ActorView::stateClass);
			handle.RegisterMember("link", &ActorView::link);
			handle.RegisterMember("label", &ActorView::label);
			handle.RegisterMember("manual_away", &ActorView::manualAway);
			handle.RegisterMember("face_lost", &ActorView::faceLost);
			handle.RegisterMember("disconnected", &ActorView::disconnected);
			handle.RegisterMember("selected", &ActorView::selected);
			handle.RegisterMember("label_left", &ActorView::labelLeft);
			handle.RegisterMember("label_top", &ActorView::labelTop);
			handle.RegisterMember("box_left", &ActorView::boxLeft);
			handle.RegisterMember("box_top", &ActorView::boxTop);
			handle.RegisterMember("box_width", &ActorView::boxWidth);
			handle.RegisterMember("box_height", &ActorView::boxHeight);
		}
		constructor.RegisterArray<std::vector<ActorView>>();
		constructor.RegisterArray<std::vector<Rml::String>>();

		constructor.Bind("layouts", &layouts);
		constructor.Bind("presets", &presets);
		constructor.Bind("luts", &luts);
		constructor.Bind("models", &models);
		constructor.Bind("actors", &actors);

		constructor.Bind("layout", &layout);
		constructor.Bind("room_code", &roomCode);
		constructor.Bind("has_room", &hasRoom);
		constructor.Bind("blackout", &blackout);
		constructor.Bind("spotlight", &spotlight);
		constructor.Bind("self_away", &selfAway);
		constructor.Bind("output_visible", &outputVisible);
		constructor.Bind("preview_chroma", &previewChroma);
		constructor.Bind("room_full", &roomFull);
		constructor.Bind("max_actors", &maxActors);
		constructor.Bind("has_selection", &hasSelection);
		constructor.Bind("selected_name", &selectedName);
		constructor.Bind("away_delay", &awayDelay);
		constructor.Bind("return_delay", &returnDelay);

		constructor.Bind("host_lock", &hostLock);
		constructor.Bind("normalized", &normalized);
		constructor.Bind("preset", &preset);
		constructor.Bind("brightness", &brightness);
		constructor.Bind("warmth", &warmth);
		constructor.Bind("mood", &mood);
		constructor.Bind("detail_open", &detailOpen);
		constructor.Bind("light_angle", &lightAngle);
		constructor.Bind("light_strength", &lightStrength);
		constructor.Bind("shadow_strength", &shadowStrength);
		constructor.Bind("shadow_distance", &shadowDistance);
		constructor.Bind("rim_r", &rimR);
		constructor.Bind("rim_g", &rimG);
		constructor.Bind("rim_b", &rimB);
		constructor.Bind("rim_strength", &rimStrength);
		constructor.Bind("rim_width", &rimWidth);
		constructor.Bind("bloom_threshold", &bloomThreshold);
		constructor.Bind("bloom_intensity", &bloomIntensity);
		constructor.Bind("lut", &lut);
		constructor.Bind("lut_strength", &lutStrength);
		constructor.Bind("vignette", &vignette);

		constructor.Bind("hotkey_blackout", &hotkeyBlackout);
		constructor.Bind("hotkey_spotlight", &hotkeySpotlight);
		constructor.Bind("hotkey_away", &hotkeyAway);
		constructor.Bind("hotkey_blackout_ok", &hotkeyBlackoutOk);
		constructor.Bind("hotkey_spotlight_ok", &hotkeySpotlightOk);
		constructor.Bind("hotkey_away_ok", &hotkeyAwayOk);

		// ---- 操作 ----
		auto argInt = [](const Rml::VariantList& args) { return args.empty() ? 0 : args[0].Get<int>(); };

		constructor.BindEventCallback("set_layout", [this, argInt](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args)
		{
			stage->ApplyLayout(static_cast<StageLayout>(std::clamp(argInt(args), 0, static_cast<int>(StageLayout::Count) - 1)));
		});
		constructor.BindEventCallback("create_room", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&)
		{
			// 通信は未実装なので、6桁のコードを作るところまで
			std::random_device device;
			std::uniform_int_distribution<int> digit(0, 9);
			Rml::String code;
			for (int i = 0; i < 6; ++i)
			{
				if (i == 3)
				{
					code += ' ';
				}
				code += static_cast<char>('0' + digit(device));
			}
			Sync(roomCode, code, "room_code");
			Sync(hasRoom, true, "has_room");
		});
		constructor.BindEventCallback("toggle_blackout", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { stage->ToggleBlackout(); });
		constructor.BindEventCallback("toggle_spotlight", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { stage->ToggleSpotlight(); });
		constructor.BindEventCallback("toggle_self_away", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { stage->ToggleManualAway(); });
		constructor.BindEventCallback("output_changed", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&)
		{
			if (output != nullptr)
			{
				output->SetVisible(outputVisible);
			}
		});

		constructor.BindEventCallback("select_actor", [this, argInt](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args)
		{
			stage->SetSelected(argInt(args));
		});
		constructor.BindEventCallback("actor_changed", [this, argInt](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args)
		{
			const int index = argInt(args);
			if (index >= 0 && index < static_cast<int>(actors.size()) && index < static_cast<int>(stage->GetActorCount()))
			{
				StageActor& actor = stage->GetActor(static_cast<size_t>(index));
				actor.presence.SetManualAway(actors[index].manualAway);
				actor.testFaceLost = actors[index].faceLost;
				actor.testDisconnected = actors[index].disconnected;
			}
		});
		constructor.BindEventCallback("remove_actor", [this, argInt](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList& args)
		{
			event.StopPropagation();  // 参加者の選択（親の click）を起こさない
			stage->RemoveActor(static_cast<size_t>(argInt(args)));
		});
		constructor.BindEventCallback("add_actor", [this, argInt](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args)
		{
			const int index = argInt(args);
			if (index >= 0 && index < static_cast<int>(modelPaths.size()))
			{
				stage->AddActor(modelPaths[index], ToUtf8(modelPaths[index].filename()));
			}
		});
		constructor.BindEventCallback("presence_changed", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&)
		{
			const int selected = stage->GetSelected();
			if (selected >= 0 && selected < static_cast<int>(stage->GetActorCount()))
			{
				PresenceStateMachine::Settings& settings = stage->GetActor(static_cast<size_t>(selected)).presence.GetSettings();
				settings.awayDelay = awayDelay;
				settings.returnDelay = returnDelay;
			}
		});

		constructor.BindEventCallback("set_preset", [this, argInt](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args)
		{
			stage->GetLook().ApplyPreset(static_cast<StagePreset>(std::clamp(argInt(args), 0, static_cast<int>(StagePreset::Count) - 1)));
		});
		constructor.BindEventCallback("set_lut", [this, argInt](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args)
		{
			stage->GetLook().lut = static_cast<StageLut>(std::clamp(argInt(args), 0, static_cast<int>(StageLut::Count) - 1));
		});
		constructor.BindEventCallback("look_changed", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { ApplyLook(); });
		constructor.BindEventCallback("normalize", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { stage->NormalizeLooks(); });
		constructor.BindEventCallback("reset_normalize", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { stage->ResetNormalize(); });
		constructor.BindEventCallback("toggle_detail", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&)
		{
			Sync(detailOpen, !detailOpen, "detail_open");
		});

		model = constructor.GetModelHandle();
		return true;
	}

	// UI のスライダーの値をルックに反映する
	void ApplyLook()
	{
		StageLook& look = stage->GetLook();
		look.brightness = brightness;
		look.warmth = warmth;
		look.mood = mood;
		look.lightAngle = lightAngle;
		look.lightStrength = lightStrength;
		look.shadowStrength = shadowStrength;
		look.shadowDistance = shadowDistance;
		look.rimColor[0] = rimR;
		look.rimColor[1] = rimG;
		look.rimColor[2] = rimB;
		look.rimStrength = rimStrength;
		look.rimWidth = rimWidth;
		look.bloomThreshold = bloomThreshold;
		look.bloomIntensity = bloomIntensity;
		look.lutStrength = lutStrength;
		look.vignette = vignette;
	}

	// ---- プレビューのマウス操作 ----
	void ProcessEvent(Rml::Event& event) override
	{
		const Rml::Vector2f mouse(event.GetParameter<float>("mouse_x", 0.0f), event.GetParameter<float>("mouse_y", 0.0f));
		const D3D12_VIEWPORT& viewport = owner->_previewViewport;
		auto toNdc = [&](const Rml::Vector2f& p)
		{
			return Rml::Vector2f((p.x - viewport.TopLeftX) / viewport.Width * 2.0f - 1.0f,
				1.0f - (p.y - viewport.TopLeftY) / viewport.Height * 2.0f);
		};

		switch (event.GetId())
		{
		case Rml::EventId::Mousedown:
			if (event.GetParameter<int>("button", 0) == 0 && event.GetCurrentElement() == previewElement)
			{
				const Rml::Vector2f ndc = toNdc(mouse);
				const int hit = stage->HitTest(ndc.x, ndc.y);
				if (hit >= 0)
				{
					stage->SetSelected(hit);
				}
				dragging = hit >= 0;
				lastMouse = mouse;
			}
			break;
		case Rml::EventId::Mousemove:
			hoverMouse = mouse;
			if (dragging)
			{
				const int selected = stage->GetSelected();
				if (selected >= 0)
				{
					stage->MoveActor(static_cast<size_t>(selected), (mouse.x - lastMouse.x) / viewport.Width * 2.0f,
						-(mouse.y - lastMouse.y) / viewport.Height * 2.0f);
				}
				lastMouse = mouse;
			}
			break;
		case Rml::EventId::Mouseup:
			dragging = false;
			break;
		case Rml::EventId::Mousescroll:
		{
			const float delta = event.GetParameter<float>("wheel_delta_y", 0.0f);
			if (delta != 0.0f)
			{
				const Rml::Vector2f ndc = toNdc(hoverMouse);
				const int hit = stage->HitTest(ndc.x, ndc.y);
				const int target = (hit >= 0) ? hit : stage->GetSelected();
				if (target >= 0)
				{
					stage->ScaleActor(static_cast<size_t>(target), std::pow(1.08f, -delta));
				}
				event.StopPropagation();
			}
			break;
		}
		default:
			break;
		}
	}
};

StageView::StageView() = default;

StageView::~StageView()
{
	Shutdown();
}

bool StageView::Initialize(Rml::Context* context, Stage& stage, OutputWindow* output)
{
	if (context == nullptr)
	{
		return false;
	}
	_impl = std::make_unique<Impl>();
	Impl& impl = *_impl;
	impl.context = context;
	impl.stage = &stage;
	impl.output = output;
	impl.owner = this;

	for (int i = 0; i < static_cast<int>(StageLayout::Count); ++i)
	{
		impl.layouts.push_back(GetStageLayoutName(static_cast<StageLayout>(i)));
	}
	for (int i = 0; i < static_cast<int>(StagePreset::Count); ++i)
	{
		impl.presets.push_back(GetStagePresetName(static_cast<StagePreset>(i)));
	}
	for (int i = 0; i < static_cast<int>(StageLut::Count); ++i)
	{
		impl.luts.push_back(GetStageLutName(static_cast<StageLut>(i)));
	}
	impl.modelPaths = FindModels("Live2D");
	for (const fs::path& path : impl.modelPaths)
	{
		impl.models.push_back(ToUtf8(path.filename()));
	}
	impl.hotkeyBlackout = OutputWindow::GetHotkeyLabel(StageHotkey::Blackout);
	impl.hotkeySpotlight = OutputWindow::GetHotkeyLabel(StageHotkey::Spotlight);
	impl.hotkeyAway = OutputWindow::GetHotkeyLabel(StageHotkey::ManualAway);

	if (!impl.Bind(context))
	{
		return false;
	}

	impl.document = context->LoadDocument("UI/stage.rml");
	if (impl.document == nullptr)
	{
		return false;
	}
	impl.document->Show();

	impl.previewElement = impl.document->GetElementById("preview");
	impl.overlayElement = impl.document->GetElementById("overlay");
	if (impl.previewElement != nullptr)
	{
		impl.previewElement->AddEventListener(Rml::EventId::Mousedown, &impl);
		impl.previewElement->AddEventListener(Rml::EventId::Mousescroll, &impl);
	}
	impl.document->AddEventListener(Rml::EventId::Mousemove, &impl);
	impl.document->AddEventListener(Rml::EventId::Mouseup, &impl);

	Update();
	return true;
}

void StageView::Shutdown()
{
	if (!_impl)
	{
		return;
	}
	Impl& impl = *_impl;
	if (impl.document != nullptr)
	{
		if (impl.previewElement != nullptr)
		{
			impl.previewElement->RemoveEventListener(Rml::EventId::Mousedown, &impl);
			impl.previewElement->RemoveEventListener(Rml::EventId::Mousescroll, &impl);
		}
		impl.document->RemoveEventListener(Rml::EventId::Mousemove, &impl);
		impl.document->RemoveEventListener(Rml::EventId::Mouseup, &impl);
		impl.document->Close();
		impl.document = nullptr;
	}
	if (impl.context != nullptr)
	{
		impl.context->RemoveDataModel("stage");
	}
	_impl.reset();
}

void StageView::GetPreviewBackground(float rgb[3]) const
{
	const bool chroma = _impl && _impl->previewChroma;
	rgb[0] = chroma ? 0.0f : 0.10f;
	rgb[1] = chroma ? 1.0f : 0.11f;
	rgb[2] = chroma ? 0.0f : 0.13f;
}

void StageView::Update()
{
	if (!_impl)
	{
		return;
	}
	Impl& impl = *_impl;
	Stage& stage = *impl.stage;

	// ---- プレビューの範囲（#preview の中に 16:9 で収める）----
	if (impl.previewElement != nullptr)
	{
		const Rml::Vector2f offset = impl.previewElement->GetAbsoluteOffset(Rml::BoxArea::Content);
		const Rml::Vector2f size = impl.previewElement->GetBox().GetSize(Rml::BoxArea::Content);
		const float margin = 12.0f;
		const float aspect = static_cast<float>(StageRenderer::Width) / StageRenderer::Height;
		float width = (std::max)(size.x - margin * 2.0f, 16.0f);
		float height = width / aspect;
		if (height > size.y - margin * 2.0f)
		{
			height = (std::max)(size.y - margin * 2.0f, 9.0f);
			width = height * aspect;
		}
		const float left = offset.x + (size.x - width) * 0.5f;
		const float top = offset.y + (size.y - height) * 0.5f;
		_previewViewport = { left, top, width, height, 0.0f, 1.0f };
		_previewScissor = { static_cast<LONG>(left), static_cast<LONG>(top), static_cast<LONG>(left + width), static_cast<LONG>(top + height) };
		_previewArea = { static_cast<LONG>(offset.x), static_cast<LONG>(offset.y), static_cast<LONG>(offset.x + size.x), static_cast<LONG>(offset.y + size.y) };

		// 名前ラベルなどを置く層をプレビューの範囲に合わせる
		if (impl.overlayElement != nullptr)
		{
			const float rect[4] = { left - offset.x, top - offset.y, width, height };
			if (std::memcmp(rect, impl.overlayRect, sizeof(rect)) != 0)
			{
				std::memcpy(impl.overlayRect, rect, sizeof(rect));
				impl.overlayElement->SetProperty(Rml::PropertyId::Left, Rml::Property(rect[0], Rml::Unit::PX));
				impl.overlayElement->SetProperty(Rml::PropertyId::Top, Rml::Property(rect[1], Rml::Unit::PX));
				impl.overlayElement->SetProperty(Rml::PropertyId::Width, Rml::Property(rect[2], Rml::Unit::PX));
				impl.overlayElement->SetProperty(Rml::PropertyId::Height, Rml::Property(rect[3], Rml::Unit::PX));
			}
		}
	}

	// ---- 上部 ----
	impl.Sync(impl.layout, static_cast<int>(stage.GetLayout()), "layout");
	impl.Sync(impl.blackout, stage.IsBlackout(), "blackout");
	impl.Sync(impl.spotlight, stage.IsSpotlight(), "spotlight");
	impl.Sync(impl.selfAway, stage.GetActorCount() > 0 && stage.GetActor(0).presence.IsManualAway(), "self_away");
	if (impl.output != nullptr)
	{
		impl.Sync(impl.outputVisible, impl.output->IsVisible(), "output_visible");
		impl.Sync(impl.hotkeyBlackoutOk, impl.output->IsHotkeyRegistered(StageHotkey::Blackout), "hotkey_blackout_ok");
		impl.Sync(impl.hotkeySpotlightOk, impl.output->IsHotkeyRegistered(StageHotkey::Spotlight), "hotkey_spotlight_ok");
		impl.Sync(impl.hotkeyAwayOk, impl.output->IsHotkeyRegistered(StageHotkey::ManualAway), "hotkey_away_ok");
	}

	// ---- 参加者 ----
	std::vector<ActorView> actors(stage.GetActorCount());
	for (size_t i = 0; i < stage.GetActorCount(); ++i)
	{
		StageActor& actor = stage.GetActor(i);
		ActorView& view = actors[i];
		const PresenceState state = actor.presence.GetState();
		const bool connectionLost = actor.presence.IsConnectionLost();
		view.name = actor.name;
		view.tier = GetActorTierName(actor.tier);
		view.state = connectionLost ? U8("接続不安定") : GetPresenceStateName(state);
		view.stateClass = StateClass(state, connectionLost);
		view.link = actor.testDisconnected ? U8("切断") : U8("良好");
		view.label = (state == PresenceState::Present && !connectionLost) ? actor.name : actor.name + "  " + view.state;
		view.manualAway = actor.presence.IsManualAway();
		view.faceLost = actor.testFaceLost;
		view.disconnected = actor.testDisconnected;
		view.selected = stage.GetSelected() == static_cast<int>(i);

		// プレビュー上の位置（overlay の左上が原点）
		float l, r, b, t;
		if (actor.live2d->GetScreenBounds(l, r, b, t))
		{
			const float width = _previewViewport.Width;
			const float height = _previewViewport.Height;
			const float x0 = (l * 0.5f + 0.5f) * width;
			const float x1 = (r * 0.5f + 0.5f) * width;
			const float y0 = (0.5f - t * 0.5f) * height;
			const float y1 = (0.5f - b * 0.5f) * height;
			view.boxLeft = Px(x0);
			view.boxTop = Px(y0);
			view.boxWidth = Px(x1 - x0);
			view.boxHeight = Px(y1 - y0);
			// ラベルは枠の上端の中央（RCSS の transform で自分の幅の半分だけ左へずらす）
			view.labelLeft = Px((x0 + x1) * 0.5f);
			view.labelTop = Px((std::max)(y0 + 4.0f, 4.0f));
		}
	}
	if (!(actors == impl.actors))
	{
		impl.actors = std::move(actors);
		impl.model.DirtyVariable("actors");
	}
	impl.Sync(impl.roomFull, stage.GetActorCount() >= Stage::MaxActors, "room_full");

	const int selected = stage.GetSelected();
	const bool hasSelection = selected >= 0 && selected < static_cast<int>(stage.GetActorCount());
	impl.Sync(impl.hasSelection, hasSelection, "has_selection");
	if (hasSelection)
	{
		const StageActor& actor = stage.GetActor(static_cast<size_t>(selected));
		impl.Sync(impl.selectedName, Rml::String(actor.name), "selected_name");
		impl.Sync(impl.awayDelay, actor.presence.GetSettings().awayDelay, "away_delay");
		impl.Sync(impl.returnDelay, actor.presence.GetSettings().returnDelay, "return_delay");
	}

	// ---- ルック ----
	const StageLook& look = stage.GetLook();
	stage.SetHostLocked(impl.hostLock);
	impl.Sync(impl.normalized, stage.IsNormalized(), "normalized");
	impl.Sync(impl.preset, static_cast<int>(look.preset), "preset");
	impl.Sync(impl.brightness, look.brightness, "brightness");
	impl.Sync(impl.warmth, look.warmth, "warmth");
	impl.Sync(impl.mood, look.mood, "mood");
	impl.Sync(impl.lightAngle, look.lightAngle, "light_angle");
	impl.Sync(impl.lightStrength, look.lightStrength, "light_strength");
	impl.Sync(impl.shadowStrength, look.shadowStrength, "shadow_strength");
	impl.Sync(impl.shadowDistance, look.shadowDistance, "shadow_distance");
	impl.Sync(impl.rimR, look.rimColor[0], "rim_r");
	impl.Sync(impl.rimG, look.rimColor[1], "rim_g");
	impl.Sync(impl.rimB, look.rimColor[2], "rim_b");
	impl.Sync(impl.rimStrength, look.rimStrength, "rim_strength");
	impl.Sync(impl.rimWidth, look.rimWidth, "rim_width");
	impl.Sync(impl.bloomThreshold, look.bloomThreshold, "bloom_threshold");
	impl.Sync(impl.bloomIntensity, look.bloomIntensity, "bloom_intensity");
	impl.Sync(impl.lut, static_cast<int>(look.lut), "lut");
	impl.Sync(impl.lutStrength, look.lutStrength, "lut_strength");
	impl.Sync(impl.vignette, look.vignette, "vignette");
}

std::string StageView::ToUtf8(const fs::path& path)
{
	const std::u8string text = path.u8string();
	return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::vector<fs::path> StageView::FindModels(const fs::path& root)
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
