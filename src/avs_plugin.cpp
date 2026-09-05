// AviSynth+ plugin. Function: DLSS5NR(clip, ...)
// GetFrame never waits on D3D12/NGX. A worker thread runs NR; the filter
// returns the current frame immediately (passthrough or a 1–2 frame delayed
// NR result). This keeps PotPlayer's audio clock from running away on seek.

#include "nr_bridge.h"
#include "settings_ini.h"

#include "avisynth.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

namespace {

std::once_flag g_init_once;
std::atomic<bool> g_init_ok{false};
std::atomic<bool> g_init_done{false};
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
	return std::wstring(local) + L"\\potplayer-dlss5-nr\\runtime";
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
		g_init_done = true;
	});
}

struct Job {
	std::vector<uint8_t> bgra; // top-down packed BGRA, pitch = w*4
	int w = 0, h = 0, n = -1;
	NrBridgeParams p{};
	bool valid = false;
};

std::mutex g_job_mu;
std::condition_variable g_job_cv;
Job g_job;
std::atomic<bool> g_run{true};

std::mutex g_out_mu;
std::vector<uint8_t> g_out;
int g_out_w = 0, g_out_h = 0, g_out_n = -999999;
bool g_out_ok = false;
std::atomic<int> g_live_n{-1};

void WorkerLoop(int gpu, std::string runtime)
{
	EnsureInit(gpu, runtime.c_str());
	Job local;
	int last_proc = -2;
	while (g_run) {
		{
			std::unique_lock<std::mutex> lk(g_job_mu);
			g_job_cv.wait(lk, [] { return g_job.valid || !g_run.load(); });
			if (!g_run)
				break;
			local.w = g_job.w;
			local.h = g_job.h;
			local.n = g_job.n;
			local.p = g_job.p;
			local.bgra.swap(g_job.bgra);
			g_job.valid = false;
		}
		if (!g_init_ok || local.w < 160 || local.h < 90 || local.bgra.empty())
			continue;
		// Drop jobs that are already too old — don't burn CreateFeature on a seek leftover.
		if (g_live_n.load() - local.n > 2)
			continue;
		if (last_proc >= 0 && local.n != last_proc + 1)
			local.p.reset = 1;
		last_proc = local.n;
		std::vector<uint8_t> out(local.bgra.size());
		const bool ok = nrbridge_seh_process(local.bgra.data(), local.w * 4, out.data(), local.w * 4, local.w, local.h,
		                                    local.p);
		if (!ok)
			continue;
		std::lock_guard<std::mutex> lk(g_out_mu);
		g_out.swap(out);
		g_out_w = local.w;
		g_out_h = local.h;
		g_out_n = local.n;
		g_out_ok = true;
	}
}

std::once_flag g_worker_once;

void StartWorker(int gpu, const char *runtime)
{
	std::call_once(g_worker_once, [gpu, runtime]() {
		std::string rt = runtime ? runtime : "";
		std::thread([gpu, rt]() { WorkerLoop(gpu, rt); }).detach();
	});
}

void SubmitJob(const uint8_t *sp, int spitch, int w, int h, int n, const NrBridgeParams &p)
{
	std::lock_guard<std::mutex> lk(g_job_mu);
	g_job.bgra.resize((size_t)w * h * 4);
	for (int y = 0; y < h; ++y) {
		const uint8_t *row = sp + (size_t)(h - 1 - y) * (size_t)spitch;
		memcpy(g_job.bgra.data() + (size_t)y * w * 4, row, (size_t)w * 4);
	}
	g_job.w = w;
	g_job.h = h;
	g_job.n = n;
	g_job.p = p;
	g_job.valid = true;
	g_job_cv.notify_one();
}

bool BlitRecentNr(uint8_t *dp, int dpitch, int w, int h, int n)
{
	std::lock_guard<std::mutex> lk(g_out_mu);
	if (!g_out_ok || g_out_w != w || g_out_h != h)
		return false;
	// More than 2 source frames behind = stale (would look like a freeze / desync).
	if (n - g_out_n > 2 || g_out_n - n > 2)
		return false;
	for (int y = 0; y < h; ++y) {
		uint8_t *row = dp + (size_t)(h - 1 - y) * (size_t)dpitch;
		memcpy(row, g_out.data() + (size_t)y * w * 4, (size_t)w * 4);
	}
	return true;
}

class DLSS5NR : public GenericVideoFilter {
public:
	int style, preset, automask, info, gpu;
	float intensity, tone, structure, skin;
	std::string runtime;
	int last_n = -2;

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
		StartWorker(gpu, this->runtime.c_str());
	}

	PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *env) override
	{
		PVideoFrame src = child->GetFrame(n, env);
		PVideoFrame dst = env->NewVideoFrame(vi);
		const int w = vi.width;
		const int h = vi.height;
		const uint8_t *sp = src->GetReadPtr();
		uint8_t *dp = dst->GetWritePtr();
		const int spitch = src->GetPitch();
		const int dpitch = dst->GetPitch();
		const int row_bytes = vi.BytesFromPixels(w);

		NrBridgeParams p{};
		NrSettingsApply(p, style, preset, intensity, tone, structure, skin, automask);
		p.reset = (last_n >= 0 && n != last_n + 1) ? 1 : 0;
		last_n = n;
		g_live_n = n;

		SubmitJob(sp, spitch, w, h, n, p);

		if (!BlitRecentNr(dp, dpitch, w, h, n))
			env->BitBlt(dp, dpitch, sp, spitch, row_bytes, h);
		return dst;
	}

	int __stdcall SetCacheHints(int cachehints, int frame_range) override
	{
		(void)frame_range;
		// GetFrame only memcpy's; D3D lives on the worker. Let Prefetch parallelize.
		return cachehints == CACHE_GET_MTMODE ? MT_NICE_FILTER : 0;
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
