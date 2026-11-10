#include <Windows.h>
#include "App.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	App app;

	app.Init(hInstance,nCmdShow);


	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);

		app.Update();
		app.Draw();
	}

	app.UnInit();

	return (int)msg.wParam;
}