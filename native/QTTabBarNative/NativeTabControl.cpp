#include "pch.h"
#include "NativeTabControl.h"

#include <Shlwapi.h>
#include <Shellapi.h>
#include <commctrl.h>
#include <strsafe.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <utility>

#include "TabBarHost.h"

using qttabbar::ConfigData;
using qttabbar::LoadConfigFromRegistry;
using qttabbar::MouseChord;
using qttabbar::MouseTarget;
using qttabbar::WriteConfigToRegistry;

namespace {
MouseChord ComposeMouseChord(MouseChord base) {
    if(::GetKeyState(VK_SHIFT) & 0x8000) {
        base |= MouseChord::Shift;
    }
    if(::GetKeyState(VK_CONTROL) & 0x8000) {
        base |= MouseChord::Ctrl;
    }
    if(::GetKeyState(VK_MENU) & 0x8000) {
        base |= MouseChord::Alt;
    }
    return base;
}

COLORREF ToColorRef(uint32_t argb) {
    BYTE r = static_cast<BYTE>((argb >> 16) & 0xFF);
    BYTE g = static_cast<BYTE>((argb >> 8) & 0xFF);
    BYTE b = static_cast<BYTE>(argb & 0xFF);
    return RGB(r, g, b);
}

HFONT CreateFontFromConfig(const qttabbar::FontConfig& font, int dpiY, bool forceBold = false) {
    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(static_cast<int>(font.size * 10), dpiY, 720);
    lf.lfWeight = forceBold ? FW_BOLD : FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    if(!font.family.empty()) {
        ::StringCchCopyW(lf.lfFaceName, ARRAYSIZE(lf.lfFaceName), font.family.c_str());
    }
    return ::CreateFontIndirectW(&lf);
}

POINT CenterRect(const RECT& rc) {
    POINT pt{rc.left + (rc.right - rc.left) / 2, rc.top + (rc.bottom - rc.top) / 2};
    return pt;
}

}  // namespace

NativeTabControl::NativeTabControl(TabBarHost& owner) noexcept
    : m_owner(owner) {}

NativeTabControl::~NativeTabControl() {
    if(m_hWnd != nullptr) {
        ::DestroyWindow(m_hWnd);
    }
    for(auto& tab : m_tabs) {
        DestroyIcon(tab);
    }
    if(m_font) {
        ::DeleteObject(m_font);
        m_font = nullptr;
    }
    if(m_boldFont) {
        ::DeleteObject(m_boldFont);
        m_boldFont = nullptr;
    }
}

std::size_t NativeTabControl::AddTab(const std::wstring& path, bool makeActive, bool allowDuplicate) {
    std::wstring normalized = NormalizePath(path);
    if(normalized.empty()) {
        return m_tabs.size();
    }

    auto matchesPath = [&](const TabItem& item) { return _wcsicmp(item.path.c_str(), normalized.c_str()) == 0; };
    auto it = std::find_if(m_tabs.begin(), m_tabs.end(), matchesPath);
    if(it == m_tabs.end() || allowDuplicate) {
        TabItem item;
        item.path = normalized;
        item.title = ExtractTitle(normalized);
        item.alias.clear();
        item.locked = false;
        EnsureIcon(item);
        m_tabs.push_back(std::move(item));
        it = std::prev(m_tabs.end());
    } else if(it != m_tabs.end()) {
        EnsureIcon(*it);
    }

    if(makeActive && it != m_tabs.end()) {
        m_activeIndex = static_cast<std::size_t>(std::distance(m_tabs.begin(), it));
        for(std::size_t i = 0; i < m_tabs.size(); ++i) {
            m_tabs[i].active = (i == m_activeIndex);
        }
    }

    LayoutTabs();
    ::InvalidateRect(m_hWnd, nullptr, FALSE);
    return static_cast<std::size_t>(std::distance(m_tabs.begin(), it));
}

std::wstring NativeTabControl::ActivateTab(std::size_t index) {
    if(index >= m_tabs.size()) {
        return {};
    }
    m_activeIndex = index;
    for(std::size_t i = 0; i < m_tabs.size(); ++i) {
        m_tabs[i].active = (i == index);
    }
    ::InvalidateRect(m_hWnd, nullptr, FALSE);
    return m_tabs[index].path;
}

std::wstring NativeTabControl::ActivateNextTab() {
    if(m_tabs.empty()) {
        return {};
    }
    std::size_t next = (m_activeIndex + 1) % m_tabs.size();
    return ActivateTab(next);
}

std::wstring NativeTabControl::ActivatePreviousTab() {
    if(m_tabs.empty()) {
        return {};
    }
    std::size_t prev = (m_activeIndex + m_tabs.size() - 1) % m_tabs.size();
    return ActivateTab(prev);
}

