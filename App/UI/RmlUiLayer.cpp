#include "RmlUiLayer.h"

#include <RmlUi/Core.h>

#include <windowsx.h>
#include <chrono>
#include <filesystem>
#include <string>

#include "RmlRenderer_D3D12.h"

// ============================================================================
// SystemInterface：時間・ログ・カーソル・クリップボード
// ============================================================================
class RmlSystem_Win32 : public Rml::SystemInterface
{
public:
	explicit RmlSystem_Win32(HWND hwnd) : _hwnd(hwnd), _start(std::chrono::steady_clock::now())
	{
		_cursorDefault = LoadCursor(nullptr, IDC_ARROW);
		_cursorMove = LoadCursor(nullptr, IDC_SIZEALL);
		_cursorPointer = LoadCursor(nullptr, IDC_HAND);
		_cursorText = LoadCursor(nullptr, IDC_IBEAM);
	}

	double GetElapsedTime() override
	{
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - _start).count();
	}

	bool LogMessage(Rml::Log::Type type, const Rml::String& message) override
	{
		const char* prefix = (type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT) ? "[RmlUi][Error] "
			: (type == Rml::Log::LT_WARNING) ? "[RmlUi][Warning] " : "[RmlUi] ";
		OutputDebugStringA((prefix + message + "\n").c_str());
		return true;
	}

	void SetMouseCursor(const Rml::String& name) override
	{
		HCURSOR cursor = _cursorDefault;
		if (name == "move")
		{
			cursor = _cursorMove;
		}
		else if (name == "pointer")
		{
			cursor = _cursorPointer;
		}
		else if (name == "text")
		{
			cursor = _cursorText;
		}
		_current = cursor;
		SetCursor(cursor);
	}

	HCURSOR GetCurrentCursor() const { return _current; }

	void SetClipboardText(const Rml::String& text) override
	{
		const std::wstring wide = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size())).wstring();
		if (!OpenClipboard(_hwnd))
		{
			return;
		}
		EmptyClipboard();
		const size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
		if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes))
		{
			memcpy(GlobalLock(memory), wide.c_str(), bytes);
			GlobalUnlock(memory);
			SetClipboardData(CF_UNICODETEXT, memory);
		}
		CloseClipboard();
	}

	void GetClipboardText(Rml::String& text) override
	{
		text.clear();
		if (!OpenClipboard(_hwnd))
		{
			return;
		}
		if (HANDLE data = GetClipboardData(CF_UNICODETEXT))
		{
			const wchar_t* wide = static_cast<const wchar_t*>(GlobalLock(data));
			if (wide != nullptr)
			{
				const std::u8string utf8 = std::filesystem::path(wide).u8string();
				text.assign(reinterpret_cast<const char*>(utf8.data()), utf8.size());
			}
			GlobalUnlock(data);
		}
		CloseClipboard();
	}

private:
	HWND _hwnd;
	std::chrono::steady_clock::time_point _start;
	HCURSOR _cursorDefault, _cursorMove, _cursorPointer, _cursorText;
	HCURSOR _current = nullptr;
};

// ============================================================================
// 入力の変換
// ============================================================================
namespace
{
	int GetKeyModifiers()
	{
		int state = 0;
		if (GetKeyState(VK_CONTROL) & 0x8000) state |= Rml::Input::KM_CTRL;
		if (GetKeyState(VK_SHIFT) & 0x8000) state |= Rml::Input::KM_SHIFT;
		if (GetKeyState(VK_MENU) & 0x8000) state |= Rml::Input::KM_ALT;
		return state;
	}

	Rml::Input::KeyIdentifier ConvertKey(WPARAM key)
	{
		using namespace Rml::Input;
		if (key >= 'A' && key <= 'Z') return static_cast<KeyIdentifier>(KI_A + (key - 'A'));
		if (key >= '0' && key <= '9') return static_cast<KeyIdentifier>(KI_0 + (key - '0'));
		switch (key)
		{
		case VK_BACK: return KI_BACK;
		case VK_TAB: return KI_TAB;
		case VK_RETURN: return KI_RETURN;
		case VK_ESCAPE: return KI_ESCAPE;
		case VK_SPACE: return KI_SPACE;
		case VK_PRIOR: return KI_PRIOR;
		case VK_NEXT: return KI_NEXT;
		case VK_END: return KI_END;
		case VK_HOME: return KI_HOME;
		case VK_LEFT: return KI_LEFT;
		case VK_UP: return KI_UP;
		case VK_RIGHT: return KI_RIGHT;
		case VK_DOWN: return KI_DOWN;
		case VK_DELETE: return KI_DELETE;
		case VK_INSERT: return KI_INSERT;
		default: return KI_UNKNOWN;
		}
	}
}

RmlUiLayer::RmlUiLayer() = default;

RmlUiLayer::~RmlUiLayer()
{
	Shutdown();
}

