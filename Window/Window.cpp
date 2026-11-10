#include "Window.h"

LRESULT CALLBACK Window::StaticWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	Window* self = nullptr;

	if (message == WM_NCCREATE)
	{
		CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
		self = reinterpret_cast<Window*>(cs->lpCreateParams);
		SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
		self->_hWnd = hWnd;
	}
	else
	{
		self = reinterpret_cast<Window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
	}

	if (self != nullptr)
	{
		return self->WndProc(hWnd, message, wParam, lParam);
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}

LRESULT Window::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	LRESULT result = 0;
	for (auto& handler : _messageHandlers)
	{
		if (handler(hWnd, message, wParam, lParam, result))
		{
			return result;
		}
	}

	switch (message)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

Window& Window::AddWndProc(MessageHandler handler)
{
	_messageHandlers.push_back(std::move(handler));
	return *this;
}

Window::Window(WindowDesc desc)
	: _hWnd(nullptr)
{
	_desc = desc;

	WNDCLASSEX wcex = {};
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.hInstance = desc.hInstance;
	wcex.lpszClassName = desc.windowClass.className;
	wcex.style = desc.windowClass.style;
	wcex.lpfnWndProc = StaticWndProc;

	if (desc.windowClass.hIcon == nullptr)
		wcex.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
	else
		wcex.hIcon = desc.windowClass.hIcon;

	if (desc.windowClass.hIconSm == nullptr)
		wcex.hIconSm = wcex.hIcon;
	else
		wcex.hIconSm = desc.windowClass.hIconSm;

	if (desc.windowClass.hCursor == nullptr)
		wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	else
		wcex.hCursor = desc.windowClass.hCursor;

	wcex.hbrBackground = desc.windowClass.hbrBackground;

	if (!RegisterClassEx(&wcex))
	{
		MessageBox(nullptr, L"ウィンドウクラスの登録に失敗しました。", L"エラー", MB_OK | MB_ICONERROR);
		return;
	}

	OnCreate();

	if(_hWnd == nullptr)
	{
		MessageBox(nullptr, L"ウィンドウの作成に失敗しました。", L"エラー", MB_OK | MB_ICONERROR);
		return;
	}

	ShowWindow(_hWnd, desc.nCmdShow);
	UpdateWindow(_hWnd);
}

Window::~Window()
{

}

void Window::OnCreate()
{
	RECT rc = { 0, 0, static_cast<LONG>(_desc.width), static_cast<LONG>(_desc.height) };
	AdjustWindowRectEx(&rc, _desc.windowSettings.style, false, _desc.windowSettings.exStyle);

	_hWnd = CreateWindowEx(
		_desc.windowSettings.exStyle,
		_desc.windowClass.className,
		_desc.windowSettings.title,
		_desc.windowSettings.style,
		CW_USEDEFAULT, CW_USEDEFAULT,
		rc.right - rc.left, rc.bottom - rc.top,
		HWND_DESKTOP, NULL, _desc.hInstance, this);
}
