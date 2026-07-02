#pragma once

#include "QTTabBarNativeGuids.h"
#include "resource.h"
#include "HookMessages.h"
#include "ConfigEnums.h"
#include "TabBarHost.h"

#include <optional>

class TabBarHost;

class RebarBreakFixer;

class BreadcrumbBarHelper;

class ATL_NO_VTABLE QTTabBarClass
    : public CComObjectRootEx<CComMultiThreadModel>
    , public CComCoClass<QTTabBarClass, &CLSID_QTTabBarClass>
    , public IQTTabBarClass
    , public IDeskBand
    , public IInputObject
    , public IObjectWithSite
    , public IPersistStream
    , public CWindowImpl<QTTabBarClass, CWindow, CControlWinTraits>
    , public ITabBarHostOwner
{
public:
    static constexpr int kButtonSeparatorIndex = 0;
    static constexpr int kButtonNavigationBackIndex = 1;
    static constexpr int kButtonNavigationForwardIndex = 2;
    static constexpr int kButtonGroupIndex = 3;
    static constexpr int kButtonRecentTabsIndex = 4;
    static constexpr int kButtonApplicationsIndex = 5;
    static constexpr int kButtonNewWindowIndex = 6;
    static constexpr int kButtonCloneTabIndex = 7;
    static constexpr int kButtonLockTabIndex = 8;
    static constexpr int kButtonMiscToolsIndex = 9;
    static constexpr int kButtonTopMostIndex = 10;
    static constexpr int kButtonCloseCurrentIndex = 11;
    static constexpr int kButtonCloseOthersIndex = 12;
    static constexpr int kButtonCloseWindowIndex = 13;
    static constexpr int kButtonCloseLeftIndex = 14;
    static constexpr int kButtonCloseRightIndex = 15;
    static constexpr int kButtonGoUpIndex = 16;
    static constexpr int kButtonRefreshIndex = 17;
    static constexpr int kButtonSearchIndex = 18;
    static constexpr int kButtonWindowOpacityIndex = 19;
    static constexpr int kButtonFilterBarIndex = 20;
    static constexpr int kButtonOptionsIndex = 21;
    static constexpr int kButtonRecentFilesIndex = 22;
    static constexpr int kButtonPluginsIndex = 23;

    QTTabBarClass() noexcept;
    ~QTTabBarClass() override;

    DECLARE_REGISTRY_RESOURCEID(IDR_QTTABBARCLASS)
    DECLARE_NOT_AGGREGATABLE(QTTabBarClass)
    DECLARE_PROTECT_FINAL_CONSTRUCT()
    DECLARE_WND_CLASS_EX(L"QTTabBarClassWindow", CS_DBLCLKS, COLOR_WINDOW)

    BEGIN_COM_MAP(QTTabBarClass)
        COM_INTERFACE_ENTRY(IQTTabBarClass)
        COM_INTERFACE_ENTRY(IDeskBand)
        COM_INTERFACE_ENTRY2(IDockingWindow, IDeskBand)
        COM_INTERFACE_ENTRY(IInputObject)
        COM_INTERFACE_ENTRY(IObjectWithSite)
        COM_INTERFACE_ENTRY2(IOleWindow, IDeskBand)
        COM_INTERFACE_ENTRY(IPersist)
        COM_INTERFACE_ENTRY(IPersistStream)
    END_COM_MAP()

    BEGIN_MSG_MAP(QTTabBarClass)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
        MESSAGE_HANDLER(WM_COMMAND, OnCommand)
        MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
        MESSAGE_HANDLER(WM_KILLFOCUS, OnKillFocus)
        MESSAGE_HANDLER(qttabbar::hooks::WM_APP_CAPTURE_NEW_WINDOW, OnCaptureNewWindow)
        MESSAGE_HANDLER(qttabbar::hooks::WM_APP_TRAY_SELECT, OnTraySelection)
        MESSAGE_HANDLER(WM_APP_UNSUBCLASS, OnUnsetRebarMonitor)
    END_MSG_MAP()

    HRESULT FinalConstruct();
    void FinalRelease();

    // IOleWindow
    IFACEMETHODIMP GetWindow(HWND* phwnd) override;
    IFACEMETHODIMP ContextSensitiveHelp(BOOL fEnterMode) override;

    // IDeskBand / IDockingWindow
    IFACEMETHODIMP ShowDW(BOOL fShow) override;
    IFACEMETHODIMP CloseDW(DWORD dwReserved) override;
    IFACEMETHODIMP ResizeBorderDW(LPCRECT prcBorder, IUnknown* punkToolbarSite, BOOL fReserved) override;
    IFACEMETHODIMP GetBandInfo(DWORD dwBandID, DWORD dwViewMode, DESKBANDINFO* pdbi) override;

    // IInputObject
    IFACEMETHODIMP UIActivateIO(BOOL fActivate, MSG* pMsg) override;
    IFACEMETHODIMP HasFocusIO() override;
    IFACEMETHODIMP TranslateAcceleratorIO(MSG* pMsg) override;

    // IObjectWithSite
    IFACEMETHODIMP SetSite(IUnknown* pUnkSite) override;
    IFACEMETHODIMP GetSite(REFIID riid, void** ppvSite) override;

    // IPersist / IPersistStream
    IFACEMETHODIMP GetClassID(CLSID* pClassID) override;
    IFACEMETHODIMP IsDirty() override;
    IFACEMETHODIMP Load(IStream* pStm) override;
    IFACEMETHODIMP Save(IStream* pStm, BOOL fClearDirty) override;
    IFACEMETHODIMP GetSizeMax(ULARGE_INTEGER* pcbSize) override;

    // ITabBarHostOwner
    HWND GetHostWindow() const noexcept override;
    HWND GetHostRebarWindow() const noexcept override;
    void NotifyTabHostFocusChange(BOOL hasFocus) override;

    // Window message handlers
    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnContextMenu(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCommand(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSetFocus(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnKillFocus(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnUnsetRebarMonitor(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCaptureNewWindow(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTraySelection(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    void HandleButtonCommand(UINT commandId);
    std::vector<std::wstring> GetOpenTabs() const;
    std::vector<std::wstring> GetClosedTabHistory() const;
    void ActivateTabByIndex(std::size_t index);
    void RestoreClosedTabByIndex(std::size_t index);
    void OpenGroupByIndex(std::size_t index);
    HWND GetWindowHandle() const noexcept { return m_hWnd; }
    std::wstring GetCurrentPath() const;
    bool ExecuteBindAction(qttabbar::BindAction action, bool isRepeat = false,
                           std::optional<std::size_t> tabIndex = std::nullopt);

private:
    HRESULT EnsureWindow();
    void DestroyTimers();
    void InitializeTimers();
    void EnsureRebarSubclass();
    void ReleaseRebarSubclass();
    void UpdateVisibility(BOOL fShow);
    void NotifyFocusChange(BOOL hasFocus);
    void StartDeferredRebarReset();
    void PersistBreakPreference() const;
    void InitializeBreadcrumbBar();
    void ResetBreadcrumbBar();
    bool HandleBreadcrumbClick(PCIDLIST_ABSOLUTE pidl, UINT modifiers, bool middle);

    bool ShouldHaveBreak() const;
    int ActiveRebarCount() const;
    REBARBANDINFOW GetRebarBand(int index, UINT mask) const;
    bool BandHasBreak() const;

    static constexpr UINT kSelectTabTimerMs = 5000;
    static constexpr UINT kShowMenuTimerMs = 0x4B0; // 1200ms

    CComPtr<IInputObjectSite> m_spInputObjectSite;
    CComPtr<IServiceProvider> m_spServiceProvider;
    CComPtr<IWebBrowser2> m_spExplorer;
    CComPtr<IUnknown> m_spSite;

    HWND m_hwndRebar;
    std::unique_ptr<RebarBreakFixer> m_rebarSubclass;
    std::unique_ptr<TabBarHost> m_tabHost;
    std::unique_ptr<BreadcrumbBarHelper> m_breadcrumbBar;

    HWND m_explorerHwnd;
    SIZE m_minSize;
    SIZE m_maxSize;
    bool m_closed;
    bool m_visible;
    bool m_vertical;
    DWORD m_bandId;

    friend class TabBarHost;
    friend class RebarBreakFixer;
};