bool RmlUiLayer::Initialize(const Desc& desc)
{
	_hwnd = desc.hwnd;
	_width = desc.width;
	_height = desc.height;

	_renderer = std::make_unique<RmlRenderer_D3D12>();
	if (!_renderer->Initialize(desc.device, desc.renderTargetFormat, desc.frameCount))
	{
		_renderer.reset();
		return false;
	}
	_system = std::make_unique<RmlSystem_Win32>(desc.hwnd);

	Rml::SetRenderInterface(_renderer.get());
	Rml::SetSystemInterface(_system.get());
	if (!Rml::Initialise())
	{
		return false;
	}
	_initialized = true;

	// 日本語フォント（最初に読み込んだものが既定、残りは文字が無いときの予備）
	const char* fonts[] =
	{
		"C:/Windows/Fonts/meiryo.ttc",
		"C:/Windows/Fonts/meiryob.ttc",
		"C:/Windows/Fonts/YuGothM.ttc",
		"C:/Windows/Fonts/segoeui.ttf",
	};
	bool loaded = false;
	for (const char* font : fonts)
	{
		std::error_code ec;
		if (std::filesystem::exists(font, ec))
		{
			loaded |= Rml::LoadFontFace(font, !loaded);
		}
	}
	if (!loaded)
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "No font could be loaded.");
	}

	_context = Rml::CreateContext("main", Rml::Vector2i(static_cast<int>(_width), static_cast<int>(_height)));
	return _context != nullptr;
}

void RmlUiLayer::Shutdown()
{
	if (_initialized)
	{
		if (_context != nullptr)
		{
			Rml::RemoveContext(_context->GetName());
			_context = nullptr;
		}
		Rml::Shutdown();
		_initialized = false;
	}
	if (_renderer)
	{
		_renderer->Release();
		_renderer.reset();
	}
	_system.reset();
}

void RmlUiLayer::Update()
{
	if (_context != nullptr)
	{
		_context->Update();
	}
}

void RmlUiLayer::Render(ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE rtv)
{
	if (_context == nullptr || !_renderer)
	{
		return;
	}
	_renderer->BeginFrame(commandList, rtv, _width, _height);
	_context->Render();
	_renderer->EndFrame();
}

bool RmlUiLayer::HandleMessage(HWND, UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result)
{
	if (_context == nullptr)
	{
		return false;
	}
	result = 0;
	const int modifiers = GetKeyModifiers();
	switch (message)
	{
	case WM_MOUSEMOVE:
		_context->ProcessMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), modifiers);
		return false;
	case WM_LBUTTONDOWN:
		SetCapture(_hwnd);
		_context->ProcessMouseButtonDown(0, modifiers);
		return false;
	case WM_LBUTTONUP:
		ReleaseCapture();
		_context->ProcessMouseButtonUp(0, modifiers);
		return false;
	case WM_RBUTTONDOWN:
		_context->ProcessMouseButtonDown(1, modifiers);
		return false;
	case WM_RBUTTONUP:
		_context->ProcessMouseButtonUp(1, modifiers);
		return false;
	case WM_MBUTTONDOWN:
		_context->ProcessMouseButtonDown(2, modifiers);
		return false;
	case WM_MBUTTONUP:
		_context->ProcessMouseButtonUp(2, modifiers);
		return false;
	case WM_MOUSEWHEEL:
		_context->ProcessMouseWheel(-static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA, modifiers);
		return false;
	case WM_MOUSELEAVE:
		_context->ProcessMouseLeave();
		return false;
	case WM_KEYDOWN:
		_context->ProcessKeyDown(ConvertKey(wParam), modifiers);
		return false;
	case WM_KEYUP:
		_context->ProcessKeyUp(ConvertKey(wParam), modifiers);
		return false;
	case WM_CHAR:
	{
		// 制御文字以外を文字入力として渡す（UTF-16 → UTF-32）
		static wchar_t highSurrogate = 0;
		const wchar_t c = static_cast<wchar_t>(wParam);
		if (c >= 0xD800 && c <= 0xDBFF)
		{
			highSurrogate = c;
			return false;
		}
		char32_t code = c;
		if (c >= 0xDC00 && c <= 0xDFFF && highSurrogate != 0)
		{
			code = 0x10000 + ((highSurrogate - 0xD800) << 10) + (c - 0xDC00);
			highSurrogate = 0;
		}
		if (code >= 32 && code != 127)
		{
			_context->ProcessTextInput(static_cast<Rml::Character>(code));
		}
		return false;
	}
	case WM_SETCURSOR:
		if (LOWORD(lParam) == HTCLIENT && _system && _system->GetCurrentCursor() != nullptr)
		{
			SetCursor(_system->GetCurrentCursor());
			result = TRUE;
			return true;
		}
		return false;
	default:
		return false;
	}
}