std::optional<std::wstring> NativeTabControl::CloseTab(std::size_t index) {
    if(index >= m_tabs.size()) {
        return std::nullopt;
    }
    if(m_tabs[index].locked) {
        return std::nullopt;
    }
    std::wstring path = m_tabs[index].path;
    DestroyIcon(m_tabs[index]);
    m_tabs.erase(m_tabs.begin() + static_cast<std::ptrdiff_t>(index));
    if(m_activeIndex >= m_tabs.size()) {
        m_activeIndex = m_tabs.empty() ? 0 : m_tabs.size() - 1;
    }
    for(std::size_t i = 0; i < m_tabs.size(); ++i) {
        m_tabs[i].active = (i == m_activeIndex);
    }
    LayoutTabs();
    ::InvalidateRect(m_hWnd, nullptr, FALSE);
    return path;
}

std::optional<std::wstring> NativeTabControl::CloseActiveTab() {
    if(m_tabs.empty()) {
        return std::nullopt;
    }
    return CloseTab(m_activeIndex);
}

std::vector<std::wstring> NativeTabControl::CloseAllExcept(std::size_t index) {
    std::vector<std::wstring> closed;
    if(index >= m_tabs.size()) {
        return closed;
    }
    std::wstring activePath = m_tabs[index].path;
    for(std::size_t i = m_tabs.size(); i-- > 0;) {
        if(i == index || m_tabs[i].locked) {
            continue;
        }
        if(auto closedPath = CloseTab(i)) {
            closed.push_back(*closedPath);
        }
    }
    auto it = std::find_if(m_tabs.begin(), m_tabs.end(), [&](const TabItem& tab) {
        return _wcsicmp(tab.path.c_str(), activePath.c_str()) == 0;
    });
    if(it != m_tabs.end()) {
        m_activeIndex = static_cast<std::size_t>(std::distance(m_tabs.begin(), it));
        for(std::size_t i = 0; i < m_tabs.size(); ++i) {
            m_tabs[i].active = (i == m_activeIndex);
        }
    }
    return closed;
}

std::vector<std::wstring> NativeTabControl::CloseTabsToLeft(std::size_t index) {
    std::vector<std::wstring> closed;
    if(index >= m_tabs.size()) {
        return closed;
    }
    std::wstring activePath = m_tabs[index].path;
    for(std::size_t i = index; i-- > 0;) {
        if(m_tabs[i].locked) {
            continue;
        }
        if(auto closedPath = CloseTab(i)) {
            closed.push_back(*closedPath);
        }
    }
    auto it = std::find_if(m_tabs.begin(), m_tabs.end(), [&](const TabItem& tab) {
        return _wcsicmp(tab.path.c_str(), activePath.c_str()) == 0;
    });
    if(it != m_tabs.end()) {
        m_activeIndex = static_cast<std::size_t>(std::distance(m_tabs.begin(), it));
        for(std::size_t i = 0; i < m_tabs.size(); ++i) {
            m_tabs[i].active = (i == m_activeIndex);
        }
    }
    return closed;
}

std::vector<std::wstring> NativeTabControl::CloseTabsToRight(std::size_t index) {
    std::vector<std::wstring> closed;
    if(index >= m_tabs.size()) {
        return closed;
    }
    std::wstring activePath = m_tabs[index].path;
    for(std::size_t i = m_tabs.size(); i-- > index + 1;) {
        if(m_tabs[i].locked) {
            continue;
        }
        if(auto closedPath = CloseTab(i)) {
            closed.push_back(*closedPath);
        }
    }
    auto it = std::find_if(m_tabs.begin(), m_tabs.end(), [&](const TabItem& tab) {
        return _wcsicmp(tab.path.c_str(), activePath.c_str()) == 0;
    });
    if(it != m_tabs.end()) {
        m_activeIndex = static_cast<std::size_t>(std::distance(m_tabs.begin(), it));
        for(std::size_t i = 0; i < m_tabs.size(); ++i) {
            m_tabs[i].active = (i == m_activeIndex);
        }
    }
    return closed;
}

std::optional<std::wstring> NativeTabControl::GetActivePath() const {
    if(m_tabs.empty() || m_activeIndex >= m_tabs.size()) {
        return std::nullopt;
    }
    return m_tabs[m_activeIndex].path;
}

std::size_t NativeTabControl::GetActiveIndex() const noexcept {
    return m_activeIndex;
}

std::vector<std::wstring> NativeTabControl::GetTabPaths() const {
    std::vector<std::wstring> paths;
    paths.reserve(m_tabs.size());
    for(const auto& tab : m_tabs) {
        paths.push_back(tab.path);
    }
    return paths;
}

