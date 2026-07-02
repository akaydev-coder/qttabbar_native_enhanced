#include "pch.h"

#include "QTDesktopTool.h"

#include "Config.h"
#include "HookLibraryBridge.h"
#include "InstanceManagerNative.h"
#include "QTTabBarClass.h"

#include <atlstr.h>
#include <shellapi.h>
#include <shlwapi.h>

using qttabbar::DesktopSettings;

namespace {

std::wstring ExtractLeafName(const std::wstring& path) {
    if(path.empty()) {
        return path;
    }
    auto pos = path.find_last_of(L"\\/");
    if(pos == std::wstring::npos) {
        return path;
    }
    return path.substr(pos + 1);
}

}  // namespace

QTDesktopTool::QTDesktopTool() noexcept = default;

QTDesktopTool::~QTDesktopTool() = default;

HRESULT QTDesktopTool::FinalConstruct() {
    m_exitEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if(!m_exitEvent) {
        return HRESULT_FROM_WIN32(::GetLastError());
    }

    m_rebuildEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if(!m_rebuildEvent) {
        HRESULT hr = HRESULT_FROM_WIN32(::GetLastError());
        ::CloseHandle(m_exitEvent);
        m_exitEvent = nullptr;
        return hr;
    }

    m_settings = qttabbar::LoadConfigFromRegistry().desktop;
    InstanceManagerNative::Instance().RegisterDesktopTool(this);
    EnsureThreads();
    ScheduleMenuRebuild();
    return S_OK;
}

void QTDesktopTool::FinalRelease() {
    if(m_closing.exchange(true)) {
        return;
    }
    InstanceManagerNative::Instance().UnregisterDesktopTool(this);
    StopThreads();
    if(m_rebuildEvent) {
        ::CloseHandle(m_rebuildEvent);
        m_rebuildEvent = nullptr;
    }
    if(m_exitEvent) {
        ::CloseHandle(m_exitEvent);
        m_exitEvent = nullptr;
    }
}

void QTDesktopTool::InvalidateModel() {
    ScheduleMenuRebuild();
}

LRESULT QTDesktopTool::OnCreate(UINT /*msg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& handled) {
    handled = FALSE;
    EnsureThreads();
    ScheduleMenuRebuild();
    return 0;
}

LRESULT QTDesktopTool::OnDestroy(UINT /*msg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& handled) {
    handled = FALSE;
    FinalRelease();
    return 0;
}

LRESULT QTDesktopTool::OnContextMenu(UINT /*msg*/, WPARAM wParam, LPARAM lParam, BOOL& handled) {
    POINT pt{};
    if(wParam != 0) {
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
    } else {
        if(lParam == static_cast<LPARAM>(-1)) {
            RECT rc{};
            ::GetWindowRect(m_hWnd, &rc);
            pt.x = rc.left;
            pt.y = rc.bottom;
        } else {
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
        }
    }

    HMENU menu = BuildPopupMenu();
    if(!menu) {
        handled = TRUE;
        return 0;
    }

    UINT command = ::TrackPopupMenuEx(menu, TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_RETURNCMD, pt.x, pt.y, m_hWnd, nullptr);
    if(command != 0) {
        auto it = m_commandActions.find(command);
        if(it != m_commandActions.end()) {
            ExecuteAction(it->second);
        }
    }

    ::DestroyMenu(menu);
    handled = TRUE;
    return 0;
}

LRESULT QTDesktopTool::OnModelReady(UINT /*msg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& handled) {
    handled = TRUE;
    return 0;
}

IFACEMETHODIMP QTDesktopTool::GetWindow(HWND* phwnd) {
    if(phwnd == nullptr) {
        return E_POINTER;
    }
    *phwnd = m_hWnd;
    return S_OK;
}

IFACEMETHODIMP QTDesktopTool::ContextSensitiveHelp(BOOL /*fEnterMode*/) {
    return E_NOTIMPL;
}

IFACEMETHODIMP QTDesktopTool::ShowDW(BOOL fShow) {
    if(m_hWnd) {
        ::ShowWindow(m_hWnd, fShow ? SW_SHOW : SW_HIDE);
    }
    return S_OK;
}

