#include <Windows.h>
#include "App.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	App app;

	app.Init(hInstance,nCmdShow);


	// Messages are handled with PeekMessage so Update/Draw run every frame even with no window messages
	MSG msg = {};
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			continue;
		}

		app.Update();
		app.Draw();
	}

	app.UnInit();

	return (int)msg.wParam;
}