std::wstring NativeTabControl::GetPath(std::size_t index) const {
    if(index >= m_tabs.size()) {
        return {};
    }
    return m_tabs[index].path;
}

std::optional<RECT> NativeTabControl::GetTabBounds(std::size_t index) const {
    if(index >= m_tabs.size()) {
        return std::nullopt;
    }
    RECT rect = m_tabs[index].metrics.bounds;
    POINT topLeft{rect.left, rect.top};
    POINT bottomRight{rect.right, rect.bottom};
    ::ClientToScreen(m_hWnd, &topLeft);
    ::ClientToScreen(m_hWnd, &bottomRight);
    rect.left = topLeft.x;
    rect.top = topLeft.y;
    rect.right = bottomRight.x;
    rect.bottom = bottomRight.y;
    return rect;
}

std::vector<std::wstring> NativeTabControl::GetTabDisplayNames() const {
    std::vector<std::wstring> names;
    names.reserve(m_tabs.size());
    for(const auto& tab : m_tabs) {
        if(!tab.alias.empty()) {
            names.push_back(tab.alias);
        } else if(!tab.title.empty()) {
            names.push_back(tab.title);
        } else {
            names.push_back(tab.path);
        }
    }
    return names;
}

std::vector<NativeTabControl::SwitchEntry> NativeTabControl::GetSwitchEntries() {
    std::vector<SwitchEntry> entries;
    entries.reserve(m_tabs.size());
    for(auto& tab : m_tabs) {
        if(m_config.tabs.showFolderIcon && tab.icon == nullptr) {
            EnsureIcon(tab);
        }
        SwitchEntry entry;
        entry.display = tab.alias.empty() ? tab.title : tab.alias;
        entry.path = tab.path;
        entry.icon = tab.icon;
        entry.locked = tab.locked;
        entries.push_back(std::move(entry));
    }
    return entries;
}

bool NativeTabControl::IsLocked(std::size_t index) const {
    if(index >= m_tabs.size()) {
        return false;
    }
    return m_tabs[index].locked;
}

bool NativeTabControl::CanCloseTab(std::size_t index) const {
    if(index >= m_tabs.size()) {
        return false;
    }
    return !m_tabs[index].locked;
}

bool NativeTabControl::HasClosableTabsToLeft(std::size_t index) const {
    if(m_tabs.empty() || index == 0 || index > m_tabs.size()) {
        return false;
    }
    if(index >= m_tabs.size()) {
        index = m_tabs.size() - 1;
    }
    for(std::size_t i = index; i-- > 0;) {
        if(!m_tabs[i].locked) {
            return true;
        }
    }
    return false;
}

bool NativeTabControl::HasClosableTabsToRight(std::size_t index) const {
    if(index >= m_tabs.size()) {
        return false;
    }
    for(std::size_t i = index + 1; i < m_tabs.size(); ++i) {
        if(!m_tabs[i].locked) {
            return true;
        }
    }
    return false;
}

bool NativeTabControl::HasClosableOtherTabs(std::size_t index) const {
    if(index >= m_tabs.size()) {
        return false;
    }
    for(std::size_t i = 0; i < m_tabs.size(); ++i) {
        if(i != index && !m_tabs[i].locked) {
            return true;
        }
    }
    return false;
}

void NativeTabControl::SetLocked(std::size_t index, bool locked) {
    if(index >= m_tabs.size()) {
        return;
    }
    if(m_tabs[index].locked == locked) {
        return;
    }
    m_tabs[index].locked = locked;
    ::InvalidateRect(m_hWnd, &m_tabs[index].metrics.bounds, FALSE);
}

void NativeTabControl::SetAlias(std::size_t index, const std::wstring& alias) {
    if(index >= m_tabs.size()) {
        return;
    }
    if(m_tabs[index].alias == alias) {
        return;
    }
    m_tabs[index].alias = alias;
    LayoutTabs();
    ::InvalidateRect(m_hWnd, nullptr, FALSE);
}

void NativeTabControl::ApplyConfiguration(const ConfigData& config) {
    m_config = config;
    SetPlusButtonVisible(m_config.tabs.needPlusButton, false);
    RefreshMetrics();
    for(auto& tab : m_tabs) {
        if(m_config.tabs.showFolderIcon) {
            EnsureIcon(tab);
        } else {
            DestroyIcon(tab);
        }
    }
    LayoutTabs();
    ::InvalidateRect(m_hWnd, nullptr, TRUE);
}

