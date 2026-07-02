#include "pch.h"

#include "FluentGlassBridge.h"

#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif

#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif

namespace qttabbar::fluent {
namespace {

constexpr const wchar_t* kCabinetClass = L"CabinetWClass";
constexpr const wchar_t* kExploreClass = L"ExploreWClass";
constexpr const wchar_t* kRebarClass = L"ReBarWindow32";
constexpr const wchar_t* kWinFormsPrefix = L"WindowsForms10.";

struct SubclassEntry {
    WNDPROC previousProc = nullptr;
    HWND explorerWindow = nullptr;
    HWND tabBarWindow = nullptr;
    HWND sideBarWindow = nullptr;
    HWND sideBarRebarWindow = nullptr;
    bool explorer = false;
    bool qtChild = false;
    FluentGlassSettings settings;
};

std::unordered_map<HWND, SubclassEntry> g_subclasses;
SRWLOCK g_subclassLock = SRWLOCK_INIT;

bool GetClassNameString(HWND hwnd, std::wstring& out) {
    wchar_t buffer[256]{};
    int length = ::GetClassNameW(hwnd, buffer, static_cast<int>(_countof(buffer)));
    if (length <= 0) {
        return false;
    }

    out.assign(buffer, length);
    return true;
}

bool StartsWith(const std::wstring& value, const wchar_t* prefix) {
    size_t prefixLength = wcslen(prefix);
    return value.size() >= prefixLength && wcsncmp(value.c_str(), prefix, prefixLength) == 0;
}

bool IsExplorerTopLevel(HWND hwnd) {
    std::wstring className;
    if (!GetClassNameString(hwnd, className)) {
        return false;
    }

    return _wcsicmp(className.c_str(), kCabinetClass) == 0 ||
           _wcsicmp(className.c_str(), kExploreClass) == 0;
}

bool IsQtTabBarChildWindow(HWND hwnd) {
    if (!::IsWindow(hwnd) || !::IsWindowVisible(hwnd)) {
        return false;
    }

    std::wstring className;
    if (!GetClassNameString(hwnd, className) || !StartsWith(className, kWinFormsPrefix)) {
        return false;
    }

    HWND parent = ::GetParent(hwnd);
    if (!parent) {
        return false;
    }

    std::wstring parentClass;
    return GetClassNameString(parent, parentClass) && _wcsicmp(parentClass.c_str(), kRebarClass) == 0;
}

int ClampExtendHeight(int height) {
    if (height < 0) {
        return 0;
    }
    if (height > 600) {
        return 600;
    }
    return height;
}

int GetWindowBottomInClient(HWND topLevel, HWND child) {
    if (!::IsWindow(topLevel) || !::IsWindow(child) || !::IsWindowVisible(child)) {
        return 0;
    }

    RECT rect{};
    if (!::GetWindowRect(child, &rect)) {
        return 0;
    }

    POINT points[2] = {{rect.left, rect.top}, {rect.right, rect.bottom}};
    ::MapWindowPoints(nullptr, topLevel, points, 2);
    return points[1].y > points[0].y ? points[1].y : 0;
}

struct FindQtContext {
    HWND topLevel = nullptr;
    HWND bestWindow = nullptr;
    RECT bestRect{};
    int index = 0;
    int bestIndex = 0;
};

BOOL CALLBACK EnumChildProcFindQt(HWND hwndChild, LPARAM lParam) {
    auto* context = reinterpret_cast<FindQtContext*>(lParam);
    if (!IsQtTabBarChildWindow(hwndChild)) {
        context->index++;
        return TRUE;
    }

    RECT rect{};
    if (!::GetWindowRect(hwndChild, &rect)) {
        context->index++;
        return TRUE;
    }

    POINT points[2] = {{rect.left, rect.top}, {rect.right, rect.bottom}};
    ::MapWindowPoints(nullptr, context->topLevel, points, 2);
    RECT rectInTop{points[0].x, points[0].y, points[1].x, points[1].y};

    if ((rectInTop.right - rectInTop.left) > 0 && (rectInTop.bottom - rectInTop.top) > 0) {
        if (!context->bestWindow ||
            rectInTop.top < context->bestRect.top ||
            (rectInTop.top == context->bestRect.top && context->index < context->bestIndex)) {
            context->bestWindow = hwndChild;
            context->bestRect = rectInTop;
            context->bestIndex = context->index;
        }
    }

    context->index++;
    return TRUE;
}

int GetQtTabBarStripHeight(HWND explorerWindow, HWND tabBarWindow) {
    FindQtContext context{};
    context.topLevel = explorerWindow;
    ::EnumChildWindows(explorerWindow, EnumChildProcFindQt, reinterpret_cast<LPARAM>(&context));
    if (context.bestWindow) {
        return context.bestRect.bottom;
    }

    return GetWindowBottomInClient(explorerWindow, tabBarWindow);
}

BOOL CALLBACK EnumChildProcFindAddressBar(HWND hwndChild, LPARAM lParam) {
    auto* bottom = reinterpret_cast<int*>(lParam);
    if (!::IsWindowVisible(hwndChild)) {
        return TRUE;
    }

    std::wstring className;
    if (!GetClassNameString(hwndChild, className)) {
        return TRUE;
    }

    bool candidate =
        _wcsicmp(className.c_str(), L"Breadcrumb Parent") == 0 ||
        _wcsicmp(className.c_str(), L"Address Band Root") == 0;
    if (!candidate) {
        return TRUE;
    }

    HWND topLevel = ::GetAncestor(hwndChild, GA_ROOT);
    int childBottom = GetWindowBottomInClient(topLevel, hwndChild);
    if (childBottom > *bottom) {
        *bottom = childBottom;
    }

    return TRUE;
}

int TryGetAddressBarBottom(HWND explorerWindow) {
    int bottom = 0;
    ::EnumChildWindows(explorerWindow, EnumChildProcFindAddressBar, reinterpret_cast<LPARAM>(&bottom));
    return bottom;
}

DWORD GetAccentColorArgb() {
    DWORD color = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(::DwmGetColorizationColor(&color, &opaque))) {
        return color;
    }

