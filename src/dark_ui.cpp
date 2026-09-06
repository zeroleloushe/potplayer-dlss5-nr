#include "dark_ui.h"

#include <objidl.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <windowsx.h>

#include <algorithm>
#include <string>
#include <vector>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "dwmapi.lib")

using Gdiplus::Color;
using Gdiplus::Graphics;
using Gdiplus::GraphicsPath;
using Gdiplus::Pen;
using Gdiplus::PointF;
using Gdiplus::RectF;
using Gdiplus::SmoothingModeAntiAlias;
using Gdiplus::SolidBrush;

namespace {

constexpr COLORREF kBg = RGB(18, 18, 20);
constexpr COLORREF kBg2 = RGB(28, 28, 32);
constexpr COLORREF kBg3 = RGB(10, 10, 12);
constexpr COLORREF kFg = RGB(245, 245, 247);
constexpr COLORREF kMuted = RGB(160, 160, 168);
constexpr COLORREF kAccent = RGB(118, 185, 0);
constexpr COLORREF kTrack = RGB(55, 55, 62);
constexpr COLORREF kLine = RGB(70, 70, 78);

ULONG_PTR g_gdip;
HBRUSH g_bg;
HFONT g_font15, g_font13, g_font20;
bool g_inited;

Color C(COLORREF c, BYTE a = 255)
{
	return Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

void AddRound(GraphicsPath &p, RectF r, float rad)
{
	float d = rad * 2.f;
	if (d > r.Width)
		d = r.Width;
	if (d > r.Height)
		d = r.Height;
	p.AddArc(r.X, r.Y, d, d, 180, 90);
	p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
	p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
	p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
	p.CloseFigure();
}

void FillRound(Graphics &g, RectF r, float rad, COLORREF fill)
{
	GraphicsPath p;
	AddRound(p, r, rad);
	SolidBrush b(C(fill));
	g.FillPath(&b, &p);
}

void FillRoundOutline(Graphics &g, RectF r, float rad, COLORREF fill, COLORREF outline, float ow = 1.f)
{
	FillRound(g, r, rad, fill);
	GraphicsPath p;
	AddRound(p, r, rad);
	Pen pen(C(outline), ow);
	g.DrawPath(&pen, &p);
}

HFONT MakeFont(int px, int weight)
{
	return CreateFontW(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
}

struct Slider {
	int lo = 0, hi = 200, pos = 100;
	bool drag = false;
};

struct Check {
	bool on = false;
	std::wstring text;
};

struct Btn {
	std::wstring text;
	bool primary = false;
	bool hover = false;
	bool down = false;
};

struct Combo {
	std::vector<std::wstring> items;
	int sel = 0;
	HWND popup = nullptr;
};

Slider *S(HWND h) { return (Slider *)GetWindowLongPtrW(h, GWLP_USERDATA); }
Check *K(HWND h) { return (Check *)GetWindowLongPtrW(h, GWLP_USERDATA); }
Btn *B(HWND h) { return (Btn *)GetWindowLongPtrW(h, GWLP_USERDATA); }
Combo *O(HWND h) { return (Combo *)GetWindowLongPtrW(h, GWLP_USERDATA); }

int SliderFromX(HWND hwnd, int x, Slider *s)
{
	RECT rc;
	GetClientRect(hwnd, &rc);
	const int pad = 10;
	int w = rc.right - pad * 2;
	if (w < 1)
		w = 1;
	float t = (x - pad) / (float)w;
	if (t < 0)
		t = 0;
	if (t > 1)
		t = 1;
	return s->lo + (int)(t * (s->hi - s->lo) + 0.5f);
}

LRESULT CALLBACK SliderProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	Slider *s = S(hwnd);
	switch (msg) {
	case WM_CREATE:
		s = new Slider();
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)s);
		return 0;
	case TBM_SETRANGEMIN:
		s->lo = (int)lParam;
		return 0;
	case TBM_SETRANGEMAX:
		s->hi = (int)lParam;
		return 0;
	case TBM_SETPOS:
		s->pos = (int)lParam;
		if (s->pos < s->lo)
			s->pos = s->lo;
		if (s->pos > s->hi)
			s->pos = s->hi;
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;
	case TBM_GETPOS:
		return s->pos;
	case WM_LBUTTONDOWN:
		SetCapture(hwnd);
		s->drag = true;
		s->pos = SliderFromX(hwnd, GET_X_LPARAM(lParam), s);
		InvalidateRect(hwnd, nullptr, FALSE);
		SendMessageW(GetParent(hwnd), WM_HSCROLL, MAKEWPARAM(SB_THUMBTRACK, s->pos), (LPARAM)hwnd);
		return 0;
	case WM_MOUSEMOVE:
		if (s->drag) {
			s->pos = SliderFromX(hwnd, GET_X_LPARAM(lParam), s);
			InvalidateRect(hwnd, nullptr, FALSE);
			SendMessageW(GetParent(hwnd), WM_HSCROLL, MAKEWPARAM(SB_THUMBTRACK, s->pos), (LPARAM)hwnd);
		}
		return 0;
	case WM_LBUTTONUP:
		if (s->drag) {
			s->drag = false;
			ReleaseCapture();
			SendMessageW(GetParent(hwnd), WM_HSCROLL, MAKEWPARAM(SB_ENDSCROLL, s->pos), (LPARAM)hwnd);
		}
		return 0;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		RECT rc;
		GetClientRect(hwnd, &rc);
		HDC mem = CreateCompatibleDC(hdc);
		HBITMAP bm = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
		HGDIOBJ old = SelectObject(mem, bm);
		Graphics g(mem);
		g.SetSmoothingMode(SmoothingModeAntiAlias);
		SolidBrush bg(C(kBg));
		g.FillRectangle(&bg, 0, 0, rc.right, rc.bottom);
		const float pad = 10.f;
		float y = rc.bottom * 0.5f;
		RectF track(pad, y - 3.f, rc.right - pad * 2.f, 6.f);
		FillRound(g, track, 3.f, kTrack);
		float t = (s->hi == s->lo) ? 0.f : (s->pos - s->lo) / (float)(s->hi - s->lo);
		RectF fill(pad, y - 3.f, track.Width * t, 6.f);
		if (fill.Width > 1.f)
			FillRound(g, fill, 3.f, kAccent);
		float cx = pad + track.Width * t;
		SolidBrush thumb(C(RGB(255, 255, 255)));
		g.FillEllipse(&thumb, cx - 8.f, y - 8.f, 16.f, 16.f);
		Pen ring(C(RGB(200, 200, 204)), 1.f);
		g.DrawEllipse(&ring, cx - 8.f, y - 8.f, 16.f, 16.f);
		BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
		SelectObject(mem, old);
		DeleteObject(bm);
		DeleteDC(mem);
		EndPaint(hwnd, &ps);
		return 0;
	}
	case WM_DESTROY:
		delete s;
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK CheckProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	Check *c = K(hwnd);
	switch (msg) {
	case WM_CREATE: {
		c = new Check();
		auto *cs = (CREATESTRUCTW *)lParam;
		if (cs->lpszName)
			c->text = cs->lpszName;
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)c);
		return 0;
	}
	case BM_SETCHECK:
		c->on = (wParam == BST_CHECKED);
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;
	case BM_GETCHECK:
		return c->on ? BST_CHECKED : BST_UNCHECKED;
	case WM_SETTEXT:
		c->text = lParam ? (wchar_t *)lParam : L"";
		InvalidateRect(hwnd, nullptr, FALSE);
		return TRUE;
	case WM_LBUTTONUP:
		c->on = !c->on;
		InvalidateRect(hwnd, nullptr, FALSE);
		SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED), (LPARAM)hwnd);
		return 0;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		RECT rc;
		GetClientRect(hwnd, &rc);
		Graphics g(hdc);
		g.SetSmoothingMode(SmoothingModeAntiAlias);
		SolidBrush bg(C(kBg));
		g.FillRectangle(&bg, 0, 0, rc.right, rc.bottom);
		FillRoundOutline(g, RectF(1, 3, 18, 18), 4.f, c->on ? kAccent : kBg2, c->on ? kAccent : kLine);
		if (c->on) {
			Pen tick(C(kBg3), 2.2f);
			g.DrawLine(&tick, 5.f, 12.f, 8.5f, 16.f);
			g.DrawLine(&tick, 8.5f, 16.f, 15.5f, 7.f);
		}
		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, kFg);
		SelectObject(hdc, g_font15);
		RECT tr{26, 1, rc.right, rc.bottom};
		DrawTextW(hdc, c->text.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		EndPaint(hwnd, &ps);
		return 0;
	}
	case WM_DESTROY:
		delete c;
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK ButtonProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	Btn *b = B(hwnd);
	switch (msg) {
	case WM_CREATE: {
		b = new Btn();
		auto *cs = (CREATESTRUCTW *)lParam;
		if (cs->lpszName)
			b->text = cs->lpszName;
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)b);
		return 0;
	}
	case WM_MOUSEMOVE:
		if (!b->hover) {
			b->hover = true;
			TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, hwnd, 0};
			TrackMouseEvent(&t);
			InvalidateRect(hwnd, nullptr, FALSE);
		}
		return 0;
	case WM_MOUSELEAVE:
		b->hover = false;
		b->down = false;
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;
	case WM_LBUTTONDOWN:
		b->down = true;
		SetCapture(hwnd);
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;
	case WM_LBUTTONUP:
		if (b->down) {
			b->down = false;
			ReleaseCapture();
			InvalidateRect(hwnd, nullptr, FALSE);
			POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
			RECT rc;
			GetClientRect(hwnd, &rc);
			if (PtInRect(&rc, pt))
				SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED), (LPARAM)hwnd);
		}
		return 0;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		RECT rc;
		GetClientRect(hwnd, &rc);
		Graphics g(hdc);
		g.SetSmoothingMode(SmoothingModeAntiAlias);
		COLORREF fill = b->primary ? kAccent : kBg2;
		if (b->down)
			fill = b->primary ? RGB(96, 150, 0) : RGB(40, 40, 46);
		else if (b->hover)
			fill = b->primary ? RGB(140, 205, 20) : RGB(38, 38, 44);
		FillRoundOutline(g, RectF(0.5f, 0.5f, rc.right - 1.f, rc.bottom - 1.f), 8.f, fill,
		                 b->primary ? fill : kLine);
		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, b->primary ? kBg3 : kFg);
		SelectObject(hdc, g_font15);
		DrawTextW(hdc, b->text.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		EndPaint(hwnd, &ps);
		return 0;
	}
	case WM_DESTROY:
		delete b;
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static HWND g_combo_open = nullptr;