IFACEMETHODIMP QTDesktopTool::CloseDW(DWORD /*dwReserved*/) {
    ShowDW(FALSE);
    return S_OK;
}

IFACEMETHODIMP QTDesktopTool::ResizeBorderDW(LPCRECT /*prcBorder*/, IUnknown* /*punkToolbarSite*/, BOOL /*fReserved*/) {
    return E_NOTIMPL;
}

IFACEMETHODIMP QTDesktopTool::GetBandInfo(DWORD /*dwBandID*/, DWORD dwViewMode, DESKBANDINFO* pdbi) {
    if(pdbi == nullptr) {
        return E_POINTER;
    }

    if(pdbi->dwMask & DBIM_MINSIZE) {
        pdbi->ptMinSize.x = 200;
        pdbi->ptMinSize.y = kDefaultHeight;
    }
    if(pdbi->dwMask & DBIM_MAXSIZE) {
        pdbi->ptMaxSize.x = -1;
        pdbi->ptMaxSize.y = kDefaultHeight;
    }
    if(pdbi->dwMask & DBIM_INTEGRAL) {
        pdbi->ptIntegral.x = 1;
        pdbi->ptIntegral.y = 1;
    }
    if(pdbi->dwMask & DBIM_ACTUAL) {
        pdbi->ptActual.x = 200;
        pdbi->ptActual.y = kDefaultHeight;
    }
    if(pdbi->dwMask & DBIM_MODEFLAGS) {
        pdbi->dwModeFlags = DBIMF_NORMAL | DBIMF_VARIABLEHEIGHT;
        if(dwViewMode & DBIF_VIEWMODE_VERTICAL) {
            pdbi->dwModeFlags |= DBIMF_USECHEVRON;
        }
    }
    if(pdbi->dwMask & DBIM_TITLE) {
        auto title = LoadStringResource(IDS_DESKTOPTOOL_TITLE);
        if(!title.empty()) {
            wcsncpy_s(pdbi->wszTitle, title.c_str(), _TRUNCATE);
        }
    }
    return S_OK;
}

IFACEMETHODIMP QTDesktopTool::UIActivateIO(BOOL fActivate, MSG* /*pMsg*/) {
    if(fActivate && m_hWnd) {
        ::SetFocus(m_hWnd);
    }
    return S_OK;
}

IFACEMETHODIMP QTDesktopTool::HasFocusIO() {
    return (::GetFocus() == m_hWnd) ? S_OK : S_FALSE;
}

IFACEMETHODIMP QTDesktopTool::TranslateAcceleratorIO(MSG* /*pMsg*/) {
    return S_FALSE;
}

IFACEMETHODIMP QTDesktopTool::SetSite(IUnknown* pUnkSite) {
    if(pUnkSite == nullptr) {
        m_spInputObjectSite.Release();
        m_spServiceProvider.Release();
        m_spExplorer.Release();
        m_spSite.Release();
        m_hwndRebar = nullptr;
        m_explorerHwnd = nullptr;
        ScheduleMenuRebuild();
        return S_OK;
    }

    m_spSite = pUnkSite;
    m_spInputObjectSite.Release();
    m_spServiceProvider.Release();
    m_spExplorer.Release();

    pUnkSite->QueryInterface(IID_PPV_ARGS(&m_spInputObjectSite));
    pUnkSite->QueryInterface(IID_PPV_ARGS(&m_spServiceProvider));

    if(m_spServiceProvider) {
        m_spServiceProvider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&m_spExplorer));
    }

    CComPtr<IOleWindow> oleWindow;
    if(SUCCEEDED(pUnkSite->QueryInterface(IID_PPV_ARGS(&oleWindow)))) {
        oleWindow->GetWindow(&m_hwndRebar);
    }

    EnsureExplorerWindow();
    ScheduleMenuRebuild();
    return S_OK;
}

IFACEMETHODIMP QTDesktopTool::GetSite(REFIID riid, void** ppvSite) {
    if(ppvSite == nullptr) {
        return E_POINTER;
    }
    if(!m_spSite) {
        *ppvSite = nullptr;
        return E_FAIL;
    }
    return m_spSite->QueryInterface(riid, ppvSite);
}

