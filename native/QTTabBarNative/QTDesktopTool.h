#pragma once

#include "QTTabBarNativeGuids.h"
#include "resource.h"
#include "HookManagerNative.h"
#include "Config.h"

#include <array>
#include <atomic>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

#include <atlbase.h>
#include <atlcom.h>
#include <atlctl.h>
#include <atlwin.h>

class QTTabBarClass;

class ATL_NO_VTABLE QTDesktopTool
    : public CComObjectRootEx<CComMultiThreadModel>
    , public CComCoClass<QTDesktopTool, &CLSID_QTDesktopTool>
    , public IQTDesktopTool
    , public IDeskBand
    , public IInputObject
    , public IObjectWithSite
    , public IPersistStream
    , public CWindowImpl<QTDesktopTool, CWindow, CControlWinTraits>
    , public qttabbar::hooks::IHookEventSink
{
public:
    QTDesktopTool() noexcept;
    ~QTDesktopTool() override;

    void InvalidateModel();

    DECLARE_REGISTRY_RESOURCEID(IDR_QTDESKTOPTOOL)
    DECLARE_NOT_AGGREGATABLE(QTDesktopTool)
    DECLARE_PROTECT_FINAL_CONSTRUCT()
    DECLARE_WND_CLASS_EX(L"QTDesktopToolWindow", CS_DBLCLKS, COLOR_WINDOW)

    BEGIN_COM_MAP(QTDesktopTool)
        COM_INTERFACE_ENTRY(IQTDesktopTool)
        COM_INTERFACE_ENTRY(IDeskBand)
        COM_INTERFACE_ENTRY2(IDockingWindow, IDeskBand)
        COM_INTERFACE_ENTRY(IInputObject)
        COM_INTERFACE_ENTRY(IObjectWithSite)
        COM_INTERFACE_ENTRY2(IOleWindow, IDeskBand)
        COM_INTERFACE_ENTRY(IPersist)
        COM_INTERFACE_ENTRY(IPersistStream)
    END_COM_MAP()

    BEGIN_MSG_MAP(QTDesktopTool)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
        MESSAGE_HANDLER(WM_APP_MODEL_READY, OnModelReady)
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

private:
    enum class SectionKind : int {
        Group = 0,
        RecentTabs = 1,
        Applications = 2,
        RecentFiles = 3,
    };

    enum class DesktopCommandType {
        None,
        RestoreClosedTab,
        ActivateTab,
        LaunchApplication,
        OpenRecentFile,
        InvokeGroup,
        ToggleLockMenu,
        ToggleOneClick,
        ToggleAppShortcuts,
        ToggleIncludeSection,
        ToggleExpandSection,
        MoveSectionUp,
        MoveSectionDown,
    };

    struct MenuItemModel {
        std::wstring text;
        SectionKind section = SectionKind::Group;
        DesktopCommandType command = DesktopCommandType::None;
        size_t index = 0;
        std::wstring payload;
        bool enabled = true;
        bool separator = false;
    };

    struct SectionModel {
        SectionKind kind = SectionKind::Group;
        std::wstring title;
        bool expanded = false;
        std::vector<MenuItemModel> items;
    };

    struct DesktopAction {
        DesktopCommandType type = DesktopCommandType::None;
        std::optional<SectionKind> section;
        size_t index = 0;
        std::wstring payload;
    };

    static constexpr UINT WM_APP_MODEL_READY = WM_APP + 0x31;
    static constexpr UINT kDefaultHeight = 320;

    LRESULT OnCreate(UINT msg, WPARAM wParam, LPARAM lParam, BOOL& handled);
    LRESULT OnDestroy(UINT msg, WPARAM wParam, LPARAM lParam, BOOL& handled);
    LRESULT OnContextMenu(UINT msg, WPARAM wParam, LPARAM lParam, BOOL& handled);
    LRESULT OnModelReady(UINT msg, WPARAM wParam, LPARAM lParam, BOOL& handled);

    void EnsureThreads();
    void StopThreads();
    void TaskbarThreadMain();
    void DesktopThreadMain();

    void ScheduleMenuRebuild();
    std::vector<SectionModel> BuildMenuModelSnapshot(const qttabbar::DesktopSettings& settings) const;
    SectionModel BuildSectionModel(SectionKind kind, const qttabbar::DesktopSettings& settings) const;
    std::vector<MenuItemModel> BuildGroupItems() const;
    std::vector<MenuItemModel> BuildRecentTabItems() const;
    std::vector<MenuItemModel> BuildApplicationItems() const;
    std::vector<MenuItemModel> BuildRecentFileItems() const;

    HMENU BuildPopupMenu();
    HMENU BuildSettingsMenu();
    void AppendSettingsToggles(HMENU menu, const qttabbar::DesktopSettings& settings);
    void AppendSettingsIncludeMenu(HMENU menu, const qttabbar::DesktopSettings& settings);
    void AppendSettingsExpandMenu(HMENU menu, const qttabbar::DesktopSettings& settings);
    void AppendSettingsReorderMenu(HMENU menu, const qttabbar::DesktopSettings& settings);

    std::wstring LoadStringResource(UINT id) const;
    std::wstring GetSectionTitle(SectionKind kind) const;
    std::array<SectionKind, 4> BuildSectionOrder(const qttabbar::DesktopSettings& settings) const;
    static SectionKind IndexToSection(int index);
    static int SectionToIndex(SectionKind section);

    void ResetCommandState();
    UINT GenerateCommandId();
    void RegisterAction(UINT id, DesktopAction action);
    void ExecuteAction(const DesktopAction& action);
    void ToggleSetting(DesktopCommandType type, std::optional<SectionKind> section);
    void MoveSection(SectionKind section, int delta);
    void PersistDesktopSettings();
    void EnsureExplorerWindow();
    QTTabBarClass* ResolveTabBar() const;

    void InitializeHookLibrary();
    void HandleHookResult(int hookId, int retcode);
    BOOL HandleHookNewWindow(PCIDLIST_ABSOLUTE pidl);
    void OnHookResult(int hookId, int retcode) override;
    BOOL OnHookNewWindow(PCIDLIST_ABSOLUTE pidl) override;

    CComPtr<IInputObjectSite> m_spInputObjectSite;
    CComPtr<IServiceProvider> m_spServiceProvider;
    CComPtr<IWebBrowser2> m_spExplorer;
    CComPtr<IUnknown> m_spSite;

    HWND m_hwndRebar = nullptr;
    HWND m_explorerHwnd = nullptr;

    HANDLE m_exitEvent = nullptr;
    HANDLE m_rebuildEvent = nullptr;

    std::thread m_taskbarThread;
    std::thread m_desktopThread;

    mutable std::mutex m_settingsMutex;
    qttabbar::DesktopSettings m_settings;

    mutable std::mutex m_modelMutex;
    std::vector<SectionModel> m_menuModel;
    std::atomic<bool> m_menuReady{false};

    std::unordered_map<UINT, DesktopAction> m_commandActions;
    UINT m_nextCommandId = 0x6000;

    std::atomic<bool> m_closing{false};
    std::atomic<bool> m_hookInitialized{false};
};
