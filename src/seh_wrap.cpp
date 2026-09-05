// SEH trampolines. MSVC: the function that contains __try must not have
// C++ objects with destructors. Callers live in avs/vs plugins.

#include "nr_bridge.h"

#include <stdio.h>
#include <windows.h>

static char g_seh_msg[128];

const char *nrbridge_seh_message()
{
	return g_seh_msg;
}

bool nrbridge_seh_init(int gpu_index, const wchar_t *runtime_dir, const wchar_t *shim_dir)
{
	g_seh_msg[0] = 0;
	__try {
		return nrbridge::init(gpu_index, runtime_dir, shim_dir);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		_snprintf(g_seh_msg, sizeof(g_seh_msg), "AV 0x%08X during NR init (NGX/D3D12/LoadLibrary)", GetExceptionCode());
		return false;
	}
}

bool nrbridge_seh_process(const uint8_t *src_bgra, int src_row_pitch, uint8_t *dst_bgra, int dst_row_pitch, int width,
                         int height, const NrBridgeParams &params)
{
	g_seh_msg[0] = 0;
	__try {
		return nrbridge::process(src_bgra, src_row_pitch, dst_bgra, dst_row_pitch, width, height, params);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		_snprintf(g_seh_msg, sizeof(g_seh_msg), "AV 0x%08X during NR process", GetExceptionCode());
		return false;
	}
}
