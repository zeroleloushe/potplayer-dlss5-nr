// D3D12 + NGX feature-18 backend. CPU BGRA8 in/out.
// Load-time: _nvngx.dll from the driver, nvngx_dlssnr.dll from the user,
// nvngx.dll_pot.dll as the caller shim (path must contain "nvngx.dll").

#include "nr_bridge.h"

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

using NGXResult = int;
static constexpr NGXResult NGX_SUCCESS = 1;
static constexpr int NR_FEATURE_ID = 18;
static constexpr unsigned long long APP_ID = 141959980ULL;
static constexpr const char *PROJECT_ID = "53f803cc-a12f-4d69-90d5-19b7599cad19";
static constexpr const wchar_t *SHIM_NAME = L"nvngx.dll_pot.dll";
static constexpr const wchar_t *NR_DLL_NAME = L"nvngx_dlssnr.dll";

struct NGXHandle {
	unsigned int Id;
};

struct NGXParameter {
	virtual void Set(const char *, unsigned long long) = 0;
	virtual void Set(const char *, float) = 0;
	virtual void Set(const char *, double) = 0;
	virtual void Set(const char *, unsigned int) = 0;
	virtual void Set(const char *, int) = 0;
	virtual void Set(const char *, ID3D11Resource *) = 0;
	virtual void Set(const char *, ID3D12Resource *) = 0;
	virtual void Set(const char *, void *) = 0;
	virtual NGXResult Get(const char *, unsigned long long *) const = 0;
	virtual NGXResult Get(const char *, float *) const = 0;
	virtual NGXResult Get(const char *, double *) const = 0;
	virtual NGXResult Get(const char *, unsigned int *) const = 0;
	virtual NGXResult Get(const char *, int *) const = 0;
	virtual NGXResult Get(const char *, ID3D11Resource **) const = 0;
	virtual NGXResult Get(const char *, ID3D12Resource **) const = 0;
	virtual NGXResult Get(const char *, void **) const = 0;
	virtual void Reset() = 0;
};

using InitProjectIdFn = NGXResult(__cdecl *)(const char *, int, const char *, const wchar_t *, ID3D12Device *, int,
                                             const void *);
using AllocParamsFn = NGXResult(__cdecl *)(NGXParameter **);
using CreateFeatureFn = NGXResult(__cdecl *)(ID3D12GraphicsCommandList *, int, NGXParameter *, NGXHandle **);
using EvaluateFeatureFn = NGXResult(__cdecl *)(ID3D12GraphicsCommandList *, const NGXHandle *, const NGXParameter *,
                                               void *);
using ReleaseFeatureFn = NGXResult(__cdecl *)(NGXHandle *);
using ShutdownFn = NGXResult(__cdecl *)();
using SnippetInitFn = NGXResult(__cdecl *)(unsigned long long, const wchar_t *, ID3D12Device *, const void *, int);

using ShimInitFn = NGXResult(__cdecl *)(void *, unsigned long long, const wchar_t *, ID3D12Device *, int, const void *);
using ShimCreateFn = NGXResult(__cdecl *)(void *, ID3D12GraphicsCommandList *, int, NGXParameter *, NGXHandle **);
using ShimEvaluateFn = NGXResult(__cdecl *)(void *, ID3D12GraphicsCommandList *, const NGXHandle *, const NGXParameter *,
                                            void *);
using ShimReleaseFn = NGXResult(__cdecl *)(void *, NGXHandle *);
using ShimShutdownFn = NGXResult(__cdecl *)(void *);

