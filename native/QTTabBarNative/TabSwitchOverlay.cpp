#include "pch.h"
#include "TabSwitchOverlay.h"

#include <VersionHelpers.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <vsstyle.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "UxTheme.lib")

namespace {
constexpr int kMaxVisibleItems = 11;
constexpr int kHeaderVerticalPadding = 4;
constexpr int kBodyTopOffset = 11;
constexpr int kWindowBaseWidth = 0x184; // 388px at 96 DPI
constexpr int kWindowBaseHeader = 0x2A;  // 42px at 96 DPI (matches managed layout)
constexpr COLORREF kHoverBackground = RGB(230, 230, 230);
constexpr COLORREF kSelectedOutline = RGB(45, 110, 180);

inline COLORREF AlphaBlendColor(COLORREF base, COLORREF overlay, BYTE alpha) {
    BYTE inv = 255 - alpha;
    BYTE br = GetRValue(base);
    BYTE bg = GetGValue(base);
    BYTE bb = GetBValue(base);
    BYTE or_ = GetRValue(overlay);
    BYTE og = GetGValue(overlay);
    BYTE ob = GetBValue(overlay);
    BYTE r = static_cast<BYTE>((br * inv + or_ * alpha) / 255);
    BYTE g = static_cast<BYTE>((bg * inv + og * alpha) / 255);
    BYTE b = static_cast<BYTE>((bb * inv + ob * alpha) / 255);
    return RGB(r, g, b);
}

inline UINT BuildDrawTextFormat(UINT align) {
    UINT format = DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX;
    switch(align) {
    case DT_RIGHT:
        format |= DT_RIGHT;
        break;
    case DT_CENTER:
        format |= DT_CENTER;
        break;
    default:
        format |= DT_LEFT;
        break;
    }
    format |= DT_END_ELLIPSIS;
    return format;
}

HFONT CreateMenuFont(BOOL bold) {
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    if(!::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, metrics.cbSize, &metrics, 0)) {
        return nullptr;
    }
    LOGFONTW font = metrics.lfMenuFont;
    if(bold) {
        font.lfWeight = FW_BOLD;
    }
    return ::CreateFontIndirectW(&font);
}

} // namespace

TabSwitchOverlay::TabSwitchOverlay() noexcept
    : m_selectedIndex(0)
    , m_initialIndex(0)
    , m_hoverIndex(kInvalidIndex)
    , m_compositionEnabled(false)
    , m_visible(false)
    , m_menuHeight(::GetSystemMetrics(SM_CYMENU))
    , m_dpi(96)
    , m_ownerWindow(nullptr)
    , m_font(nullptr)
    , m_boldFont(nullptr) {}

TabSwitchOverlay::~TabSwitchOverlay() {
    if(m_font) {
        ::DeleteObject(m_font);
        m_font = nullptr;
    }
    if(m_boldFont) {
        ::DeleteObject(m_boldFont);
        m_boldFont = nullptr;
    }
}

void TabSwitchOverlay::SetCommitCallback(std::function<void(std::size_t)> callback) {
    m_commitCallback = std::move(callback);
}

