#include "pch.h"
#include "TaskbarTabPreviewManager.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <strsafe.h>

#include <algorithm>

#pragma comment(lib, "Dwmapi.lib")

namespace {
constexpr wchar_t kProxyWindowClass[] = L"QTTabBarNative_TaskbarTabProxy";
constexpr int kDefaultPreviewWidth = 420;
constexpr int kDefaultPreviewHeight = 236;
constexpr COLORREF kPreviewBackground = RGB(245, 248, 250);
constexpr COLORREF kPreviewBorder = RGB(180, 190, 200);
constexpr COLORREF kTitleText = RGB(25, 25, 25);
constexpr COLORREF kPathText = RGB(80, 86, 92);

void EnableIconicPreview(HWND hwnd) {
    BOOL enabled = TRUE;
    ::DwmSetWindowAttribute(hwnd, DWMWA_FORCE_ICONIC_REPRESENTATION, &enabled, sizeof(enabled));
    ::DwmSetWindowAttribute(hwnd, DWMWA_HAS_ICONIC_BITMAP, &enabled, sizeof(enabled));
}

std::wstring EllipsizeText(HDC hdc, const std::wstring& text, int maxWidth) {
    if(text.empty() || maxWidth <= 0) {
        return {};
    }
    SIZE size{};
    if(::GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &size) && size.cx <= maxWidth) {
        return text;
    }

    std::wstring result = text;
    constexpr wchar_t ellipsis[] = L"...";
    while(result.size() > 1) {
        result.pop_back();
        std::wstring candidate = result + ellipsis;
        if(::GetTextExtentPoint32W(hdc, candidate.c_str(), static_cast<int>(candidate.size()), &size) &&
           size.cx <= maxWidth) {
            return candidate;
        }
    }
    return ellipsis;
}

HFONT CreatePreviewFont(int pointSize, int weight) {
    LOGFONTW lf{};
    HDC screen = ::GetDC(nullptr);
    int dpiY = screen ? ::GetDeviceCaps(screen, LOGPIXELSY) : 96;
    if(screen) {
        ::ReleaseDC(nullptr, screen);
    }
    lf.lfHeight = -MulDiv(pointSize, dpiY, 72);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    ::StringCchCopyW(lf.lfFaceName, ARRAYSIZE(lf.lfFaceName), L"Segoe UI");
    return ::CreateFontIndirectW(&lf);
}

} // namespace

TaskbarTabPreviewManager::TaskbarTabPreviewManager() noexcept = default;

TaskbarTabPreviewManager::~TaskbarTabPreviewManager() {
    Clear();
}

void TaskbarTabPreviewManager::SetOwner(HWND ownerHwnd,
                                        std::function<void(std::size_t)> activateCallback,
                                        std::function<void(std::size_t)> closeCallback) {
    HWND normalizedOwner = nullptr;
    if(ownerHwnd != nullptr) {
        normalizedOwner = ::GetAncestor(ownerHwnd, GA_ROOT);
        if(normalizedOwner == nullptr) {
            normalizedOwner = ownerHwnd;
        }
    }
    if(m_ownerHwnd != normalizedOwner) {
        Clear();
        m_ownerHwnd = normalizedOwner;
    }
    m_activateCallback = std::move(activateCallback);
    m_closeCallback = std::move(closeCallback);
}

