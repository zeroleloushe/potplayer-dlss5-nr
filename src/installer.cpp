#include "setup_res.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <windows.h>

#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")

enum {
	IDC_PATH = 401,
	IDC_BROWSE = 402,
	IDC_INSTALL = 403,
	IDC_LOG = 404,
	IDC_REG = 405,
	IDC_SVP = 406,
	IDC_PROFILES = 407
};

static HWND g_path, g_log, g_reg, g_svp, g_profiles;
static HFONT g_font;
static HBRUSH g_bg;

static std::wstring Join(const std::wstring &a, const wchar_t *b)
{
	if (a.empty())
		return b;
	if (a.back() == L'\\' || a.back() == L'/')
		return a + b;
	return a + L"\\" + b;
}

static bool Exists(const std::wstring &p)
{
	return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static bool IsDir(const std::wstring &p)
{
	DWORD a = GetFileAttributesW(p.c_str());
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static void EnsureDir(const std::wstring &p)
{
	if (p.empty() || Exists(p))
		return;
	size_t slash = p.find_last_of(L"\\/");
	if (slash != std::wstring::npos)
		EnsureDir(p.substr(0, slash));
	CreateDirectoryW(p.c_str(), nullptr);
}

static std::wstring LocalApp()
{
	wchar_t b[MAX_PATH]{};
	GetEnvironmentVariableW(L"LOCALAPPDATA", b, MAX_PATH);
	return b;
}

static std::wstring InstallIni() { return Join(Join(LocalApp(), L"potplayer-dlss5-nr"), L"install.ini"); }

static void Log(const wchar_t *line)
{
	if (!g_log)
		return;
	SendMessageW(g_log, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
	SendMessageW(g_log, EM_REPLACESEL, FALSE, (LPARAM)line);
	SendMessageW(g_log, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
}

static bool ExtractRes(int id, const std::wstring &dest)
{
	HMODULE mod = GetModuleHandleW(nullptr);
	HRSRC rs = FindResourceW(mod, MAKEINTRESOURCEW(id), RT_RCDATA);
	if (!rs) {
		Log((L"нет ресурса для " + dest).c_str());
		return false;
	}
	HGLOBAL g = LoadResource(mod, rs);
	DWORD n = SizeofResource(mod, rs);
	const void *p = LockResource(g);
	if (!p || !n)
		return false;
	size_t slash = dest.find_last_of(L"\\/");
	if (slash != std::wstring::npos)
		EnsureDir(dest.substr(0, slash));
	HANDLE h = CreateFileW(dest.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) {
		Log((L"не записался: " + dest + L"  (закрой PotPlayer)").c_str());
		return false;
	}
	DWORD w = 0;
	BOOL ok = WriteFile(h, p, n, &w, nullptr);
	CloseHandle(h);
	if (!ok || w != n) {
		Log((L"ошибка записи: " + dest).c_str());
		return false;
	}
	Log((L"  " + dest).c_str());
	return true;
}

static bool FindNamed(const std::wstring &root, const wchar_t *name, int depth, std::wstring &out)
{
	if (depth > 7 || root.empty())
		return false;
	std::wstring direct = Join(root, name);
	if (Exists(direct) && !IsDir(direct)) {
		out = direct;
		return true;
	}
	WIN32_FIND_DATAW fd{};
	HANDLE h = FindFirstFileW(Join(root, L"*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	do {
		if (fd.cFileName[0] == L'.' && (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0)))
			continue;
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			continue;
		if (_wcsicmp(fd.cFileName, L"Windows") == 0 || _wcsicmp(fd.cFileName, L"$Recycle.Bin") == 0)
			continue;
		if (FindNamed(Join(root, fd.cFileName), name, depth + 1, out)) {
			FindClose(h);
			return true;
		}
	} while (FindNextFileW(h, &fd));
	FindClose(h);
	return false;
}

static std::wstring DirOf(const std::wstring &file)
{
	size_t s = file.find_last_of(L"\\/");
	return s == std::wstring::npos ? file : file.substr(0, s);
}

static std::wstring GuessPotPlayer()
{
	wchar_t last[MAX_PATH]{};
	GetPrivateProfileStringW(L"setup", L"potplayer", L"", last, MAX_PATH, InstallIni().c_str());
	if (last[0] && IsDir(last) &&
	    (Exists(Join(last, L"PotPlayerMini64.exe")) || Exists(Join(last, L"PotPlayer64.exe")) ||
	     Exists(Join(last, L"PotPlayerMini.exe"))))
		return last;

	wchar_t user[MAX_PATH]{};
	GetEnvironmentVariableW(L"USERPROFILE", user, MAX_PATH);
	const wchar_t *cands[] = {
	    L"PotPlayer",
	    L"DAUM\\PotPlayer",
	};
	std::wstring home = user;
	std::vector<std::wstring> paths = {
	    Join(home, L"PotPlayer"),
	    Join(home, L"Downloads\\PotPlayer"),
	    L"C:\\Program Files\\DAUM\\PotPlayer",
	    L"C:\\Program Files (x86)\\DAUM\\PotPlayer",
	    Join(Join(LocalApp(), L"DAUM"), L"PotPlayer"),
	};
	HKEY keys[] = {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
	const wchar_t *reg[] = {L"Software\\DAUM\\PotPlayer64", L"Software\\DAUM\\PotPlayerMini64",
	                        L"Software\\DAUM\\PotPlayer"};
	for (HKEY root : keys) {
		for (auto *k : reg) {
			HKEY h;
			if (RegOpenKeyExW(root, k, 0, KEY_READ, &h) != ERROR_SUCCESS)
				continue;
			wchar_t val[MAX_PATH]{};
			DWORD n = sizeof(val), t = 0;
			const wchar_t *names[] = {L"ProgramPath", L"InstallPath", L"Path", L"Folder"};
			for (auto *nm : names) {
				n = sizeof(val);
				if (RegQueryValueExW(h, nm, nullptr, &t, (LPBYTE)val, &n) == ERROR_SUCCESS && val[0]) {
					std::wstring p = val;
					if (Exists(p) && !IsDir(p))
						p = DirOf(p);
					if (IsDir(p))
						paths.insert(paths.begin(), p);
				}
			}
			RegCloseKey(h);
		}
	}
	for (auto &p : paths) {
		if (Exists(Join(p, L"PotPlayerMini64.exe")) || Exists(Join(p, L"PotPlayer64.exe")) ||
		    Exists(Join(p, L"PotPlayerMini.exe")))
			return p;
	}
	(void)cands;
	return Join(home, L"PotPlayer");
}

static bool BrowseFolder(HWND owner, std::wstring &out)
{
	IFileDialog *dlg = nullptr;
	if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
		return false;
	DWORD opt = 0;
	dlg->GetOptions(&opt);
	dlg->SetOptions(opt | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
	dlg->SetTitle(L"Папка PotPlayer");
	bool ok = false;
	if (SUCCEEDED(dlg->Show(owner))) {
		IShellItem *item = nullptr;
		if (SUCCEEDED(dlg->GetResult(&item))) {
			PWSTR p = nullptr;
			if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) {
				out = p;
				CoTaskMemFree(p);
				ok = true;
			}
			item->Release();
		}
	}
	dlg->Release();
	return ok;
}

static bool RegisterFilter(const std::wstring &dll)
{
	HMODULE m = LoadLibraryW(dll.c_str());
	if (!m) {
		Log(L"не загрузился nr-potfilter.dll");
		return false;
	}
	auto fn = reinterpret_cast<HRESULT(WINAPI *)()>(GetProcAddress(m, "DllRegisterServer"));
	HRESULT hr = fn ? fn() : E_FAIL;
	FreeLibrary(m);
	if (FAILED(hr)) {
		Log(L"регистрация фильтра не удалась");
		return false;
	}
	Log(L"фильтр DLSS 5 NR зарегистрирован (двойной клик в Менеджере фильтров)");
	return true;
}

static void WriteLauncher(const std::wstring &pot)
{
	std::wstring cmd = Join(pot, L"DLSS5-NR-Settings.cmd");
	const char *txt = "@echo off\r\ncd /d \"%~dp0\"\r\nstart \"\" \"%~dp0nr-settings.exe\"\r\n";
	HANDLE h = CreateFileW(cmd.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE)
		return;
	DWORD w = 0;
	WriteFile(h, txt, (DWORD)strlen(txt), &w, nullptr);
	CloseHandle(h);
}

static void DoInstall(HWND hwnd)
{
	wchar_t path[MAX_PATH]{};
	GetWindowTextW(g_path, path, MAX_PATH);
	std::wstring pot = path;
	while (!pot.empty() && (pot.back() == L'\\' || pot.back() == L' '))
		pot.pop_back();
	if (!IsDir(pot)) {
		Log(L"Папка PotPlayer не найдена.");
		return;
	}
	if (!Exists(Join(pot, L"PotPlayerMini64.exe")) && !Exists(Join(pot, L"PotPlayer64.exe")) &&
	    !Exists(Join(pot, L"PotPlayerMini.exe"))) {
		Log(L"В этой папке нет PotPlayerMini64.exe — проверь путь.");
		return;
	}

	EnableWindow(GetDlgItem(hwnd, IDC_INSTALL), FALSE);
	Log(L"Установка...");
	EnsureDir(Join(LocalApp(), L"potplayer-dlss5-nr"));
	WritePrivateProfileStringW(L"setup", L"potplayer", pot.c_str(), InstallIni().c_str());

	std::wstring plugin, scripts;
	std::wstring found;
	if (FindNamed(pot, L"svpflow1.dll", 0, found) || FindNamed(DirOf(pot), L"svpflow1.dll", 0, found)) {
		plugin = DirOf(found);
		Log((L"svpflow1.dll → " + plugin).c_str());
	} else if (FindNamed(pot, L"AviSynth.dll", 0, found)) {
		std::wstring base = DirOf(found);
		if (IsDir(Join(base, L"plugins64+")))
			plugin = Join(base, L"plugins64+");
		else if (IsDir(Join(base, L"plugins64")))
			plugin = Join(base, L"plugins64");
		else
			plugin = base;
		Log((L"AviSynth.dll → " + plugin).c_str());
	} else {
		plugin = pot;
		Log(L"svpflow1.dll не найден, кладу плагины в корень PotPlayer");
	}

	if (FindNamed(pot, L"svp.avs", 0, found) || FindNamed(pot, L"GPU-3-High.avs", 0, found))
		scripts = DirOf(found);
	else
		scripts = plugin;

	bool ok = true;
	ok &= ExtractRes(IDR_DLSS5NR, Join(plugin, L"DLSS5NR.dll"));
	ok &= ExtractRes(IDR_SHIM, Join(plugin, L"nvngx.dll_pot.dll"));
	std::wstring vsfound;
	if (FindNamed(pot, L"vapoursynth.dll", 0, vsfound))
		ExtractRes(IDR_VSNR, Join(DirOf(vsfound), L"vsdlss5nr.dll"));
	ok &= ExtractRes(IDR_SETTINGS, Join(pot, L"nr-settings.exe"));
	ok &= ExtractRes(IDR_POTFILTER, Join(pot, L"nr-potfilter.dll"));
	WriteLauncher(pot);

	if (SendMessageW(g_profiles, BM_GETCHECK, 0, 0) == BST_CHECKED) {
		ExtractRes(IDR_GPU3NR, Join(scripts, L"GPU-3-High-NR.avs"));
		ExtractRes(IDR_GPUNR, Join(scripts, L"GPU-NR.avs"));
		ExtractRes(IDR_CPU3NR, Join(scripts, L"CPU-3-High-NR.avs"));
		ExtractRes(IDR_AFTER_AVS, Join(Join(pot, L"dlss5-nr"), L"after_svp.avs"));
		ExtractRes(IDR_AFTER_VPY, Join(Join(pot, L"dlss5-nr"), L"after_svp.vpy"));
	}
	if (SendMessageW(g_svp, BM_GETCHECK, 0, 0) == BST_CHECKED) {
		std::wstring dest = Join(scripts, L"svp.avs");
		if (Exists(dest)) {
			CopyFileW(dest.c_str(), Join(scripts, L"svp.avs.bak").c_str(), FALSE);
			Log(L"старый svp.avs сохранён как svp.avs.bak");
		}
		ExtractRes(IDR_SVP, dest);
	}

	if (SendMessageW(g_reg, BM_GETCHECK, 0, 0) == BST_CHECKED)
		RegisterFilter(Join(pot, L"nr-potfilter.dll"));

	std::wstring runtime = Join(Join(LocalApp(), L"potplayer-dlss5-nr"), L"runtime");
	EnsureDir(runtime);
	Log((L"runtime: " + runtime).c_str());
	if (Exists(Join(runtime, L"nvngx_dlssnr.dll")))
		Log(L"nvngx_dlssnr.dll уже на месте");
	else {
		Log(L"СКОПИРУЙ nvngx_dlssnr.dll в runtime (из игры с DLSS 5 NR).");
		ShellExecuteW(hwnd, L"open", runtime.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}

	if (ok) {
		Log(L"");
		Log(L"Готово. В PotPlayer: AviSynth-скрипт GPU-3-High-NR.avs");
		Log(L"Настройки: F5 → Фильтры → Менеджер фильтров → DLSS 5 NR");
		MessageBoxW(hwnd, L"Установка завершена.", L"DLSS 5 NR", MB_OK | MB_ICONINFORMATION);
	} else {
		Log(L"Были ошибки — смотри лог.");
		MessageBoxW(hwnd, L"Часть файлов не скопировалась. Закрой PotPlayer и повтори.", L"DLSS 5 NR",
		            MB_OK | MB_ICONWARNING);
	}
	EnableWindow(GetDlgItem(hwnd, IDC_INSTALL), TRUE);
}

static HWND AddTxt(HWND parent, const wchar_t *t, int x, int y, int w, int h, int id = 0)
{
	HWND c = CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE, x, y, w, h, parent, (HMENU)(INT_PTR)id,
	                         GetModuleHandleW(nullptr), nullptr);
	SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
	return c;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_CREATE: {
		g_bg = CreateSolidBrush(RGB(18, 18, 20));
		g_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
		HFONT big = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
		                        L"Segoe UI");
		HWND title = AddTxt(hwnd, L"DLSS 5 NR для PotPlayer", 20, 16, 500, 28);
		SendMessageW(title, WM_SETFONT, (WPARAM)big, TRUE);
		AddTxt(hwnd, L"Укажи папку портативного PotPlayer (где PotPlayerMini64.exe).", 20, 48, 520, 22);
		AddTxt(hwnd, L"Папка", 20, 84, 70, 22);
		g_path = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP, 90,
		                         80, 330, 26, hwnd, (HMENU)IDC_PATH, GetModuleHandleW(nullptr), nullptr);
		SendMessageW(g_path, WM_SETFONT, (WPARAM)g_font, TRUE);
		HWND br = CreateWindowExW(0, L"BUTTON", L"Обзор…", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 430, 79,
		                          90, 28, hwnd, (HMENU)IDC_BROWSE, GetModuleHandleW(nullptr), nullptr);
		SendMessageW(br, WM_SETFONT, (WPARAM)g_font, TRUE);
		g_reg = CreateWindowExW(0, L"BUTTON", L"Зарегистрировать настройки в Менеджере фильтров",
		                        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, 20, 118, 500, 24, hwnd,
		                        (HMENU)IDC_REG, GetModuleHandleW(nullptr), nullptr);
		g_profiles = CreateWindowExW(0, L"BUTTON", L"Скопировать профили GPU-3-High-NR / GPU-NR",
		                             WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, 20, 144, 500, 24, hwnd,
		                             (HMENU)IDC_PROFILES, GetModuleHandleW(nullptr), nullptr);
		g_svp = CreateWindowExW(0, L"BUTTON", L"Заменить svp.avs (старый уйдёт в .bak)",
		                        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, 20, 170, 500, 24, hwnd,
		                        (HMENU)IDC_SVP, GetModuleHandleW(nullptr), nullptr);
		SendMessageW(g_reg, WM_SETFONT, (WPARAM)g_font, TRUE);
		SendMessageW(g_profiles, WM_SETFONT, (WPARAM)g_font, TRUE);
		SendMessageW(g_svp, WM_SETFONT, (WPARAM)g_font, TRUE);
		SendMessageW(g_reg, BM_SETCHECK, BST_CHECKED, 0);
		SendMessageW(g_profiles, BM_SETCHECK, BST_CHECKED, 0);
		HWND go = CreateWindowExW(0, L"BUTTON", L"Установить",
		                          WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP, 20, 206, 160, 36, hwnd,
		                          (HMENU)IDC_INSTALL, GetModuleHandleW(nullptr), nullptr);
		SendMessageW(go, WM_SETFONT, (WPARAM)g_font, TRUE);
		g_log = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
		                        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL, 20,
		                        256, 500, 200, hwnd, (HMENU)IDC_LOG, GetModuleHandleW(nullptr), nullptr);
		SendMessageW(g_log, WM_SETFONT, (WPARAM)g_font, TRUE);
		SetWindowTextW(g_path, GuessPotPlayer().c_str());
		Log(L"NVIDIA nvngx_dlssnr.dll установщик не кладёт — его надо взять из игры.");
		return 0;
	}
	case WM_COMMAND:
		if (LOWORD(wParam) == IDC_BROWSE) {
			std::wstring p;
			if (BrowseFolder(hwnd, p))
				SetWindowTextW(g_path, p.c_str());
		} else if (LOWORD(wParam) == IDC_INSTALL) {
			DoInstall(hwnd);
		}
		return 0;
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
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show)
{
	SetProcessDPIAware();
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
	InitCommonControlsEx(&icc);

	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = WndProc;
	wc.hInstance = inst;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = CreateSolidBrush(RGB(18, 18, 20));
	wc.lpszClassName = L"Dlss5NrSetup";
	RegisterClassExW(&wc);

	RECT wr{0, 0, 540, 480};
	AdjustWindowRect(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
	HWND hwnd = CreateWindowExW(0, L"Dlss5NrSetup", L"DLSS 5 NR — установка",
	                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT,
	                            wr.right - wr.left, wr.bottom - wr.top, nullptr, nullptr, inst, nullptr);
	BOOL dark = TRUE;
	DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
	ShowWindow(hwnd, show);

	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0)) {
		if (!IsDialogMessageW(hwnd, &msg)) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}
	CoUninitialize();
	return (int)msg.wParam;
}
