#pragma once

#include <Windows.h>

#include <functional>
#include <utility>

class BreadcrumbBarHelper {
public:
    static constexpr UINT kModifierShift = 0x01;
    static constexpr UINT kModifierCtrl = 0x02;
    static constexpr UINT kModifierAlt = 0x04;

    using ItemClickedCallback = std::function<bool(PCIDLIST_ABSOLUTE pidl, UINT modifiers, bool middle)>;

    explicit BreadcrumbBarHelper(HWND hwndToolbar);
    ~BreadcrumbBarHelper();

    BreadcrumbBarHelper(const BreadcrumbBarHelper&) = delete;
    BreadcrumbBarHelper& operator=(const BreadcrumbBarHelper&) = delete;

    void SetItemClickedCallback(ItemClickedCallback callback);
    void Reset();

private:
    static LRESULT CALLBACK ToolbarWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK ParentWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    static BreadcrumbBarHelper* LookupToolbar(HWND hwnd);
    static BreadcrumbBarHelper* LookupParent(HWND hwnd);

    bool HandleToolbarMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    bool HandleParentMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    bool HandleMiddleClick(LPARAM lParam);
    bool HandleCommand(WPARAM wParam);
    bool InvokeForCommand(int commandId, bool middle, UINT modifiers);
    bool InvokeForIndex(int index, bool middle, UINT modifiers);

    int ButtonCount() const;
    int CommandToIndex(int commandId) const;
    int HitTest(const POINT& pt) const;
    int IndexToCommand(int index) const;
    LPARAM GetButtonLParam(int commandId) const;

    static UINT CurrentModifiers();

    HWND m_hwndToolbar;
    HWND m_hwndParent;
    WNDPROC m_prevToolbarProc;
    WNDPROC m_prevParentProc;
    HDPA m_hdpa;
    ItemClickedCallback m_callback;

    static UINT s_breadcrumbDpaMessage;
};