LRESULT CALLBACK PopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	HWND owner = (HWND)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
	Combo *c = owner ? O(owner) : nullptr;
	switch (msg) {
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		RECT rc;
		GetClientRect(hwnd, &rc);
		Graphics g(hdc);
		g.SetSmoothingMode(SmoothingModeAntiAlias);
		SolidBrush bg(C(kBg2));
		g.FillRectangle(&bg, 0, 0, rc.right, rc.bottom);
		Pen border(C(kLine), 1);
		g.DrawRectangle(&border, 0, 0, rc.right - 1, rc.bottom - 1);
		SetBkMode(hdc, TRANSPARENT);
		SelectObject(hdc, g_font15);
		int y = 0;
		for (int i = 0; i < (int)c->items.size(); ++i) {
			RECT ir{8, y, rc.right - 8, y + 28};
			if (i == c->sel) {
				GraphicsPath p;
				AddRound(p, RectF(4, (float)y + 2, rc.right - 8.f, 24), 4);
				SolidBrush hi(C(RGB(40, 50, 20)));
				g.FillPath(&hi, &p);
			}
			SetTextColor(hdc, i == c->sel ? kAccent : kFg);
			DrawTextW(hdc, c->items[i].c_str(), -1, &ir, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
			y += 28;
		}
		EndPaint(hwnd, &ps);
		return 0;
	}
	case WM_LBUTTONUP: {
		int i = GET_Y_LPARAM(lParam) / 28;
		if (c && i >= 0 && i < (int)c->items.size()) {
			c->sel = i;
			InvalidateRect(owner, nullptr, FALSE);
			SendMessageW(GetParent(owner), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(owner), CBN_SELCHANGE), (LPARAM)owner);
		}
		DestroyWindow(hwnd);
		if (c)
			c->popup = nullptr;
		g_combo_open = nullptr;
		return 0;
	}
	case WM_ACTIVATE:
		if (LOWORD(wParam) == WA_INACTIVE) {
			DestroyWindow(hwnd);
			if (c)
				c->popup = nullptr;
			g_combo_open = nullptr;
		}
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK ComboProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	Combo *c = O(hwnd);
	switch (msg) {
	case WM_CREATE:
		c = new Combo();
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)c);
		return 0;
	case CB_ADDSTRING:
		c->items.emplace_back(lParam ? (wchar_t *)lParam : L"");
		return (LRESULT)c->items.size() - 1;
	case CB_SETCURSEL:
		c->sel = (int)wParam;
		if (c->sel < 0)
			c->sel = 0;
		if (c->sel >= (int)c->items.size())
			c->sel = (int)c->items.size() - 1;
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;
	case CB_GETCURSEL:
		return c->sel;
	case WM_LBUTTONUP: {
		if (c->popup) {
			DestroyWindow(c->popup);
			c->popup = nullptr;
			g_combo_open = nullptr;
			return 0;
		}
		RECT rc;
		GetWindowRect(hwnd, &rc);
		int h = 8 + 28 * (int)c->items.size();
		HWND pop = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"Dui.Popup", L"", WS_POPUP | WS_VISIBLE, rc.left,
		                           rc.bottom + 2, rc.right - rc.left, h, hwnd, nullptr, DuiModule(), nullptr);
		SetWindowLongPtrW(pop, GWLP_USERDATA, (LONG_PTR)hwnd);
		c->popup = pop;
		g_combo_open = hwnd;
		return 0;
	}
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		RECT rc;
		GetClientRect(hwnd, &rc);
		Graphics g(hdc);
		g.SetSmoothingMode(SmoothingModeAntiAlias);
		FillRoundOutline(g, RectF(0.5f, 0.5f, rc.right - 1.f, rc.bottom - 1.f), 6.f, kBg2, kLine);
		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, kFg);
		SelectObject(hdc, g_font15);
		RECT tr{12, 0, rc.right - 28, rc.bottom};
		if (!c->items.empty() && c->sel >= 0 && c->sel < (int)c->items.size())
			DrawTextW(hdc, c->items[c->sel].c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		SolidBrush tri(C(kMuted));
		PointF pts[3] = {PointF(rc.right - 18.f, 10.f), PointF(rc.right - 8.f, 10.f), PointF(rc.right - 13.f, 17.f)};
		g.FillPolygon(&tri, pts, 3);
		EndPaint(hwnd, &ps);
		return 0;
	}
	case WM_DESTROY:
		if (c->popup)
			DestroyWindow(c->popup);
		delete c;
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void RegisterOne(const wchar_t *name, WNDPROC proc)
{
	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = proc;
	wc.hInstance = DuiModule();
	wc.hCursor = LoadCursor(nullptr, IDC_HAND);
	wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	wc.lpszClassName = name;
	RegisterClassExW(&wc);
}

} // namespace

