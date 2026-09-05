// AviSynth+ plugin. Function: DLSS5NR(clip, ...)
// Loaded by AviSynth Filter / PotPlayer's AviSynth path. SVP runs first;
// append DLSS5NR() after interpolation.

#include "nr_bridge.h"
#include "settings_ini.h"

#include "avisynth.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include <windows.h>

namespace {

std::once_flag g_init_once;
bool g_init_ok = false;
std::wstring g_runtime;
std::wstring g_shim_dir;
int g_gpu = 0;
std::string g_init_error;

std::wstring Widen(const char *s)
{
	if (!s || !s[0])
		return {};
	int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
	std::wstring w((size_t)std::max(n - 1, 0), 0);
	if (n > 1)
		MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
	return w;
}

std::wstring DefaultRuntimeDir()
{
	wchar_t local[MAX_PATH]{};
	if (!GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH))
		return L".\\runtime";
	std::wstring p = std::wstring(local) + L"\\potplayer-dlss5-nr\\runtime";
	return p;
}

std::wstring DirOfSelf()
{
	wchar_t mod[MAX_PATH]{};
	HMODULE self = nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                   reinterpret_cast<LPCWSTR>(&DirOfSelf), &self);
	if (!GetModuleFileNameW(self, mod, MAX_PATH))
		return {};
	wchar_t *slash = wcsrchr(mod, L'\\');
	if (slash)
		slash[1] = 0;
	return mod;
}

void EnsureInit(int gpu, const char *runtime)
{
	std::call_once(g_init_once, [&]() {
		g_gpu = gpu;
		g_runtime = runtime && runtime[0] ? Widen(runtime) : DefaultRuntimeDir();
		g_shim_dir = DirOfSelf();
		g_init_ok = nrbridge_seh_init(g_gpu, g_runtime.c_str(), g_shim_dir.c_str());
		if (!g_init_ok) {
			g_init_error = nrbridge::last_error();
			if (g_init_error.empty() && nrbridge_seh_message()[0])
				g_init_error = nrbridge_seh_message();
			if (g_init_error.empty())
				g_init_error = "NR init failed (see %LOCALAPPDATA%\\potplayer-dlss5-nr\\nr.log)";
		}
	});
}

class DLSS5NR : public GenericVideoFilter {
public:
	int style, preset, automask, info, gpu;
	float intensity, tone, structure, skin;
	std::string runtime;
	int last_n = -2;
	int skip_left = 0;
	bool pending_reset = false;

	DLSS5NR(PClip child, int style, int preset, float intensity, float tone, float structure, float skin, bool automask,
	        bool info, int gpu, const char *runtime, IScriptEnvironment *env)
	    : GenericVideoFilter(child), style(style), preset(preset), automask(automask ? 1 : 0), info(info ? 1 : 0),
	      gpu(gpu), intensity(intensity), tone(tone), structure(structure), skin(skin),
	      runtime(runtime ? runtime : "")
	{
		if (!vi.IsRGB32())
			env->ThrowError("DLSS5NR: convert the clip to RGB32 first (ConvertToRGB32).");
		if (vi.width < 160 || vi.height < 90)
			env->ThrowError("DLSS5NR: frame smaller than 160x90.");
		// Do NOT touch D3D12/NGX here. Constructor runs while AviSynth builds the
		// graph (and after Prefetch in broken scripts) — a crash becomes
		// "Access Violation (svp.avs, line N)".
	}

	PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *env) override
	{
		PVideoFrame src = child->GetFrame(n, env);

		const bool jumped = (last_n >= 0 && n != last_n + 1);
		if (jumped) {
			// Seek / scrub: NR is 20–80ms and serialized. Prefetch would stall
			// the video clock while audio keeps going. Pass through a few source
			// frames so PotPlayer can resync, then resume NR with a history reset.
			skip_left = 8;
			pending_reset = true;
		}
		last_n = n;

		PVideoFrame dst = env->NewVideoFrame(vi);
		const uint8_t *sp = src->GetReadPtr();
		uint8_t *dp = dst->GetWritePtr();
		const int spitch = src->GetPitch();
		const int dpitch = dst->GetPitch();
		const int row_bytes = vi.BytesFromPixels(vi.width);

		if (skip_left > 0) {
			skip_left--;
			env->BitBlt(dp, dpitch, sp, spitch, row_bytes, vi.height);
			return dst;
		}

		EnsureInit(gpu, runtime.c_str());
		if (!g_init_ok) {
			env->ThrowError("DLSS5NR init failed: %s", g_init_error.c_str());
		}

		const int w = vi.width;
		const int h = vi.height;
		NrBridgeParams p{};
		NrSettingsApply(p, style, preset, intensity, tone, structure, skin, automask);
		p.reset = pending_reset ? 1 : 0;
		pending_reset = false;

		LARGE_INTEGER freq{}, t0{}, t1{};
		QueryPerformanceFrequency(&freq);
		QueryPerformanceCounter(&t0);
		const bool ok = nrbridge_seh_process(sp + (size_t)(h - 1) * (size_t)spitch, -spitch,
		                                    dp + (size_t)(h - 1) * (size_t)dpitch, -dpitch, w, h, p);
		QueryPerformanceCounter(&t1);
		const double ms = freq.QuadPart ? (t1.QuadPart - t0.QuadPart) * 1000.0 / freq.QuadPart : 0;
		if (ms > 80.0)
			skip_left = 3;

		if (!ok) {
			env->BitBlt(dp, dpitch, sp, spitch, row_bytes, h);
			return dst;
		}
		return dst;
	}

	int __stdcall SetCacheHints(int cachehints, int frame_range) override
	{
		(void)frame_range;
		return cachehints == CACHE_GET_MTMODE ? MT_SERIALIZED : 0;
	}
};

AVSValue __cdecl Create_DLSS5NR(AVSValue args, void *, IScriptEnvironment *env)
{
	const char *rt = args[10].Defined() ? args[10].AsString() : "";
	return new DLSS5NR(args[0].AsClip(), args[1].AsInt(1), args[2].AsInt(0), (float)args[3].AsFloat(1.0),
	                   (float)args[4].AsFloat(1.0), (float)args[5].AsFloat(1.5), (float)args[6].AsFloat(2.0),
	                   args[7].AsBool(true), args[8].AsBool(false), args[9].AsInt(0), rt, env);
}

} // namespace

const AVS_Linkage *AVS_linkage = nullptr;

extern "C" __declspec(dllexport) const char *__stdcall AvisynthPluginInit3(IScriptEnvironment *env,
                                                                           const AVS_Linkage *const vectors)
{
	AVS_linkage = vectors;
	env->AddFunction("DLSS5NR",
	                 "c[style]i[preset]i[intensity]f[tone]f[structure]f[skin]f[automask]b[info]b[gpu]i[runtime]s",
	                 Create_DLSS5NR, 0);
	return "DLSS 5 Neural Rendering for PotPlayer / SVP";
}
