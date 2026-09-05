#include "settings_ui.h"

#include <windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int show)
{
	HWND hwnd = NrSettingsCreate(nullptr, nullptr, false);
	if (!hwnd)
		return 1;
	ShowWindow(hwnd, show);
	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0)) {
		if (!IsDialogMessageW(hwnd, &msg)) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}
	return (int)msg.wParam;
}