namespace {

std::mutex g_mu;
bool g_initialized = false;
int g_gpu_index = 0;
std::string g_gpu_name;
std::string g_error;
std::string g_status;
std::wstring g_runtime_dir;
std::wstring g_data_path;

HMODULE g_core_mod = nullptr;
HMODULE g_nr_mod = nullptr;
HMODULE g_shim_mod = nullptr;

InitProjectIdFn g_core_init_pid = nullptr;
AllocParamsFn g_core_alloc = nullptr;
CreateFeatureFn g_core_create = nullptr;
EvaluateFeatureFn g_core_eval = nullptr;
ReleaseFeatureFn g_core_release = nullptr;
ShutdownFn g_core_shutdown = nullptr;

SnippetInitFn g_nr_init = nullptr;
CreateFeatureFn g_nr_create = nullptr;
EvaluateFeatureFn g_nr_eval = nullptr;
ReleaseFeatureFn g_nr_release = nullptr;

ShimInitFn g_shim_init = nullptr;
ShimCreateFn g_shim_create = nullptr;
ShimEvaluateFn g_shim_eval = nullptr;
ShimReleaseFn g_shim_release = nullptr;

ComPtr<ID3D12Device> g_device;
ComPtr<ID3D12CommandQueue> g_queue;
ComPtr<ID3D12CommandAllocator> g_cmd_alloc;
ComPtr<ID3D12GraphicsCommandList> g_cmd;
ComPtr<ID3D12Fence> g_fence;
HANDLE g_fence_event = nullptr;
UINT64 g_fence_value = 0;

ComPtr<ID3D12Resource> g_color;
ComPtr<ID3D12Resource> g_output;
ComPtr<ID3D12Resource> g_upload;
ComPtr<ID3D12Resource> g_readback;
void *g_upload_ptr = nullptr;
void *g_readback_ptr = nullptr;
UINT g_row_pitch = 0;
UINT64 g_total_bytes = 0;
int g_width = 0;
int g_height = 0;

NGXParameter *g_params = nullptr;
NGXHandle *g_feature = nullptr;
bool g_channel_order_checked = false;
bool g_slots_are_bgra = false;

uint16_t g_lut8_to_half[256];

void SetError(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	g_error = buf;
	g_status = buf;
	wchar_t dir[MAX_PATH]{};
	if (GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH)) {
		wchar_t path[MAX_PATH]{};
		swprintf(path, MAX_PATH, L"%s\\potplayer-dlss5-nr", dir);
		CreateDirectoryW(path, nullptr);
		swprintf(path, MAX_PATH, L"%s\\potplayer-dlss5-nr\\nr.log", dir);
		FILE *f = nullptr;
		if (_wfopen_s(&f, path, L"ab") == 0 && f) {
			SYSTEMTIME st{};
			GetLocalTime(&st);
			fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d %s\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
			        st.wSecond, buf);
			fclose(f);
		}
	}
}

void SetStatus(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	g_status = buf;
}

uint16_t FloatToHalf(float f)
{
	uint32_t x;
	memcpy(&x, &f, 4);
	uint32_t sign = (x >> 16) & 0x8000;
	int32_t exp = (int32_t)((x >> 23) & 0xFF) - 127 + 15;
	uint32_t man = x & 0x7FFFFF;
	if ((x & 0x7FFFFFFF) == 0)
		return (uint16_t)sign;
	if (exp <= 0) {
		if (exp < -10)
			return (uint16_t)sign;
		man = (man | 0x800000) >> (1 - exp);
		return (uint16_t)(sign | (man >> 13));
	}
	if (exp >= 31)
		return (uint16_t)(sign | 0x7C00);
	return (uint16_t)(sign | (exp << 10) | (man >> 13));
}

float HalfToFloat(uint16_t h)
{
	uint32_t sign = (uint32_t)(h & 0x8000) << 16;
	uint32_t exp = (h >> 10) & 0x1F;
	uint32_t man = h & 0x3FF;
	uint32_t x;
	if (exp == 0) {
		if (man == 0) {
			x = sign;
		} else {
			exp = 127 - 15 + 1;
			while ((man & 0x400) == 0) {
				man <<= 1;
				exp--;
			}
			man &= 0x3FF;
			x = sign | (exp << 23) | (man << 13);
		}
	} else if (exp == 31) {
		x = sign | 0x7F800000 | (man << 13);
	} else {
		x = sign | ((exp - 15 + 127) << 23) | (man << 13);
	}
	float f;
	memcpy(&f, &x, 4);
	return f;
}

void BuildLut()
{
	for (int i = 0; i < 256; ++i)
		g_lut8_to_half[i] = FloatToHalf((float)i / 255.0f);
}

D3D12_RESOURCE_BARRIER Barrier(ID3D12Resource *r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
	D3D12_RESOURCE_BARRIER b{};
	b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	b.Transition.pResource = r;
	b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	b.Transition.StateBefore = from;
	b.Transition.StateAfter = to;
	return b;
}

