// potplayer-dlss5-nr — DLSS 5 Neural Rendering for PotPlayer + SVP
// Bridge to NVIDIA DLSS NR (NGX feature 18) on D3D12.
// CPU-staged BGRA8 in / BGRA8 out. Runtime DLL is user-supplied.
//
// Architecture follows the public contract of obs-dlss5-nr (GPL-2.0).

#pragma once

#include <cstdint>

struct NrBridgeParams {
	int style;        // 0 default, 1 natural, 2 cinematic
	int preset;       // 0–3
	float intensity;  // 0..2, 1 = default
	float tone;       // local tone strength
	float structure;  // local structure strength
	float skin;       // skin structure, meaningful with automask
	int automask;     // 0/1
	int ui_correction;
	int reset;        // 1 = drop temporal history (seek / scene cut)
};

namespace nrbridge {

// gpu_index: index among NVIDIA adapters (VendorId 0x10DE).
// runtime_dir: folder with user-supplied nvngx_dlssnr.dll (UTF-16).
// shim_dir: folder with nvngx.dll_pot.dll (UTF-16); may be null (next to this DLL).
bool init(int gpu_index, const wchar_t *runtime_dir, const wchar_t *shim_dir);

// src/dst: 8-bit BGRA, pitches in bytes. Fail-open: on error dst is not written
// and the function returns false — caller should pass the source through.
bool process(const uint8_t *src_bgra, int src_row_pitch, uint8_t *dst_bgra, int dst_row_pitch,
             int width, int height, const NrBridgeParams &params);

void shutdown();
bool ready();
const char *gpu_name();
const char *last_error();
const char *status_text();

} // namespace nrbridge

// SEH wrappers (seh_wrap.cpp) — never let NVIDIA/D3D12 AV kill AviSynth.
bool nrbridge_seh_init(int gpu_index, const wchar_t *runtime_dir, const wchar_t *shim_dir);
bool nrbridge_seh_process(const uint8_t *src_bgra, int src_row_pitch, uint8_t *dst_bgra, int dst_row_pitch, int width,
                         int height, const NrBridgeParams &params);
const char *nrbridge_seh_message();
