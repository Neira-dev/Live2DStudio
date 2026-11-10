#pragma once

#include <Windows.h>
#include <string>
#include <functional>
#include <vector>

class Window
{
public:
	struct WindowClass {
		LPCWSTR className = L"WindowClass";
		UINT style = CS_CLASSDC | CS_DBLCLKS;
		HICON hIcon = nullptr;
		HICON hIconSm = nullptr;
		HCURSOR hCursor = nullptr;
		HBRUSH hbrBackground = nullptr;
	};

	struct WindowSettings
	{
		DWORD style = WS_OVERLAPPEDWINDOW;
		DWORD exStyle = WS_EX_OVERLAPPEDWINDOW;
		WCHAR title[256] = L"Window";
	};

	struct WindowDesc
	{
		HINSTANCE hInstance = nullptr;
		int nCmdShow;
		UINT width = 800;
		UINT height = 600;
		WindowClass windowClass = {};
		WindowSettings windowSettings = {};
	};

	/**
	 * @brief メッセージハンドラ
	 * @details trueを返すと以降のデフォルト処理をスキップする。falseを返すと通常のWndProcの処理に続けて渡される。
	 */
	using MessageHandler = std::function<bool(HWND, UINT, WPARAM, LPARAM, LRESULT&)>;

	/**
	 * @brief ウィンドウプロシージャ
	 * @details 継承先でoverrideすることで既定の処理を置き換えられる
	 */
	virtual LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

public:
	Window(WindowDesc desc);
	virtual ~Window();

	/**
	 * @brief メインウィンドウのポインターを取得する
	 * @return Window* メインウィンドウのポインター
	 */
	HWND GetHwnd() const { return _hWnd; }

	/**
	 * @brief ウィンドウの設定情報を取得する
	 * @return WindowDesc ウィンドウの設定情報
	 */
	WindowDesc GetDesc() const { return _desc; }

	/**
	 * @brief WndProc に処理を追加する
	 * @details 追加した順に呼ばれ、trueを返したハンドラ以降の処理はスキップされる
	 * @param handler 追加するハンドラ
	 * @return Window&
	 */
	Window& AddWndProc(MessageHandler handler);

	/**
	 * @brief メッセージハンドラを追加する（AddWndProc と同じ）
	 * @param handler 追加するハンドラ
	 * @return Window&
	 */
	Window& operator+=(MessageHandler handler) { return AddWndProc(std::move(handler)); }

protected:
	/**
	 * @brief ウインドウ作成を行う 
	 */
	virtual void OnCreate();

	/**
	 * @brief WinAPIに登録する静的ウィンドウプロシージャ
	 * @details GWLP_USERDATAからthisを復元し、仮想WndProcへ処理を委譲する
	 */
	static LRESULT CALLBACK StaticWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

protected:
	HWND _hWnd; // ウィンドウハンドル
	WindowDesc _desc; // ウィンドウの設定情報
	std::vector<MessageHandler> _messageHandlers; // += で追加されたメッセージハンドラ
};