void NativeTabControl::RefreshMetrics() {
    HDC hdc = ::GetDC(m_hWnd);
    int dpiY = hdc ? ::GetDeviceCaps(hdc, LOGPIXELSY) : 96;
    if(hdc) {
        ::ReleaseDC(m_hWnd, hdc);
    }

    if(m_font) {
        ::DeleteObject(m_font);
        m_font = nullptr;
    }
    if(m_boldFont) {
        ::DeleteObject(m_boldFont);
        m_boldFont = nullptr;
    }

    m_font = CreateFontFromConfig(m_config.skin.tabTextFont, dpiY, false);
    bool bold = m_config.skin.activeTabInBold;
    if(bold) {
        m_boldFont = CreateFontFromConfig(m_config.skin.tabTextFont, dpiY, true);
    }

    m_tabHeight = (std::max)(16, m_config.skin.tabHeight);
    m_tabMinWidth = (std::max)(48, m_config.skin.tabMinWidth);
    m_tabMaxWidth = (std::max)(m_tabMinWidth, m_config.skin.tabMaxWidth);
    m_iconSize = (std::min)(24, (std::max)(16, m_config.skin.tabHeight - 8));
    m_horizontalPadding = m_config.skin.tabContentMargin.left + m_config.skin.tabContentMargin.right + 16;
    m_spacing = (std::max)(2, m_config.skin.tabSizeMargin.right);
}

void NativeTabControl::FocusTabBar() {
    if(m_hWnd) {
        ::SetFocus(m_hWnd);
    }
}

void NativeTabControl::EnsureLayout() {
    LayoutTabs();
    ::InvalidateRect(m_hWnd, nullptr, FALSE);
}

void NativeTabControl::SetPlusButtonVisible(bool visible, bool persist) {
    m_showPlusButton = visible;
    if(persist) {
        m_config.tabs.needPlusButton = visible;
        WriteConfigToRegistry(m_config, false);
    }
    LayoutTabs();
    ::InvalidateRect(m_hWnd, nullptr, FALSE);
}

void NativeTabControl::NotifyExplorerPathChanged(const std::wstring& path) {
    if(path.empty()) {
        return;
    }
    auto matchesPath = [&](const TabItem& item) { return _wcsicmp(item.path.c_str(), path.c_str()) == 0; };
    auto it = std::find_if(m_tabs.begin(), m_tabs.end(), matchesPath);
    if(it != m_tabs.end()) {
        std::size_t index = static_cast<std::size_t>(std::distance(m_tabs.begin(), it));
        ActivateTab(index);
    } else {
        AddTab(path, true, true);
    }
}

LRESULT NativeTabControl::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    ConfigData config = LoadConfigFromRegistry();
    ApplyConfiguration(config);
    return 0;
}

LRESULT NativeTabControl::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    for(auto& tab : m_tabs) {
        DestroyIcon(tab);
    }
    m_tabs.clear();
    return 0;
}

LRESULT NativeTabControl::OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
    bHandled = TRUE;
    int width = LOWORD(lParam);
    int height = HIWORD(lParam);
    UNREFERENCED_PARAMETER(width);
    UNREFERENCED_PARAMETER(height);
    LayoutTabs();
    return 0;
}

LRESULT NativeTabControl::OnPaint(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    PAINTSTRUCT ps{};
    HDC hdc = ::BeginPaint(m_hWnd, &ps);
    if(hdc) {
        DrawControl(hdc);
    }
    ::EndPaint(m_hWnd, &ps);
    return 0;
}

LRESULT NativeTabControl::OnEraseBkgnd(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    return 1;
}

LRESULT NativeTabControl::OnMouseMove(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
    bHandled = TRUE;
    POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    StartMouseTracking();
    auto tabIndex = HitTestTab(pt);
    UpdateHoverState(tabIndex, pt);
    return 0;
}

LRESULT NativeTabControl::OnMouseLeave(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    m_hotIndex.reset();
    m_pressedClose.reset();
    for(auto& tab : m_tabs) {
        tab.hovered = false;
        tab.closeHovered = false;
    }
    ::InvalidateRect(m_hWnd, nullptr, FALSE);
    m_trackingMouse = false;
    POINT emptyPoint{0, 0};
    m_owner.OnTabControlHoverChanged(std::nullopt, emptyPoint);
    return 0;
}