bool TabSwitchOverlay::Show(HWND ownerWindow, std::vector<Entry> entries, std::size_t selectedIndex,
                            std::size_t initialIndex) {
    if(entries.empty()) {
        return false;
    }

    if(!IsWindow()) {
        HWND parent = nullptr;
        RECT rc = {0, 0, 0, 0};
        DWORD style = WS_POPUP;
        DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
        if(Create(parent, rc, L"", style, exStyle) == nullptr) {
            return false;
        }
    }

    m_entries = std::move(entries);
    m_itemRects.assign(m_entries.size(), RECT{});
    m_selectedIndex = (std::min)(selectedIndex, m_entries.size() - 1);
    m_initialIndex = (std::min)(initialIndex, m_entries.size() - 1);
    m_hoverIndex = kInvalidIndex;
    m_ownerWindow = ownerWindow;

    m_dpi = Dpi();
    m_menuHeight = std::max<int>(::GetSystemMetrics(SM_CYMENU), Scale(18));
    UpdateCompositionState();

    int visibleCount = static_cast<int>(std::min<std::size_t>(kMaxVisibleItems, m_entries.size()));
    int width = Scale(kWindowBaseWidth);
    int height = Scale(kWindowBaseHeader) + m_menuHeight * (visibleCount + 1);

    HWND target = ownerWindow != nullptr ? ::GetAncestor(ownerWindow, GA_ROOT) : nullptr;
    RECT ownerRect{};
    if(target != nullptr) {
        ::GetWindowRect(target, &ownerRect);
    } else if(ownerWindow != nullptr) {
        ::GetWindowRect(ownerWindow, &ownerRect);
    }

    int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;

    UpdateLayout();
    ::SetWindowPos(m_hWnd, HWND_TOPMOST, x, y, width, height,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
    m_visible = true;
    ::InvalidateRect(m_hWnd, nullptr, TRUE);
    return true;
}

void TabSwitchOverlay::Hide(bool commit) {
    if(!IsWindow()) {
        return;
    }
    if(!m_visible) {
        ::ShowWindow(m_hWnd, SW_HIDE);
        return;
    }
    m_visible = false;
    ::ShowWindow(m_hWnd, SW_HIDE);
    (void)commit;
}

std::size_t TabSwitchOverlay::Cycle(bool reverse) {
    if(m_entries.empty()) {
        return 0;
    }
    if(reverse) {
        if(m_selectedIndex == 0) {
            m_selectedIndex = m_entries.size() - 1;
        } else {
            --m_selectedIndex;
        }
    } else {
        ++m_selectedIndex;
        if(m_selectedIndex >= m_entries.size()) {
            m_selectedIndex = 0;
        }
    }
    m_hoverIndex = kInvalidIndex;
    if(IsWindow()) {
        ::InvalidateRect(m_hWnd, nullptr, TRUE);
    }
    return m_selectedIndex;
}

int TabSwitchOverlay::Dpi() const noexcept {
    if(IsWindow()) {
#if defined(_WIN32_WINNT) && _WIN32_WINNT >= _WIN32_WINNT_WIN10
        if(::IsWindows10OrGreater()) {
            return ::GetDpiForWindow(m_hWnd);
        }
#endif
    }
    HDC screen = ::GetDC(nullptr);
    int dpi = screen ? ::GetDeviceCaps(screen, LOGPIXELSX) : 96;
    if(screen) {
        ::ReleaseDC(nullptr, screen);
    }
    return dpi > 0 ? dpi : 96;
}

int TabSwitchOverlay::Scale(int value) const noexcept {
    return MulDiv(value, static_cast<int>(m_dpi), 96);
}

LRESULT TabSwitchOverlay::OnCreate(UINT, WPARAM, LPARAM, BOOL& bHandled) {
    bHandled = TRUE;
    m_font = CreateMenuFont(FALSE);
    m_boldFont = CreateMenuFont(TRUE);
    UpdateCompositionState();
    return 0;
}

LRESULT TabSwitchOverlay::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled) {
    bHandled = TRUE;
    if(m_font) {
        ::DeleteObject(m_font);
        m_font = nullptr;
    }
    if(m_boldFont) {
        ::DeleteObject(m_boldFont);
        m_boldFont = nullptr;
    }
    return 0;
}

LRESULT TabSwitchOverlay::OnPaint(UINT, WPARAM, LPARAM, BOOL& bHandled) {
    bHandled = TRUE;
    PAINTSTRUCT ps{};
    HDC hdc = ::BeginPaint(m_hWnd, &ps);
    if(hdc) {
        DrawContent(hdc);
    }
    ::EndPaint(m_hWnd, &ps);
    return 0;
}

LRESULT TabSwitchOverlay::OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL& bHandled) {
    bHandled = TRUE;
    return 1;
}

LRESULT TabSwitchOverlay::OnMouseMove(UINT, WPARAM, LPARAM lParam, BOOL& bHandled) {
    bHandled = TRUE;
    EnsureMouseTracking();
    POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    std::size_t newHover = kInvalidIndex;
    for(std::size_t i = 0; i < m_itemRects.size(); ++i) {
        if(::PtInRect(&m_itemRects[i], pt)) {
            newHover = i;
            break;
        }
    }
    if(newHover != m_hoverIndex) {
        std::size_t oldHover = m_hoverIndex;
        m_hoverIndex = newHover;
        if(oldHover != kInvalidIndex && oldHover < m_itemRects.size()) {
            ::InvalidateRect(m_hWnd, &m_itemRects[oldHover], FALSE);
        }
        if(newHover != kInvalidIndex && newHover < m_itemRects.size()) {
            ::InvalidateRect(m_hWnd, &m_itemRects[newHover], FALSE);
        }
    }
    return 0;
}