HMODULE DuiModule()
{
	HMODULE m = nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                   (LPCWSTR)&DuiInit, &m);
	return m;
}

void DuiInit()
{
	if (g_inited)
		return;
	Gdiplus::GdiplusStartupInput in;
	Gdiplus::GdiplusStartup(&g_gdip, &in, nullptr);
	g_bg = CreateSolidBrush(kBg);
	g_font15 = MakeFont(15, FW_NORMAL);
	g_font13 = MakeFont(13, FW_NORMAL);
	g_font20 = MakeFont(20, FW_SEMIBOLD);
	RegisterOne(L"Dui.Slider", SliderProc);
	RegisterOne(L"Dui.Check", CheckProc);
	RegisterOne(L"Dui.Button", ButtonProc);
	RegisterOne(L"Dui.Combo", ComboProc);
	RegisterOne(L"Dui.Popup", PopupProc);
	g_inited = true;
}

HFONT DuiFont(int px, int weight)
{
	if (px >= 20 && weight >= FW_SEMIBOLD)
		return g_font20;
	if (px <= 13)
		return g_font13;
	return g_font15;
}

HBRUSH DuiBgBrush() { return g_bg; }
COLORREF DuiBg() { return kBg; }
COLORREF DuiFg() { return kFg; }
COLORREF DuiMuted() { return kMuted; }
COLORREF DuiAccent() { return kAccent; }