IFACEMETHODIMP QTDesktopTool::GetClassID(CLSID* pClassID) {
    if(pClassID == nullptr) {
        return E_POINTER;
    }
    *pClassID = CLSID_QTDesktopTool;
    return S_OK;
}

IFACEMETHODIMP QTDesktopTool::IsDirty() {
    return S_FALSE;
}

IFACEMETHODIMP QTDesktopTool::Load(IStream* /*pStm*/) {
    return S_OK;
}

IFACEMETHODIMP QTDesktopTool::Save(IStream* /*pStm*/, BOOL /*fClearDirty*/) {
    return S_OK;
}

IFACEMETHODIMP QTDesktopTool::GetSizeMax(ULARGE_INTEGER* pcbSize) {
    if(pcbSize == nullptr) {
        return E_POINTER;
    }
    pcbSize->QuadPart = 0;
    return S_OK;
}

void QTDesktopTool::EnsureThreads() {
    if(!m_taskbarThread.joinable()) {
        m_taskbarThread = std::thread(&QTDesktopTool::TaskbarThreadMain, this);
    }
    if(!m_desktopThread.joinable()) {
        m_desktopThread = std::thread(&QTDesktopTool::DesktopThreadMain, this);
    }
}

void QTDesktopTool::StopThreads() {
    if(m_exitEvent) {
        ::SetEvent(m_exitEvent);
    }
    if(m_rebuildEvent) {
        ::SetEvent(m_rebuildEvent);
    }

    if(m_taskbarThread.joinable()) {
        m_taskbarThread.join();
    }
    if(m_desktopThread.joinable()) {
        m_desktopThread.join();
    }
}

void QTDesktopTool::TaskbarThreadMain() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    HANDLE handles[2] = {m_exitEvent, m_rebuildEvent};
    for(;;) {
        DWORD wait = ::WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if(wait == WAIT_OBJECT_0) {
            break;
        }
        if(wait == WAIT_OBJECT_0 + 1) {
            ::ResetEvent(m_rebuildEvent);
            DesktopSettings settingsCopy;
            {
                std::lock_guard guard(m_settingsMutex);
                settingsCopy = m_settings;
            }
            auto model = BuildMenuModelSnapshot(settingsCopy);
            {
                std::lock_guard guard(m_modelMutex);
                m_menuModel = std::move(model);
            }
            m_menuReady.store(true, std::memory_order_release);
            if(m_hWnd) {
                ::PostMessageW(m_hWnd, WM_APP_MODEL_READY, 0, 0);
            }
        }
    }

    CoUninitialize();
}

void QTDesktopTool::DesktopThreadMain() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    InitializeHookLibrary();
    if(m_exitEvent) {
        ::WaitForSingleObject(m_exitEvent, INFINITE);
    }
    if(m_hookInitialized.load()) {
        qttabbar::hooks::HookManagerNative::Instance().UnregisterSink(this);
        m_hookInitialized.store(false);
    }
    CoUninitialize();
}

void QTDesktopTool::ScheduleMenuRebuild() {
    if(m_closing.load()) {
        return;
    }
    m_menuReady.store(false, std::memory_order_release);
    if(m_rebuildEvent) {
        ::SetEvent(m_rebuildEvent);
    }
}

std::vector<QTDesktopTool::SectionModel> QTDesktopTool::BuildMenuModelSnapshot(const DesktopSettings& settings) const {
    std::vector<SectionModel> sections;
    auto order = BuildSectionOrder(settings);
    for(auto kind : order) {
        bool include = false;
        switch(kind) {
        case SectionKind::Group:
            include = settings.includeGroup;
            break;
        case SectionKind::RecentTabs:
            include = settings.includeRecentTab;
            break;
        case SectionKind::Applications:
            include = settings.includeApplication;
            break;
        case SectionKind::RecentFiles:
            include = settings.includeRecentFile;
            break;
        }
        if(!include) {
            continue;
        }
        sections.emplace_back(BuildSectionModel(kind, settings));
    }
    return sections;
}