LRESULT TabSwitchOverlay::OnMouseLeave(UINT, WPARAM, LPARAM, BOOL& bHandled) {
    bHandled = TRUE;
    if(m_hoverIndex != kInvalidIndex && m_hoverIndex < m_itemRects.size()) {
        RECT invalidate = m_itemRects[m_hoverIndex];
        m_hoverIndex = kInvalidIndex;
        ::InvalidateRect(m_hWnd, &invalidate, FALSE);
    }
    return 0;
}

LRESULT TabSwitchOverlay::OnLButtonUp(UINT, WPARAM, LPARAM lParam, BOOL& bHandled) {
    bHandled = TRUE;
    if(!m_visible) {
        return 0;
    }
    POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    if(m_hoverIndex != kInvalidIndex && m_hoverIndex < m_itemRects.size()) {
        if(::PtInRect(&m_itemRects[m_hoverIndex], pt)) {
            m_selectedIndex = m_hoverIndex;
            ::InvalidateRect(m_hWnd, nullptr, FALSE);
            if(m_commitCallback) {
                m_commitCallback(m_selectedIndex);
            }
        }
    }
    return 0;
}

LRESULT TabSwitchOverlay::OnMouseActivate(UINT, WPARAM, LPARAM, BOOL& bHandled) {
    bHandled = TRUE;
    return MA_NOACTIVATE;
}

LRESULT TabSwitchOverlay::OnNcHitTest(UINT, WPARAM, LPARAM, BOOL& bHandled) {
    bHandled = TRUE;
    return HTCLIENT;
}

LRESULT TabSwitchOverlay::OnCompositionChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
    bHandled = TRUE;
    UpdateCompositionState();
    ::InvalidateRect(m_hWnd, nullptr, TRUE);
    return 0;
}

void TabSwitchOverlay::UpdateCompositionState() {
    BOOL enabled = FALSE;
    if(SUCCEEDED(::DwmIsCompositionEnabled(&enabled))) {
        m_compositionEnabled = enabled == TRUE;
    } else {
        m_compositionEnabled = false;
    }
    if(m_compositionEnabled && IsWindow()) {
        MARGINS margins = {-1};
        ::DwmExtendFrameIntoClientArea(m_hWnd, &margins);
    }
}

void TabSwitchOverlay::UpdateLayout() {
    if(!IsWindow()) {
        return;
    }
    RECT rc{};
    ::GetClientRect(m_hWnd, &rc);
    int width = rc.right - rc.left;
    int bodyTop = Scale(kHeaderVerticalPadding) + m_menuHeight + Scale(kBodyTopOffset);
    int rowHeight = m_menuHeight;
    int rowLeft = m_compositionEnabled ? Scale(2) : Scale(6);
    int rowRight = width - Scale(6);

    m_itemRects.assign(m_entries.size(), RECT{});

    if(m_entries.empty()) {
        return;
    }

    const int count = static_cast<int>(m_entries.size());
    const bool overflow = count > kMaxVisibleItems;
    const int beforeCount = overflow ? 5 : (count - 1) / 2;
    const int afterCount = overflow ? 5 : ((count - 1) - beforeCount);

    auto indexFromOffset = [&](int offset) {
        int idx = static_cast<int>(m_selectedIndex) + offset;
        while(idx < 0) idx += count;
        while(idx >= count) idx -= count;
        return idx;
    };

    int y = bodyTop;
    auto assignRect = [&](int index, int top) {
        RECT full{rowLeft, top, rowRight, top + rowHeight};
        if(index >= 0 && index < count) {
            m_itemRects[index] = full;
        }
    };

    for(int i = beforeCount; i > 0; --i) {
        int index = indexFromOffset(-i);
        assignRect(index, y);
        y += rowHeight;
    }

    assignRect(static_cast<int>(m_selectedIndex), y);
    y += rowHeight;

    for(int i = 0; i < afterCount; ++i) {
        int index = indexFromOffset(i + 1);
        assignRect(index, y);
        y += rowHeight;
    }

    // Actual drawing occurs in DrawContent where rectangles are recomputed per paint.
}

