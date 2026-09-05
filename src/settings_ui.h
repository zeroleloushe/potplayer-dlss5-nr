#pragma once
#include <windows.h>

// Shared settings panel used by nr-settings.exe and the DirectShow property page.
HWND NrSettingsCreate(HWND parent, const RECT *rc, bool as_child);
SIZE NrSettingsIdealSize();