void TaskbarTabPreviewManager::SetEnabled(bool enabled) {
    if(m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    if(!m_enabled) {
        Clear();
    }
}

void TaskbarTabPreviewManager::Update(const std::vector<Entry>& entries) {
    HWND owner = ResolveOwnerWindow();
    if(!m_enabled || owner == nullptr || entries.empty()) {
        Clear();
        return;
    }
    if(FAILED(EnsureTaskbar())) {
        Clear();
        return;
    }
    if(!EnsureProxyCount(entries.size())) {
        Clear();
        return;
    }

    for(std::size_t i = 0; i < entries.size(); ++i) {
        UpdateProxy(*m_proxies[i], entries[i], i);
    }
    UpdateTaskbarOrderAndActive();
}

void TaskbarTabPreviewManager::Clear() {
    for(auto& proxy : m_proxies) {
        DestroyProxy(proxy);
    }
    m_proxies.clear();
    m_taskbarReady = false;
}

HRESULT TaskbarTabPreviewManager::EnsureTaskbar() {
    if(m_taskbarReady && m_taskbar) {
        return S_OK;
    }
    m_taskbar.Release();
    HRESULT hr = ::CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_taskbar));
    if(FAILED(hr) || !m_taskbar) {
        return FAILED(hr) ? hr : E_FAIL;
    }
    hr = m_taskbar->HrInit();
    if(SUCCEEDED(hr)) {
        m_taskbarReady = true;
    }
    return hr;
}

HWND TaskbarTabPreviewManager::ResolveOwnerWindow() const {
    if(m_ownerHwnd == nullptr) {
        return nullptr;
    }
    HWND root = ::GetAncestor(m_ownerHwnd, GA_ROOT);
    return root != nullptr ? root : m_ownerHwnd;
}

bool TaskbarTabPreviewManager::EnsureProxyCount(std::size_t count) {
    while(m_proxies.size() > count) {
        DestroyProxy(m_proxies.back());
        m_proxies.pop_back();
    }
    while(m_proxies.size() < count) {
        if(!CreateProxy(m_proxies.size())) {
            return false;
        }
    }
    return true;
}

bool TaskbarTabPreviewManager::CreateProxy(std::size_t index) {
    if(RegisterProxyClass() == 0) {
        return false;
    }

    auto proxy = std::make_unique<ProxyWindow>();
    proxy->index = index;
    proxy->owner = this;

    HWND hwnd = ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                  kProxyWindowClass,
                                  L"QTTabBar",
                                  WS_POPUP,
                                  CW_USEDEFAULT,
                                  CW_USEDEFAULT,
                                  1,
                                  1,
                                  nullptr,
                                  nullptr,
                                  _AtlBaseModule.GetModuleInstance(),
                                  proxy.get());
    if(hwnd == nullptr) {
        return false;
    }

    proxy->hwnd = hwnd;
    EnableIconicPreview(hwnd);
    RegisterProxy(*proxy, m_proxies.empty() ? nullptr : m_proxies.back()->hwnd);
    m_proxies.push_back(std::move(proxy));
    return true;
}

void TaskbarTabPreviewManager::DestroyProxy(std::unique_ptr<ProxyWindow>& proxy) {
    if(!proxy) {
        return;
    }
    if(m_taskbar && proxy->hwnd != nullptr) {
        m_taskbar->UnregisterTab(proxy->hwnd);
    }
    if(proxy->icon != nullptr) {
        ::DestroyIcon(proxy->icon);
        proxy->icon = nullptr;
    }
    if(proxy->hwnd != nullptr) {
        ::SetWindowLongPtrW(proxy->hwnd, GWLP_USERDATA, 0);
        ::DestroyWindow(proxy->hwnd);
        proxy->hwnd = nullptr;
    }
}

void TaskbarTabPreviewManager::RegisterProxy(ProxyWindow& proxy, HWND previous) {
    HWND owner = ResolveOwnerWindow();
    if(!m_taskbar || owner == nullptr || proxy.hwnd == nullptr) {
        return;
    }
    if(SUCCEEDED(m_taskbar->RegisterTab(proxy.hwnd, owner))) {
        m_taskbar->SetTabOrder(proxy.hwnd, previous);
    }
}

void TaskbarTabPreviewManager::UpdateProxy(ProxyWindow& proxy, const Entry& entry, std::size_t index) {
    proxy.index = index;
    proxy.active = entry.active;
    proxy.title = entry.title.empty() ? entry.path : entry.title;
    proxy.path = entry.path;
    ::SetWindowTextW(proxy.hwnd, proxy.title.c_str());
    SetProxyIcon(proxy, entry.icon);
    if(m_taskbar) {
        m_taskbar->SetThumbnailTooltip(proxy.hwnd, proxy.title.c_str());
    }
    ::DwmInvalidateIconicBitmaps(proxy.hwnd);
}

