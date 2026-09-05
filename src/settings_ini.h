#pragma once

#include "nr_bridge.h"

#include <windows.h>
#include <cstdlib>
#include <cstdio>
#include <string>

struct NrUiSettings {
	int style = 1;
	int preset = 0;
	float intensity = 1.0f;
	float tone = 1.0f;
	float structure = 1.5f;
	float skin = 2.0f;
	int automask = 1;
	bool loaded = false;
};

inline float NrClamp(float v, float lo, float hi)
{
	if (v < lo)
		return lo;
	if (v > hi)
		return hi;
	return v;
}

inline std::wstring NrSettingsDir()
{
	wchar_t local[MAX_PATH]{};
	if (!GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH))
		return L".";
	std::wstring d = std::wstring(local) + L"\\potplayer-dlss5-nr";
	CreateDirectoryW(d.c_str(), nullptr);
	return d;
}

inline std::wstring NrSettingsIniPath()
{
	return NrSettingsDir() + L"\\settings.ini";
}

inline NrUiSettings NrSettingsLoad()
{
	NrUiSettings s;
	const std::wstring path = NrSettingsIniPath();
	if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
		return s;
	s.loaded = true;
	s.style = GetPrivateProfileIntW(L"dlss5nr", L"style", 1, path.c_str());
	s.preset = GetPrivateProfileIntW(L"dlss5nr", L"preset", 0, path.c_str());
	s.automask = GetPrivateProfileIntW(L"dlss5nr", L"automask", 1, path.c_str()) ? 1 : 0;
	if (s.style < 0 || s.style > 2)
		s.style = 1;
	if (s.preset < 0 || s.preset > 3)
		s.preset = 0;
	wchar_t buf[64]{};
	auto rf = [&](const wchar_t *key, float def) {
		GetPrivateProfileStringW(L"dlss5nr", key, L"", buf, 64, path.c_str());
		if (!buf[0])
			return def;
		return NrClamp((float)_wtof(buf), 0.f, 2.f);
	};
	s.intensity = rf(L"intensity", 1.0f);
	s.tone = rf(L"tone", 1.0f);
	s.structure = rf(L"structure", 1.5f);
	s.skin = rf(L"skin", 2.0f);
	return s;
}

inline void NrSettingsSave(const NrUiSettings &s)
{
	const std::wstring path = NrSettingsIniPath();
	wchar_t b[32];
	auto wi = [&](const wchar_t *k, int v) {
		swprintf(b, 32, L"%d", v);
		WritePrivateProfileStringW(L"dlss5nr", k, b, path.c_str());
	};
	auto wf = [&](const wchar_t *k, float v) {
		swprintf(b, 32, L"%.2f", v);
		WritePrivateProfileStringW(L"dlss5nr", k, b, path.c_str());
	};
	wi(L"style", s.style);
	wi(L"preset", s.preset);
	wi(L"automask", s.automask ? 1 : 0);
	wf(L"intensity", s.intensity);
	wf(L"tone", s.tone);
	wf(L"structure", s.structure);
	wf(L"skin", s.skin);
}

inline void NrSettingsApply(NrBridgeParams &p, int style, int preset, float intensity, float tone, float structure,
                            float skin, int automask)
{
	static FILETIME last{};
	static bool have = false;
	static NrUiSettings cached{};
	const std::wstring path = NrSettingsIniPath();
	WIN32_FILE_ATTRIBUTE_DATA fad{};
	if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
		have = false;
		p.style = style;
		p.preset = preset;
		p.intensity = intensity;
		p.tone = tone;
		p.structure = structure;
		p.skin = skin;
		p.automask = automask;
		return;
	}
	if (!have || CompareFileTime(&fad.ftLastWriteTime, &last) != 0) {
		last = fad.ftLastWriteTime;
		cached = NrSettingsLoad();
		have = cached.loaded;
	}
	if (!have) {
		p.style = style;
		p.preset = preset;
		p.intensity = intensity;
		p.tone = tone;
		p.structure = structure;
		p.skin = skin;
		p.automask = automask;
		return;
	}
	p.style = cached.style;
	p.preset = cached.preset;
	p.intensity = cached.intensity;
	p.tone = cached.tone;
	p.structure = cached.structure;
	p.skin = cached.skin;
	p.automask = cached.automask;
}