QTDesktopTool::SectionModel QTDesktopTool::BuildSectionModel(SectionKind kind, const DesktopSettings& settings) const {
    SectionModel section;
    section.kind = kind;
    section.title = GetSectionTitle(kind);
    switch(kind) {
    case SectionKind::Group:
        section.expanded = settings.groupExpanded;
        section.items = BuildGroupItems();
        break;
    case SectionKind::RecentTabs:
        section.expanded = settings.recentTabExpanded;
        section.items = BuildRecentTabItems();
        break;
    case SectionKind::Applications:
        section.expanded = settings.applicationExpanded;
        section.items = BuildApplicationItems();
        break;
    case SectionKind::RecentFiles:
        section.expanded = settings.recentFileExpanded;
        section.items = BuildRecentFileItems();
        break;
    }
    return section;
}

std::vector<QTDesktopTool::MenuItemModel> QTDesktopTool::BuildGroupItems() const {
    std::vector<MenuItemModel> result;
    auto groups = InstanceManagerNative::Instance().GetDesktopGroups();
    if(groups.empty()) {
        MenuItemModel placeholder;
        placeholder.text = LoadStringResource(IDS_DESKTOPTOOL_NOT_AVAILABLE);
        placeholder.enabled = false;
        placeholder.section = SectionKind::Group;
        result.push_back(std::move(placeholder));
        return result;
    }

    for(size_t index = 0; index < groups.size(); ++index) {
        MenuItemModel item;
        item.section = SectionKind::Group;
        item.text = groups[index].name;
        item.command = DesktopCommandType::None;
        item.index = index;
        item.enabled = false;
        result.push_back(std::move(item));
    }
    return result;
}

std::vector<QTDesktopTool::MenuItemModel> QTDesktopTool::BuildRecentTabItems() const {
    std::vector<MenuItemModel> result;
    if(auto* tabBar = ResolveTabBar(); tabBar != nullptr) {
        auto history = tabBar->GetClosedTabHistory();
        if(history.empty()) {
            MenuItemModel placeholder;
            placeholder.text = LoadStringResource(IDS_DESKTOPTOOL_EMPTY);
            placeholder.enabled = false;
            placeholder.section = SectionKind::RecentTabs;
            result.push_back(std::move(placeholder));
            return result;
        }
        for(size_t index = 0; index < history.size(); ++index) {
            MenuItemModel item;
            item.section = SectionKind::RecentTabs;
            item.text = ExtractLeafName(history[index]);
            if(item.text.empty()) {
                item.text = history[index];
            }
            item.command = DesktopCommandType::RestoreClosedTab;
            item.index = index;
            result.push_back(std::move(item));
        }
        return result;
    }

    MenuItemModel placeholder;
    placeholder.text = LoadStringResource(IDS_DESKTOPTOOL_NO_TABBAR);
    placeholder.enabled = false;
    placeholder.section = SectionKind::RecentTabs;
    result.push_back(std::move(placeholder));
    return result;
}

std::vector<QTDesktopTool::MenuItemModel> QTDesktopTool::BuildApplicationItems() const {
    std::vector<MenuItemModel> result;
    auto apps = InstanceManagerNative::Instance().GetDesktopApplications();
    if(apps.empty()) {
        MenuItemModel placeholder;
        placeholder.text = LoadStringResource(IDS_DESKTOPTOOL_NOT_AVAILABLE);
        placeholder.enabled = false;
        placeholder.section = SectionKind::Applications;
        result.push_back(std::move(placeholder));
        return result;
    }

    for(size_t index = 0; index < apps.size(); ++index) {
        MenuItemModel item;
        item.section = SectionKind::Applications;
        item.text = apps[index].name;
        item.command = DesktopCommandType::LaunchApplication;
        item.index = index;
        item.enabled = !apps[index].command.empty();
        if(!item.enabled) {
            item.command = DesktopCommandType::None;
        }
        result.push_back(std::move(item));
    }
    return result;
}

std::vector<QTDesktopTool::MenuItemModel> QTDesktopTool::BuildRecentFileItems() const {
    std::vector<MenuItemModel> result;
    auto files = InstanceManagerNative::Instance().GetDesktopRecentFiles();
    if(files.empty()) {
        MenuItemModel placeholder;
        placeholder.text = LoadStringResource(IDS_DESKTOPTOOL_NOT_AVAILABLE);
        placeholder.enabled = false;
        placeholder.section = SectionKind::RecentFiles;
        result.push_back(std::move(placeholder));
        return result;
    }

    for(size_t index = 0; index < files.size(); ++index) {
        MenuItemModel item;
        item.section = SectionKind::RecentFiles;
        item.text = ExtractLeafName(files[index]);
        if(item.text.empty()) {
            item.text = files[index];
        }
        item.command = DesktopCommandType::OpenRecentFile;
        item.index = index;
        item.payload = files[index];
        result.push_back(std::move(item));
    }
    return result;
}