void TaskbarTabPreviewManager::UpdateTaskbarOrderAndActive() {
    if(!m_taskbar) {
        return;
    }
    HWND previous = nullptr;
    for(auto& proxy : m_proxies) {
        if(proxy && proxy->hwnd != nullptr) {
            m_taskbar->SetTabOrder(proxy->hwnd, previous);
            previous = proxy->hwnd;
        }
    }
    for(auto& proxy : m_proxies) {
        if(proxy && proxy->hwnd != nullptr && proxy->active) {
            m_taskbar->SetTabActive(proxy->hwnd, ResolveOwnerWindow(), 0);
            return;
        }
    }
    if(!m_proxies.empty() && m_proxies.front() && m_proxies.front()->hwnd != nullptr) {
        m_taskbar->SetTabActive(m_proxies.front()->hwnd, ResolveOwnerWindow(), 0);
    }
}

void TaskbarTabPreviewManager::ActivateProxy(std::size_t index) {
    if(m_activateCallback) {
        m_activateCallback(index);
    }
    HWND owner = ResolveOwnerWindow();
    if(owner != nullptr) {
        ::ShowWindow(owner, SW_SHOWNORMAL);
        ::SetForegroundWindow(owner);
    }
    if(index < m_proxies.size() && m_taskbar && m_proxies[index]) {
        m_taskbar->SetTabActive(m_proxies[index]->hwnd, owner, 0);
    }
}

void TaskbarTabPreviewManager::CloseProxy(std::size_t index) {
    if(m_closeCallback) {
        m_closeCallback(index);
    }
}