bool WaitGpu()
{
	const UINT64 v = ++g_fence_value;
	if (FAILED(g_queue->Signal(g_fence.Get(), v)))
		return false;
	if (g_fence->GetCompletedValue() < v) {
		if (FAILED(g_fence->SetEventOnCompletion(v, g_fence_event)))
			return false;
		WaitForSingleObject(g_fence_event, INFINITE);
	}
	return true;
}

bool ResetList()
{
	if (FAILED(g_cmd_alloc->Reset()))
		return false;
	if (FAILED(g_cmd->Reset(g_cmd_alloc.Get(), nullptr)))
		return false;
	return true;
}

bool ExecuteList()
{
	if (FAILED(g_cmd->Close()))
		return false;
	ID3D12CommandList *lists[] = {g_cmd.Get()};
	g_queue->ExecuteCommandLists(1, lists);
	return WaitGpu();
}

HMODULE TryLoad(const wchar_t *path)
{
	if (!path || !path[0])
		return nullptr;
	__try {
		return LoadLibraryW(path);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return nullptr;
	}
}

HMODULE LoadCoreFromDriverStore()
{
	WIN32_FIND_DATAW fd{};
	wchar_t pattern[] = L"C:\\Windows\\System32\\DriverStore\\FileRepository\\nv*";
	HANDLE h = FindFirstFileW(pattern, &fd);
	if (h == INVALID_HANDLE_VALUE)
		return nullptr;

	HMODULE best = nullptr;
	FILETIME best_time{};
	bool have_best = false;
	do {
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			continue;
		wchar_t dll[MAX_PATH];
		swprintf(dll, MAX_PATH, L"C:\\Windows\\System32\\DriverStore\\FileRepository\\%s\\_nvngx.dll",
		         fd.cFileName);
		if (GetFileAttributesW(dll) == INVALID_FILE_ATTRIBUTES)
			continue;
		if (!have_best || CompareFileTime(&fd.ftLastWriteTime, &best_time) > 0) {
			if (best)
				FreeLibrary(best);
			best = LoadLibraryW(dll);
			if (best) {
				best_time = fd.ftLastWriteTime;
				have_best = true;
			}
		}
	} while (FindNextFileW(h, &fd));
	FindClose(h);
	return best;
}

HMODULE LoadCoreNGX(const wchar_t *runtime_dir)
{
	wchar_t path[MAX_PATH];
	if (runtime_dir && runtime_dir[0]) {
		swprintf(path, MAX_PATH, L"%s\\_nvngx.dll", runtime_dir);
		if (HMODULE m = TryLoad(path))
			return m;
	}
	if (HMODULE m = LoadLibraryW(L"_nvngx.dll"))
		return m;
	return LoadCoreFromDriverStore();
}

HMODULE LoadNextToSelf(const wchar_t *name)
{
	wchar_t mod[MAX_PATH]{};
	HMODULE self = nullptr;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                        reinterpret_cast<LPCWSTR>(&LoadNextToSelf), &self))
		return nullptr;
	if (!GetModuleFileNameW(self, mod, MAX_PATH))
		return nullptr;
	wchar_t *slash = wcsrchr(mod, L'\\');
	if (!slash)
		return nullptr;
	slash[1] = 0;
	wcsncat(mod, name, MAX_PATH - wcslen(mod) - 1);
	return LoadLibraryW(mod);
}

ComPtr<ID3D12Device> CreateDevice(int nvidia_index)
{
	ComPtr<IDXGIFactory1> factory;
	if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
		return nullptr;

	int seen = 0;
	for (UINT i = 0;; ++i) {
		ComPtr<IDXGIAdapter1> adapter;
		if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND)
			break;
		DXGI_ADAPTER_DESC1 desc{};
		adapter->GetDesc1(&desc);
		if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) || desc.VendorId != 0x10DE)
			continue;
		if (seen++ != nvidia_index)
			continue;
		char gpu_utf8[512]{};
		WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, gpu_utf8, sizeof(gpu_utf8), nullptr, nullptr);
		if (gpu_utf8[0])
			g_gpu_name = gpu_utf8;
		ComPtr<ID3D12Device> d;
		if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&d))))
			return d;
		return nullptr;
	}
	SetError("No NVIDIA adapter at index %d", nvidia_index);
	return nullptr;
}

