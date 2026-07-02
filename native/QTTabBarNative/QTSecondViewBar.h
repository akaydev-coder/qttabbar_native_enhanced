#pragma once

#include "QTTabBarNativeGuids.h"
#include "Resource.h"

#include <memory>

#include <shobjidl_core.h>

#include "TabBarHost.h"

class QTSecondViewBar;

class ExplorerBrowserHost final : public CWindowImpl<ExplorerBrowserHost, CWindow, CControlWinTraits> {
public:
    DECLARE_WND_CLASS_EX(L"QTTabBarNative_SecondViewBrowserHost", CS_DBLCLKS, COLOR_WINDOW);

    explicit ExplorerBrowserHost(QTSecondViewBar& owner) noexcept;
    ~ExplorerBrowserHost() override;

    BEGIN_MSG_MAP(ExplorerBrowserHost)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
    END_MSG_MAP()

    IExplorerBrowser* GetBrowser() const noexcept { return m_spBrowser; }

private:
    HRESULT EnsureWindow();
    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    void NavigateToDefaultFolder();

    QTSecondViewBar& m_owner;
    CComPtr<IExplorerBrowser> m_spBrowser;
};

class SplitterBar final : public CWindowImpl<SplitterBar, CWindow, CControlWinTraits> {
public:
    DECLARE_WND_CLASS_EX(L"QTTabBarNative_SecondViewSplitter", CS_DBLCLKS, COLOR_WINDOW);

    explicit SplitterBar(QTSecondViewBar& owner) noexcept;

    BEGIN_MSG_MAP(SplitterBar)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
        MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
        MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)
        MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChanged)
    END_MSG_MAP()

private:
    LRESULT OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSetCursor(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCaptureChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDefault(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    void UpdatePositionFromPoint(POINT pt);

    QTSecondViewBar& m_owner;
    bool m_dragging;
};

class ATL_NO_VTABLE QTSecondViewBar
    : public CComObjectRootEx<CComMultiThreadModel>
    , public CComCoClass<QTSecondViewBar, &CLSID_QTSecondViewBar>
    , public IQTSecondViewBar
    , public IDeskBand
    , public IInputObject
    , public IObjectWithSite
    , public IPersistStream
    , public CWindowImpl<QTSecondViewBar, CWindow, CControlWinTraits>
    , public ITabBarHostOwner {
public:
    friend class SplitterBar;

    QTSecondViewBar() noexcept;
    ~QTSecondViewBar() override;

    DECLARE_REGISTRY_RESOURCEID(IDR_QTSECONDVIEWBAR)
    DECLARE_NOT_AGGREGATABLE(QTSecondViewBar)
    DECLARE_PROTECT_FINAL_CONSTRUCT()
    DECLARE_WND_CLASS_EX(L"QTSecondViewBarWindow", CS_DBLCLKS, COLOR_WINDOW)

    BEGIN_COM_MAP(QTSecondViewBar)
        COM_INTERFACE_ENTRY(IQTSecondViewBar)
        COM_INTERFACE_ENTRY(IDeskBand)
        COM_INTERFACE_ENTRY2(IDockingWindow, IDeskBand)
        COM_INTERFACE_ENTRY(IInputObject)
        COM_INTERFACE_ENTRY(IObjectWithSite)
        COM_INTERFACE_ENTRY2(IOleWindow, IDeskBand)
        COM_INTERFACE_ENTRY(IPersist)
        COM_INTERFACE_ENTRY(IPersistStream)
    END_COM_MAP()

    BEGIN_MSG_MAP(QTSecondViewBar)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
        MESSAGE_HANDLER(WM_COMMAND, OnCommand)
        MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
        MESSAGE_HANDLER(WM_KILLFOCUS, OnKillFocus)
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

private:
    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnContextMenu(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCommand(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSetFocus(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnKillFocus(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    void EnsureTabHost();
    HRESULT EnsureWindow();
    void EnsureBrowserHost();
    void DestroyChildren();
    void UpdateLayout();
    void LoadPreferences();
    void SavePreferences() const;
    void SetExplorer(IWebBrowser2* explorer);
    void NotifyFocusChange(BOOL hasFocus);
    void BeginDragResize();
    void EndDragResize();
    void UpdateSplitterFromPoint(POINT pt);

    static constexpr wchar_t kRegistryRoot[] = L"Software\\QTTabBar\\SecondViewBar";
    static constexpr wchar_t kPaneRatioValue[] = L"PaneRatio";
    static constexpr wchar_t kUserResizedValue[] = L"UserResized";

    CComPtr<IInputObjectSite> m_spInputObjectSite;
    CComPtr<IServiceProvider> m_spServiceProvider;
    CComPtr<IWebBrowser2> m_spExplorer;
    CComPtr<IUnknown> m_spSite;

    HWND m_hwndRebar;
    DWORD m_bandId;
    bool m_visible;
    bool m_closed;
    bool m_userResized;
    float m_paneRatio;

    SIZE m_minSize;
    SIZE m_maxSize;

    std::unique_ptr<TabBarHost> m_tabHost;
    std::unique_ptr<ExplorerBrowserHost> m_browserHost;
    std::unique_ptr<SplitterBar> m_splitter;
};