HBITMAP TaskbarTabPreviewManager::CreatePreviewBitmap(const ProxyWindow& proxy, int width, int height) const {
    if(width <= 0) {
        width = kDefaultPreviewWidth;
    }
    if(height <= 0) {
        height = kDefaultPreviewHeight;
    }

    HDC screen = ::GetDC(nullptr);
    HDC mem = screen ? ::CreateCompatibleDC(screen) : nullptr;
    if(mem == nullptr) {
        if(screen) {
            ::ReleaseDC(nullptr, screen);
        }
        return nullptr;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = ::CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if(screen) {
        ::ReleaseDC(nullptr, screen);
    }
    if(bitmap == nullptr) {
        ::DeleteDC(mem);
        return nullptr;
    }

    HGDIOBJ oldBitmap = ::SelectObject(mem, bitmap);
    RECT rc{0, 0, width, height};
    HBRUSH background = ::CreateSolidBrush(kPreviewBackground);
    ::FillRect(mem, &rc, background);
    ::DeleteObject(background);

    HPEN border = ::CreatePen(PS_SOLID, 1, kPreviewBorder);
    HGDIOBJ oldPen = ::SelectObject(mem, border);
    HGDIOBJ oldBrush = ::SelectObject(mem, ::GetStockObject(HOLLOW_BRUSH));
    ::Rectangle(mem, 0, 0, width, height);
    ::SelectObject(mem, oldBrush);
    ::SelectObject(mem, oldPen);
    ::DeleteObject(border);

    int margin = 24;
    int iconSize = (std::min)(48, (std::max)(24, height / 5));
    if(proxy.icon != nullptr) {
        ::DrawIconEx(mem, margin, margin, proxy.icon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
    }

    HFONT titleFont = CreatePreviewFont(18, FW_SEMIBOLD);
    HFONT pathFont = CreatePreviewFont(10, FW_NORMAL);
    HGDIOBJ oldFont = ::SelectObject(mem, titleFont);
    ::SetBkMode(mem, TRANSPARENT);
    ::SetTextColor(mem, kTitleText);
    int textLeft = margin + iconSize + 16;
    int textWidth = width - textLeft - margin;
    std::wstring title = EllipsizeText(mem, proxy.title, textWidth);
    RECT titleRc{textLeft, margin + 2, width - margin, margin + iconSize};
    ::DrawTextW(mem, title.c_str(), static_cast<int>(title.size()), &titleRc,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    ::SelectObject(mem, pathFont);
    ::SetTextColor(mem, kPathText);
    std::wstring path = EllipsizeText(mem, proxy.path, width - (margin * 2));
    RECT pathRc{margin, margin + iconSize + 22, width - margin, margin + iconSize + 64};
    ::DrawTextW(mem, path.c_str(), static_cast<int>(path.size()), &pathRc,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

    ::SelectObject(mem, oldFont);
    if(titleFont) {
        ::DeleteObject(titleFont);
    }
    if(pathFont) {
        ::DeleteObject(pathFont);
    }
    ::SelectObject(mem, oldBitmap);
    ::DeleteDC(mem);
    return bitmap;
}

void TaskbarTabPreviewManager::SetProxyIcon(ProxyWindow& proxy, HICON icon) {
    if(proxy.icon != nullptr) {
        ::DestroyIcon(proxy.icon);
        proxy.icon = nullptr;
    }
    proxy.icon = icon != nullptr ? ::CopyIcon(icon) : nullptr;
    ::SendMessageW(proxy.hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(proxy.icon));
    ::SendMessageW(proxy.hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(proxy.icon));
}

ATOM TaskbarTabPreviewManager::RegisterProxyClass() {
    static ATOM atom = 0;
    if(atom != 0) {
        return atom;
    }
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &TaskbarTabPreviewManager::ProxyWndProc;
    wc.hInstance = _AtlBaseModule.GetModuleInstance();
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kProxyWindowClass;
    atom = ::RegisterClassExW(&wc);
    return atom;
}

LRESULT CALLBACK TaskbarTabPreviewManager::ProxyWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    ProxyWindow* proxy = reinterpret_cast<ProxyWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if(message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        proxy = reinterpret_cast<ProxyWindow*>(create->lpCreateParams);
        if(proxy != nullptr) {
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(proxy));
        }
    }
    if(proxy != nullptr && proxy->owner != nullptr) {
        return proxy->owner->HandleProxyMessage(*proxy, message, wParam, lParam);
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT TaskbarTabPreviewManager::HandleProxyMessage(ProxyWindow& proxy, UINT message, WPARAM wParam, LPARAM lParam) {
    switch(message) {
    case WM_ACTIVATE:
        if(LOWORD(wParam) != WA_INACTIVE) {
            ActivateProxy(proxy.index);
            return 0;
        }
        break;
    case WM_SYSCOMMAND:
        if((wParam & 0xFFF0) == SC_CLOSE) {
            CloseProxy(proxy.index);
            return 0;
        }
        break;
    case WM_CLOSE:
        CloseProxy(proxy.index);
        return 0;
    case WM_GETICON:
        if(proxy.icon != nullptr) {
            return reinterpret_cast<LRESULT>(proxy.icon);
        }
        break;
    case kWmDwmSendIconicThumbnail: {
        int width = HIWORD(lParam);
        int height = LOWORD(lParam);
        HBITMAP bitmap = CreatePreviewBitmap(proxy, width, height);
        if(bitmap != nullptr) {
            ::DwmSetIconicThumbnail(proxy.hwnd, bitmap, DWM_SIT_DISPLAYFRAME);
            ::DeleteObject(bitmap);
        }
        return 0;
    }
    case kWmDwmSendIconicLivePreviewBitmap: {
        HBITMAP bitmap = CreatePreviewBitmap(proxy, kDefaultPreviewWidth, kDefaultPreviewHeight);
        if(bitmap != nullptr) {
            ::DwmSetIconicLivePreviewBitmap(proxy.hwnd, bitmap, nullptr, DWM_SIT_DISPLAYFRAME);
            ::DeleteObject(bitmap);
        }
        return 0;
    }
    }
    return ::DefWindowProcW(proxy.hwnd, message, wParam, lParam);
}
