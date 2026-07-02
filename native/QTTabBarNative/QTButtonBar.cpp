#include "pch.h"
#include "QTButtonBar.h"

#include <CommCtrl.h>
#include <Shlwapi.h>

#include <algorithm>
#include <cstring>

#include "Config.h"
#include "InstanceManagerNative.h"
#include "PluginManagerNative.h"
#include "GroupsManagerNative.h"
#include "QTTabBarClass.h"
#include "AppsManagerNative.h"
#include "RecentFileHistoryNative.h"
#include "PluginContracts.h"

using qttabbar::ConfigData;
using qttabbar::LoadConfigFromRegistry;
using qttabbar::plugins::PluginManagerNative;
using qttabbar::plugins::PluginClientVTable;
using qttabbar::plugins::PluginMetadataNative;
using qttabbar::plugins::PluginMenuType;
using qttabbar::plugins::PluginEndCode;

namespace {
constexpr UINT kToolbarId = 100;
constexpr UINT kSearchBoxId = 101;
constexpr UINT kButtonbarDynamicFirst = ID_BUTTONBAR_DYNAMIC_FIRST;
constexpr UINT kButtonbarDynamicLast = ID_BUTTONBAR_DYNAMIC_LAST;

struct MenuGuard {
    HMENU handle = nullptr;
    explicit MenuGuard(HMENU menu) : handle(menu) {}
    ~MenuGuard() {
        if(handle) {
            ::DestroyMenu(handle);
        }
    }
};

std::wstring ExtractLeafName(const std::wstring& path) {
    if(path.empty()) {
        return path;
    }
    wchar_t buffer[MAX_PATH];
    ::StringCchCopyW(buffer, ARRAYSIZE(buffer), path.c_str());
    ::PathStripPathW(buffer);
    return buffer;
}

bool CopyTextToClipboard(HWND owner, const std::wstring& text) {
    if(!::OpenClipboard(owner)) {
        return false;
    }
    struct ClipboardCloser {
        ~ClipboardCloser() { ::CloseClipboard(); }
    } closer;
    if(!::EmptyClipboard()) {
        return false;
    }
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL handle = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if(handle == nullptr) {
        return false;
    }
    void* data = ::GlobalLock(handle);
    if(!data) {
        ::GlobalFree(handle);
        return false;
    }
    std::memcpy(data, text.c_str(), bytes);
    ::GlobalUnlock(handle);
    if(::SetClipboardData(CF_UNICODETEXT, handle) == nullptr) {
        ::GlobalFree(handle);
        return false;
    }
    return true;
}

}  // namespace

QTButtonBar::QTButtonBar() noexcept
    : m_hwndRebar(nullptr)
    , m_hwndToolbar(nullptr)
    , m_hwndSearch(nullptr)
    , m_explorerHwnd(nullptr)
    , m_minSize{240, 32}
    , m_maxSize{-1, -1}
    , m_closed(false)
    , m_visible(false)
    , m_bandId(0)
    , m_nextDynamicCommand(kButtonbarDynamicFirst) {}

QTButtonBar::~QTButtonBar() = default;

HRESULT QTButtonBar::FinalConstruct() {
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES | ICC_COOL_CLASSES | ICC_TAB_CLASSES;
    if(!::InitCommonControlsEx(&icc)) {
        return HRESULT_FROM_WIN32(::GetLastError());
    }
    return S_OK;
}

void QTButtonBar::FinalRelease() {
    UnregisterWithInstanceManager();
    DestroyChildWindows();
    if(m_hWnd != nullptr && ::IsWindow(m_hWnd)) {
        ::DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }
    m_spExplorer.Release();
    m_spServiceProvider.Release();
    m_spInputObjectSite.Release();
    m_spSite.Release();
}

bool QTButtonBar::InvokeCommand(UINT commandId) {
    if(m_hWnd == nullptr) {
        return false;
    }
    ::SendMessageW(m_hWnd, WM_COMMAND, commandId, 0);
    return true;
}

HRESULT QTButtonBar::EnsureWindow() {
    if(m_hWnd != nullptr) {
        return S_OK;
    }
    if(m_hwndRebar == nullptr) {
        return E_UNEXPECTED;
    }
    RECT rc = {0, 0, m_minSize.cx, m_minSize.cy};
    HWND hwnd = Create(m_hwndRebar, rc, L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
    if(hwnd == nullptr) {
        return HRESULT_FROM_WIN32(::GetLastError());
    }
    return S_OK;
}

void QTButtonBar::DestroyChildWindows() {
    if(m_hwndToolbar && ::IsWindow(m_hwndToolbar)) {
        ::DestroyWindow(m_hwndToolbar);
        m_hwndToolbar = nullptr;
    }
    if(m_hwndSearch && ::IsWindow(m_hwndSearch)) {
        ::DestroyWindow(m_hwndSearch);
        m_hwndSearch = nullptr;
    }
}

LRESULT QTButtonBar::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    InitializeToolbar();
    return 0;
}