    return 0xFF404040;
}

void ApplyDwmColors(HWND explorerWindow, const FluentGlassSettings& settings) {
    HWND foreground = ::GetForegroundWindow();
    bool active = foreground == explorerWindow || ::IsChild(explorerWindow, foreground);
    DWORD color = active ? GetAccentColorArgb() : settings.inactiveColorArgb;

    if (settings.applyCaptionColor) {
        ::DwmSetWindowAttribute(explorerWindow, DWMWA_CAPTION_COLOR, &color, sizeof(color));
    }
    if (settings.applyBorderColor) {
        ::DwmSetWindowAttribute(explorerWindow, DWMWA_BORDER_COLOR, &color, sizeof(color));
    }
}

void ApplySideBarMargins(HWND explorerWindow,
                         HWND sideBarWindow,
                         HWND sideBarRebarWindow,
                         MARGINS& margins) {
    HWND geometryWindow = ::IsWindow(sideBarRebarWindow) && ::IsWindowVisible(sideBarRebarWindow)
        ? sideBarRebarWindow
        : sideBarWindow;
    if (!::IsWindow(geometryWindow) || !::IsWindowVisible(geometryWindow) ||
        ::GetAncestor(geometryWindow, GA_ROOT) != explorerWindow) {
        return;
    }

    RECT sideRect{};
    RECT clientRect{};
    if (!::GetWindowRect(geometryWindow, &sideRect) ||
        !::GetClientRect(explorerWindow, &clientRect)) {
        return;
    }

    POINT points[2] = {{sideRect.left, sideRect.top}, {sideRect.right, sideRect.bottom}};
    ::MapWindowPoints(nullptr, explorerWindow, points, 2);
    int width = points[1].x - points[0].x;
    int height = points[1].y - points[0].y;
    int clientWidth = clientRect.right - clientRect.left;
    if (width <= 0 || height <= width * 2 || width >= clientWidth / 2) {
        return;
    }

    if (points[0].x + width / 2 < clientWidth / 2) {
        margins.cxLeftWidth = std::clamp(static_cast<int>(points[1].x), 0, 400);
    }
    else {
        margins.cxRightWidth = std::clamp(
            clientWidth - static_cast<int>(points[0].x), 0, 400);
    }
}

HRESULT ApplyGlassAndColor(HWND explorerWindow,
                           HWND tabBarWindow,
                           HWND sideBarWindow,
                           HWND sideBarRebarWindow,
                           const FluentGlassSettings& settings) {
    if (!::IsWindow(explorerWindow) || !IsExplorerTopLevel(explorerWindow)) {
        return E_INVALIDARG;
    }

    BOOL compositionEnabled = FALSE;
    HRESULT hr = ::DwmIsCompositionEnabled(&compositionEnabled);
    if (FAILED(hr) || !compositionEnabled) {
        return FAILED(hr) ? hr : S_FALSE;
    }

    int qtStrip = GetQtTabBarStripHeight(explorerWindow, tabBarWindow);
    if (qtStrip <= 0) {
        return S_FALSE;
    }

    int extendTo = qtStrip;
    if (settings.addressMode == 1) {
        int addressBottom = TryGetAddressBarBottom(explorerWindow);
        UINT dpi = ::GetDpiForWindow(explorerWindow);
        int maxAddressGap = ::MulDiv(48, dpi ? static_cast<int>(dpi) : 96, 96);
        int maxAddressBottom = ::MulDiv(240, dpi ? static_cast<int>(dpi) : 96, 96);
        if (addressBottom > extendTo &&
            addressBottom <= extendTo + maxAddressGap &&
            addressBottom < maxAddressBottom) {
            extendTo = addressBottom;
        }
    }
    else if (settings.addressMode == 2) {
        extendTo = qtStrip + std::clamp(settings.addressExtraPixels, 0, 400);
    }

    MARGINS margins{};
    margins.cyTopHeight = ClampExtendHeight(extendTo);
    ApplySideBarMargins(explorerWindow, sideBarWindow, sideBarRebarWindow, margins);
    hr = ::DwmExtendFrameIntoClientArea(explorerWindow, &margins);
    if (SUCCEEDED(hr) && margins.cyTopHeight > 0) {
        ApplyDwmColors(explorerWindow, settings);
    }

    return hr;
}

void StoreSubclass(HWND hwnd, const SubclassEntry& entry) {
    ::AcquireSRWLockExclusive(&g_subclassLock);
    g_subclasses[hwnd] = entry;
    ::ReleaseSRWLockExclusive(&g_subclassLock);
}

bool TryGetSubclass(HWND hwnd, SubclassEntry& entry) {
    ::AcquireSRWLockShared(&g_subclassLock);
    auto it = g_subclasses.find(hwnd);
    bool found = it != g_subclasses.end();
    if (found) {
        entry = it->second;
    }
    ::ReleaseSRWLockShared(&g_subclassLock);
    return found;
}

bool RemoveSubclass(HWND hwnd, SubclassEntry& entry) {
    ::AcquireSRWLockExclusive(&g_subclassLock);
    auto it = g_subclasses.find(hwnd);
    bool found = it != g_subclasses.end();
    if (found) {
        entry = it->second;
        g_subclasses.erase(it);
    }
    ::ReleaseSRWLockExclusive(&g_subclassLock);
    return found;
}

HRESULT EnsureSubclass(HWND hwnd, const SubclassEntry& requested);
void SubclassQtChildren(HWND explorerWindow,
                        HWND tabBarWindow,
                        HWND sideBarWindow,
                        HWND sideBarRebarWindow,
                        const FluentGlassSettings& settings);

LRESULT CALLBACK FluentGlassWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    SubclassEntry entry{};
    if (!TryGetSubclass(hwnd, entry)) {
        return ::DefWindowProcW(hwnd, message, wParam, lParam);
    }

