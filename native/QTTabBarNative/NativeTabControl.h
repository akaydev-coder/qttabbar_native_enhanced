#pragma once

#include <atlbase.h>
#include <atlwin.h>

#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "Config.h"

class TabBarHost;

class NativeTabControl final : public CWindowImpl<NativeTabControl, CWindow, CControlWinTraits> {
public:
    struct TabMetrics {
        RECT bounds{};
        RECT closeButton{};
    };

    struct TabItem {
        std::wstring path;
        std::wstring title;
        std::wstring alias;
        HICON icon = nullptr;
        TabMetrics metrics{};
        bool active = false;
        bool hovered = false;
        bool closeHovered = false;
        bool closePressed = false;
        bool locked = false;
    };

    struct SwitchEntry {
        std::wstring display;
        std::wstring path;
        HICON icon = nullptr;
        bool locked = false;
    };

    explicit NativeTabControl(TabBarHost& owner) noexcept;
    ~NativeTabControl() override;

    DECLARE_WND_CLASS_EX(L"QTTabBarNative_NativeTabControl", CS_DBLCLKS, COLOR_WINDOW);

    BEGIN_MSG_MAP(NativeTabControl)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
        MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
        MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
        MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDblClk)
        MESSAGE_HANDLER(WM_MBUTTONDOWN, OnMButtonDown)
        MESSAGE_HANDLER(WM_MBUTTONUP, OnMButtonUp)
        MESSAGE_HANDLER(WM_MBUTTONDBLCLK, OnMButtonDblClk)
        MESSAGE_HANDLER(WM_XBUTTONDOWN, OnXButtonDown)
        MESSAGE_HANDLER(WM_XBUTTONUP, OnXButtonUp)
        MESSAGE_HANDLER(WM_RBUTTONUP, OnRButtonUp)
        MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
        MESSAGE_HANDLER(WM_GETDLGCODE, OnGetDlgCode)
    END_MSG_MAP()

    std::size_t AddTab(const std::wstring& path, bool makeActive, bool allowDuplicate);
    std::wstring ActivateTab(std::size_t index);
    std::wstring ActivateNextTab();
    std::wstring ActivatePreviousTab();
    std::optional<std::wstring> CloseTab(std::size_t index);
    std::optional<std::wstring> CloseActiveTab();
    std::vector<std::wstring> CloseAllExcept(std::size_t index);
    std::vector<std::wstring> CloseTabsToLeft(std::size_t index);
    std::vector<std::wstring> CloseTabsToRight(std::size_t index);

    std::optional<std::wstring> GetActivePath() const;
    std::size_t GetActiveIndex() const noexcept;
    std::vector<std::wstring> GetTabPaths() const;
    std::wstring GetPath(std::size_t index) const;
    std::vector<SwitchEntry> GetSwitchEntries();
    bool IsLocked(std::size_t index) const;
    bool CanCloseTab(std::size_t index) const;
    bool HasClosableTabsToLeft(std::size_t index) const;
    bool HasClosableTabsToRight(std::size_t index) const;
    bool HasClosableOtherTabs(std::size_t index) const;
    void SetLocked(std::size_t index, bool locked);
    void SetAlias(std::size_t index, const std::wstring& alias);
    std::size_t GetCount() const noexcept { return m_tabs.size(); }

    std::optional<RECT> GetTabBounds(std::size_t index) const;
    std::vector<std::wstring> GetTabDisplayNames() const;

    void ApplyConfiguration(const qttabbar::ConfigData& config);
    void RefreshMetrics();

    void FocusTabBar();
    void EnsureLayout();

    void SetPlusButtonVisible(bool visible, bool persist = true);
    bool PlusButtonVisible() const noexcept { return m_showPlusButton; }

    void NotifyExplorerPathChanged(const std::wstring& path);

private:
    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnMouseLeave(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnLButtonDblClk(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnMButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnMButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnMButtonDblClk(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnXButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnXButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnRButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnMouseWheel(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnGetDlgCode(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    void LayoutTabs();
    void DrawControl(HDC hdc) const;
    void DrawTab(HDC hdc, const TabItem& tab, bool hot) const;
    void DrawCloseButton(HDC hdc, const RECT& bounds, bool hot, bool pressed) const;
    void DrawPlusButton(HDC hdc) const;

    std::wstring NormalizePath(const std::wstring& path) const;
    std::wstring ExtractTitle(const std::wstring& path) const;
    SIZE MeasureTitle(const std::wstring& text) const;
    void EnsureIcon(TabItem& tab);
    void DestroyIcon(TabItem& tab);

    std::optional<std::size_t> HitTestTab(POINT clientPt) const;
    bool HitTestClose(const TabItem& tab, POINT clientPt) const;
    bool HitTestPlus(POINT clientPt) const;
    void StartMouseTracking();
    void StopMouseTracking();

    void RequestSelectTab(std::size_t index);
    void RequestCloseTab(std::size_t index);
    void RequestContextMenu(std::optional<std::size_t> index, const POINT& screenPoint);
    void RequestNewTab();
    void RequestBeginDrag(std::size_t index, const POINT& screenPoint);

    void UpdateHoverState(std::optional<std::size_t> newHotIndex, POINT clientPt);
    void UpdateCloseHover(std::optional<std::size_t> newHotIndex, POINT clientPt);

    TabBarHost& m_owner;
    std::vector<TabItem> m_tabs;
    qttabbar::ConfigData m_config;
    HFONT m_font = nullptr;
    HFONT m_boldFont = nullptr;
    int m_iconSize = 16;
    int m_tabHeight = 28;
    int m_tabMinWidth = 80;
    int m_tabMaxWidth = 200;
    int m_horizontalPadding = 12;
    int m_spacing = 6;
    bool m_showPlusButton = false;
    RECT m_plusButtonRect{};

    std::optional<std::size_t> m_hotIndex;
    std::optional<std::size_t> m_pressedTab;
    std::optional<std::size_t> m_pressedClose;
    bool m_trackingMouse = false;

    std::size_t m_activeIndex = 0;
};
