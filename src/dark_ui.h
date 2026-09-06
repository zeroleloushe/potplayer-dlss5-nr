#pragma once
#include <windows.h>
#include <commctrl.h>

// Dark NVIDIA-green widgets used by nr-settings and the installer.

void DuiInit();
HMODULE DuiModule();
HFONT DuiFont(int px, int weight = FW_NORMAL);
HBRUSH DuiBgBrush();
COLORREF DuiBg();
COLORREF DuiFg();
COLORREF DuiMuted();
COLORREF DuiAccent();

HWND DuiSlider(HWND parent, int id, int x, int y, int w, int h);
HWND DuiCheck(HWND parent, int id, const wchar_t *text, int x, int y, int w, int h);
HWND DuiButton(HWND parent, int id, const wchar_t *text, int x, int y, int w, int h, bool primary);
HWND DuiCombo(HWND parent, int id, int x, int y, int w, int h);
HWND DuiLabel(HWND parent, const wchar_t *text, int x, int y, int w, int h, int px = 15, int weight = FW_NORMAL,
              COLORREF color = 0);
void DuiDarkTitlebar(HWND hwnd);
void DuiPaintBackground(HDC hdc, HWND hwnd);
LRESULT DuiColorStatic(WPARAM wParam, COLORREF fg = 0);