    if (entry.qtChild) {
        if (message == WM_ERASEBKGND && entry.settings.suppressQtChildErase) {
            return 1;
        }

        if (message == WM_NCDESTROY) {
            RemoveSubclass(hwnd, entry);
            ::SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(entry.previousProc));
            return ::CallWindowProcW(entry.previousProc, hwnd, message, wParam, lParam);
        }
    }

    if (entry.explorer) {
        switch (message) {
        case WM_NCACTIVATE:
        case WM_ACTIVATE:
        case WM_ACTIVATEAPP:
        case WM_SIZE:
        case WM_WINDOWPOSCHANGED:
        case WM_DPICHANGED:
        case WM_THEMECHANGED:
        case WM_SETTINGCHANGE:
        case WM_DWMCOMPOSITIONCHANGED:
            ApplyGlassAndColor(
                entry.explorerWindow,
                entry.tabBarWindow,
                entry.sideBarWindow,
                entry.sideBarRebarWindow,
                entry.settings);
            SubclassQtChildren(
                entry.explorerWindow,
                entry.tabBarWindow,
                entry.sideBarWindow,
                entry.sideBarRebarWindow,
                entry.settings);
            break;

        case WM_NCDESTROY:
            RemoveSubclass(hwnd, entry);
            ::SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(entry.previousProc));
            return ::CallWindowProcW(entry.previousProc, hwnd, message, wParam, lParam);
        }
    }

    return ::CallWindowProcW(entry.previousProc, hwnd, message, wParam, lParam);
}