LRESULT NativeTabControl::OnLButtonDown(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    bHandled = TRUE;
    POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    auto tabIndex = HitTestTab(pt);
    MouseChord chord = ComposeMouseChord(MouseChord::Left);
    if(tabIndex) {
        if(m_owner.HandleMouseAction(MouseTarget::Tab, chord, tabIndex)) {
            return 0;
        }
    } else {
        if(m_owner.HandleMouseAction(MouseTarget::TabBarBackground, chord)) {
            return 0;
        }
    }
    if(tabIndex) {
        std::size_t index = *tabIndex;
        if(HitTestClose(m_tabs[index], pt)) {
            m_pressedClose = index;
            m_tabs[index].closePressed = true;
            ::SetCapture(m_hWnd);
            ::InvalidateRect(m_hWnd, &m_tabs[index].metrics.bounds, FALSE);
            return 0;
        }
        m_pressedTab = index;
        ::SetCapture(m_hWnd);
    } else if(m_showPlusButton && HitTestPlus(pt)) {
        m_pressedTab.reset();
        m_pressedClose.reset();
        RequestNewTab();
    }
    return 0;
}

LRESULT NativeTabControl::OnLButtonUp(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
    bHandled = TRUE;
    POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    if(::GetCapture() == m_hWnd) {
        ::ReleaseCapture();
    }
    if(m_pressedClose) {
        std::size_t index = *m_pressedClose;
        m_tabs[index].closePressed = false;
        if(HitTestClose(m_tabs[index], pt)) {
            RequestCloseTab(index);
        }
        m_pressedClose.reset();
        ::InvalidateRect(m_hWnd, &m_tabs[index].metrics.bounds, FALSE);
        return 0;
    }
    if(m_pressedTab) {
        std::size_t index = *m_pressedTab;
        if(HitTestTab(pt) == m_pressedTab) {
            RequestSelectTab(index);
        }
        m_pressedTab.reset();
        return 0;
    }
    if(m_showPlusButton && HitTestPlus(pt)) {
        RequestNewTab();
    }
    return 0;
}

LRESULT NativeTabControl::OnLButtonDblClk(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
    bHandled = TRUE;
    POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    auto tabIndex = HitTestTab(pt);
    MouseChord chord = ComposeMouseChord(MouseChord::Double);
    if(tabIndex) {
        if(m_owner.HandleMouseAction(MouseTarget::Tab, chord, tabIndex)) {
            return 0;
        }
    } else {
        if(m_owner.HandleMouseAction(MouseTarget::TabBarBackground, chord)) {
            return 0;
        }
    }
    if(tabIndex) {
        RequestBeginDrag(*tabIndex, pt);
    } else if(m_showPlusButton && HitTestPlus(pt)) {
        RequestNewTab();
    }
    return 0;
}

LRESULT NativeTabControl::OnMButtonDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
    bHandled = TRUE;
    POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    auto tabIndex = HitTestTab(pt);
    MouseChord chord = ComposeMouseChord(MouseChord::Middle);
    if(tabIndex) {
        if(m_owner.HandleMouseAction(MouseTarget::Tab, chord, tabIndex)) {
            return 0;
        }
    } else {
        if(m_owner.HandleMouseAction(MouseTarget::TabBarBackground, chord)) {
            return 0;
        }
    }
    return 0;
}

LRESULT NativeTabControl::OnMButtonUp(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    return 0;
}

LRESULT NativeTabControl::OnMButtonDblClk(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
    bHandled = TRUE;
    POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    auto tabIndex = HitTestTab(pt);
    MouseChord chord = ComposeMouseChord(MouseChord::Double);
    if(tabIndex) {
        if(m_owner.HandleMouseAction(MouseTarget::Tab, chord, tabIndex)) {
            return 0;
        }
    } else {
        if(m_owner.HandleMouseAction(MouseTarget::TabBarBackground, chord)) {
            return 0;
        }
    }
    return 0;
}

LRESULT NativeTabControl::OnXButtonDown(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
    WORD button = GET_XBUTTON_WPARAM(wParam);
    MouseChord base = (button == XBUTTON2) ? MouseChord::X2 : MouseChord::X1;
    MouseChord chord = ComposeMouseChord(base);
    if(m_owner.HandleMouseAction(MouseTarget::Anywhere, chord)) {
        bHandled = TRUE;
        return TRUE;
    }
    bHandled = FALSE;
    return FALSE;
}

LRESULT NativeTabControl::OnXButtonUp(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    return 0;
}

LRESULT NativeTabControl::OnRButtonUp(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
    bHandled = TRUE;
    POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    POINT screen = pt;
    ::ClientToScreen(m_hWnd, &screen);
    auto tabIndex = HitTestTab(pt);
    RequestContextMenu(tabIndex, screen);
    return 0;
}

LRESULT NativeTabControl::OnMouseWheel(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    if(m_tabs.empty()) {
        return 0;
    }
    int delta = GET_WHEEL_DELTA_WPARAM(wParam);
    if(delta > 0) {
        std::size_t next = (m_activeIndex + m_tabs.size() - 1) % m_tabs.size();
        RequestSelectTab(next);
    } else if(delta < 0) {
        std::size_t next = (m_activeIndex + 1) % m_tabs.size();
        RequestSelectTab(next);
    }
    return 0;
}