LRESULT QTButtonBar::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    DestroyChildWindows();
    return 0;
}

LRESULT QTButtonBar::OnSize(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    bHandled = TRUE;
    int width = LOWORD(lParam);
    int height = HIWORD(lParam);
    UpdateLayout(width, height);
    return 0;
}

LRESULT QTButtonBar::OnCommand(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    bHandled = TRUE;
    UINT commandId = LOWORD(wParam);
    if(HIWORD(wParam) == 0) {
        HandleButtonCommand(commandId);
        return 0;
    }
    if(reinterpret_cast<HWND>(lParam) == m_hwndSearch && HIWORD(wParam) == EN_SETFOCUS) {
        return 0;
    }
    return 0;
}

LRESULT QTButtonBar::OnNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
    bHandled = FALSE;
    auto* header = reinterpret_cast<NMHDR*>(lParam);
    if(header == nullptr) {
        return 0;
    }
    if(header->hwndFrom == m_hwndToolbar && header->code == TBN_DROPDOWN) {
        auto* info = reinterpret_cast<NMTOOLBARW*>(lParam);
        if(info != nullptr) {
            RECT rc = info->rcButton;
            ::MapWindowPoints(m_hwndToolbar, HWND_DESKTOP, reinterpret_cast<POINT*>(&rc), 2);
            POINT pt{rc.left, rc.bottom};
            ShowDropdown(info->iItem, pt);
            bHandled = TRUE;
            return TBDDRET_DEFAULT;
        }
    }
    return 0;
}