HMENU QTDesktopTool::BuildPopupMenu() {
    ResetCommandState();
    HMENU menu = ::CreatePopupMenu();
    if(!menu) {
        return nullptr;
    }

    std::vector<SectionModel> sections;
    {
        std::lock_guard guard(m_modelMutex);
        sections = m_menuModel;
    }

    if(!m_menuReady.load(std::memory_order_acquire)) {
        MenuItemModel placeholder;
        placeholder.text = LoadStringResource(IDS_DESKTOPTOOL_LOADING);
        placeholder.enabled = false;
        ::AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, placeholder.text.c_str());
    } else {
        bool firstSection = true;
        for(const auto& section : sections) {
            if(!firstSection) {
                ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            }
            firstSection = false;
            std::wstring title = section.title;
            if(title.empty()) {
                title = L"-";
            }
            ::AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, title.c_str());
            for(const auto& item : section.items) {
                if(item.separator) {
                    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                    continue;
                }
                UINT flags = MF_STRING;
                if(!item.enabled || item.command == DesktopCommandType::None) {
                    flags |= MF_GRAYED;
                }
                UINT id = 0;
                if(item.enabled && item.command != DesktopCommandType::None) {
                    id = GenerateCommandId();
                    RegisterAction(id, DesktopAction{item.command, item.section, item.index, item.payload});
                }
                ::AppendMenuW(menu, flags, id, item.text.c_str());
            }
        }
    }

    if(!sections.empty()) {
        ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }

    auto settingsMenu = BuildSettingsMenu();
    if(settingsMenu) {
        auto settingsTitle = LoadStringResource(IDS_DESKTOPTOOL_SETTINGS);
        ::AppendMenuW(menu, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(settingsMenu), settingsTitle.c_str());
    }

    return menu;
}

HMENU QTDesktopTool::BuildSettingsMenu() {
    DesktopSettings settingsCopy;
    {
        std::lock_guard guard(m_settingsMutex);
        settingsCopy = m_settings;
    }

    HMENU menu = ::CreatePopupMenu();
    if(!menu) {
        return nullptr;
    }

    AppendSettingsToggles(menu, settingsCopy);
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendSettingsIncludeMenu(menu, settingsCopy);
    AppendSettingsExpandMenu(menu, settingsCopy);
    AppendSettingsReorderMenu(menu, settingsCopy);

    return menu;
}

void QTDesktopTool::AppendSettingsToggles(HMENU menu, const DesktopSettings& settings) {
    auto lockTitle = LoadStringResource(IDS_DESKTOPTOOL_LOCK_MENU);
    UINT lockId = GenerateCommandId();
    RegisterAction(lockId, DesktopAction{DesktopCommandType::ToggleLockMenu, std::nullopt, 0, {}});
    ::AppendMenuW(menu, MF_STRING | (settings.lockMenu ? MF_CHECKED : 0), lockId, lockTitle.c_str());

    auto clickTitle = LoadStringResource(IDS_DESKTOPTOOL_ONE_CLICK);
    UINT clickId = GenerateCommandId();
    RegisterAction(clickId, DesktopAction{DesktopCommandType::ToggleOneClick, std::nullopt, 0, {}});
    ::AppendMenuW(menu, MF_STRING | (settings.oneClickMenu ? MF_CHECKED : 0), clickId, clickTitle.c_str());

    auto appShortcutTitle = LoadStringResource(IDS_DESKTOPTOOL_APP_SHORTCUTS);
    UINT shortcutId = GenerateCommandId();
    RegisterAction(shortcutId, DesktopAction{DesktopCommandType::ToggleAppShortcuts, std::nullopt, 0, {}});
    ::AppendMenuW(menu, MF_STRING | (settings.enableAppShortcuts ? MF_CHECKED : 0), shortcutId, appShortcutTitle.c_str());
}