LRESULT NativeTabControl::OnGetDlgCode(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    return DLGC_WANTARROWS | DLGC_WANTCHARS;
}

void NativeTabControl::LayoutTabs() {
    if(m_hWnd == nullptr) {
        return;
    }
    RECT rc{};
    ::GetClientRect(m_hWnd, &rc);
    int x = rc.left + 4;
    int y = rc.top + 4;
    int rowHeight = m_tabHeight;
    int available = rc.right - rc.left - 8;
    int plusWidth = m_showPlusButton ? rowHeight : 0;

    for(std::size_t i = 0; i < m_tabs.size(); ++i) {
        TabItem& tab = m_tabs[i];
        const std::wstring& display = tab.alias.empty() ? tab.title : tab.alias;
        SIZE textSize = MeasureTitle(display);
        int width = textSize.cx + m_horizontalPadding;
        if(m_config.tabs.showFolderIcon) {
            width += m_iconSize + 4;
        }
        width = std::clamp(width, m_tabMinWidth, m_tabMaxWidth);
        if(m_config.tabs.multipleTabRows && (x + width + plusWidth > available + rc.left) && (x != rc.left + 4)) {
            x = rc.left + 4;
            y += rowHeight + 4;
        }
        tab.metrics.bounds = {x, y, x + width, y + rowHeight};
        tab.metrics.closeButton = tab.metrics.bounds;
        tab.metrics.closeButton.left = tab.metrics.bounds.right - (rowHeight - 4);
        tab.metrics.closeButton.right = tab.metrics.bounds.right - 4;
        int closeHeight = rowHeight - 8;
        tab.metrics.closeButton.top = tab.metrics.bounds.top + (rowHeight - closeHeight) / 2;
        tab.metrics.closeButton.bottom = tab.metrics.closeButton.top + closeHeight;
        x += width + m_spacing;
    }

    if(m_showPlusButton) {
        int plusSize = rowHeight - 4;
        if(x + plusSize > rc.right - 4) {
            x = rc.right - plusSize - 4;
        }
        m_plusButtonRect = {x, y, x + plusSize, y + plusSize};
    } else {
        ::SetRectEmpty(&m_plusButtonRect);
    }
}

void NativeTabControl::DrawControl(HDC hdc) const {
    RECT rc{};
    ::GetClientRect(m_hWnd, &rc);

    HDC memDC = ::CreateCompatibleDC(hdc);
    HBITMAP memBmp = ::CreateCompatibleBitmap(hdc, rc.right - rc.left, rc.bottom - rc.top);
    HGDIOBJ oldBmp = ::SelectObject(memDC, memBmp);

    HBRUSH background = ::CreateSolidBrush(ToColorRef(m_config.skin.rebarColor.argb));
    ::FillRect(memDC, &rc, background);
    ::DeleteObject(background);

    HFONT fontToUse = m_font ? m_font : static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
    HGDIOBJ oldFont = ::SelectObject(memDC, fontToUse);

    for(const auto& tab : m_tabs) {
        bool hot = m_hotIndex && *m_hotIndex < m_tabs.size() && *m_hotIndex == (&tab - &m_tabs[0]);
        DrawTab(memDC, tab, hot);
    }

    if(m_showPlusButton && !::IsRectEmpty(&m_plusButtonRect)) {
        DrawPlusButton(memDC);
    }

    ::SelectObject(memDC, oldFont);
    ::BitBlt(hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, memDC, 0, 0, SRCCOPY);
    ::SelectObject(memDC, oldBmp);
    ::DeleteObject(memBmp);
    ::DeleteDC(memDC);
}