void QTButtonBar::InitializeToolbar() {
    m_hwndToolbar = ::CreateWindowExW(0, TOOLBARCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | CCS_NORESIZE | CCS_NOPARENTALIGN |
                                                                    TBSTYLE_FLAT | TBSTYLE_LIST | TBSTYLE_TOOLTIPS,
                                      0, 0, 0, 0, m_hWnd, reinterpret_cast<HMENU>(kToolbarId), _AtlBaseModule.GetModuleInstance(),
                                      nullptr);
    if(m_hwndToolbar == nullptr) {
        return;
    }
    ::SendMessageW(m_hwndToolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    ::SendMessageW(m_hwndToolbar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_DRAWDDARROWS | TBSTYLE_EX_DOUBLEBUFFER);

    CreateToolbarButtons();

    m_hwndSearch = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                     0, 0, 0, 0, m_hWnd, reinterpret_cast<HMENU>(kSearchBoxId),
                                     _AtlBaseModule.GetModuleInstance(), nullptr);
    if(m_hwndSearch != nullptr) {
        ::SendMessageW(m_hwndSearch, WM_SETFONT, reinterpret_cast<WPARAM>(::GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    }
}

void QTButtonBar::CreateToolbarButtons() {
    std::vector<TBBUTTON> buttons;
    buttons.reserve(32);

    ConfigData config = LoadConfigFromRegistry();
    const auto& definitions = DefaultButtons();
    std::vector<int> order = config.bbar.buttonIndexes.empty() ? std::vector<int>() : config.bbar.buttonIndexes;
    if(order.empty()) {
        for(const auto& def : definitions) {
            order.push_back(def.index);
        }
    }

    for(int index : order) {
        auto it = std::find_if(definitions.begin(), definitions.end(), [index](const ButtonDefinition& def) {
            return def.index == index;
        });
        if(it == definitions.end()) {
            if(index == QTTabBarClass::kButtonSeparatorIndex) {
                TBBUTTON sep{};
                sep.fsStyle = TBSTYLE_SEP;
                buttons.push_back(sep);
            }
            continue;
        }

        const ButtonDefinition& def = *it;
        TBBUTTON button{};
        button.iBitmap = I_IMAGENONE;
        button.idCommand = def.commandId;
        button.fsState = TBSTATE_ENABLED;
        button.fsStyle = TBSTYLE_BUTTON;
        if(def.dropdown) {
            button.fsStyle |= BTNS_DROPDOWN;
        }
        if(def.checkable) {
            button.fsStyle |= TBSTYLE_CHECK;
        }
        button.iString = reinterpret_cast<INT_PTR>(def.text);
        buttons.push_back(button);
    }

    if(!buttons.empty()) {
        ::SendMessageW(m_hwndToolbar, TB_ADDBUTTONS, static_cast<WPARAM>(buttons.size()),
                       reinterpret_cast<LPARAM>(buttons.data()));
    }
}

void QTButtonBar::UpdateLayout(int width, int height) {
    if(m_hwndToolbar == nullptr) {
        return;
    }
    int searchWidth = 180;
    if(width < searchWidth + 16) {
        searchWidth = (std::max)(80, width / 3);
    }
    int toolbarWidth = width - searchWidth - 4;
    if(toolbarWidth < 0) {
        toolbarWidth = width;
    }
    ::MoveWindow(m_hwndToolbar, 0, 0, toolbarWidth, height, TRUE);
    if(m_hwndSearch != nullptr) {
        ::MoveWindow(m_hwndSearch, toolbarWidth + 4, 2, searchWidth, height - 4, TRUE);
    }
}

void QTButtonBar::HandleButtonCommand(UINT commandId) {
    auto handlerIt = m_menuHandlers.find(commandId);
    if(handlerIt != m_menuHandlers.end()) {
        auto handler = handlerIt->second;
        m_menuHandlers.erase(handlerIt);
        if(handler) {
            handler();
        }
        return;
    }

    switch(commandId) {
    case ID_BUTTONBAR_SEARCH_FOCUS:
        if(m_hwndSearch != nullptr) {
            ::SetFocus(m_hwndSearch);
        }
        return;
    case ID_BUTTONBAR_GROUPS:
    case ID_BUTTONBAR_RECENT_TABS:
    case ID_BUTTONBAR_RECENT_FILES:
    case ID_BUTTONBAR_APPLICATIONS:
    case ID_BUTTONBAR_MISC_TOOLS:
    case ID_BUTTONBAR_PLUGINS:
        if(m_hwndToolbar != nullptr) {
            RECT rc{};
            if(::SendMessageW(m_hwndToolbar, TB_GETRECT, commandId, reinterpret_cast<LPARAM>(&rc))) {
                ::MapWindowPoints(m_hwndToolbar, HWND_DESKTOP, reinterpret_cast<POINT*>(&rc), 2);
                POINT pt{rc.left, rc.bottom};
                ShowDropdown(commandId, pt);
                return;
            }
        }
        break;
    default:
        break;
    }

    InstanceManagerNative::Instance().NotifyButtonCommand(m_explorerHwnd, commandId);
}

void QTButtonBar::ShowDropdown(UINT commandId, const POINT& screenPoint) {
    MenuGuard menu(::CreatePopupMenu());
    if(menu.handle == nullptr) {
        return;
    }
    ClearMenuHandlers();

    switch(commandId) {
    case ID_BUTTONBAR_NAVIGATION_BACK:
    case ID_BUTTONBAR_NAVIGATION_FORWARD:
        BuildNavigationMenu(menu.handle);
        break;
    case ID_BUTTONBAR_GROUPS:
        BuildGroupsMenu(menu.handle);
        break;
    case ID_BUTTONBAR_RECENT_TABS:
        BuildRecentTabsMenu(menu.handle);
        break;
    case ID_BUTTONBAR_RECENT_FILES:
        BuildRecentFilesMenu(menu.handle);
        break;
    case ID_BUTTONBAR_APPLICATIONS:
        BuildApplicationsMenu(menu.handle);
        break;
    case ID_BUTTONBAR_MISC_TOOLS:
        BuildMiscToolsMenu(menu.handle);
        break;
    case ID_BUTTONBAR_PLUGINS:
        BuildPluginsMenu(menu.handle);
        break;
    default:
        return;
    }

    if(::GetMenuItemCount(menu.handle) == 0) {
        ::AppendMenuW(menu.handle, MF_GRAYED | MF_STRING, 0, L"(empty)");
    }

    ::TrackPopupMenuEx(menu.handle, TPM_LEFTALIGN | TPM_LEFTBUTTON, screenPoint.x, screenPoint.y, m_hWnd, nullptr);
}

void QTButtonBar::BuildNavigationMenu(HMENU menu) {
    auto* tabBar = InstanceManagerNative::Instance().FindTabBar(m_explorerHwnd);
    if(tabBar == nullptr) {
        ::AppendMenuW(menu, MF_GRAYED | MF_STRING, 0, L"No tab bar available");
        return;
    }

    auto paths = tabBar->GetOpenTabs();
    if(paths.empty()) {
        ::AppendMenuW(menu, MF_GRAYED | MF_STRING, 0, L"No tabs");
        return;
    }

    for(size_t index = 0; index < paths.size(); ++index) {
        UINT id = 0;
        if(!TryAllocateDynamicCommand(id)) {
            AppendOverflowPlaceholder(menu);
            return;
        }
        std::wstring text = ExtractLeafName(paths[index]);
        if(text.empty()) {
            text = paths[index];
        }
        ::AppendMenuW(menu, MF_STRING, id, text.c_str());
        m_menuHandlers[id] = [tabBar, index]() { tabBar->ActivateTabByIndex(index); };
    }
}

void QTButtonBar::BuildGroupsMenu(HMENU menu) {
    auto loadString = [] (UINT id, const wchar_t* fallback) {
        wchar_t buffer[128] = {};
        HINSTANCE instance = _AtlBaseModule.GetResourceInstance();
        int length = ::LoadStringW(instance, id, buffer, static_cast<int>(_countof(buffer)));
        if(length > 0) {
            return std::wstring(buffer, length);
        }
        return std::wstring(fallback);
    };

    auto groups = qttabbar::GroupsManagerNative::Instance().GetGroups();
    if(groups.empty()) {
        const std::wstring noGroups = loadString(IDS_CONTEXT_MENU_NO_GROUPS, L"(no groups)");
        ::AppendMenuW(menu, MF_GRAYED | MF_STRING, 0, noGroups.c_str());
        return;
    }

    auto* tabBar = InstanceManagerNative::Instance().FindTabBar(m_explorerHwnd);
    if(tabBar == nullptr) {
        ::AppendMenuW(menu, MF_GRAYED | MF_STRING, 0, L"No tab bar available");
        return;
    }

    for(std::size_t index = 0; index < groups.size(); ++index) {
        UINT id = 0;
        if(!TryAllocateDynamicCommand(id)) {
            AppendOverflowPlaceholder(menu);
            return;
        }
        ::AppendMenuW(menu, MF_STRING, id, groups[index].name.c_str());
        m_menuHandlers[id] = [tabBar, index]() { tabBar->OpenGroupByIndex(index); };
    }

    if(groups.size() > 1) {
        HMENU reorder = ::CreatePopupMenu();
        if(reorder != nullptr) {
            ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            for(size_t index = 0; index < groups.size(); ++index) {
                if(index > 0) {
                    UINT upId = 0;
                    if(TryAllocateDynamicCommand(upId)) {
                        std::wstring label = L"Move up: " + groups[index].name;
                        ::AppendMenuW(reorder, MF_STRING, upId, label.c_str());
                        m_menuHandlers[upId] = [index]() {
                            auto all = qttabbar::GroupsManagerNative::Instance().GetGroups();
                            if(index >= all.size()) {
                                return;
                            }
                            std::vector<std::wstring> order;
                            order.reserve(all.size());
                            for(const auto& group : all) {
                                order.push_back(group.name);
                            }
                            std::swap(order[index], order[index - 1]);
                            qttabbar::GroupsManagerNative::Instance().Reorder(order);
                        };
                    }
                }
                if(index + 1 < groups.size()) {
                    UINT downId = 0;
                    if(TryAllocateDynamicCommand(downId)) {
                        std::wstring label = L"Move down: " + groups[index].name;
                        ::AppendMenuW(reorder, MF_STRING, downId, label.c_str());
                        m_menuHandlers[downId] = [index]() {
                            auto all = qttabbar::GroupsManagerNative::Instance().GetGroups();
                            if(index + 1 >= all.size()) {
                                return;
                            }
                            std::vector<std::wstring> order;
                            order.reserve(all.size());
                            for(const auto& group : all) {
                                order.push_back(group.name);
                            }
                            std::swap(order[index], order[index + 1]);
                            qttabbar::GroupsManagerNative::Instance().Reorder(order);
                        };
                    }
                }
            }
            ::AppendMenuW(menu, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(reorder), L"Reorder");
        }
    }
}

void QTButtonBar::BuildRecentTabsMenu(HMENU menu) {
    auto* tabBar = InstanceManagerNative::Instance().FindTabBar(m_explorerHwnd);
    if(tabBar == nullptr) {
        ::AppendMenuW(menu, MF_GRAYED | MF_STRING, 0, L"No tab bar available");
        return;
    }

    auto history = tabBar->GetClosedTabHistory();
    if(history.empty()) {
        ::AppendMenuW(menu, MF_GRAYED | MF_STRING, 0, L"No recent tabs");
        return;
    }

    for(size_t index = 0; index < history.size(); ++index) {
        UINT id = 0;
        if(!TryAllocateDynamicCommand(id)) {
            AppendOverflowPlaceholder(menu);
            return;
        }
        std::wstring text = ExtractLeafName(history[index]);
        if(text.empty()) {
            text = history[index];
        }
        ::AppendMenuW(menu, MF_STRING, id, text.c_str());
        m_menuHandlers[id] = [tabBar, index]() { tabBar->RestoreClosedTabByIndex(index); };
    }
}

void QTButtonBar::BuildRecentFilesMenu(HMENU menu) {
    auto files = qttabbar::RecentFileHistoryNative::Instance().GetRecentFiles();
    if(files.empty()) {
        ::AppendMenuW(menu, MF_GRAYED | MF_STRING, 0, L"(no recent files)");
        return;
    }

    for(size_t index = 0; index < files.size(); ++index) {
        UINT id = 0;
        if(!TryAllocateDynamicCommand(id)) {
            AppendOverflowPlaceholder(menu);
            return;
        }
        std::wstring text = ExtractLeafName(files[index]);
        if(text.empty()) {
            text = files[index];
        }
        ::AppendMenuW(menu, MF_STRING, id, text.c_str());
        m_menuHandlers[id] = [file = files[index]]() {
            ::ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            qttabbar::RecentFileHistoryNative::Instance().Add(file);
        };
    }

    UINT clearId = 0;
    if(TryAllocateDynamicCommand(clearId)) {
        ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(menu, MF_STRING, clearId, L"Clear recent files");
        m_menuHandlers[clearId] = []() { qttabbar::RecentFileHistoryNative::Instance().Clear(); };
    }
}

void QTButtonBar::BuildApplicationsMenu(HMENU menu) {
    auto nodes = qttabbar::AppsManagerNative::Instance().GetRootNodes();
    if(nodes.empty()) {
        ::AppendMenuW(menu, MF_GRAYED | MF_STRING, 0, L"(no applications)");
        return;
    }

    auto* tabBar = InstanceManagerNative::Instance().FindTabBar(m_explorerHwnd);
    std::wstring currentPath;
    if(tabBar != nullptr) {
        currentPath = tabBar->GetCurrentPath();
    }

    std::function<void(const std::vector<qttabbar::UserAppMenuNodeNative>&, HMENU)> addNodes;
    addNodes = [this, &addNodes, currentPath](const std::vector<qttabbar::UserAppMenuNodeNative>& entries,
                                              HMENU parent) {
        for(const auto& node : entries) {
            if(node.isFolder) {
                HMENU sub = ::CreatePopupMenu();
                if(sub == nullptr) {
                    continue;
                }
                ::AppendMenuW(parent, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(sub), node.app.name.c_str());
                addNodes(node.children, sub);
                if(::GetMenuItemCount(sub) == 0) {
                    ::AppendMenuW(sub, MF_GRAYED | MF_STRING, 0, L"(empty)");
                }
            } else {
                UINT id = 0;
                if(!TryAllocateDynamicCommand(id)) {
                    AppendOverflowPlaceholder(parent);
                    return;
                }
                ::AppendMenuW(parent, MF_STRING, id, node.app.name.c_str());
                m_menuHandlers[id] = [entry = node.app, hwnd = m_hWnd, currentPath]() {
                    qttabbar::AppExecutionContextNative context;
                    context.currentDirectory = currentPath;
                    context.parentWindow = hwnd;
                    qttabbar::AppsManagerNative::Instance().Execute(entry, context);
                };
            }
        }
    };

    addNodes(nodes, menu);

    if(nodes.size() > 1) {
        HMENU reorder = ::CreatePopupMenu();
        if(reorder != nullptr) {
            ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            for(size_t index = 0; index < nodes.size(); ++index) {
                if(index > 0) {
                    UINT upId = 0;
                    if(TryAllocateDynamicCommand(upId)) {
                        std::wstring label = L"Move up: " + nodes[index].app.name;
                        ::AppendMenuW(reorder, MF_STRING, upId, label.c_str());
                        m_menuHandlers[upId] = [index]() {
                            qttabbar::AppsManagerNative::Instance().MoveRootNodeUp(index);
                        };
                    }
                }
                if(index + 1 < nodes.size()) {
                    UINT downId = 0;
                    if(TryAllocateDynamicCommand(downId)) {
                        std::wstring label = L"Move down: " + nodes[index].app.name;
                        ::AppendMenuW(reorder, MF_STRING, downId, label.c_str());
                        m_menuHandlers[downId] = [index]() {
                            qttabbar::AppsManagerNative::Instance().MoveRootNodeDown(index);
                        };
                    }
                }
            }
            ::AppendMenuW(menu, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(reorder), L"Reorder");
        }
    }
}

void QTButtonBar::BuildPluginsMenu(HMENU menu) {
    auto plugins = PluginManagerNative::Instance().EnumerateMetadata();
    if(plugins.empty()) {
        ::AppendMenuW(menu, MF_GRAYED | MF_STRING, 0, L"No plugins detected");
        return;
    }

    for(const auto& metadata : plugins) {
        UINT id = 0;
        if(!TryAllocateDynamicCommand(id)) {
            AppendOverflowPlaceholder(menu);
            return;
        }
        bool enabled = metadata.enabled != FALSE;
        UINT flags = MF_STRING;
        if(!enabled) {
            flags |= MF_GRAYED;
        }
        ::AppendMenuW(menu, flags, id, metadata.name);
        m_menuHandlers[id] = [metadata, enabled, this]() {
            std::wstring pluginName(metadata.name);
            if(!enabled) {
                ::MessageBoxW(m_hWnd, L"Plugin is disabled.", pluginName.c_str(), MB_ICONINFORMATION | MB_OK);
                return;
            }
            std::wstring pluginId(metadata.id);
            void* instance = nullptr;
            const PluginClientVTable* vtable = nullptr;
            HRESULT hr = PluginManagerNative::Instance().CreateInstance(pluginId, &instance, &vtable);
            if(FAILED(hr) || instance == nullptr) {
                wchar_t buffer[256] = {};
                _snwprintf_s(buffer, std::size(buffer), L"Failed to load plugin '%s' (0x%08lX)", pluginName.c_str(), hr);
                ::MessageBoxW(m_hWnd, buffer, L"QTTabBar", MB_ICONERROR | MB_OK);
                return;
            }
            struct PluginCleanup {
                void* instance;
                ~PluginCleanup() {
                    if(instance) {
                        PluginManagerNative::Instance().DispatchClose(instance, PluginEndCode::WindowClosed);
                        PluginManagerNative::Instance().DestroyInstance(instance);
                    }
                }
            } cleanup{instance};
            PluginManagerNative::Instance().DispatchOpen(instance, nullptr, m_spExplorer);
            PluginManagerNative::Instance().DispatchMenuClick(instance, PluginMenuType::Bar, pluginName.c_str(), nullptr);
        };
    }
}

void QTButtonBar::BuildMiscToolsMenu(HMENU menu) {
    auto* tabBar = InstanceManagerNative::Instance().FindTabBar(m_explorerHwnd);
    if(tabBar == nullptr) {
        ::AppendMenuW(menu, MF_GRAYED | MF_STRING, 0, L"Tab bar unavailable");
        return;
    }

    std::wstring currentPath = tabBar->GetCurrentPath();
    std::wstring folderName = ExtractLeafName(currentPath);
    std::vector<std::wstring> openTabs = tabBar->GetOpenTabs();

    auto appendAction = [this, menu](const std::wstring& label, std::function<void()> action) {
        UINT id = 0;
        if(!TryAllocateDynamicCommand(id)) {
            AppendOverflowPlaceholder(menu);
            return;
        }
        ::AppendMenuW(menu, MF_STRING, id, label.c_str());
        m_menuHandlers[id] = std::move(action);
    };

    appendAction(L"Copy current path", [currentPath, this]() {
        if(currentPath.empty()) {
            return;
        }
        CopyTextToClipboard(m_hWnd, currentPath);
    });

    appendAction(L"Copy folder name", [folderName, this]() {
        if(folderName.empty()) {
            return;
        }
        CopyTextToClipboard(m_hWnd, folderName);
    });

    appendAction(L"Copy all tab paths", [paths = std::move(openTabs), this]() {
        if(paths.empty()) {
            return;
        }
        std::wstring joined;
        for(size_t i = 0; i < paths.size(); ++i) {
            joined.append(paths[i]);
            if(i + 1 < paths.size()) {
                joined.append(L"\r\n");
            }
        }
        CopyTextToClipboard(m_hWnd, joined);
    });

    appendAction(L"Open options", [this]() {
        InstanceManagerNative::Instance().NotifyButtonCommand(m_explorerHwnd, ID_BUTTONBAR_OPTIONS);
    });
}

void QTButtonBar::ExecuteMenuCommand(UINT commandId) {
    HandleButtonCommand(commandId);
}

bool QTButtonBar::TryAllocateDynamicCommand(UINT& commandId) {
    constexpr UINT kRangeSize = kButtonbarDynamicLast - kButtonbarDynamicFirst + 1;
    if(m_menuHandlers.size() >= static_cast<size_t>(kRangeSize)) {
        ATLTRACE(L"QTButtonBar::TryAllocateDynamicCommand exhausted command range\n");
        return false;
    }

    UINT candidate = m_nextDynamicCommand;
    for(UINT attempts = 0; attempts < kRangeSize; ++attempts) {
        if(candidate > kButtonbarDynamicLast) {
            candidate = kButtonbarDynamicFirst;
        }
        if(m_menuHandlers.find(candidate) == m_menuHandlers.end()) {
            commandId = candidate;
            candidate++;
            if(candidate > kButtonbarDynamicLast) {
                candidate = kButtonbarDynamicFirst;
            }
            m_nextDynamicCommand = candidate;
            return true;
        }
        candidate++;
    }

    ATLTRACE(L"QTButtonBar::TryAllocateDynamicCommand failed to find available id\n");
    return false;
}

void QTButtonBar::AppendOverflowPlaceholder(HMENU menu) const {
    if(menu == nullptr) {
        return;
    }
    wchar_t buffer[128] = {};
    HINSTANCE instance = _AtlBaseModule.GetResourceInstance();
    int length = ::LoadStringW(instance, IDS_BUTTONBAR_MENU_OVERFLOW, buffer, static_cast<int>(_countof(buffer)));
    if(length <= 0) {
        ::StringCchCopyW(buffer, _countof(buffer), L"(too many items)");
    }
    ::AppendMenuW(menu, MF_GRAYED | MF_STRING, 0, buffer);
}

void QTButtonBar::ClearMenuHandlers() {
    m_menuHandlers.clear();
    m_nextDynamicCommand = kButtonbarDynamicFirst;
}

HWND QTButtonBar::ExplorerWindowFromBrowser() const {
    if(!m_spExplorer) {
        return nullptr;
    }
    SHANDLE_PTR handle = 0;
    if(SUCCEEDED(m_spExplorer->get_HWND(&handle))) {
        return reinterpret_cast<HWND>(handle);
    }
    return nullptr;
}

void QTButtonBar::RegisterWithInstanceManager() {
    if(m_explorerHwnd != nullptr) {
        InstanceManagerNative::Instance().RegisterButtonBar(m_explorerHwnd, this);
    }
}

void QTButtonBar::UnregisterWithInstanceManager() {
    InstanceManagerNative::Instance().UnregisterButtonBar(this);
    m_explorerHwnd = nullptr;
}

const std::vector<QTButtonBar::ButtonDefinition>& QTButtonBar::DefaultButtons() {
    static const std::vector<ButtonDefinition> kButtons = {
        {QTTabBarClass::kButtonNavigationBackIndex, ID_BUTTONBAR_NAVIGATION_BACK, L"Back", true, false},
        {QTTabBarClass::kButtonNavigationForwardIndex, ID_BUTTONBAR_NAVIGATION_FORWARD, L"Forward", true, false},
        {QTTabBarClass::kButtonGroupIndex, ID_BUTTONBAR_GROUPS, L"Groups", true, false},
        {QTTabBarClass::kButtonRecentTabsIndex, ID_BUTTONBAR_RECENT_TABS, L"Recent Tabs", true, false},
        {QTTabBarClass::kButtonApplicationsIndex, ID_BUTTONBAR_APPLICATIONS, L"Applications", true, false},
        {QTTabBarClass::kButtonNewWindowIndex, ID_BUTTONBAR_NEW_WINDOW, L"New Window", false, false},
        {QTTabBarClass::kButtonCloneTabIndex, ID_BUTTONBAR_CLONE_TAB, L"Clone", false, false},
        {QTTabBarClass::kButtonLockTabIndex, ID_BUTTONBAR_LOCK_TAB, L"Lock", false, true},
        {QTTabBarClass::kButtonMiscToolsIndex, ID_BUTTONBAR_MISC_TOOLS, L"Tools", true, false},
        {QTTabBarClass::kButtonTopMostIndex, ID_BUTTONBAR_TOPMOST, L"TopMost", false, true},
        {QTTabBarClass::kButtonCloseCurrentIndex, ID_BUTTONBAR_CLOSE_TAB, L"Close", false, false},
        {QTTabBarClass::kButtonCloseOthersIndex, ID_BUTTONBAR_CLOSE_OTHERS, L"Close Others", false, false},
        {QTTabBarClass::kButtonCloseWindowIndex, ID_BUTTONBAR_CLOSE_WINDOW, L"Close Window", false, false},
        {QTTabBarClass::kButtonCloseLeftIndex, ID_BUTTONBAR_CLOSE_LEFT, L"Close Left", false, false},
        {QTTabBarClass::kButtonCloseRightIndex, ID_BUTTONBAR_CLOSE_RIGHT, L"Close Right", false, false},
        {QTTabBarClass::kButtonGoUpIndex, ID_BUTTONBAR_GO_UP, L"Up", false, false},
        {QTTabBarClass::kButtonRefreshIndex, ID_BUTTONBAR_REFRESH, L"Refresh", false, false},
        {QTTabBarClass::kButtonSearchIndex, ID_BUTTONBAR_SEARCH_FOCUS, L"Search", false, false},
        {QTTabBarClass::kButtonWindowOpacityIndex, ID_BUTTONBAR_WINDOW_OPACITY, L"Opacity", true, false},
        {QTTabBarClass::kButtonFilterBarIndex, ID_BUTTONBAR_FILTER_BAR, L"Filter", false, true},
        {QTTabBarClass::kButtonOptionsIndex, ID_BUTTONBAR_OPTIONS, L"Options", false, false},
        {QTTabBarClass::kButtonRecentFilesIndex, ID_BUTTONBAR_RECENT_FILES, L"Recent Files", true, false},
        {QTTabBarClass::kButtonPluginsIndex, ID_BUTTONBAR_PLUGINS, L"Plugins", true, false},
    };
    return kButtons;
}

IFACEMETHODIMP QTButtonBar::GetWindow(HWND* phwnd) {
    if(phwnd == nullptr) {
        return E_POINTER;
    }
    HRESULT hr = EnsureWindow();
    if(FAILED(hr)) {
        return hr;
    }
    *phwnd = m_hWnd;
    return S_OK;
}

IFACEMETHODIMP QTButtonBar::ContextSensitiveHelp(BOOL /*fEnterMode*/) {
    return E_NOTIMPL;
}

IFACEMETHODIMP QTButtonBar::ShowDW(BOOL fShow) {
    HRESULT hr = EnsureWindow();
    if(FAILED(hr)) {
        return hr;
    }
    m_visible = fShow != FALSE;
    if(m_hWnd != nullptr) {
        ::ShowWindow(m_hWnd, fShow ? SW_SHOW : SW_HIDE);
    }
    return S_OK;
}

IFACEMETHODIMP QTButtonBar::CloseDW(DWORD /*dwReserved*/) {
    m_closed = true;
    ShowDW(FALSE);
    DestroyChildWindows();
    UnregisterWithInstanceManager();
    if(m_hWnd != nullptr && ::IsWindow(m_hWnd)) {
        ::DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }
    m_spExplorer.Release();
    m_spInputObjectSite.Release();
    m_spServiceProvider.Release();
    m_spSite.Release();
    m_hwndRebar = nullptr;
    return S_OK;
}

IFACEMETHODIMP QTButtonBar::ResizeBorderDW(LPCRECT /*prcBorder*/, IUnknown* /*punkToolbarSite*/, BOOL /*fReserved*/) {
    return E_NOTIMPL;
}

IFACEMETHODIMP QTButtonBar::GetBandInfo(DWORD dwBandID, DWORD dwViewMode, DESKBANDINFO* pdbi) {
    if(pdbi == nullptr) {
        return E_POINTER;
    }
    m_bandId = dwBandID;
    if((pdbi->dwMask & DBIM_MINSIZE) != 0) {
        pdbi->ptMinSize.x = m_minSize.cx;
        pdbi->ptMinSize.y = m_minSize.cy;
    }
    if((pdbi->dwMask & DBIM_MAXSIZE) != 0) {
        pdbi->ptMaxSize.x = m_maxSize.cx;
        pdbi->ptMaxSize.y = m_maxSize.cy;
    }
    if((pdbi->dwMask & DBIM_ACTUAL) != 0) {
        pdbi->ptActual.x = m_minSize.cx;
        pdbi->ptActual.y = m_minSize.cy;
    }
    if((pdbi->dwMask & DBIM_INTEGRAL) != 0) {
        pdbi->ptIntegral.x = m_minSize.cx;
        pdbi->ptIntegral.y = 0;
    }
    if((pdbi->dwMask & DBIM_MODEFLAGS) != 0) {
        pdbi->dwModeFlags = DBIMF_NORMAL | DBIMF_USECHEVRON;
    }
    if((pdbi->dwMask & DBIM_BKCOLOR) != 0) {
        pdbi->dwMask &= ~DBIM_BKCOLOR;
    }
    if((pdbi->dwMask & DBIM_TITLE) != 0) {
        ::StringCchCopyW(pdbi->wszTitle, ARRAYSIZE(pdbi->wszTitle), L"QTButtonBar");
    }
    return S_OK;
}

IFACEMETHODIMP QTButtonBar::UIActivateIO(BOOL fActivate, MSG* /*pMsg*/) {
    if(fActivate && m_hwndToolbar != nullptr) {
        ::SetFocus(m_hwndToolbar);
    }
    return S_OK;
}

IFACEMETHODIMP QTButtonBar::HasFocusIO() {
    if(m_hWnd == nullptr) {
        return S_FALSE;
    }
    HWND focus = ::GetFocus();
    if(focus == m_hWnd || ::IsChild(m_hWnd, focus)) {
        return S_OK;
    }
    return S_FALSE;
}

IFACEMETHODIMP QTButtonBar::TranslateAcceleratorIO(MSG* /*pMsg*/) {
    return S_FALSE;
}

IFACEMETHODIMP QTButtonBar::SetSite(IUnknown* pUnkSite) {
    if(pUnkSite == nullptr) {
        UnregisterWithInstanceManager();
        m_spInputObjectSite.Release();
        m_spServiceProvider.Release();
        m_spExplorer.Release();
        m_spSite.Release();
        m_hwndRebar = nullptr;
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

    if(m_spExplorer) {
        m_explorerHwnd = ExplorerWindowFromBrowser();
        RegisterWithInstanceManager();
    }

    CComPtr<IOleWindow> spOleWindow;
    if(SUCCEEDED(pUnkSite->QueryInterface(IID_PPV_ARGS(&spOleWindow)))) {
        spOleWindow->GetWindow(&m_hwndRebar);
    }

    return S_OK;
}

IFACEMETHODIMP QTButtonBar::GetSite(REFIID riid, void** ppvSite) {
    if(ppvSite == nullptr) {
        return E_POINTER;
    }
    if(!m_spSite) {
        *ppvSite = nullptr;
        return E_FAIL;
    }
    return m_spSite->QueryInterface(riid, ppvSite);
}

IFACEMETHODIMP QTButtonBar::GetClassID(CLSID* pClassID) {
    if(pClassID == nullptr) {
        return E_POINTER;
    }
    *pClassID = CLSID_QTButtonBar;
    return S_OK;
}

IFACEMETHODIMP QTButtonBar::IsDirty() {
    return S_FALSE;
}

IFACEMETHODIMP QTButtonBar::Load(IStream* /*pStm*/) {
    return S_OK;
}

IFACEMETHODIMP QTButtonBar::Save(IStream* /*pStm*/, BOOL /*fClearDirty*/) {
    return S_OK;
}

IFACEMETHODIMP QTButtonBar::GetSizeMax(ULARGE_INTEGER* pcbSize) {
    if(pcbSize == nullptr) {
        return E_POINTER;
    }
    pcbSize->QuadPart = 0;
    return S_OK;
}
