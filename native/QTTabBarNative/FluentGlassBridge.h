#pragma once

#include <windows.h>

namespace qttabbar::fluent {

struct FluentGlassSettings {
    int addressMode = 1;
    int addressExtraPixels = 40;
    DWORD inactiveColorArgb = 0xFF202020;
    BOOL applyCaptionColor = TRUE;
    BOOL applyBorderColor = TRUE;
    BOOL suppressQtChildErase = TRUE;
};

HRESULT ApplyFluentGlass(HWND explorerWindow,
                         HWND tabBarWindow,
                         HWND sideBarWindow,
                         HWND sideBarRebarWindow,
                         const FluentGlassSettings& settings);
HRESULT DisableFluentGlass(HWND explorerWindow);
void ShutdownFluentGlass();

}  // namespace qttabbar::fluent