HRESULT EnsureSubclass(HWND hwnd, const SubclassEntry& requested) {
    if (!::IsWindow(hwnd)) {
        return E_INVALIDARG;
    }

    SubclassEntry current{};
    if (TryGetSubclass(hwnd, current)) {
        current.explorerWindow = requested.explorerWindow;
        current.tabBarWindow = requested.tabBarWindow;
        current.sideBarWindow = requested.sideBarWindow;
        current.sideBarRebarWindow = requested.sideBarRebarWindow;
        current.settings = requested.settings;
        current.explorer = current.explorer || requested.explorer;
        current.qtChild = current.qtChild || requested.qtChild;
        StoreSubclass(hwnd, current);
        return S_OK;
    }

    WNDPROC previous = reinterpret_cast<WNDPROC>(
        ::SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(FluentGlassWndProc)));
    if (!previous) {
        return HRESULT_FROM_WIN32(::GetLastError());
    }

    SubclassEntry entry = requested;
    entry.previousProc = previous;
    StoreSubclass(hwnd, entry);
    return S_OK;
}

struct SubclassQtContext {
    HWND explorerWindow = nullptr;
    HWND tabBarWindow = nullptr;
    HWND sideBarWindow = nullptr;
    HWND sideBarRebarWindow = nullptr;
    FluentGlassSettings settings;
};

BOOL CALLBACK EnumChildProcSubclassQt(HWND hwndChild, LPARAM lParam) {
    auto* context = reinterpret_cast<SubclassQtContext*>(lParam);
    if (!IsQtTabBarChildWindow(hwndChild)) {
        return TRUE;
    }

    SubclassEntry tabEntry{};
    tabEntry.explorerWindow = context->explorerWindow;
    tabEntry.tabBarWindow = context->tabBarWindow;
    tabEntry.sideBarWindow = context->sideBarWindow;
    tabEntry.sideBarRebarWindow = context->sideBarRebarWindow;
    tabEntry.qtChild = true;
    tabEntry.settings = context->settings;
    EnsureSubclass(hwndChild, tabEntry);
    return TRUE;
}

void SubclassQtChildren(HWND explorerWindow,
                        HWND tabBarWindow,
                        HWND sideBarWindow,
                        HWND sideBarRebarWindow,
                        const FluentGlassSettings& settings) {
    SubclassQtContext context{};
    context.explorerWindow = explorerWindow;
    context.tabBarWindow = tabBarWindow;
    context.sideBarWindow = sideBarWindow;
    context.sideBarRebarWindow = sideBarRebarWindow;
    context.settings = settings;
    ::EnumChildWindows(explorerWindow, EnumChildProcSubclassQt, reinterpret_cast<LPARAM>(&context));
}

void ResetDwm(HWND explorerWindow) {
    if (!::IsWindow(explorerWindow)) {
        return;
    }

    MARGINS margins{};
    ::DwmExtendFrameIntoClientArea(explorerWindow, &margins);

    DWORD defaultColor = 0xFFFFFFFF;
    ::DwmSetWindowAttribute(explorerWindow, DWMWA_CAPTION_COLOR, &defaultColor, sizeof(defaultColor));
    ::DwmSetWindowAttribute(explorerWindow, DWMWA_BORDER_COLOR, &defaultColor, sizeof(defaultColor));
}

void RestoreSubclass(HWND hwnd, const SubclassEntry& entry) {
    if (::IsWindow(hwnd)) {
        ::SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(entry.previousProc));
    }
}

}  // namespace