void TabSwitchOverlay::DrawContent(HDC hdc) const {
    RECT client{};
    ::GetClientRect(m_hWnd, &client);
    int width = client.right - client.left;
    int height = client.bottom - client.top;

    HDC memDC = ::CreateCompatibleDC(hdc);
    if(!memDC) {
        return;
    }
    HBITMAP memBmp = ::CreateCompatibleBitmap(hdc, width, height);
    if(!memBmp) {
        ::DeleteDC(memDC);
        return;
    }
    HGDIOBJ oldBmp = ::SelectObject(memDC, memBmp);

    COLORREF background = m_compositionEnabled ? RGB(0, 0, 0) : ::GetSysColor(COLOR_MENU);
    HBRUSH brush = ::CreateSolidBrush(background);
    ::FillRect(memDC, &client, brush);
    ::DeleteObject(brush);

    DrawHeader(memDC, client);

    if(!m_entries.empty()) {
        int rowHeight = m_menuHeight;
        int rowLeft = Scale(6);
        if(m_compositionEnabled) {
            rowLeft = Scale(2);
        }
        int rowRight = width - Scale(6);
        const int count = static_cast<int>(m_entries.size());
        const bool overflow = count > kMaxVisibleItems;
        const int beforeCount = overflow ? 5 : (count - 1) / 2;
        const int afterCount = overflow ? 5 : ((count - 1) - beforeCount);

        auto indexFromOffset = [&](int offset) {
            int idx = static_cast<int>(m_selectedIndex) + offset;
            while(idx < 0) idx += count;
            while(idx >= count) idx -= count;
            return idx;
        };

        int y = Scale(kHeaderVerticalPadding) + m_menuHeight + Scale(kBodyTopOffset);
        auto drawRowAt = [&](int index, bool selected, bool hovered) {
            RECT full{rowLeft, y, rowRight, y + rowHeight};
            DrawRow(memDC, full, m_entries[index], index, selected, hovered, index == m_initialIndex);

            // cache rectangle for hit-testing
            if(index >= 0 && static_cast<std::size_t>(index) < m_itemRects.size()) {
                m_itemRects[index] = full;
            }
            y += rowHeight;
        };

        for(int i = beforeCount; i > 0; --i) {
            int index = indexFromOffset(-i);
            bool hovered = m_hoverIndex == static_cast<std::size_t>(index);
            drawRowAt(index, false, hovered);
        }

        bool hoveredSelected = m_hoverIndex == m_selectedIndex;
        drawRowAt(static_cast<int>(m_selectedIndex), true, hoveredSelected);

        for(int i = 0; i < afterCount; ++i) {
            int index = indexFromOffset(i + 1);
            bool hovered = m_hoverIndex == static_cast<std::size_t>(index);
            drawRowAt(index, false, hovered);
        }
    }

    ::BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

    ::SelectObject(memDC, oldBmp);
    ::DeleteObject(memBmp);
    ::DeleteDC(memDC);
}

void TabSwitchOverlay::DrawHeader(HDC hdc, const RECT& clientRect) const {
    if(m_entries.empty()) {
        return;
    }
    RECT header = clientRect;
    int inset = m_compositionEnabled ? 0 : Scale(2);
    header.left += inset;
    header.right -= inset;
    header.top += Scale(kHeaderVerticalPadding);
    header.bottom = header.top + m_menuHeight;

    std::wstring text = m_entries[m_selectedIndex].path;
    if(text.empty()) {
        text = m_entries[m_selectedIndex].display;
    }

    DrawText(hdc, header, text, false, true, DT_CENTER);
}

