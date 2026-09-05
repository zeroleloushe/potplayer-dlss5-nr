// Caller-validation trampoline for nvngx_dlssnr.dll.
//
// Every gated export of the 310.8 NR snippet does, in effect:
//   GetModuleHandleExA(FROM_ADDRESS, return_address)
//   GetModuleFileNameW(...)
//   if (!wcsstr(path, L"nvngx.dll")) return 0xBAD00002;
//
// This DLL is built as nvngx.dll_pot.dll so the path contains that substring.
// It holds no NGX state — it just calls the real function so the return
// address belongs to this module.

#include <d3d12.h>
#include <windows.h>

using NGXResult = int;

struct NGXHandle {
	unsigned int Id;
};
struct NGXParameter;

using SnippetInitFn = NGXResult(__cdecl *)(unsigned long long, const wchar_t *, ID3D12Device *, const void *, int);
using CreateFn = NGXResult(__cdecl *)(ID3D12GraphicsCommandList *, int, NGXParameter *, NGXHandle **);
using EvalFn = NGXResult(__cdecl *)(ID3D12GraphicsCommandList *, const NGXHandle *, const NGXParameter *, void *);
using ReleaseFn = NGXResult(__cdecl *)(NGXHandle *);
using ShutdownFn = NGXResult(__cdecl *)();

static volatile LONG g_post_call_sink = 0;

static __forceinline NGXResult FinishCall(NGXResult r)
{
	g_post_call_sink = static_cast<LONG>(r);
	return r;
}

extern "C" {

__declspec(dllexport) __declspec(noinline) NGXResult __cdecl DLSSNR_CallInit(
    void *real_fn, unsigned long long app_id, const wchar_t *path, ID3D12Device *device, int version,
    const void *common_info)
{
	NGXResult r = reinterpret_cast<SnippetInitFn>(real_fn)(app_id, path, device, common_info, version);
	return FinishCall(r);
}

__declspec(dllexport) __declspec(noinline) NGXResult __cdecl DLSSNR_CallCreate(
    void *real_fn, ID3D12GraphicsCommandList *list, int feature_id, NGXParameter *params, NGXHandle **handle)
{
	NGXResult r = reinterpret_cast<CreateFn>(real_fn)(list, feature_id, params, handle);
	return FinishCall(r);
}

__declspec(dllexport) __declspec(noinline) NGXResult __cdecl DLSSNR_CallEvaluate(
    void *real_fn, ID3D12GraphicsCommandList *list, const NGXHandle *handle, const NGXParameter *params,
    void *callback)
{
	NGXResult r = reinterpret_cast<EvalFn>(real_fn)(list, handle, params, callback);
	return FinishCall(r);
}

__declspec(dllexport) __declspec(noinline) NGXResult __cdecl DLSSNR_CallRelease(void *real_fn, NGXHandle *handle)
{
	NGXResult r = reinterpret_cast<ReleaseFn>(real_fn)(handle);
	return FinishCall(r);
}

__declspec(dllexport) __declspec(noinline) NGXResult __cdecl DLSSNR_CallShutdown(void *real_fn)
{
	NGXResult r = reinterpret_cast<ShutdownFn>(real_fn)();
	return FinishCall(r);
}

} // extern "C"