HRESULT ApplyFluentGlass(HWND explorerWindow,
                         HWND tabBarWindow,
                         HWND sideBarWindow,
                         HWND sideBarRebarWindow,
                         const FluentGlassSettings& settings) {
    HRESULT hr = ApplyGlassAndColor(
        explorerWindow, tabBarWindow, sideBarWindow, sideBarRebarWindow, settings);
    if (FAILED(hr)) {
        return hr;
    }

    SubclassEntry explorerEntry{};
    explorerEntry.explorerWindow = explorerWindow;
    explorerEntry.tabBarWindow = tabBarWindow;
    explorerEntry.sideBarWindow = sideBarWindow;
    explorerEntry.sideBarRebarWindow = sideBarRebarWindow;
    explorerEntry.explorer = true;
    explorerEntry.settings = settings;
    HRESULT subclassHr = EnsureSubclass(explorerWindow, explorerEntry);
    if (FAILED(subclassHr)) {
        return subclassHr;
    }

    if (::IsWindow(tabBarWindow)) {
        SubclassEntry tabEntry{};
        tabEntry.explorerWindow = explorerWindow;
        tabEntry.tabBarWindow = tabBarWindow;
        tabEntry.sideBarWindow = sideBarWindow;
        tabEntry.sideBarRebarWindow = sideBarRebarWindow;
        tabEntry.qtChild = true;
        tabEntry.settings = settings;
        EnsureSubclass(tabBarWindow, tabEntry);
    }

    if (::IsWindow(sideBarWindow)) {
        SubclassEntry sideEntry{};
        sideEntry.explorerWindow = explorerWindow;
        sideEntry.tabBarWindow = tabBarWindow;
        sideEntry.sideBarWindow = sideBarWindow;
        sideEntry.sideBarRebarWindow = sideBarRebarWindow;
        sideEntry.qtChild = true;
        sideEntry.settings = settings;
        EnsureSubclass(sideBarWindow, sideEntry);
    }

    if (::IsWindow(sideBarRebarWindow)) {
        SubclassEntry rebarEntry{};
        rebarEntry.explorerWindow = explorerWindow;
        rebarEntry.tabBarWindow = tabBarWindow;
        rebarEntry.sideBarWindow = sideBarWindow;
        rebarEntry.sideBarRebarWindow = sideBarRebarWindow;
        rebarEntry.qtChild = true;
        rebarEntry.settings = settings;
        EnsureSubclass(sideBarRebarWindow, rebarEntry);

        HWND baseBarWindow = ::GetParent(sideBarRebarWindow);
        if (::IsWindow(baseBarWindow) && baseBarWindow != explorerWindow) {
            EnsureSubclass(baseBarWindow, rebarEntry);
        }
    }

    SubclassQtChildren(
        explorerWindow, tabBarWindow, sideBarWindow, sideBarRebarWindow, settings);
    return hr;
}

HRESULT DisableFluentGlass(HWND explorerWindow) {
    std::vector<std::pair<HWND, SubclassEntry>> toRestore;
    ::AcquireSRWLockExclusive(&g_subclassLock);
    for (auto it = g_subclasses.begin(); it != g_subclasses.end();) {
        if (it->first == explorerWindow || it->second.explorerWindow == explorerWindow) {
            toRestore.emplace_back(it->first, it->second);
            it = g_subclasses.erase(it);
        }
        else {
            ++it;
        }
    }
    ::ReleaseSRWLockExclusive(&g_subclassLock);

    for (const auto& item : toRestore) {
        RestoreSubclass(item.first, item.second);
    }

    ResetDwm(explorerWindow);
    return S_OK;
}

void ShutdownFluentGlass() {
    std::vector<std::pair<HWND, SubclassEntry>> toRestore;
    ::AcquireSRWLockExclusive(&g_subclassLock);
    for (const auto& item : g_subclasses) {
        toRestore.emplace_back(item.first, item.second);
    }
    g_subclasses.clear();
    ::ReleaseSRWLockExclusive(&g_subclassLock);

    for (const auto& item : toRestore) {
        RestoreSubclass(item.first, item.second);
    }
}

}  // namespace qttabbar::fluent
