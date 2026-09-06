#include "settings_ui.h"
#include "dark_ui.h"
#include "settings_ini.h"

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
	IDC_RESET = 140
};

static HWND g_style, g_preset, g_auto, g_ontop;
static HWND g_tb[4], g_val[4];
static NrUiSettings g_s;
static bool g_child;

static const wchar_t *kSliderNames[4] = {L"Интенсивность (степень)", L"Тон", L"Структура", L"Кожа"};

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

static float FromTick(int t) { return t / 100.f; }

static void SetValLabel(int i, float v)
{
	wchar_t b[16];
	swprintf(b, 16, L"%.2f", v);
	SetWindowTextW(g_val[i], b);
}

static void Save() { NrSettingsSave(g_s); }

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

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_CREATE: {
		g_ontop = nullptr;
		DuiLabel(hwnd, L"DLSS 5 Neural Rendering", 24, 18, 420, 28, 20, FW_SEMIBOLD);
		DuiLabel(hwnd, L"Крутится на лету, ролик перезапускать не нужно.", 24, 48, 420, 20, 14, FW_NORMAL, DuiMuted());

		DuiLabel(hwnd, L"Стиль", 24, 88, 90, 22, 15, FW_NORMAL, DuiMuted());
		g_style = DuiCombo(hwnd, IDC_STYLE, 160, 82, 270, 32);
		SendMessageW(g_style, CB_ADDSTRING, 0, (LPARAM)L"0  Default");
		SendMessageW(g_style, CB_ADDSTRING, 0, (LPARAM)L"1  Natural");
		SendMessageW(g_style, CB_ADDSTRING, 0, (LPARAM)L"2  Cinematic");

		DuiLabel(hwnd, L"Пресет", 24, 128, 90, 22, 15, FW_NORMAL, DuiMuted());
		g_preset = DuiCombo(hwnd, IDC_PRESET, 160, 122, 270, 32);
		SendMessageW(g_preset, CB_ADDSTRING, 0, (LPARAM)L"0");
		SendMessageW(g_preset, CB_ADDSTRING, 0, (LPARAM)L"1");
		SendMessageW(g_preset, CB_ADDSTRING, 0, (LPARAM)L"2");
		SendMessageW(g_preset, CB_ADDSTRING, 0, (LPARAM)L"3");

		int y = 174;
		for (int i = 0; i < 4; ++i) {
			DuiLabel(hwnd, kSliderNames[i], 24, y, 280, 20);
			g_val[i] = DuiLabel(hwnd, L"1.00", 360, y, 70, 20, 15, FW_SEMIBOLD, DuiAccent());
			g_tb[i] = DuiSlider(hwnd, IDC_INTENSITY + i, 16, y + 22, 412, 28);
			y += 72;
		}

		g_auto = DuiCheck(hwnd, IDC_AUTOMASK, L"Автомаска (кожа)", 24, y + 4, 220, 26);
		if (!g_child)
			g_ontop = DuiCheck(hwnd, IDC_ONTOP, L"Поверх окон", 260, y + 4, 180, 26);

		DuiButton(hwnd, IDC_RESET, L"Сброс", 24, y + 44, 140, 36, false);
		std::wstring hint = L"INI: " + NrSettingsIniPath();
		DuiLabel(hwnd, hint.c_str(), 24, y + 92, 420, 36, 12, FW_NORMAL, DuiMuted());

		g_s = NrSettingsLoad();
		if (!g_s.loaded)
			g_s = NrUiSettings{};
		LoadIntoUi();
		if (g_ontop)
			SendMessageW(g_ontop, BM_SETCHECK, BST_CHECKED, 0);
		Save();
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
		} else if (id == IDC_ONTOP && g_ontop) {
			BOOL on = SendMessageW(g_ontop, BM_GETCHECK, 0, 0) == BST_CHECKED;
			SetWindowPos(GetAncestor(hwnd, GA_ROOT), on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
			             SWP_NOMOVE | SWP_NOSIZE);
		} else if (id == IDC_RESET) {
			g_s = NrUiSettings{};
			g_s.loaded = true;
			LoadIntoUi();
			if (g_ontop)
				SendMessageW(g_ontop, BM_SETCHECK, BST_CHECKED, 0);
			Save();
		}
		return 0;
	}
	case WM_CTLCOLORSTATIC: {
		COLORREF extra = (COLORREF)GetWindowLongPtrW((HWND)lParam, GWLP_USERDATA);
		return DuiColorStatic(wParam, extra);
	}
	case WM_ERASEBKGND:
		DuiPaintBackground((HDC)wParam, hwnd);
		return 1;
	case WM_DESTROY:
		if (!g_child)
			PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

SIZE NrSettingsIdealSize() { return SIZE{470, 620}; }

HWND NrSettingsCreate(HWND parent, const RECT *rc, bool as_child)
{
	SetProcessDPIAware();
	DuiInit();
	g_child = as_child;

	static bool registered = false;
	if (!registered) {
		WNDCLASSEXW wc{};
		wc.cbSize = sizeof(wc);
		wc.lpfnWndProc = WndProc;
		wc.hInstance = DuiModule();
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.hbrBackground = DuiBgBrush();
		wc.lpszClassName = L"PotPlayerDlss5NrSettings";
		RegisterClassExW(&wc);
		registered = true;
	}

	DWORD style = as_child ? (WS_CHILD | WS_VISIBLE) : (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX);
	int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = 470, h = 620;
	if (rc) {
		x = rc->left;
		y = rc->top;
		w = rc->right - rc->left;
		h = rc->bottom - rc->top;
	} else if (!as_child) {
		RECT wr{0, 0, 470, 620};
		AdjustWindowRect(&wr, style, FALSE);
		w = wr.right - wr.left;
		h = wr.bottom - wr.top;
	}

	HWND hwnd = CreateWindowExW(0, L"PotPlayerDlss5NrSettings", L"DLSS 5 NR - настройки", style, x, y, w, h, parent,
	                            nullptr, DuiModule(), nullptr);
	if (!as_child) {
		DuiDarkTitlebar(hwnd);
		SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	}
	return hwnd;
}