static HWND Mk(const wchar_t *cls, HWND parent, int id, const wchar_t *text, int x, int y, int w, int h)
{
	return CreateWindowExW(0, cls, text ? text : L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, x, y, w, h, parent,
	                       (HMENU)(INT_PTR)id, DuiModule(), nullptr);
}

HWND DuiSlider(HWND parent, int id, int x, int y, int w, int h)
{
	HWND hwnd = Mk(L"Dui.Slider", parent, id, L"", x, y, w, h);
	SendMessageW(hwnd, TBM_SETRANGEMIN, 0, 0);
	SendMessageW(hwnd, TBM_SETRANGEMAX, 0, 200);
	return hwnd;
}

HWND DuiCheck(HWND parent, int id, const wchar_t *text, int x, int y, int w, int h)
{
	return Mk(L"Dui.Check", parent, id, text, x, y, w, h);
}

HWND DuiButton(HWND parent, int id, const wchar_t *text, int x, int y, int w, int h, bool primary)
{
	HWND hwnd = Mk(L"Dui.Button", parent, id, text, x, y, w, h);
	if (Btn *b = B(hwnd))
		b->primary = primary;
	InvalidateRect(hwnd, nullptr, FALSE);
	return hwnd;
}

HWND DuiCombo(HWND parent, int id, int x, int y, int w, int h)
{
	return Mk(L"Dui.Combo", parent, id, L"", x, y, w, h);
}

HWND DuiLabel(HWND parent, const wchar_t *text, int x, int y, int w, int h, int px, int weight, COLORREF color)
{
	HWND ctl = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h, parent, nullptr, DuiModule(),
	                           nullptr);
	SendMessageW(ctl, WM_SETFONT, (WPARAM)DuiFont(px, weight), TRUE);
	if (color)
		SetWindowLongPtrW(ctl, GWLP_USERDATA, (LONG_PTR)color);
	return ctl;
}

void DuiDarkTitlebar(HWND hwnd)
{
	BOOL dark = TRUE;
	DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
}

void DuiPaintBackground(HDC hdc, HWND hwnd)
{
	RECT rc;
	GetClientRect(hwnd, &rc);
	FillRect(hdc, &rc, g_bg);
}

LRESULT DuiColorStatic(WPARAM wParam, COLORREF fg)
{
	HDC hdc = (HDC)wParam;
	SetTextColor(hdc, fg ? fg : kFg);
	SetBkColor(hdc, kBg);
	return (LRESULT)g_bg;
}
