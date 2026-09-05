// Live settings panel for DLSS 5 NR. Writes
// %LOCALAPPDATA%\potplayer-dlss5-nr\settings.ini which the plugin reloads
// on the next frame — no need to restart the video.

#include "nr_bridge.h"
#include "settings_ini.h"

#include <commctrl.h>
#include <windows.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")

#include <dwmapi.h>
#include <cstdio>
#include <string>

enum {
	IDC_STYLE = 101,
	IDC_PRESET = 102,
	IDC_INTENSITY = 110,
	IDC_TONE = 111,
	IDC_STRUCTURE = 112,
	IDC_SKIN = 113,
	IDC_INTENSITY_V = 120,
	IDC_TONE_V = 121,
	IDC_STRUCTURE_V = 122,
	IDC_SKIN_V = 123,
	IDC_AUTOMASK = 130,
	IDC_ONTOP = 131,
	IDC_RESET = 140,
	IDC_HINT = 150
};

static HWND g_hwnd;
static HWND g_style, g_preset, g_auto, g_ontop;
static HWND g_tb[4], g_val[4];
static HFONT g_font, g_fontBig;
static HBRUSH g_bg;
static NrUiSettings g_s;

static const wchar_t *kSliderNames[4] = {L"Интенсивность (степень)", L"Тон", L"Структура", L"Кожа"};
static const float kSliderDefault[4] = {1.0f, 1.0f, 1.5f, 2.0f};

static float *Slot(NrUiSettings &s, int i)
{
	switch (i) {
	case 0:
		return &s.intensity;
	case 1:
		return &s.tone;
	case 2:
		return &s.structure;
	default:
		return &s.skin;
	}
}

static int ToTick(float v)
{
	int t = (int)(v * 100.f + 0.5f);
	if (t < 0)
		t = 0;
	if (t > 200)
		t = 200;
	return t;
}

static float FromTick(int t)
{
	return t / 100.f;
}

static void SetValLabel(int i, float v)
{
	wchar_t b[16];
	swprintf(b, 16, L"%.2f", v);
	SetWindowTextW(g_val[i], b);
}

static void Save()
{
	NrSettingsSave(g_s);
}

static void LoadIntoUi()
{
	SendMessageW(g_style, CB_SETCURSEL, (WPARAM)g_s.style, 0);
	SendMessageW(g_preset, CB_SETCURSEL, (WPARAM)g_s.preset, 0);
	SendMessageW(g_auto, BM_SETCHECK, g_s.automask ? BST_CHECKED : BST_UNCHECKED, 0);
	for (int i = 0; i < 4; ++i) {
		SendMessageW(g_tb[i], TBM_SETPOS, TRUE, ToTick(*Slot(g_s, i)));
		SetValLabel(i, *Slot(g_s, i));
	}
}

