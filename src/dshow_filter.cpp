// DirectShow stub filter + property page so PotPlayer can open DLSS 5 NR
// settings from F5 → Filter Control → Filter Management (Add external filter).
// The filter has no pins and must NOT be inserted into the playback graph.

#include "settings_ui.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ocidl.h>
#include <strmif.h>

#include <atomic>
#include <cstdio>
#include <new>

static const CLSID CLSID_NrFilter = {0x8c4e9a21, 0x5d15, 0x4e05, {0x9a, 0x11, 0x50, 0xd3, 0x5f, 0x5e, 0x01, 0xa1}};
static const CLSID CLSID_NrPage = {0x8c4e9a21, 0x5d15, 0x4e05, {0x9a, 0x11, 0x50, 0xd3, 0x5f, 0x5e, 0x01, 0xa2}};
static const GUID CLSID_LegacyAmFilterCategory = {
    0x083863f1, 0x70de, 0x11d0, {0xbd, 0x40, 0x00, 0xa0, 0xc9, 0x11, 0xce, 0x86}};

static std::atomic<long> g_locks{0};

static HMODULE ThisMod()
{
	HMODULE m = nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                   reinterpret_cast<LPCWSTR>(&ThisMod), &m);
	return m;
}

static void GuidStr(const CLSID &id, wchar_t out[39])
{
	StringFromGUID2(id, out, 39);
}

class NrPage final : public IPropertyPage {
	long refs_ = 1;
	HWND hwnd_ = nullptr;
	IPropertyPageSite *site_ = nullptr;

public:
	NrPage() { g_locks++; }
	~NrPage()
	{
		if (site_)
			site_->Release();
		g_locks--;
	}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override
	{
		if (!ppv)
			return E_POINTER;
		if (riid == IID_IUnknown || riid == IID_IPropertyPage) {
			*ppv = static_cast<IPropertyPage *>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&refs_); }
	ULONG STDMETHODCALLTYPE Release() override
	{
		long n = InterlockedDecrement(&refs_);
		if (!n)
			delete this;
		return (ULONG)n;
	}