void QTDesktopTool::AppendSettingsIncludeMenu(HMENU menu, const DesktopSettings& settings) {
    auto includeTitle = LoadStringResource(IDS_DESKTOPTOOL_INCLUDE);
    HMENU includeMenu = ::CreatePopupMenu();
    if(!includeMenu) {
        return;
    }

    auto addToggle = [&](SectionKind kind, bool checked) {
        auto title = GetSectionTitle(kind);
        UINT id = GenerateCommandId();
        RegisterAction(id, DesktopAction{DesktopCommandType::ToggleIncludeSection, kind, 0, {}});
        ::AppendMenuW(includeMenu, MF_STRING | (checked ? MF_CHECKED : 0), id, title.c_str());
    };

    addToggle(SectionKind::Group, settings.includeGroup);
    addToggle(SectionKind::RecentTabs, settings.includeRecentTab);
    addToggle(SectionKind::Applications, settings.includeApplication);
    addToggle(SectionKind::RecentFiles, settings.includeRecentFile);

    ::AppendMenuW(menu, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(includeMenu), includeTitle.c_str());
}

void QTDesktopTool::AppendSettingsExpandMenu(HMENU menu, const DesktopSettings& settings) {
    auto expandTitle = LoadStringResource(IDS_DESKTOPTOOL_EXPAND);
    HMENU expandMenu = ::CreatePopupMenu();
    if(!expandMenu) {
        return;
    }

    auto addToggle = [&](SectionKind kind, bool checked) {
        auto title = GetSectionTitle(kind);
        UINT id = GenerateCommandId();
        RegisterAction(id, DesktopAction{DesktopCommandType::ToggleExpandSection, kind, 0, {}});
        ::AppendMenuW(expandMenu, MF_STRING | (checked ? MF_CHECKED : 0), id, title.c_str());
    };

    addToggle(SectionKind::Group, settings.groupExpanded);
    addToggle(SectionKind::RecentTabs, settings.recentTabExpanded);
    addToggle(SectionKind::Applications, settings.applicationExpanded);
    addToggle(SectionKind::RecentFiles, settings.recentFileExpanded);

    ::AppendMenuW(menu, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(expandMenu), expandTitle.c_str());
}

void QTDesktopTool::AppendSettingsReorderMenu(HMENU menu, const DesktopSettings& settings) {
    auto reorderTitle = LoadStringResource(IDS_DESKTOPTOOL_REORDER);
    HMENU reorderMenu = ::CreatePopupMenu();
    if(!reorderMenu) {
        return;
    }

    auto addEntry = [&](SectionKind kind) {
        auto title = GetSectionTitle(kind);
        HMENU sub = ::CreatePopupMenu();
        if(!sub) {
            return;
        }
        UINT upId = GenerateCommandId();
        RegisterAction(upId, DesktopAction{DesktopCommandType::MoveSectionUp, kind, 0, {}});
        auto moveUp = LoadStringResource(IDS_DESKTOPTOOL_MOVE_UP);
        ::AppendMenuW(sub, MF_STRING, upId, moveUp.c_str());
        UINT downId = GenerateCommandId();
        RegisterAction(downId, DesktopAction{DesktopCommandType::MoveSectionDown, kind, 0, {}});
        auto moveDown = LoadStringResource(IDS_DESKTOPTOOL_MOVE_DOWN);
        ::AppendMenuW(sub, MF_STRING, downId, moveDown.c_str());
        ::AppendMenuW(reorderMenu, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(sub), title.c_str());
    };

    addEntry(SectionKind::Group);
    addEntry(SectionKind::RecentTabs);
    addEntry(SectionKind::Applications);
    addEntry(SectionKind::RecentFiles);

    ::AppendMenuW(menu, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(reorderMenu), reorderTitle.c_str());
}

std::wstring QTDesktopTool::LoadStringResource(UINT id) const {
    CStringW text;
    text.LoadStringW(id);
    return std::wstring(text.GetString(), text.GetLength());
}