static HWND AddStatic(HWND parent, const wchar_t *text, int x, int y, int w, int h, int id = 0)
{
	HWND h = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h, parent, (HMENU)(INT_PTR)id,
	                         GetModuleHandleW(nullptr), nullptr);
	SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE);
	return h;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_CREATE: {
		g_bg = CreateSolidBrush(RGB(18, 18, 20));
		g_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
		                     L"Segoe UI");
		g_fontBig = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
		                       L"Segoe UI");
		HWND title = AddStatic(hwnd, L"DLSS 5 Neural Rendering", 20, 16, 400, 28);
		SendMessageW(title, WM_SETFONT, (WPARAM)g_fontBig, TRUE);
		AddStatic(hwnd, L"Крутится на лету, ролик перезапускать не нужно.", 20, 46, 400, 20, IDC_HINT);

		AddStatic(hwnd, L"Стиль", 20, 80, 120, 20);
		g_style = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP, 160, 76,
		                          250, 120, hwnd, (HMENU)IDC_STYLE, GetModuleHandleW(nullptr), nullptr);
		SendMessageW(g_style, WM_SETFONT, (WPARAM)g_font, TRUE);
		SendMessageW(g_style, CB_ADDSTRING, 0, (LPARAM)L"0  Default");
		SendMessageW(g_style, CB_ADDSTRING, 0, (LPARAM)L"1  Natural");
		SendMessageW(g_style, CB_ADDSTRING, 0, (LPARAM)L"2  Cinematic");

		AddStatic(hwnd, L"Пресет", 20, 114, 120, 20);
		g_preset = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP, 160, 110,
		                           250, 140, hwnd, (HMENU)IDC_PRESET, GetModuleHandleW(nullptr), nullptr);
		SendMessageW(g_preset, WM_SETFONT, (WPARAM)g_font, TRUE);
		SendMessageW(g_preset, CB_ADDSTRING, 0, (LPARAM)L"0");
		SendMessageW(g_preset, CB_ADDSTRING, 0, (LPARAM)L"1");
		SendMessageW(g_preset, CB_ADDSTRING, 0, (LPARAM)L"2");
		SendMessageW(g_preset, CB_ADDSTRING, 0, (LPARAM)L"3");

		int y = 160;
		for (int i = 0; i < 4; ++i) {
			AddStatic(hwnd, kSliderNames[i], 20, y, 260, 20);
			g_val[i] = AddStatic(hwnd, L"1.00", 330, y, 80, 20, IDC_INTENSITY_V + i);
			g_tb[i] = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | WS_TABSTOP, 20,
			                          y + 22, 390, 36, hwnd, (HMENU)(INT_PTR)(IDC_INTENSITY + i), GetModuleHandleW(nullptr),
			                          nullptr);
			SendMessageW(g_tb[i], TBM_SETRANGEMIN, FALSE, 0);
			SendMessageW(g_tb[i], TBM_SETRANGEMAX, FALSE, 200);
			SendMessageW(g_tb[i], TBM_SETTICFREQ, 25, 0);
			y += 64;
		}

		g_auto = CreateWindowExW(0, L"BUTTON", L"Автомаска (кожа)",
		                         WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, 20, y, 220, 24, hwnd,
		                         (HMENU)IDC_AUTOMASK, GetModuleHandleW(nullptr), nullptr);
		SendMessageW(g_auto, WM_SETFONT, (WPARAM)g_font, TRUE);
		g_ontop = CreateWindowExW(0, L"BUTTON", L"Поверх окон",
		                          WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, 250, y, 160, 24, hwnd,
		                          (HMENU)IDC_ONTOP, GetModuleHandleW(nullptr), nullptr);
		SendMessageW(g_ontop, WM_SETFONT, (WPARAM)g_font, TRUE);
		SendMessageW(g_ontop, BM_SETCHECK, BST_CHECKED, 0);

		HWND reset = CreateWindowExW(0, L"BUTTON", L"Сброс", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 20,
		                             y + 40, 120, 32, hwnd, (HMENU)IDC_RESET, GetModuleHandleW(nullptr), nullptr);
		SendMessageW(reset, WM_SETFONT, (WPARAM)g_font, TRUE);

		std::wstring hint = L"INI: " + NrSettingsIniPath();
		AddStatic(hwnd, hint.c_str(), 20, y + 84, 400, 36);

		g_s = NrSettingsLoad();
		if (!g_s.loaded)
			g_s = NrUiSettings{};
		LoadIntoUi();
		Save();
		SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		return 0;
	}
	case WM_HSCROLL: {
		HWND tb = (HWND)lParam;
		for (int i = 0; i < 4; ++i) {
			if (tb == g_tb[i]) {
				float v = FromTick((int)SendMessageW(tb, TBM_GETPOS, 0, 0));
				*Slot(g_s, i) = v;
				SetValLabel(i, v);
				Save();
				break;
			}
		}
		return 0;
	}
	case WM_COMMAND: {
		const int id = LOWORD(wParam);
		const int code = HIWORD(wParam);
		if (id == IDC_STYLE && code == CBN_SELCHANGE) {
			g_s.style = (int)SendMessageW(g_style, CB_GETCURSEL, 0, 0);
			Save();
		} else if (id == IDC_PRESET && code == CBN_SELCHANGE) {
			g_s.preset = (int)SendMessageW(g_preset, CB_GETCURSEL, 0, 0);
			Save();
		} else if (id == IDC_AUTOMASK) {
			g_s.automask = SendMessageW(g_auto, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0;
			Save();
		} else if (id == IDC_ONTOP) {
			BOOL on = SendMessageW(g_ontop, BM_GETCHECK, 0, 0) == BST_CHECKED;
			SetWindowPos(hwnd, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		} else if (id == IDC_RESET) {
			g_s = NrUiSettings{};
			g_s.loaded = true;
			LoadIntoUi();
			Save();
		}
		return 0;
	}
	case WM_CTLCOLORSTATIC: {
		HDC hdc = (HDC)wParam;
		SetTextColor(hdc, RGB(245, 245, 247));
		SetBkColor(hdc, RGB(18, 18, 20));
		return (LRESULT)g_bg;
	}
	case WM_ERASEBKGND: {
		RECT rc;
		GetClientRect(hwnd, &rc);
		FillRect((HDC)wParam, &rc, g_bg);
		return 1;
	}
	case WM_DESTROY:
		if (g_bg)
			DeleteObject(g_bg);
		if (g_font)
			DeleteObject(g_font);
		if (g_fontBig)
			DeleteObject(g_fontBig);
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show)
{
	SetProcessDPIAware();
	INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
	InitCommonControlsEx(&icc);

	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = WndProc;
	wc.hInstance = inst;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = CreateSolidBrush(RGB(18, 18, 20));
	wc.lpszClassName = L"PotPlayerDlss5NrSettings";
	wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
	RegisterClassExW(&wc);

	const int ww = 450, hh = 560;
	RECT wr{0, 0, ww, hh};
	AdjustWindowRect(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
	g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"DLSS 5 NR — настройки",
	                         WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT,
	                         wr.right - wr.left, wr.bottom - wr.top, nullptr, nullptr, inst, nullptr);

	BOOL dark = TRUE;
	DwmSetWindowAttribute(g_hwnd, 20, &dark, sizeof(dark));

	ShowWindow(g_hwnd, show);
	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0)) {
		if (!IsDialogMessageW(g_hwnd, &msg)) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}
	return (int)msg.wParam;
}