bool SetupD3D12()
{
	g_device = CreateDevice(g_gpu_index);
	if (!g_device) {
		if (g_error.empty())
			SetError("Could not create a D3D12 device for NVIDIA GPU index %d", g_gpu_index);
		return false;
	}
	D3D12_COMMAND_QUEUE_DESC q{};
	q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	if (FAILED(g_device->CreateCommandQueue(&q, IID_PPV_ARGS(&g_queue)))) {
		SetError("CreateCommandQueue failed");
		return false;
	}
	if (FAILED(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_cmd_alloc)))) {
		SetError("CreateCommandAllocator failed");
		return false;
	}
	if (FAILED(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_cmd_alloc.Get(), nullptr,
	                                       IID_PPV_ARGS(&g_cmd)))) {
		SetError("CreateCommandList failed");
		return false;
	}
	g_cmd->Close();
	if (FAILED(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)))) {
		SetError("CreateFence failed");
		return false;
	}
	g_fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	return g_fence_event != nullptr;
}

ComPtr<ID3D12Resource> CreateTex(UINT w, UINT h, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state)
{
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = w;
	desc.Height = h;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.Flags = flags;
	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	ComPtr<ID3D12Resource> r;
	if (FAILED(g_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&r))))
		return nullptr;
	return r;
}

ComPtr<ID3D12Resource> CreateBuffer(UINT64 bytes, D3D12_HEAP_TYPE heap_type)
{
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = bytes;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = heap_type;
	D3D12_RESOURCE_STATES state =
	    heap_type == D3D12_HEAP_TYPE_READBACK ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_GENERIC_READ;
	ComPtr<ID3D12Resource> r;
	if (FAILED(g_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&r))))
		return nullptr;
	return r;
}

void ReleaseFeatureAndResources()
{
	if (g_feature) {
		if (g_shim_release && g_nr_release)
			g_shim_release(reinterpret_cast<void *>(g_nr_release), g_feature);
		else if (g_core_release)
			g_core_release(g_feature);
		g_feature = nullptr;
	}
	if (g_upload && g_upload_ptr) {
		g_upload->Unmap(0, nullptr);
		g_upload_ptr = nullptr;
	}
	if (g_readback && g_readback_ptr) {
		g_readback->Unmap(0, nullptr);
		g_readback_ptr = nullptr;
	}
	g_color.Reset();
	g_output.Reset();
	g_upload.Reset();
	g_readback.Reset();
	g_width = g_height = 0;
	g_channel_order_checked = false;
}

void SetCommonParams(int width, int height, const NrBridgeParams &p)
{
	g_params->Set("DLSSNR.Width", width);
	g_params->Set("DLSSNR.Height", height);
	g_params->Set("DLSSNR.Enabled", 1);
	g_params->Set("DLSSNR.Reset", p.reset ? 1 : 0);
	g_params->Set("DLSSNR.Style", p.style);
	g_params->Set("DLSSNR.Hint.Render.Preset", p.preset);
	g_params->Set("DLSSNR.Intensity", p.intensity);
	g_params->Set("DLSSNR.LocalToneStrength", p.tone);
	g_params->Set("DLSSNR.LocalStructureStrength", p.structure);
	g_params->Set("DLSSNR.SkinStructureStrength", p.skin);
	g_params->Set("DLSSNR.UseAutoMask", p.automask);
	g_params->Set("DLSSNR.UICorrection", p.ui_correction);
	g_params->Set("DLSSNR.DepthInverted", 1);
	g_params->Set("DLSSNR.ScalingRatio", 1.0f);
	g_params->Set("DLSSNR.MVecScaleX", 1.0f);
	g_params->Set("DLSSNR.MVecScaleY", 1.0f);
	g_params->Set("DLSSNR.Color", g_color.Get());
	g_params->Set("DLSSNR.Output", g_output.Get());
	g_params->Set("DLSSNR.Backbuffer", g_output.Get());
	g_params->Set("DLSSNR.ColorSubrectBaseX", 0);
	g_params->Set("DLSSNR.ColorSubrectBaseY", 0);
	g_params->Set("DLSSNR.ColorSubrectWidth", width);
	g_params->Set("DLSSNR.ColorSubrectHeight", height);
	g_params->Set("DLSSNR.OutputSubrectBaseX", 0);
	g_params->Set("DLSSNR.OutputSubrectBaseY", 0);
	g_params->Set("DLSSNR.OutputSubrectWidth", width);
	g_params->Set("DLSSNR.OutputSubrectHeight", height);
}