std::wstring QTDesktopTool::GetSectionTitle(SectionKind kind) const {
    switch(kind) {
    case SectionKind::Group:
        return LoadStringResource(IDS_DESKTOPTOOL_GROUPS);
    case SectionKind::RecentTabs:
        return LoadStringResource(IDS_DESKTOPTOOL_RECENT_TABS);
    case SectionKind::Applications:
        return LoadStringResource(IDS_DESKTOPTOOL_APPLICATIONS);
    case SectionKind::RecentFiles:
        return LoadStringResource(IDS_DESKTOPTOOL_RECENT_FILES);
    }
    return {};
}

std::array<QTDesktopTool::SectionKind, 4> QTDesktopTool::BuildSectionOrder(const DesktopSettings& settings) const {
    return {
        IndexToSection(settings.firstItem),
        IndexToSection(settings.secondItem),
        IndexToSection(settings.thirdItem),
        IndexToSection(settings.fourthItem),
    };
}

QTDesktopTool::SectionKind QTDesktopTool::IndexToSection(int index) {
    switch(index) {
    case 0:
        return SectionKind::Group;
    case 1:
        return SectionKind::RecentTabs;
    case 2:
        return SectionKind::Applications;
    case 3:
        return SectionKind::RecentFiles;
    default:
        return SectionKind::Group;
    }
}

int QTDesktopTool::SectionToIndex(SectionKind section) {
    switch(section) {
    case SectionKind::Group:
        return 0;
    case SectionKind::RecentTabs:
        return 1;
    case SectionKind::Applications:
        return 2;
    case SectionKind::RecentFiles:
        return 3;
    }
    return 0;
}

void QTDesktopTool::ResetCommandState() {
    m_commandActions.clear();
    m_nextCommandId = 0x6000;
}

UINT QTDesktopTool::GenerateCommandId() {
    return m_nextCommandId++;
}

void QTDesktopTool::RegisterAction(UINT id, DesktopAction action) {
    m_commandActions.emplace(id, std::move(action));
}

