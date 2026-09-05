// VapourSynth plugin: core.dlss5.NR(clip, ...)
// SVP RIFE path uses VapourSynth Filter — append this after SmoothFps.

#include "nr_bridge.h"

#include "VapourSynth4.h"
#include "VSHelper4.h"

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

struct FilterData {
	VSNode *node;
	VSVideoInfo vi;
	int style, preset, automask, gpu;
	float intensity, tone, structure, skin;
	std::string runtime;
	int last_n = -2;
};

void EnsureInit(int gpu, const std::string &runtime)
{
	std::call_once(g_init_once, [&]() {
		g_runtime = runtime.empty() ? DefaultRuntimeDir() : Widen(runtime.c_str());
		g_shim_dir = DirOfSelf();
		g_init_ok = nrbridge::init(gpu, g_runtime.c_str(), g_shim_dir.c_str());
	});
}

const VSFrame *VS_CC getFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx,
                              VSCore *core, const VSAPI *vsapi)
{
	(void)frameData;
	auto *d = static_cast<FilterData *>(instanceData);
	if (activationReason == arInitial) {
		vsapi->requestFrameFilter(n, d->node, frameCtx);
		return nullptr;
	}
	if (activationReason != arAllFramesReady)
		return nullptr;

	const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
	EnsureInit(d->gpu, d->runtime);
	if (!g_init_ok)
		return src;

	const int w = vsapi->getFrameWidth(src, 0);
	const int h = vsapi->getFrameHeight(src, 0);
	const VSVideoFormat *fmt = vsapi->getVideoFrameFormat(src);
	if (fmt->colorFamily != cfRGB || fmt->sampleType != stInteger || fmt->bitsPerSample != 8 || fmt->numPlanes != 3) {
		vsapi->setFilterError("dlss5.NR: clip must be RGB24 (planar 8-bit).", frameCtx);
		vsapi->freeFrame(src);
		return nullptr;
	}

	std::vector<uint8_t> bgra((size_t)w * h * 4), out((size_t)w * h * 4);
	const uint8_t *r = vsapi->getReadPtr(src, 0);
	const uint8_t *g = vsapi->getReadPtr(src, 1);
	const uint8_t *b = vsapi->getReadPtr(src, 2);
	const int rp = vsapi->getStride(src, 0);
	const int gp = vsapi->getStride(src, 1);
	const int bp = vsapi->getStride(src, 2);
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			uint8_t *p = bgra.data() + ((size_t)y * w + x) * 4;
			p[0] = b[y * bp + x];
			p[1] = g[y * gp + x];
			p[2] = r[y * rp + x];
			p[3] = 255;
		}
	}

	NrBridgeParams params{};
	params.style = d->style;
	params.preset = d->preset;
	params.intensity = d->intensity;
	params.tone = d->tone;
	params.structure = d->structure;
	params.skin = d->skin;
	params.automask = d->automask;
	params.reset = (d->last_n >= 0 && n != d->last_n + 1) ? 1 : 0;
	d->last_n = n;

	const bool ok = nrbridge::process(bgra.data(), w * 4, out.data(), w * 4, w, h, params);
	if (!ok) {
		return src; // pass-through
	}

	VSFrame *dst = vsapi->newVideoFrame(fmt, w, h, src, core);
	uint8_t *wr = vsapi->getWritePtr(dst, 0);
	uint8_t *wg = vsapi->getWritePtr(dst, 1);
	uint8_t *wb = vsapi->getWritePtr(dst, 2);
	const int wrp = vsapi->getStride(dst, 0);
	const int wgp = vsapi->getStride(dst, 1);
	const int wbp = vsapi->getStride(dst, 2);
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			const uint8_t *p = out.data() + ((size_t)y * w + x) * 4;
			wb[y * wbp + x] = p[0];
			wg[y * wgp + x] = p[1];
			wr[y * wrp + x] = p[2];
		}
	}
	vsapi->freeFrame(src);
	return dst;
}

void VS_CC freeFilter(void *instanceData, VSCore *core, const VSAPI *vsapi)
{
	(void)core;
	auto *d = static_cast<FilterData *>(instanceData);
	vsapi->freeNode(d->node);
	delete d;
}

void VS_CC createNR(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi)
{
	(void)userData;
	auto *d = new FilterData();
	int err = 0;
	d->node = vsapi->mapGetNode(in, "clip", 0, &err);
	d->vi = *vsapi->getVideoInfo(d->node);
	d->style = (int)vsapi->mapGetInt(in, "style", 0, &err);
	if (err)
		d->style = 1;
	d->preset = (int)vsapi->mapGetInt(in, "preset", 0, &err);
	if (err)
		d->preset = 0;
	d->intensity = (float)vsapi->mapGetFloat(in, "intensity", 0, &err);
	if (err)
		d->intensity = 1.0f;
	d->tone = (float)vsapi->mapGetFloat(in, "tone", 0, &err);
	if (err)
		d->tone = 1.0f;
	d->structure = (float)vsapi->mapGetFloat(in, "structure", 0, &err);
	if (err)
		d->structure = 1.5f;
	d->skin = (float)vsapi->mapGetFloat(in, "skin", 0, &err);
	if (err)
		d->skin = 2.0f;
	d->automask = vsapi->mapGetInt(in, "automask", 0, &err) ? 1 : 0;
	if (err)
		d->automask = 1;
	d->gpu = (int)vsapi->mapGetInt(in, "gpu", 0, &err);
	if (err)
		d->gpu = 0;
	const char *rt = vsapi->mapGetData(in, "runtime", 0, &err);
	d->runtime = (!err && rt) ? rt : "";

	if (!vsh::isConstantVideoFormat(&d->vi) || d->vi.format.colorFamily != cfRGB || d->vi.format.bitsPerSample != 8) {
		vsapi->mapSetError(out, "dlss5.NR: clip must be constant format RGB24.");
		vsapi->freeNode(d->node);
		delete d;
		return;
	}

	VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
	vsapi->createVideoFilter(out, "NR", &d->vi, getFrame, freeFilter, fmParallelRequests, deps, 1, d, core);
}

} // namespace

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi)
{
	vspapi->configPlugin("dev.potplayer.dlss5", "dlss5", "DLSS 5 Neural Rendering", VS_MAKE_VERSION(1, 0),
	                     VAPOURSYNTH_API_VERSION, 0, plugin);
	vspapi->registerFunction("NR",
	                         "clip:vnode;style:int:opt;preset:int:opt;intensity:float:opt;tone:float:opt;structure:float:"
	                         "opt;skin:float:opt;automask:int:opt;gpu:int:opt;runtime:data:opt;",
	                         "clip:vnode;", createNR, nullptr, plugin);
}