bool EnsureResources(int width, int height)
{
	if (g_width == width && g_height == height && g_feature && g_color && g_output)
		return true;

	ReleaseFeatureAndResources();

	g_row_pitch = (width * 8 + 255) & ~255u; // RGBA16F = 8 bytes/px, 256-byte align
	g_total_bytes = (UINT64)g_row_pitch * (UINT64)height;

	g_color = CreateTex((UINT)width, (UINT)height, D3D12_RESOURCE_FLAG_NONE,
	                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	g_output = CreateTex((UINT)width, (UINT)height, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
	                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	g_upload = CreateBuffer(g_total_bytes, D3D12_HEAP_TYPE_UPLOAD);
	g_readback = CreateBuffer(g_total_bytes, D3D12_HEAP_TYPE_READBACK);
	if (!g_color || !g_output || !g_upload || !g_readback) {
		SetError("Failed to allocate %dx%d NR textures", width, height);
		ReleaseFeatureAndResources();
		return false;
	}
	if (FAILED(g_upload->Map(0, nullptr, &g_upload_ptr)) || !g_upload_ptr) {
		SetError("persistent upload Map failed");
		ReleaseFeatureAndResources();
		return false;
	}
	if (FAILED(g_readback->Map(0, nullptr, &g_readback_ptr)) || !g_readback_ptr) {
		SetError("persistent readback Map failed");
		ReleaseFeatureAndResources();
		return false;
	}

	NrBridgeParams dummy{};
	dummy.intensity = 1.0f;
	dummy.tone = 1.0f;
	dummy.structure = 1.5f;
	dummy.skin = 2.0f;
	dummy.reset = 1;
	SetCommonParams(width, height, dummy);

	if (!ResetList()) {
		SetError("Reset command list before CreateFeature failed");
		return false;
	}
	NGXResult r = NGX_SUCCESS;
	if (g_nr_create && g_shim_create)
		r = g_shim_create(reinterpret_cast<void *>(g_nr_create), g_cmd.Get(), NR_FEATURE_ID, g_params, &g_feature);
	else if (g_core_create)
		r = g_core_create(g_cmd.Get(), NR_FEATURE_ID, g_params, &g_feature);
	else {
		SetError("No CreateFeature entry point");
		return false;
	}
	if (!ExecuteList()) {
		SetError("CreateFeature execute failed");
		return false;
	}
	if (r != NGX_SUCCESS || !g_feature) {
		SetError("CreateFeature(18) failed: 0x%08X", (unsigned)r);
		g_feature = nullptr;
		return false;
	}
	g_width = width;
	g_height = height;
	SetStatus("NR ready %dx%d on %s", width, height, g_gpu_name.c_str());
	return true;
}

uint8_t ClampU8(float x)
{
	if (x < 0.f)
		return 0;
	if (x > 1.f)
		return 255;
	return (uint8_t)(x * 255.f + 0.5f);
}

} // namespace

namespace nrbridge {

bool init(int gpu_index, const wchar_t *runtime_dir, const wchar_t *shim_dir)
{
	std::lock_guard<std::mutex> lock(g_mu);
	if (g_initialized)
		return true;

	g_error.clear();
	g_gpu_index = gpu_index;
	g_runtime_dir = runtime_dir ? runtime_dir : L"";
	BuildLut();

	wchar_t local[MAX_PATH]{};
	if (GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH)) {
		g_data_path = std::wstring(local) + L"\\potplayer-dlss5-nr";
		CreateDirectoryW(g_data_path.c_str(), nullptr);
	} else {
		g_data_path = g_runtime_dir.empty() ? L"." : g_runtime_dir;
	}

	g_core_mod = LoadCoreNGX(runtime_dir);
	if (!g_core_mod) {
		SetError("Could not load _nvngx.dll (NVIDIA driver NGX core)");
		return false;
	}

	wchar_t nr_path[MAX_PATH]{};
	if (runtime_dir && runtime_dir[0])
		swprintf(nr_path, MAX_PATH, L"%s\\%s", runtime_dir, NR_DLL_NAME);
	g_nr_mod = TryLoad(nr_path);
	if (!g_nr_mod)
		g_nr_mod = LoadNextToSelf(NR_DLL_NAME);
	if (!g_nr_mod) {
		SetError("nvngx_dlssnr.dll not found. Put it in the runtime folder (see README).");
		return false;
	}

	if (shim_dir && shim_dir[0]) {
		wchar_t p[MAX_PATH]{};
		swprintf(p, MAX_PATH, L"%s\\%s", shim_dir, SHIM_NAME);
		g_shim_mod = TryLoad(p);
	}
	if (!g_shim_mod && runtime_dir && runtime_dir[0]) {
		wchar_t p[MAX_PATH]{};
		swprintf(p, MAX_PATH, L"%s\\%s", runtime_dir, SHIM_NAME);
		g_shim_mod = TryLoad(p);
	}
	if (!g_shim_mod)
		g_shim_mod = LoadNextToSelf(SHIM_NAME);
	if (!g_shim_mod)
		g_shim_mod = LoadLibraryW(SHIM_NAME);
	if (!g_shim_mod) {
		SetError("nvngx.dll_pot.dll (caller shim) not found next to the plugin");
		return false;
	}

	g_core_init_pid = (InitProjectIdFn)GetProcAddress(g_core_mod, "NVSDK_NGX_D3D12_Init_ProjectID");
	g_core_alloc = (AllocParamsFn)GetProcAddress(g_core_mod, "NVSDK_NGX_D3D12_AllocateParameters");
	g_core_create = (CreateFeatureFn)GetProcAddress(g_core_mod, "NVSDK_NGX_D3D12_CreateFeature");
	g_core_eval = (EvaluateFeatureFn)GetProcAddress(g_core_mod, "NVSDK_NGX_D3D12_EvaluateFeature");
	g_core_release = (ReleaseFeatureFn)GetProcAddress(g_core_mod, "NVSDK_NGX_D3D12_ReleaseFeature");
	g_core_shutdown = (ShutdownFn)GetProcAddress(g_core_mod, "NVSDK_NGX_D3D12_Shutdown");

	g_nr_init = (SnippetInitFn)GetProcAddress(g_nr_mod, "NVSDK_NGX_D3D12_Init_Ext");
	g_nr_create = (CreateFeatureFn)GetProcAddress(g_nr_mod, "NVSDK_NGX_D3D12_CreateFeature");
	g_nr_eval = (EvaluateFeatureFn)GetProcAddress(g_nr_mod, "NVSDK_NGX_D3D12_EvaluateFeature");
	g_nr_release = (ReleaseFeatureFn)GetProcAddress(g_nr_mod, "NVSDK_NGX_D3D12_ReleaseFeature");

	g_shim_init = (ShimInitFn)GetProcAddress(g_shim_mod, "DLSSNR_CallInit");
	g_shim_create = (ShimCreateFn)GetProcAddress(g_shim_mod, "DLSSNR_CallCreate");
	g_shim_eval = (ShimEvaluateFn)GetProcAddress(g_shim_mod, "DLSSNR_CallEvaluate");
	g_shim_release = (ShimReleaseFn)GetProcAddress(g_shim_mod, "DLSSNR_CallRelease");

	if (!g_core_init_pid || !g_core_alloc) {
		SetError("_nvngx.dll is missing Init_ProjectID / AllocateParameters");
		return false;
	}
	if (!g_nr_init || !g_nr_create || !g_nr_eval) {
		SetError("nvngx_dlssnr.dll is missing Init_Ext / CreateFeature / EvaluateFeature");
		return false;
	}
	if (!g_shim_init || !g_shim_create || !g_shim_eval) {
		SetError("caller shim is missing DLSSNR_Call* exports");
		return false;
	}

	if (!SetupD3D12())
		return false;

	NGXResult ir = 0;
	for (int ver = 0x13; ver <= 0x20; ++ver) {
		ir = g_core_init_pid(PROJECT_ID, 0, "", g_data_path.c_str(), g_device.Get(), ver, nullptr);
		if (ir == NGX_SUCCESS)
			break;
	}
	if (ir != NGX_SUCCESS) {
		SetError("NGX Init_ProjectID failed: 0x%08X", (unsigned)ir);
		return false;
	}

	NGXResult sr = g_shim_init(reinterpret_cast<void *>(g_nr_init), APP_ID, g_data_path.c_str(), g_device.Get(), 0x15,
	                           nullptr);
	if (sr != NGX_SUCCESS) {
		SetError("NR Init_Ext via shim failed: 0x%08X (need signed nvngx_dlssnr.dll, 0xBAD00002 = caller gate)",
		         (unsigned)sr);
		return false;
	}

	if (g_core_alloc(&g_params) != NGX_SUCCESS || !g_params) {
		SetError("AllocateParameters failed");
		return false;
	}

	g_initialized = true;
	SetStatus("NGX + NR initialized on %s", g_gpu_name.empty() ? "NVIDIA GPU" : g_gpu_name.c_str());
	return true;
}

bool process(const uint8_t *src_bgra, int src_row_pitch, uint8_t *dst_bgra, int dst_row_pitch, int width, int height,
             const NrBridgeParams &params)
{
	std::lock_guard<std::mutex> lock(g_mu);
	if (!g_initialized) {
		SetError("nrbridge::init was not called");
		return false;
	}
	if (width < 160 || height < 90) {
		SetError("frame too small for NR (%dx%d)", width, height);
		return false;
	}
	if (!EnsureResources(width, height))
		return false;
	if (!g_upload_ptr || !g_readback_ptr) {
		SetError("upload/readback not mapped");
		return false;
	}
	if (!ResetList()) {
		SetError("command list reset failed");
		return false;
	}

	const ptrdiff_t spitch = src_row_pitch;
	const ptrdiff_t dpitch = dst_row_pitch;
	constexpr uint16_t kOne = 0x3C00;
	auto *dst_base = static_cast<uint8_t *>(g_upload_ptr);
	for (int y = 0; y < height; ++y) {
		auto *row = reinterpret_cast<uint16_t *>(dst_base + (size_t)y * g_row_pitch);
		const uint8_t *src = src_bgra + (ptrdiff_t)y * spitch;
		for (int x = 0; x < width; ++x) {
			const uint8_t *px = src + x * 4;
			row[x * 4 + 0] = g_lut8_to_half[px[2]];
			row[x * 4 + 1] = g_lut8_to_half[px[1]];
			row[x * 4 + 2] = g_lut8_to_half[px[0]];
			row[x * 4 + 3] = kOne;
		}
	}

	auto b1 = Barrier(g_color.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
	g_cmd->ResourceBarrier(1, &b1);
	D3D12_TEXTURE_COPY_LOCATION dst_loc{};
	dst_loc.pResource = g_color.Get();
	dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	D3D12_TEXTURE_COPY_LOCATION src_loc{};
	src_loc.pResource = g_upload.Get();
	src_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	src_loc.PlacedFootprint.Offset = 0;
	src_loc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	src_loc.PlacedFootprint.Footprint.Width = (UINT)width;
	src_loc.PlacedFootprint.Footprint.Height = (UINT)height;
	src_loc.PlacedFootprint.Footprint.Depth = 1;
	src_loc.PlacedFootprint.Footprint.RowPitch = g_row_pitch;
	g_cmd->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, nullptr);
	auto b2 = Barrier(g_color.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	g_cmd->ResourceBarrier(1, &b2);

	SetCommonParams(width, height, params);

	NGXResult er = g_shim_eval(reinterpret_cast<void *>(g_nr_eval), g_cmd.Get(), g_feature, g_params, nullptr);
	if (er != NGX_SUCCESS) {
		if (!ExecuteList()) {
			/* still report NGX error */
		}
		SetError("EvaluateFeature(18) failed: 0x%08X", (unsigned)er);
		return false;
	}

	auto b3 = Barrier(g_output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
	g_cmd->ResourceBarrier(1, &b3);
	D3D12_TEXTURE_COPY_LOCATION out_src{};
	out_src.pResource = g_output.Get();
	out_src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	D3D12_TEXTURE_COPY_LOCATION out_dst{};
	out_dst.pResource = g_readback.Get();
	out_dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	out_dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	out_dst.PlacedFootprint.Footprint.Width = (UINT)width;
	out_dst.PlacedFootprint.Footprint.Height = (UINT)height;
	out_dst.PlacedFootprint.Footprint.Depth = 1;
	out_dst.PlacedFootprint.Footprint.RowPitch = g_row_pitch;
	g_cmd->CopyTextureRegion(&out_dst, 0, 0, 0, &out_src, nullptr);
	auto b4 = Barrier(g_output.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	g_cmd->ResourceBarrier(1, &b4);

	if (!ExecuteList()) {
		SetError("NR execute/fence failed");
		return false;
	}

	auto *mapped = static_cast<uint8_t *>(g_readback_ptr);

	if (!g_channel_order_checked) {
		double mae_rgba = 0, mae_bgra = 0;
		const int samples = (std::min)(width * height, 4096);
		const int step = (std::max)(1, (width * height) / samples);
		int n = 0;
		for (int i = 0; i < width * height; i += step) {
			int y = i / width, x = i % width;
			auto *row = reinterpret_cast<const uint16_t *>(mapped + (size_t)y * g_row_pitch);
			float or_ = HalfToFloat(row[x * 4 + 0]);
			float og = HalfToFloat(row[x * 4 + 1]);
			float ob = HalfToFloat(row[x * 4 + 2]);
			const uint8_t *s = src_bgra + (ptrdiff_t)y * spitch + x * 4;
			float sr = s[2] / 255.f, sg = s[1] / 255.f, sb = s[0] / 255.f;
			mae_rgba += std::fabs(or_ - sr) + std::fabs(og - sg) + std::fabs(ob - sb);
			mae_bgra += std::fabs(or_ - sb) + std::fabs(og - sg) + std::fabs(ob - sr);
			++n;
		}
		g_slots_are_bgra = n && mae_bgra < mae_rgba;
		g_channel_order_checked = true;
	}

	for (int y = 0; y < height; ++y) {
		auto *row = reinterpret_cast<const uint16_t *>(mapped + (size_t)y * g_row_pitch);
		uint8_t *dst = dst_bgra + (ptrdiff_t)y * dpitch;
		for (int x = 0; x < width; ++x) {
			float c0 = HalfToFloat(row[x * 4 + 0]);
			float c1 = HalfToFloat(row[x * 4 + 1]);
			float c2 = HalfToFloat(row[x * 4 + 2]);
			if (g_slots_are_bgra) {
				dst[x * 4 + 0] = ClampU8(c0);
				dst[x * 4 + 1] = ClampU8(c1);
				dst[x * 4 + 2] = ClampU8(c2);
			} else {
				dst[x * 4 + 0] = ClampU8(c2);
				dst[x * 4 + 1] = ClampU8(c1);
				dst[x * 4 + 2] = ClampU8(c0);
			}
			dst[x * 4 + 3] = 255;
		}
	}
	return true;
}

void shutdown()
{
	std::lock_guard<std::mutex> lock(g_mu);
	ReleaseFeatureAndResources();
	g_params = nullptr;
	if (g_core_shutdown)
		g_core_shutdown();
	g_cmd.Reset();
	g_cmd_alloc.Reset();
	g_queue.Reset();
	g_fence.Reset();
	g_device.Reset();
	if (g_fence_event) {
		CloseHandle(g_fence_event);
		g_fence_event = nullptr;
	}
	if (g_shim_mod)
		FreeLibrary(g_shim_mod);
	if (g_nr_mod)
		FreeLibrary(g_nr_mod);
	if (g_core_mod)
		FreeLibrary(g_core_mod);
	g_shim_mod = g_nr_mod = g_core_mod = nullptr;
	g_initialized = false;
}

bool ready()
{
	return g_initialized;
}

const char *gpu_name()
{
	return g_gpu_name.c_str();
}

const char *last_error()
{
	return g_error.c_str();
}

const char *status_text()
{
	return g_status.c_str();
}

} // namespace nrbridge
