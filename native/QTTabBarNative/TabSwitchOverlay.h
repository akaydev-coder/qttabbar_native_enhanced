#pragma once

#include <atlbase.h>
#include <atlwin.h>

#include <functional>
#include <vector>

class TabSwitchOverlay final : public CWindowImpl<TabSwitchOverlay, CWindow, CControlWinTraits> {
public:
    struct Entry {
        std::wstring display;
        std::wstring path;
        HICON icon = nullptr;
        bool locked = false;
    };

    TabSwitchOverlay() noexcept;
    ~TabSwitchOverlay() override;

    DECLARE_WND_CLASS_EX(L"QTTabBarNative_TabSwitchOverlay", CS_DBLCLKS, COLOR_WINDOW);

    TabSwitchOverlay(const TabSwitchOverlay&) = delete;
    TabSwitchOverlay& operator=(const TabSwitchOverlay&) = delete;

    void SetCommitCallback(std::function<void(std::size_t)> callback);

    bool Show(HWND ownerWindow, std::vector<Entry> entries, std::size_t selectedIndex, std::size_t initialIndex);
    void Hide(bool commit);

    bool IsVisible() const noexcept { return m_visible; }
    std::size_t Cycle(bool reverse);
    std::size_t SelectedIndex() const noexcept { return m_selectedIndex; }

    BEGIN_MSG_MAP(TabSwitchOverlay)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
        MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
        MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
        MESSAGE_HANDLER(WM_MOUSEACTIVATE, OnMouseActivate)
        MESSAGE_HANDLER(WM_NCHITTEST, OnNcHitTest)
        MESSAGE_HANDLER(WM_DWMCOMPOSITIONCHANGED, OnCompositionChanged)
    END_MSG_MAP()

private:
    static constexpr std::size_t kInvalidIndex = static_cast<std::size_t>(-1);

    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnMouseLeave(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnMouseActivate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnNcHitTest(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCompositionChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    void UpdateCompositionState();
    void UpdateLayout();
    void DrawContent(HDC hdc) const;
    void DrawHeader(HDC hdc, const RECT& clientRect) const;
    void DrawRow(HDC hdc, const RECT& rowRect, const Entry& entry, std::size_t index, bool selected,
                 bool hovered, bool initial) const;
    void DrawText(HDC hdc, const RECT& rect, const std::wstring& text, bool selected, bool bold,
                  UINT align) const;
    void DrawTextOnGlass(HDC hdc, const RECT& rect, const std::wstring& text, bool bold, UINT format) const;
    void EnsureMouseTracking();

    HFONT ResolveFont(bool bold) const;

    int Dpi() const noexcept;
    int Scale(int value) const noexcept;

    std::function<void(std::size_t)> m_commitCallback;
    std::vector<Entry> m_entries;
    mutable std::vector<RECT> m_itemRects;
    std::size_t m_selectedIndex;
    std::size_t m_initialIndex;
    std::size_t m_hoverIndex;
    bool m_compositionEnabled;
    bool m_visible;
    int m_menuHeight;
    UINT m_dpi;
    HWND m_ownerWindow;
    HFONT m_font;
    HFONT m_boldFont;
};