void QTDesktopTool::ExecuteAction(const DesktopAction& action) {
    switch(action.type) {
    case DesktopCommandType::RestoreClosedTab: {
        if(auto* tabBar = ResolveTabBar(); tabBar != nullptr) {
            tabBar->RestoreClosedTabByIndex(action.index);
        }
        ScheduleMenuRebuild();
        break;
    }
    case DesktopCommandType::LaunchApplication: {
        auto apps = InstanceManagerNative::Instance().GetDesktopApplications();
        if(action.index < apps.size()) {
            const auto& app = apps[action.index];
            SHELLEXECUTEINFOW exec{};
            exec.cbSize = sizeof(exec);
            exec.fMask = SEE_MASK_FLAG_NO_UI;
            exec.lpFile = app.command.c_str();
            exec.lpParameters = app.arguments.c_str();
            exec.lpDirectory = app.workingDirectory.empty() ? nullptr : app.workingDirectory.c_str();
            exec.nShow = SW_SHOWNORMAL;
            ::ShellExecuteExW(&exec);
        }
        break;
    }
    case DesktopCommandType::OpenRecentFile: {
        auto files = InstanceManagerNative::Instance().GetDesktopRecentFiles();
        if(action.index < files.size()) {
            ::ShellExecuteW(nullptr, L"open", files[action.index].c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        break;
    }
    case DesktopCommandType::ToggleLockMenu:
    case DesktopCommandType::ToggleOneClick:
    case DesktopCommandType::ToggleAppShortcuts:
    case DesktopCommandType::ToggleIncludeSection:
    case DesktopCommandType::ToggleExpandSection:
        ToggleSetting(action.type, action.section);
        break;
    case DesktopCommandType::MoveSectionUp:
        if(action.section.has_value()) {
            MoveSection(*action.section, -1);
        }
        break;
    case DesktopCommandType::MoveSectionDown:
        if(action.section.has_value()) {
            MoveSection(*action.section, 1);
        }
        break;
    case DesktopCommandType::InvokeGroup:
    case DesktopCommandType::None:
    default:
        break;
    }
}

void QTDesktopTool::ToggleSetting(DesktopCommandType type, std::optional<SectionKind> section) {
    std::lock_guard guard(m_settingsMutex);
    auto toggle = [](bool& value) { value = !value; };
    switch(type) {
    case DesktopCommandType::ToggleLockMenu:
        toggle(m_settings.lockMenu);
        break;
    case DesktopCommandType::ToggleOneClick:
        toggle(m_settings.oneClickMenu);
        break;
    case DesktopCommandType::ToggleAppShortcuts:
        toggle(m_settings.enableAppShortcuts);
        break;
    case DesktopCommandType::ToggleIncludeSection:
        if(section) {
            switch(*section) {
            case SectionKind::Group:
                toggle(m_settings.includeGroup);
                break;
            case SectionKind::RecentTabs:
                toggle(m_settings.includeRecentTab);
                break;
            case SectionKind::Applications:
                toggle(m_settings.includeApplication);
                break;
            case SectionKind::RecentFiles:
                toggle(m_settings.includeRecentFile);
                break;
            }
        }
        break;
    case DesktopCommandType::ToggleExpandSection:
        if(section) {
            switch(*section) {
            case SectionKind::Group:
                toggle(m_settings.groupExpanded);
                break;
            case SectionKind::RecentTabs:
                toggle(m_settings.recentTabExpanded);
                break;
            case SectionKind::Applications:
                toggle(m_settings.applicationExpanded);
                break;
            case SectionKind::RecentFiles:
                toggle(m_settings.recentFileExpanded);
                break;
            }
        }
        break;
    default:
        break;
    }
    PersistDesktopSettings();
    ScheduleMenuRebuild();
}

void QTDesktopTool::MoveSection(SectionKind section, int delta) {
    std::lock_guard guard(m_settingsMutex);
    auto order = BuildSectionOrder(m_settings);
    auto it = std::find(order.begin(), order.end(), section);
    if(it == order.end()) {
        return;
    }
    auto index = static_cast<int>(std::distance(order.begin(), it));
    auto newIndex = index + delta;
    if(newIndex < 0 || newIndex >= static_cast<int>(order.size())) {
        return;
    }
    std::swap(order[index], order[newIndex]);
    m_settings.firstItem = SectionToIndex(order[0]);
    m_settings.secondItem = SectionToIndex(order[1]);
    m_settings.thirdItem = SectionToIndex(order[2]);
    m_settings.fourthItem = SectionToIndex(order[3]);
    PersistDesktopSettings();
    ScheduleMenuRebuild();
}

void QTDesktopTool::PersistDesktopSettings() {
    DesktopSettings snapshot;
    {
        std::lock_guard guard(m_settingsMutex);
        snapshot = m_settings;
    }
    qttabbar::ConfigData config = qttabbar::LoadConfigFromRegistry();
    config.desktop = snapshot;
    qttabbar::WriteConfigToRegistry(config, true);
}

void QTDesktopTool::EnsureExplorerWindow() {
    m_explorerHwnd = nullptr;
    if(m_spExplorer) {
        SHANDLE_PTR handle = 0;
        if(SUCCEEDED(m_spExplorer->get_HWND(&handle))) {
            m_explorerHwnd = reinterpret_cast<HWND>(handle);
        }
    }
    if(m_hookInitialized.load() && m_spExplorer) {
        qttabbar::hooks::HookLibraryBridge::Instance().InitShellBrowserHook(m_spExplorer);
    }
    ScheduleMenuRebuild();
}

QTTabBarClass* QTDesktopTool::ResolveTabBar() const {
    if(m_explorerHwnd == nullptr) {
        return nullptr;
    }
    return InstanceManagerNative::Instance().FindTabBar(m_explorerHwnd);
}

void QTDesktopTool::InitializeHookLibrary() {
    if(m_hookInitialized.load()) {
        return;
    }
    qttabbar::hooks::HookManagerNative::Instance().RegisterSink(this);
    m_hookInitialized.store(true);
}

void QTDesktopTool::HandleHookResult(int /*hookId*/, int /*retcode*/) {
    ScheduleMenuRebuild();
}

BOOL QTDesktopTool::HandleHookNewWindow(PCIDLIST_ABSOLUTE /*pidl*/) {
    ScheduleMenuRebuild();
    return TRUE;
}

void QTDesktopTool::OnHookResult(int hookId, int retcode) {
    HandleHookResult(hookId, retcode);
}

BOOL QTDesktopTool::OnHookNewWindow(PCIDLIST_ABSOLUTE pidl) {
    return HandleHookNewWindow(pidl);
}