void NativeTabControl::DrawTab(HDC hdc, const TabItem& tab, bool hot) const {
    RECT bounds = tab.metrics.bounds;
    COLORREF bgColor = ToColorRef(tab.active ? m_config.skin.tabShadActiveColor.argb
                                             : (hot ? m_config.skin.tabShadHotColor.argb
                                                    : m_config.skin.tabShadInactiveColor.argb));
    HBRUSH brush = ::CreateSolidBrush(bgColor);
    ::FillRect(hdc, &bounds, brush);
    ::DeleteObject(brush);

    COLORREF textColor = ToColorRef(tab.active ? m_config.skin.tabTextActiveColor.argb
                                               : (hot ? m_config.skin.tabTextHotColor.argb
                                                      : m_config.skin.tabTextInactiveColor.argb));
    HFONT fontToUse = tab.active && m_boldFont ? m_boldFont : (m_font ? m_font : static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT)));
    HGDIOBJ oldFont = ::SelectObject(hdc, fontToUse);
    ::SetTextColor(hdc, textColor);
    ::SetBkMode(hdc, TRANSPARENT);

    RECT textRc = bounds;
    textRc.left += 8;
    if(m_config.tabs.showFolderIcon && tab.icon) {
        int iconY = bounds.top + (bounds.bottom - bounds.top - m_iconSize) / 2;
        ::DrawIconEx(hdc, textRc.left, iconY, tab.icon, m_iconSize, m_iconSize, 0, nullptr, DI_NORMAL);
        textRc.left += m_iconSize + 4;
    }
    if(m_config.tabs.showCloseButtons && !tab.locked) {
        textRc.right = tab.metrics.closeButton.left - 4;
    } else {
        textRc.right -= 8;
    }

    const std::wstring& display = tab.alias.empty() ? tab.title : tab.alias;
    ::DrawTextW(hdc, display.c_str(), static_cast<int>(display.size()), &textRc,
                DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

    ::SelectObject(hdc, oldFont);

    if(m_config.tabs.showCloseButtons && !tab.locked) {
        bool showButton = !m_config.tabs.closeBtnsOnHover || hot || tab.active;
        if(showButton) {
            DrawCloseButton(hdc, tab.metrics.closeButton, tab.closeHovered, tab.closePressed);
        }
    }
}

void NativeTabControl::DrawCloseButton(HDC hdc, const RECT& bounds, bool hot, bool pressed) const {
    COLORREF border = ToColorRef(hot ? m_config.skin.tabTextHotColor.argb : m_config.skin.tabTextInactiveColor.argb);
    if(pressed) {
        HBRUSH brush = ::CreateSolidBrush(border);
        ::FillRect(hdc, &bounds, brush);
        ::DeleteObject(brush);
    } else if(hot) {
        HBRUSH brush = ::CreateSolidBrush(border);
        RECT fill = bounds;
        ::InflateRect(&fill, -1, -1);
        ::FillRect(hdc, &fill, brush);
        ::DeleteObject(brush);
    }
    HPEN pen = ::CreatePen(PS_SOLID, 1, ToColorRef(m_config.skin.tabTextActiveColor.argb));
    HGDIOBJ oldPen = ::SelectObject(hdc, pen);
    POINT tl{bounds.left + 4, bounds.top + 4};
    POINT br{bounds.right - 4, bounds.bottom - 4};
    ::MoveToEx(hdc, tl.x, tl.y, nullptr);
    ::LineTo(hdc, br.x, br.y);
    ::MoveToEx(hdc, tl.x, br.y, nullptr);
    ::LineTo(hdc, br.x, tl.y);
    ::SelectObject(hdc, oldPen);
    ::DeleteObject(pen);
}

void NativeTabControl::DrawPlusButton(HDC hdc) const {
    HPEN pen = ::CreatePen(PS_SOLID, 2, ToColorRef(m_config.skin.tabTextHotColor.argb));
    HGDIOBJ oldPen = ::SelectObject(hdc, pen);
    POINT center = CenterRect(m_plusButtonRect);
    int length = (m_plusButtonRect.right - m_plusButtonRect.left) / 3;
    ::MoveToEx(hdc, center.x - length, center.y, nullptr);
    ::LineTo(hdc, center.x + length, center.y);
    ::MoveToEx(hdc, center.x, center.y - length, nullptr);
    ::LineTo(hdc, center.x, center.y + length);
    ::SelectObject(hdc, oldPen);
    ::DeleteObject(pen);
    ::Rectangle(hdc, m_plusButtonRect.left, m_plusButtonRect.top, m_plusButtonRect.right, m_plusButtonRect.bottom);
}

std::wstring NativeTabControl::NormalizePath(const std::wstring& path) const {
    return path;
}

std::wstring NativeTabControl::ExtractTitle(const std::wstring& path) const {
    if(path.empty()) {
        return path;
    }
    wchar_t buffer[MAX_PATH];
    ::StringCchCopyW(buffer, ARRAYSIZE(buffer), path.c_str());
    ::PathStripPathW(buffer);
    if(buffer[0] != L'\0') {
        return buffer;
    }
    return path;
}

SIZE NativeTabControl::MeasureTitle(const std::wstring& text) const {
    SIZE size{0, 0};
    HDC hdc = ::GetDC(m_hWnd);
    HFONT fontToUse = m_font ? m_font : static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
    HGDIOBJ oldFont = ::SelectObject(hdc, fontToUse);
    ::GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &size);
    ::SelectObject(hdc, oldFont);
    ::ReleaseDC(m_hWnd, hdc);
    return size;
}