	HRESULT STDMETHODCALLTYPE SetPageSite(IPropertyPageSite *p) override
	{
		if (site_)
			site_->Release();
		site_ = p;
		if (site_)
			site_->AddRef();
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE Activate(HWND parent, LPCRECT prc, BOOL) override
	{
		Deactivate();
		hwnd_ = NrSettingsCreate(parent, prc, true);
		return hwnd_ ? S_OK : E_FAIL;
	}
	HRESULT STDMETHODCALLTYPE Deactivate() override
	{
		if (hwnd_) {
			DestroyWindow(hwnd_);
			hwnd_ = nullptr;
		}
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetPageInfo(PROPPAGEINFO *p) override
	{
		if (!p)
			return E_POINTER;
		memset(p, 0, sizeof(*p));
		p->cb = sizeof(*p);
		p->pszTitle = (LPOLESTR)CoTaskMemAlloc(32 * sizeof(wchar_t));
		if (p->pszTitle)
			wcscpy_s(p->pszTitle, 32, L"DLSS 5 NR");
		p->size = NrSettingsIdealSize();
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE SetObjects(ULONG, IUnknown **) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE Show(UINT nCmdShow) override
	{
		if (hwnd_)
			ShowWindow(hwnd_, nCmdShow);
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE Move(LPCRECT prc) override
	{
		if (hwnd_ && prc)
			MoveWindow(hwnd_, prc->left, prc->top, prc->right - prc->left, prc->bottom - prc->top, TRUE);
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE IsPageDirty() override { return S_FALSE; }
	HRESULT STDMETHODCALLTYPE Apply() override { return S_OK; }
	HRESULT STDMETHODCALLTYPE Help(LPCOLESTR) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE TranslateAccelerator(MSG *) override { return S_FALSE; }
};

class NrFilter final : public IBaseFilter, public ISpecifyPropertyPages {
	long refs_ = 1;
	IFilterGraph *graph_ = nullptr;
	FILTER_STATE state_ = State_Stopped;

public:
	NrFilter() { g_locks++; }
	~NrFilter()
	{
		if (graph_)
			graph_->Release();
		g_locks--;
	}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override
	{
		if (!ppv)
			return E_POINTER;
		if (riid == IID_IUnknown || riid == IID_IPersist || riid == IID_IMediaFilter || riid == IID_IBaseFilter) {
			*ppv = static_cast<IBaseFilter *>(this);
			AddRef();
			return S_OK;
		}
		if (riid == IID_ISpecifyPropertyPages) {
			*ppv = static_cast<ISpecifyPropertyPages *>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&refs_); }
	ULONG STDMETHODCALLTYPE Release() override
	{
		long n = InterlockedDecrement(&refs_);
		if (!n)
			delete this;
		return (ULONG)n;
	}

	HRESULT STDMETHODCALLTYPE GetClassID(CLSID *p) override
	{
		if (!p)
			return E_POINTER;
		*p = CLSID_NrFilter;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE Stop() override
	{
		state_ = State_Stopped;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE Pause() override
	{
		state_ = State_Paused;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE Run(REFERENCE_TIME) override
	{
		state_ = State_Running;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetState(DWORD, FILTER_STATE *s) override
	{
		if (!s)
			return E_POINTER;
		*s = state_;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE SetSyncSource(IReferenceClock *) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE GetSyncSource(IReferenceClock **c) override
	{
		if (c)
			*c = nullptr;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE EnumPins(IEnumPins **e) override
	{
		// No pins: cannot be connected into the graph.
		if (!e)
			return E_POINTER;
		*e = nullptr;
		return E_NOTIMPL;
	}
	HRESULT STDMETHODCALLTYPE FindPin(LPCWSTR, IPin **) override { return E_FAIL; }
	HRESULT STDMETHODCALLTYPE QueryFilterInfo(FILTER_INFO *info) override
	{
		if (!info)
			return E_POINTER;
		wcscpy_s(info->achName, L"DLSS 5 NR");
		info->pGraph = graph_;
		if (graph_)
			graph_->AddRef();
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE JoinFilterGraph(IFilterGraph *g, LPCWSTR) override
	{
		if (graph_)
			graph_->Release();
		graph_ = g;
		if (graph_)
			graph_->AddRef();
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE QueryVendorInfo(LPWSTR *s) override
	{
		if (!s)
			return E_POINTER;
		*s = (LPWSTR)CoTaskMemAlloc(24 * sizeof(wchar_t));
		if (!*s)
			return E_OUTOFMEMORY;
		wcscpy_s(*s, 24, L"potplayer-dlss5-nr");
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetPages(CAUUID *p) override
	{
		if (!p)
			return E_POINTER;
		p->cElems = 1;
		p->pElems = (GUID *)CoTaskMemAlloc(sizeof(GUID));
		if (!p->pElems)
			return E_OUTOFMEMORY;
		p->pElems[0] = CLSID_NrPage;
		return S_OK;
	}
};

class Factory final : public IClassFactory {
	long refs_ = 1;
	CLSID clsid_;

public:
	explicit Factory(REFCLSID c) : clsid_(c) { g_locks++; }
	~Factory() { g_locks--; }
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override
	{
		if (!ppv)
			return E_POINTER;
		if (riid == IID_IUnknown || riid == IID_IClassFactory) {
			*ppv = static_cast<IClassFactory *>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&refs_); }
	ULONG STDMETHODCALLTYPE Release() override
	{
		long n = InterlockedDecrement(&refs_);
		if (!n)
			delete this;
		return (ULONG)n;
	}
	HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown *outer, REFIID riid, void **ppv) override
	{
		if (outer)
			return CLASS_E_NOAGGREGATION;
		IUnknown *obj = nullptr;
		if (clsid_ == CLSID_NrFilter)
			obj = static_cast<IBaseFilter *>(new (std::nothrow) NrFilter());
		else if (clsid_ == CLSID_NrPage)
			obj = static_cast<IPropertyPage *>(new (std::nothrow) NrPage());
		if (!obj)
			return E_OUTOFMEMORY;
		HRESULT hr = obj->QueryInterface(riid, ppv);
		obj->Release();
		return hr;
	}
	HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override
	{
		if (lock)
			g_locks++;
		else
			g_locks--;
		return S_OK;
	}
};

static HRESULT WriteRegStr(HKEY root, const wchar_t *path, const wchar_t *name, const wchar_t *val)
{
	HKEY k;
	if (RegCreateKeyExW(root, path, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS)
		return E_FAIL;
	LONG r = RegSetValueExW(k, name, 0, REG_SZ, (const BYTE *)val, (DWORD)((wcslen(val) + 1) * sizeof(wchar_t)));
	RegCloseKey(k);
	return r == ERROR_SUCCESS ? S_OK : E_FAIL;
}

extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **ppv)
{
	if (clsid != CLSID_NrFilter && clsid != CLSID_NrPage)
		return CLASS_E_CLASSNOTAVAILABLE;
	Factory *f = new (std::nothrow) Factory(clsid);
	if (!f)
		return E_OUTOFMEMORY;
	HRESULT hr = f->QueryInterface(riid, ppv);
	f->Release();
	return hr;
}

extern "C" HRESULT WINAPI DllCanUnloadNow() { return g_locks == 0 ? S_OK : S_FALSE; }

extern "C" HRESULT WINAPI DllRegisterServer()
{
	wchar_t path[MAX_PATH]{};
	GetModuleFileNameW(ThisMod(), path, MAX_PATH);
	wchar_t fg[39], pg[39], cg[39];
	GuidStr(CLSID_NrFilter, fg);
	GuidStr(CLSID_NrPage, pg);
	GuidStr(CLSID_LegacyAmFilterCategory, cg);
	wchar_t k[256];
	swprintf(k, 256, L"Software\\Classes\\CLSID\\%s", fg);
	WriteRegStr(HKEY_CURRENT_USER, k, nullptr, L"DLSS 5 NR");
	swprintf(k, 256, L"Software\\Classes\\CLSID\\%s\\InprocServer32", fg);
	WriteRegStr(HKEY_CURRENT_USER, k, nullptr, path);
	WriteRegStr(HKEY_CURRENT_USER, k, L"ThreadingModel", L"Both");
	swprintf(k, 256, L"Software\\Classes\\CLSID\\%s", pg);
	WriteRegStr(HKEY_CURRENT_USER, k, nullptr, L"DLSS 5 NR Page");
	swprintf(k, 256, L"Software\\Classes\\CLSID\\%s\\InprocServer32", pg);
	WriteRegStr(HKEY_CURRENT_USER, k, nullptr, path);
	WriteRegStr(HKEY_CURRENT_USER, k, L"ThreadingModel", L"Both");
	swprintf(k, 256, L"Software\\Classes\\CLSID\\%s\\Instance\\%s", cg, fg);
	WriteRegStr(HKEY_CURRENT_USER, k, L"CLSID", fg);
	WriteRegStr(HKEY_CURRENT_USER, k, L"FriendlyName", L"DLSS 5 NR");
	WriteRegStr(HKEY_CURRENT_USER, k, L"FilterData", L"");
	return S_OK;
}

extern "C" HRESULT WINAPI DllUnregisterServer()
{
	wchar_t fg[39], pg[39], cg[39], k[256];
	GuidStr(CLSID_NrFilter, fg);
	GuidStr(CLSID_NrPage, pg);
	GuidStr(CLSID_LegacyAmFilterCategory, cg);
	swprintf(k, 256, L"Software\\Classes\\CLSID\\%s", fg);
	RegDeleteTreeW(HKEY_CURRENT_USER, k);
	swprintf(k, 256, L"Software\\Classes\\CLSID\\%s", pg);
	RegDeleteTreeW(HKEY_CURRENT_USER, k);
	swprintf(k, 256, L"Software\\Classes\\CLSID\\%s\\Instance\\%s", cg, fg);
	RegDeleteTreeW(HKEY_CURRENT_USER, k);
	return S_OK;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
