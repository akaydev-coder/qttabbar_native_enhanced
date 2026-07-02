#pragma once

#include "QTTabBarNativeGuids.h"
#include "resource.h"

#include <atlbase.h>
#include <atlcom.h>
#include <atlwin.h>
#include <exdisp.h>

#include <functional>
#include <unordered_map>
#include <vector>

class InstanceManagerNative;

class ATL_NO_VTABLE QTButtonBar
    : public CComObjectRootEx<CComMultiThreadModel>
    , public CComCoClass<QTButtonBar, &CLSID_QTButtonBar>
    , public IDeskBand
    , public IInputObject
    , public IObjectWithSite
    , public IPersistStream
    , public CWindowImpl<QTButtonBar, CWindow, CControlWinTraits> {
public:
    QTButtonBar() noexcept;
    ~QTButtonBar() override;

    DECLARE_REGISTRY_RESOURCEID(IDR_QTBUTTONBAR)
    DECLARE_NOT_AGGREGATABLE(QTButtonBar)
    DECLARE_PROTECT_FINAL_CONSTRUCT()
    DECLARE_WND_CLASS_EX(L"QTButtonBarWindow", CS_DBLCLKS, COLOR_WINDOW)

    BEGIN_COM_MAP(QTButtonBar)
        COM_INTERFACE_ENTRY(IDeskBand)
        COM_INTERFACE_ENTRY2(IDockingWindow, IDeskBand)
        COM_INTERFACE_ENTRY(IInputObject)
        COM_INTERFACE_ENTRY(IObjectWithSite)
        COM_INTERFACE_ENTRY2(IOleWindow, IDeskBand)
        COM_INTERFACE_ENTRY(IPersist)
        COM_INTERFACE_ENTRY(IPersistStream)
    END_COM_MAP()

    BEGIN_MSG_MAP(QTButtonBar)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_COMMAND, OnCommand)
        MESSAGE_HANDLER(WM_NOTIFY, OnNotify)
    END_MSG_MAP()

    HRESULT FinalConstruct();
    void FinalRelease();

    bool InvokeCommand(UINT commandId);

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

private:
    struct ButtonDefinition {
        int index;
        UINT commandId;
        const wchar_t* text;
        bool dropdown;
        bool checkable;
    };

    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCommand(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    HRESULT EnsureWindow();
    void DestroyChildWindows();
    void InitializeToolbar();
    void CreateToolbarButtons();
    void UpdateLayout(int width, int height);
    void HandleButtonCommand(UINT commandId);
    void ShowDropdown(UINT commandId, const POINT& screenPoint);
    void BuildNavigationMenu(HMENU menu);
    void BuildGroupsMenu(HMENU menu);
    void BuildRecentTabsMenu(HMENU menu);
    void BuildRecentFilesMenu(HMENU menu);
    void BuildApplicationsMenu(HMENU menu);
    void BuildPluginsMenu(HMENU menu);
    void BuildMiscToolsMenu(HMENU menu);
    void ExecuteMenuCommand(UINT commandId);
    void ClearMenuHandlers();
    bool TryAllocateDynamicCommand(UINT& commandId);
    void AppendOverflowPlaceholder(HMENU menu) const;

    HWND ExplorerWindowFromBrowser() const;
    void RegisterWithInstanceManager();
    void UnregisterWithInstanceManager();

    static const std::vector<ButtonDefinition>& DefaultButtons();

    CComPtr<IInputObjectSite> m_spInputObjectSite;
    CComPtr<IServiceProvider> m_spServiceProvider;
    CComPtr<IWebBrowser2> m_spExplorer;
    CComPtr<IUnknown> m_spSite;

    HWND m_hwndRebar;
    HWND m_hwndToolbar;
    HWND m_hwndSearch;
    HWND m_explorerHwnd;

    SIZE m_minSize;
    SIZE m_maxSize;
    bool m_closed;
    bool m_visible;

    UINT m_bandId;
    UINT m_nextDynamicCommand;
    std::unordered_map<UINT, std::function<void()>> m_menuHandlers;
};