void NativeTabControl::EnsureIcon(TabItem& tab) {
    if(!m_config.tabs.showFolderIcon || tab.icon != nullptr) {
        return;
    }
    SHFILEINFOW sfi{};
    if(::SHGetFileInfoW(tab.path.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON)) {
        tab.icon = sfi.hIcon;
    }
}

void NativeTabControl::DestroyIcon(TabItem& tab) {
    if(tab.icon) {
        ::DestroyIcon(tab.icon);
        tab.icon = nullptr;
    }
}

std::optional<std::size_t> NativeTabControl::HitTestTab(POINT clientPt) const {
    for(std::size_t i = 0; i < m_tabs.size(); ++i) {
        if(::PtInRect(&m_tabs[i].metrics.bounds, clientPt)) {
            return i;
        }
    }
    return std::nullopt;
}

bool NativeTabControl::HitTestClose(const TabItem& tab, POINT clientPt) const {
    if(tab.locked) {
        return false;
    }
    return ::PtInRect(&tab.metrics.closeButton, clientPt) != FALSE;
}

bool NativeTabControl::HitTestPlus(POINT clientPt) const {
    return m_showPlusButton && ::PtInRect(&m_plusButtonRect, clientPt);
}

void NativeTabControl::StartMouseTracking() {
    if(m_trackingMouse) {
        return;
    }
    TRACKMOUSEEVENT tme{};
    tme.cbSize = sizeof(tme);
    tme.dwFlags = TME_LEAVE;
    tme.hwndTrack = m_hWnd;
    if(::TrackMouseEvent(&tme)) {
        m_trackingMouse = true;
    }
}

void NativeTabControl::StopMouseTracking() {
    if(!m_trackingMouse) {
        return;
    }
    TRACKMOUSEEVENT tme{};
    tme.cbSize = sizeof(tme);
    tme.dwFlags = TME_CANCEL | TME_LEAVE;
    tme.hwndTrack = m_hWnd;
    ::TrackMouseEvent(&tme);
    m_trackingMouse = false;
}

void NativeTabControl::RequestSelectTab(std::size_t index) {
    m_owner.OnTabControlTabSelected(index);
}

void NativeTabControl::RequestCloseTab(std::size_t index) {
    m_owner.OnTabControlCloseRequested(index);
}

void NativeTabControl::RequestContextMenu(std::optional<std::size_t> index, const POINT& screenPoint) {
    m_owner.OnTabControlContextMenuRequested(index, screenPoint);
}

void NativeTabControl::RequestNewTab() {
    m_owner.OnTabControlNewTabRequested();
}

void NativeTabControl::RequestBeginDrag(std::size_t index, const POINT& screenPoint) {
    m_owner.OnTabControlBeginDrag(index, screenPoint);
}

void NativeTabControl::UpdateHoverState(std::optional<std::size_t> newHotIndex, POINT clientPt) {
    if(newHotIndex == m_hotIndex) {
        if(newHotIndex) {
            UpdateCloseHover(newHotIndex, clientPt);
            RECT bounds = m_tabs[*newHotIndex].metrics.bounds;
            POINT anchor{bounds.left, bounds.bottom};
            ::ClientToScreen(m_hWnd, &anchor);
            m_owner.OnTabControlHoverChanged(newHotIndex, anchor);
        }
        return;
    }
    if(m_hotIndex && *m_hotIndex < m_tabs.size()) {
        m_tabs[*m_hotIndex].hovered = false;
        m_tabs[*m_hotIndex].closeHovered = false;
    }
    m_hotIndex = newHotIndex;
    if(m_hotIndex && *m_hotIndex < m_tabs.size()) {
        m_tabs[*m_hotIndex].hovered = true;
        UpdateCloseHover(m_hotIndex, clientPt);
        RECT bounds = m_tabs[*m_hotIndex].metrics.bounds;
        POINT anchor{bounds.left, bounds.bottom};
        ::ClientToScreen(m_hWnd, &anchor);
        m_owner.OnTabControlHoverChanged(m_hotIndex, anchor);
    } else {
        POINT emptyPoint{0, 0};
        m_owner.OnTabControlHoverChanged(std::nullopt, emptyPoint);
    }
    ::InvalidateRect(m_hWnd, nullptr, FALSE);
}

void NativeTabControl::UpdateCloseHover(std::optional<std::size_t> newHotIndex, POINT clientPt) {
    if(!newHotIndex || *newHotIndex >= m_tabs.size()) {
        return;
    }
    TabItem& tab = m_tabs[*newHotIndex];
    bool hit = HitTestClose(tab, clientPt);
    if(tab.closeHovered != hit) {
        tab.closeHovered = hit;
        ::InvalidateRect(m_hWnd, &tab.metrics.bounds, FALSE);
    }
}