void TabSwitchOverlay::DrawRow(HDC hdc, const RECT& rowRect, const Entry& entry, std::size_t index,
                               bool selected, bool hovered, bool initial) const {
    RECT rect = rowRect;
    COLORREF base = ::GetSysColor(COLOR_MENU);
    if(m_compositionEnabled) {
        base = RGB(32, 32, 32);
    }

    COLORREF background = base;
    if(selected) {
        background = ::GetSysColor(COLOR_HIGHLIGHT);
    } else if(hovered) {
        background = m_compositionEnabled ? AlphaBlendColor(base, RGB(255, 255, 255), 60)
                                          : kHoverBackground;
    }

    HBRUSH brush = ::CreateSolidBrush(background);
    ::FillRect(hdc, &rect, brush);
    ::DeleteObject(brush);

    if(selected && m_compositionEnabled) {
        HPEN pen = ::CreatePen(PS_SOLID, 1, kSelectedOutline);
        HGDIOBJ oldPen = ::SelectObject(hdc, pen);
        ::SelectObject(hdc, ::GetStockObject(NULL_BRUSH));
        RECT outline = rect;
        ::Rectangle(hdc, outline.left, outline.top, outline.right, outline.bottom);
        ::SelectObject(hdc, oldPen);
        ::DeleteObject(pen);
    }

    RECT numberRect = rect;
    numberRect.right = numberRect.left + Scale(24);
    RECT iconRect = rect;
    int iconSize = Scale(16);
    iconRect.left = numberRect.right + Scale(6);
    iconRect.right = iconRect.left + iconSize;
    int iconOffset = (std::max)(0, (m_menuHeight - iconSize) / 2);
    iconRect.top += iconOffset;
    iconRect.bottom = iconRect.top + iconSize;
    RECT textRect = rect;
    textRect.left = iconRect.right + Scale(8);

    std::wstring numberText = std::to_wstring(index + 1);
    DrawText(hdc, numberRect, numberText, selected, initial, DT_RIGHT);

    if(entry.icon != nullptr) {
        ::DrawIconEx(hdc, iconRect.left, iconRect.top, entry.icon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
    }

    DrawText(hdc, textRect, entry.display, selected, initial, DT_LEFT);
}

void TabSwitchOverlay::DrawText(HDC hdc, const RECT& rect, const std::wstring& text, bool selected,
                                bool bold, UINT align) const {
    UINT format = BuildDrawTextFormat(align);
    HFONT font = ResolveFont(bold);
    HGDIOBJ oldFont = nullptr;
    if(font) {
        oldFont = ::SelectObject(hdc, font);
    }

    int oldBkMode = ::SetBkMode(hdc, TRANSPARENT);
    COLORREF oldText = ::GetTextColor(hdc);
    COLORREF textColor = selected ? ::GetSysColor(COLOR_HIGHLIGHTTEXT) : ::GetSysColor(COLOR_MENUTEXT);
    if(m_compositionEnabled) {
        textColor = selected ? RGB(255, 255, 255) : RGB(240, 240, 240);
    }

    if(m_compositionEnabled) {
        DrawTextOnGlass(hdc, rect, text, bold, format);
    } else {
        ::SetTextColor(hdc, textColor);
        ::DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), const_cast<RECT*>(&rect), format);
    }

    ::SetTextColor(hdc, oldText);
    ::SetBkMode(hdc, oldBkMode);
    if(oldFont) {
        ::SelectObject(hdc, oldFont);
    }
}

void TabSwitchOverlay::DrawTextOnGlass(HDC hdc, const RECT& rect, const std::wstring& text, bool bold,
                                       UINT format) const {
    HTHEME theme = ::OpenThemeData(m_hWnd, VSCLASS_WINDOW);
    if(!theme) {
        HFONT font = ResolveFont(bold);
        HGDIOBJ oldFont = nullptr;
        if(font) {
            oldFont = ::SelectObject(hdc, font);
        }
        ::DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), const_cast<RECT*>(&rect), format);
        if(oldFont) {
            ::SelectObject(hdc, oldFont);
        }
        return;
    }

    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    if(width <= 0 || height <= 0) {
        ::CloseThemeData(theme);
        return;
    }

    HDC memDC = ::CreateCompatibleDC(hdc);
    if(!memDC) {
        ::CloseThemeData(theme);
        return;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = ::CreateDIBSection(memDC, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if(!dib) {
        ::DeleteDC(memDC);
        ::CloseThemeData(theme);
        return;
    }

    HGDIOBJ oldBmp = ::SelectObject(memDC, dib);
    HFONT font = ResolveFont(bold);
    HGDIOBJ oldFont = nullptr;
    if(font) {
        oldFont = ::SelectObject(memDC, font);
    }

    RECT localRect{0, 0, width, height};
    DTTOPTS opts{};
    opts.dwSize = sizeof(opts);
    opts.dwFlags = DTT_COMPOSITED | DTT_GLOWSIZE | DTT_TEXTCOLOR;
    opts.crText = RGB(255, 255, 255);
    opts.iGlowSize = 8;

    ::DrawThemeTextEx(theme, memDC, 0, 0, text.c_str(), static_cast<int>(text.size()), format, &localRect, &opts);
    ::BitBlt(hdc, rect.left, rect.top, width, height, memDC, 0, 0, SRCCOPY);

    if(oldFont) {
        ::SelectObject(memDC, oldFont);
    }
    ::SelectObject(memDC, oldBmp);
    ::DeleteObject(dib);
    ::DeleteDC(memDC);
    ::CloseThemeData(theme);
}

void TabSwitchOverlay::EnsureMouseTracking() {
    TRACKMOUSEEVENT tme{};
    tme.cbSize = sizeof(tme);
    tme.dwFlags = TME_LEAVE;
    tme.hwndTrack = m_hWnd;
    ::TrackMouseEvent(&tme);
}

HFONT TabSwitchOverlay::ResolveFont(bool bold) const {
    if(bold) {
        return m_boldFont ? m_boldFont : m_font;
    }
    return m_font;
}
