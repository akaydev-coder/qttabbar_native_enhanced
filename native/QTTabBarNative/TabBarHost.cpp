#include "pch.h"
#include "TabBarHost.h"

#include <Shlwapi.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <VersionHelpers.h>
#include <wincrypt.h>

#include <algorithm>
#include <array>
#include <cwchar>
#include <cwctype>
#include <cstring>
#include <iterator>
#include <utility>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Advapi32.lib")

#include "OptionsDialog.h"
#include "NativeTabControl.h"
#include "QTButtonBar.h"
#include "QTTabBarClass.h"
#include "Resource.h"
#include "AliasStoreNative.h"
#include "ClosedTabHistoryStore.h"
#include "GroupsManagerNative.h"
#include "InstanceManager.h"
#include "InstanceManagerNative.h"
#include "TextInputDialog.h"
#include "Config.h"
#include "ConfigEnums.h"
#include "TabSwitchOverlay.h"
#include "SubDirTipWindow.h"
#include "TaskbarTabPreviewManager.h"

using qttabbar::BindAction;
using qttabbar::MouseChord;
using qttabbar::MouseTarget;

namespace {
std::wstring NormalizeUrlToPath(const std::wstring& url) {
    if(url.empty()) {
        return {};
    }

    if(url.rfind(L"file:", 0) == 0) {
        DWORD required = 0;
        if(PathCreateFromUrlW(url.c_str(), nullptr, &required, 0) == E_POINTER && required > 0) {
            std::wstring buffer(required, L'\0');
            if(SUCCEEDED(PathCreateFromUrlW(url.c_str(), buffer.data(), &required, 0))) {
                buffer.resize(required - 1); // remove trailing null
                return buffer;
            }
        }
    }

    return url;
}

std::wstring JoinTabList(const std::vector<std::wstring>& tabs) {
    std::wstring result;
    for(const auto& path : tabs) {
        if(path.empty()) {
            continue;
        }
        if(!result.empty()) {
            result.append(L";");
        }
        result.append(path);
    }
    return result;
}

std::vector<std::wstring> SplitTabsString(const std::wstring& value) {
    std::vector<std::wstring> output;
    std::wstring token;
    for(wchar_t ch : value) {
        if(ch == L';') {
            if(!token.empty()) {
                output.push_back(token);
                token.clear();
            }
        } else {
            token.push_back(ch);
        }
    }
    if(!token.empty()) {
        output.push_back(token);
    }
    return output;
}

std::wstring ExtractLeafName(const std::wstring& path) {
    if(path.empty()) {
        return {};
    }
    const wchar_t* name = PathFindFileNameW(path.c_str());
    if(name != nullptr && *name != L'\0') {
        return name;
    }
    return path;
}

} // namespace

_ATL_FUNC_INFO TabBarHost::kBeforeNavigate2Info = {CC_STDCALL, VT_EMPTY, 7,
                                                   {VT_DISPATCH, VT_BYREF | VT_VARIANT, VT_BYREF | VT_VARIANT,
                                                    VT_BYREF | VT_VARIANT, VT_BYREF | VT_VARIANT, VT_BYREF | VT_VARIANT,
                                                    VT_BYREF | VT_BOOL}};
_ATL_FUNC_INFO TabBarHost::kNavigateComplete2Info = {CC_STDCALL, VT_EMPTY, 2,
                                                     {VT_DISPATCH, VT_BYREF | VT_VARIANT}};
_ATL_FUNC_INFO TabBarHost::kDocumentCompleteInfo = {CC_STDCALL, VT_EMPTY, 2,
                                                    {VT_DISPATCH, VT_BYREF | VT_VARIANT}};
_ATL_FUNC_INFO TabBarHost::kNavigateErrorInfo = {CC_STDCALL, VT_EMPTY, 4,
                                                 {VT_DISPATCH, VT_BYREF | VT_VARIANT, VT_BYREF | VT_VARIANT,
                                                  VT_BYREF | VT_VARIANT}};
_ATL_FUNC_INFO TabBarHost::kOnQuitInfo = {CC_STDCALL, VT_EMPTY, 0, {}};

TabBarHost::TabBarHost(ITabBarHostOwner& owner) noexcept
    : m_owner(owner)
    , m_browserCookie(0)
    , m_dpiX(96)
    , m_dpiY(96)
    , m_hasFocus(false)
    , m_visible(false)
    , m_useTabSwitcher(true)
    , m_tabSwitcherActive(false)
    , m_tabSwitcherAnchorModifiers(0)
    , m_tabSwitcherTriggerKey(0) {
}

TabBarHost::~TabBarHost() {
    ATLTRACE(L"TabBarHost::~TabBarHost\n");
    DisconnectBrowserEvents();
}

void TabBarHost::Initialize() {
    RestoreSessionState();
    if(m_spBrowser) {
        ConnectBrowserEvents();
    }
}

void TabBarHost::SetExplorer(IWebBrowser2* browser) {
    if(browser == m_spBrowser.p) {
        return;
    }

    DisconnectBrowserEvents();
    m_spBrowser = browser;
    if(m_spBrowser) {
        ConnectBrowserEvents();
        CComBSTR location;
        if(SUCCEEDED(m_spBrowser->get_LocationURL(&location)) && location != nullptr) {
            UpdateActivePath(NormalizeUrlToPath(std::wstring(location, SysStringLen(location))));
        }
    }
}

void TabBarHost::ClearExplorer() {
    DisconnectBrowserEvents();
    m_spBrowser.Release();
}

void TabBarHost::ExecuteCommand(UINT commandId) {
    ATLTRACE(L"TabBarHost::ExecuteCommand %u\n", commandId);
    std::optional<std::size_t> contextIndex = ResolveContextTabIndex();
    std::size_t activeIndex = m_tabControl ? m_tabControl->GetActiveIndex() : 0;
    std::size_t targetIndex = contextIndex.value_or(activeIndex);
    std::wstring targetPath = GetTabPath(targetIndex);

    if(commandId >= ID_CONTEXT_GROUP_BASE && commandId < ID_CONTEXT_HISTORY_BASE) {
        std::size_t groupIndex = commandId - ID_CONTEXT_GROUP_BASE;
        if(!targetPath.empty()) {
            if(auto group = qttabbar::GroupsManagerNative::Instance().GetGroupByIndex(groupIndex)) {
                qttabbar::GroupsManagerNative::Instance().AppendPaths(group->name, {targetPath});
            }
        }
        m_contextTabIndex.reset();
        return;
    }

    if(commandId >= ID_CONTEXT_HISTORY_BASE && commandId <= ID_CONTEXT_DYNAMIC_LAST) {
        std::size_t historyIndex = commandId - ID_CONTEXT_HISTORY_BASE;
        RestoreClosedTabByIndex(historyIndex);
        m_contextTabIndex.reset();
        return;
    }

    switch(commandId) {
    case ID_CONTEXT_NEWTAB:
        AddTab(m_currentPath, true, true);
        break;
    case ID_CONTEXT_CLOSETAB:
        CloseTabAt(targetIndex);
        break;
    case ID_CONTEXT_CLOSE_LEFT:
        if(m_tabControl) {
            auto closed = m_tabControl->CloseTabsToLeft(targetIndex);
            if(!closed.empty()) {
                for(const auto& path : closed) {
                    RecordClosedEntry(path);
                }
                PersistClosedHistory();
            }
        }
        break;
    case ID_CONTEXT_CLOSE_RIGHT:
        if(m_tabControl) {
            auto closed = m_tabControl->CloseTabsToRight(targetIndex);
            if(!closed.empty()) {
                for(const auto& path : closed) {
                    RecordClosedEntry(path);
                }
                PersistClosedHistory();
            }
        }
        break;
    case ID_CONTEXT_CLOSE_OTHERS:
        if(m_tabControl) {
            auto closed = m_tabControl->CloseAllExcept(targetIndex);
            if(!closed.empty()) {
                for(const auto& path : closed) {
                    RecordClosedEntry(path);
                }
                PersistClosedHistory();
            }
        }
        break;
    case ID_CONTEXT_RESTORELASTCLOSED:
        RestoreLastClosed();
        break;
    case ID_CONTEXT_TOGGLE_LOCK:
        ToggleLockTab(targetIndex);
        break;
    case ID_CONTEXT_CLONE_TAB:
        if(!targetPath.empty()) {
            AddTab(targetPath, true, true);
        }
        break;
    case ID_CONTEXT_CREATE_GROUP:
        if(!targetPath.empty()) {
            std::wstring caption = LoadStringResource(IDS_CONTEXT_DIALOG_GROUP_TITLE, L"Create Group");
            std::wstring prompt = LoadStringResource(IDS_CONTEXT_DIALOG_GROUP_PROMPT, L"Enter a name for the new group:");
            if(auto name = TextInputDialog::Show(m_hWnd, caption, prompt, {}); name && !name->empty()) {
                qttabbar::GroupsManagerNative::Instance().AddGroup(*name, {targetPath});
            }
        }
        break;
    case ID_CONTEXT_OPEN_NEW_WINDOW:
        if(!targetPath.empty()) {
            ::ShellExecuteW(nullptr, L"open", L"explorer.exe", targetPath.c_str(), nullptr, SW_SHOWNORMAL);
        }
        break;
    case ID_CONTEXT_COPY_PATH:
        CopyPathToClipboard(targetPath);
        break;
    case ID_CONTEXT_PROPERTIES:
        ShowProperties(targetPath);
        break;
    case ID_CONTEXT_HISTORY_CLEAR:
        ClearClosedHistory();
        break;
    case ID_CONTEXT_OPEN_COMMAND:
        OpenCommandPrompt(targetPath);
        break;
    case ID_CONTEXT_EDIT_ALIAS: {
        if(targetPath.empty()) {
            break;
        }
        std::wstring caption = LoadStringResource(IDS_CONTEXT_DIALOG_ALIAS_TITLE, L"Tab Alias");
        std::wstring prompt = LoadStringResource(IDS_CONTEXT_DIALOG_ALIAS_PROMPT, L"Enter an alias for this tab:");
        std::wstring initial;
        if(auto existing = qttabbar::AliasStoreNative::Instance().GetAlias(targetPath)) {
            initial = *existing;
        }
        auto alias = TextInputDialog::Show(m_hWnd, caption, prompt, initial);
        if(alias.has_value()) {
            std::wstring trimmed = *alias;
            trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), [](wchar_t ch) { return !iswspace(ch); }));
            while(!trimmed.empty() && iswspace(trimmed.back())) {
                trimmed.pop_back();
            }
            SetTabAlias(targetIndex, trimmed);
            if(trimmed.empty()) {
                qttabbar::AliasStoreNative::Instance().ClearAlias(targetPath);
            } else {
                qttabbar::AliasStoreNative::Instance().SetAlias(targetPath, trimmed);
            }
        }
        break;
    }
    case ID_CONTEXT_REFRESH:
        RefreshExplorer();
        break;
    case ID_CONTEXT_OPTIONS:
        OpenOptions();
        break;
    default:
        break;
    }

    m_contextTabIndex.reset();
}

void TabBarHost::ShowContextMenu(const POINT& screenPoint) {
    if(!m_tabControl) {
        return;
    }

    auto targetIndexOpt = ResolveContextTabIndex();
    std::size_t activeIndex = m_tabControl->GetActiveIndex();
    std::size_t targetIndex = targetIndexOpt.value_or(activeIndex);
    std::size_t tabCount = m_tabControl->GetCount();
    if(tabCount == 0) {
        targetIndex = 0;
    } else if(targetIndex >= tabCount) {
        targetIndex = tabCount - 1;
    }
    std::wstring targetPath = GetTabPath(targetIndex);

    HMENU menu = BuildContextMenu(targetIndex, targetPath);
    if(menu == nullptr) {
        return;
    }

    ::TrackPopupMenuEx(menu, TPM_LEFTALIGN | TPM_LEFTBUTTON, screenPoint.x, screenPoint.y, m_hWnd, nullptr);
    ::DestroyMenu(menu);
}

void TabBarHost::SaveSessionState() const {
    std::vector<std::wstring> paths = m_tabControl ? m_tabControl->GetTabPaths() : std::vector<std::wstring>();
    std::wstring serialized = JoinTabList(paths);
    ATLTRACE(L"TabBarHost::SaveSessionState tabs='%s'\n", serialized.c_str());

    CRegKey key;
    if(key.Create(HKEY_CURRENT_USER, kRegistryRoot) == ERROR_SUCCESS) {
        key.SetStringValue(kTabsValueName, serialized.c_str());
    }
}

void TabBarHost::RestoreSessionState() {
    CRegKey key;
    if(key.Open(HKEY_CURRENT_USER, kRegistryRoot, KEY_READ) != ERROR_SUCCESS) {
        return;
    }

    ULONG chars = 0;
    if(key.QueryStringValue(kTabsValueName, nullptr, &chars) != ERROR_SUCCESS || chars == 0) {
        return;
    }

    std::wstring buffer(chars, L'\0');
    if(key.QueryStringValue(kTabsValueName, buffer.data(), &chars) == ERROR_SUCCESS) {
        if(!buffer.empty() && buffer.back() == L'\0') {
            buffer.pop_back();
        }
        auto restored = SplitTabsString(buffer);
        if(m_tabControl) {
            for(const auto& tabPath : restored) {
                m_tabControl->AddTab(tabPath, false, true);
            }
            if(m_tabControl->GetCount() > 0) {
                auto path = m_tabControl->ActivateTab(0);
                m_currentPath = path;
            }
        }
        LogTabsState(L"RestoreSessionState");
    }
}

void TabBarHost::OnBandVisibilityChanged(bool visible) {
    m_visible = visible;
    ATLTRACE(L"TabBarHost::OnBandVisibilityChanged visible=%d\n", visible);
    if(visible) {
        StartTimers();
    } else {
        StopTimers();
    }
}

bool TabBarHost::HandleMouseAction(MouseTarget target, MouseChord chord, std::optional<std::size_t> tabIndex) {
    if(!Any(chord)) {
        return false;
    }

    auto action = LookupMouseAction(target, chord);
    if(!action) {
        action = LookupMouseAction(MouseTarget::Anywhere, chord);
    }
    if(!action) {
        return false;
    }
    return ExecuteBindAction(*action, false, tabIndex);
}

bool TabBarHost::HandleAccelerator(MSG* pMsg) {
    if(pMsg == nullptr) {
        return false;
    }
    if(pMsg->message != WM_KEYDOWN && pMsg->message != WM_SYSKEYDOWN) {
        return false;
    }

    UINT vk = static_cast<UINT>(pMsg->wParam);
    UINT modifiers = CurrentModifierMask();
    bool isRepeat = (HIWORD(pMsg->lParam) & KF_REPEAT) != 0;
    auto action = LookupKeyboardAction(vk, modifiers);
    if(!action) {
        return false;
    }
    return ExecuteBindAction(*action, isRepeat);
}

bool TabBarHost::ExecuteBindAction(BindAction action, bool isRepeat, std::optional<std::size_t> tabIndex) {
    if(isRepeat && !IsRepeatAllowed(action)) {
        return false;
    }

    auto resolveTabIndex = [&]() -> std::optional<std::size_t> {
        if(!m_tabControl || m_tabControl->GetCount() == 0) {
            return std::nullopt;
        }
        if(tabIndex && *tabIndex < m_tabControl->GetCount()) {
            return tabIndex;
        }
        if(tabIndex) {
            return std::nullopt;
        }
        return m_tabControl->GetActiveIndex();
    };

    auto index = resolveTabIndex();

    switch(action) {
    case BindAction::GoBack:
        NavigateBack();
        return true;
    case BindAction::GoForward:
        NavigateForward();
        return true;
    case BindAction::GoFirst:
    case BindAction::FirstTab:
        ActivateFirstTab();
        return true;
    case BindAction::GoLast:
    case BindAction::LastTab:
        ActivateLastTab();
        return true;
    case BindAction::NextTab:
        ActivateNextTab();
        return true;
    case BindAction::PreviousTab:
        ActivatePreviousTab();
        return true;
    case BindAction::NewTab: {
        std::wstring path = ResolveTabPath(tabIndex);
        if(path.empty()) {
            return false;
        }
        AddTab(path, true, true);
        return true;
    }
    case BindAction::NewWindow: {
        std::wstring path = ResolveTabPath(tabIndex);
        if(path.empty()) {
            return false;
        }
        OpenNewWindowAtPath(path);
        return true;
    }
    case BindAction::CloseCurrent:
    case BindAction::CloseTab:
        if(index) {
            CloseTabAt(*index);
            return true;
        }
        return false;
    case BindAction::CloseAllButCurrent:
    case BindAction::CloseAllButThis:
        if(index) {
            CloseAllTabsExcept(*index);
            return true;
        }
        return false;
    case BindAction::CloseLeft:
    case BindAction::CloseLeftTab:
        if(index) {
            CloseTabsToLeftOf(*index);
            return true;
        }
        return false;
    case BindAction::CloseRight:
    case BindAction::CloseRightTab:
        if(index) {
            CloseTabsToRightOf(*index);
            return true;
        }
        return false;
    case BindAction::CloseWindow:
        if(m_spBrowser) {
            m_spBrowser->Quit();
            return true;
        }
        if(HWND explorer = m_owner.GetHostWindow()) {
            ::PostMessageW(explorer, WM_CLOSE, 0, 0);
            return true;
        }
        return false;
    case BindAction::RestoreLastClosed:
        RestoreLastClosed();
        return true;
    case BindAction::CloneCurrent:
    case BindAction::CloneTab:
        if(index) {
            CloneTabAt(*index);
            return true;
        }
        return false;
    case BindAction::TearOffCurrent:
    case BindAction::TearOffTab:
        if(index) {
            TearOffTab(*index);
            return true;
        }
        return false;
    case BindAction::LockCurrent:
    case BindAction::LockTab:
        if(index) {
            ToggleLockTab(*index);
            return true;
        }
        return false;
    case BindAction::LockAll:
        ToggleLockAllTabs();
        return true;
    case BindAction::BrowseFolder:
        return BrowseForFolder();
    case BindAction::CreateNewGroup: {
        std::wstring path = ResolveTabPath(tabIndex);
        if(path.empty()) {
            return false;
        }
        std::wstring caption = LoadStringResource(IDS_CONTEXT_DIALOG_GROUP_TITLE, L"Create Group");
        std::wstring prompt = LoadStringResource(IDS_CONTEXT_DIALOG_GROUP_PROMPT, L"Enter a name for the new group:");
        auto name = TextInputDialog::Show(m_hWnd, caption, prompt, {});
        if(!name || name->empty()) {
            return false;
        }
        qttabbar::GroupsManagerNative::Instance().AddGroup(*name, {path});
        return true;
    }
    case BindAction::ShowOptions:
        OpenOptions();
        return true;
    case BindAction::ShowToolbarMenu:
        return ShowToolbarMenu();
    case BindAction::ShowTabMenuCurrent: {
        auto menuIndex = index;
        if(!menuIndex) {
            menuIndex = resolveTabIndex();
        }
        POINT origin{};
        return ShowTabMenuAtIndex(menuIndex, origin);
    }
    case BindAction::ShowTabMenu:
        return ShowTabMenuAtCursor();
    case BindAction::ShowGroupMenu:
        return ShowButtonBarMenu(ID_BUTTONBAR_GROUPS);
    case BindAction::ShowRecentTabsMenu:
        return ShowButtonBarMenu(ID_BUTTONBAR_RECENT_TABS);
    case BindAction::ShowUserAppsMenu:
        return ShowButtonBarMenu(ID_BUTTONBAR_APPLICATIONS);
    case BindAction::ShowRecentFilesMenu:
        return ShowButtonBarMenu(ID_BUTTONBAR_RECENT_FILES);
    case BindAction::CopySelectedPaths:
        return DoFileTools(0);
    case BindAction::CopySelectedNames:
        return DoFileTools(1);
    case BindAction::CopyCurrentFolderPath:
        CopyCurrentFolderPathToClipboard();
        return true;
    case BindAction::CopyCurrentFolderName:
        CopyCurrentFolderNameToClipboard();
        return true;
    case BindAction::ChecksumSelected:
        return DoFileTools(4);
    case BindAction::ToggleTopMost:
        return ToggleTopMost();
    case BindAction::TransparencyPlus:
    case BindAction::TransparencyMinus:
        return AdjustTransparency(action == BindAction::TransparencyPlus);
    case BindAction::FocusFileList:
        return FocusExplorerView();
    case BindAction::FocusSearchBarReal:
        return FocusSearchBand();
    case BindAction::FocusSearchBarBBar:
        return FocusButtonBarSearch();
    case BindAction::ShowSDTSelected:
        return ShowSelectedSubDirTip();
    case BindAction::SendToTray:
        return SendWindowToTray();
    case BindAction::FocusTabBar:
        if(m_tabControl) {
            m_tabControl->FocusTabBar();
            return true;
        }
        return false;
    case BindAction::SortTabsByName:
    case BindAction::SortTabsByPath:
    case BindAction::SortTabsByActive:
        ATLTRACE(L"TabBarHost::ExecuteBindAction SortTabs not implemented\n");
        return false;
    case BindAction::SwitchToLastActivated:
        ATLTRACE(L"TabBarHost::ExecuteBindAction SwitchToLastActivated not implemented\n");
        return false;
    case BindAction::MergeWindows:
        return MergeAllWindows();
    case BindAction::NewFile:
        return CreateNewItem(false);
    case BindAction::NewFolder:
        return CreateNewItem(true);
    case BindAction::UpOneLevelTab:
    case BindAction::UpOneLevel:
        GoUpOneLevel();
        return true;
    case BindAction::Refresh:
        RefreshExplorer();
        return true;
    case BindAction::Paste:
        return InvokeShellVerb(L"paste");
    case BindAction::Maximize:
        if(HWND explorer = m_owner.GetHostWindow()) {
            ::ShowWindow(explorer, SW_MAXIMIZE);
            return true;
        }
        return false;
    case BindAction::Minimize:
        if(HWND explorer = m_owner.GetHostWindow()) {
            ::ShowWindow(explorer, SW_MINIMIZE);
            return true;
        }
        return false;
    case BindAction::CopyTabPath:
        if(index) {
            CopyTabPathToClipboard(*index);
            return true;
        }
        return false;
    case BindAction::TabProperties:
        if(index) {
            std::wstring path = ResolveTabPath(index);
            if(path.empty()) {
                return false;
            }
            ShowProperties(path);
            return true;
        }
        return false;
    case BindAction::ShowTabSubfolderMenu:
        if(index) {
            return ShowTabSubfolderMenu(*index);
        }
        if(auto active = resolveTabIndex()) {
            return ShowTabSubfolderMenu(*active);
        }
        return false;
    case BindAction::ItemOpenInNewTab:
    case BindAction::ItemOpenInNewTabNoSel:
    case BindAction::ItemOpenInNewWindow:
    case BindAction::ItemCut:
    case BindAction::ItemCopy:
    case BindAction::ItemDelete:
    case BindAction::ItemProperties:
    case BindAction::CopyItemPath:
    case BindAction::CopyItemName:
    case BindAction::ChecksumItem:
    case BindAction::ItemsOpenInNewTabNoSel:
    case BindAction::SortTab:
    case BindAction::TurnOffRepeat:
        return ExecuteItemCommand(action);
    case BindAction::OpenCmd: {
        std::wstring path = ResolveTabPath(tabIndex);
        if(path.empty()) {
            return false;
        }
        OpenCommandPrompt(path);
        return true;
    }
    case BindAction::Nothing:
        return false;
    default:
        ATLTRACE(L"TabBarHost::ExecuteBindAction unknown action %u\n", static_cast<UINT>(action));
        return false;
    }
}

std::optional<BindAction> TabBarHost::ResolveFolderLinkAction(MouseChord chord) const {
    if(!Any(chord)) {
        return std::nullopt;
    }
    auto action = LookupMouseAction(MouseTarget::FolderLink, chord);
    if(!action) {
        action = LookupMouseAction(MouseTarget::Anywhere, chord);
    }
    return action;
}

bool TabBarHost::HandleFolderLinkAction(BindAction action, const std::wstring& path) {
    switch(action) {
    case BindAction::ItemOpenInNewTab:
        OpenFolderInNewTab(path, true);
        return true;
    case BindAction::ItemOpenInNewTabNoSel:
    case BindAction::ItemsOpenInNewTabNoSel:
        OpenFolderInNewTab(path, false);
        return true;
    case BindAction::ItemOpenInNewWindow:
        OpenFolderInNewWindow(path);
        return true;
    case BindAction::Nothing:
        return true;
    default:
        return ExecuteBindAction(action);
    }
}

void TabBarHost::OpenFolderInNewTab(const std::wstring& path, bool activate) {
    if(path.empty()) {
        return;
    }
    AddTab(path, activate, true);
}

void TabBarHost::OpenFolderInNewWindow(const std::wstring& path) {
    OpenNewWindowAtPath(path);
}

void TabBarHost::OnParentDestroyed() {
    StopTimers();
    SaveSessionState();
    DisconnectBrowserEvents();
    HideTabSwitcher(false);
    HideSubDirTip();
}

LRESULT TabBarHost::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    ATLTRACE(L"TabBarHost::OnCreate\n");
    if(!m_tabControl) {
        m_tabControl = std::make_unique<NativeTabControl>(*this);
    }
    if(m_tabControl && !m_tabControl->IsWindow()) {
        RECT rc{};
        ::GetClientRect(m_hWnd, &rc);
        HWND hwnd = m_tabControl->Create(m_hWnd, rc, L"",
                                         WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
        if(hwnd == nullptr) {
            return -1;
        }
    }
    LoadClosedHistory();
    ReloadConfiguration();
    if(!m_subDirTipWindow) {
        m_subDirTipWindow = std::make_unique<SubDirTipWindow>(*this);
        m_subDirTipWindow->ApplyConfiguration(m_config);
    }
    if(!m_taskbarPreviews) {
        m_taskbarPreviews = std::make_unique<TaskbarTabPreviewManager>();
        m_taskbarPreviews->SetOwner(m_owner.GetHostWindow(),
                                    [this](std::size_t index) { ActivateTabByIndex(index); },
                                    [this](std::size_t index) { CloseTabAt(index); });
    }
    RefreshTaskbarPreviews();
    StartTimers();
#if defined(_WIN32_WINNT) && _WIN32_WINNT >= _WIN32_WINNT_WIN10
    if(::IsWindows10OrGreater()) {
        m_dpiX = ::GetDpiForWindow(m_hWnd);
        m_dpiY = m_dpiX;
    }
#endif
    return 0;
}

LRESULT TabBarHost::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    ATLTRACE(L"TabBarHost::OnDestroy\n");
    StopTimers();
    SaveSessionState();
    DisconnectBrowserEvents();
    HideTabSwitcher(false);
    HideSubDirTip();
    if(m_subDirTipWindow) {
        if(m_subDirTipWindow->IsWindow()) {
            m_subDirTipWindow->DestroyWindow();
        }
        m_subDirTipWindow.reset();
    }
    if(m_taskbarPreviews) {
        m_taskbarPreviews->Clear();
        m_taskbarPreviews.reset();
    }
    if(m_tabControl) {
        if(m_tabControl->IsWindow()) {
            m_tabControl->DestroyWindow();
        }
        m_tabControl.reset();
    }
    return 0;
}

LRESULT TabBarHost::OnSize(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    bHandled = TRUE;
    int width = LOWORD(lParam);
    int height = HIWORD(lParam);
    if(m_tabControl && m_tabControl->IsWindow()) {
        ::MoveWindow(m_tabControl->m_hWnd, 0, 0, width, height, TRUE);
        m_tabControl->EnsureLayout();
    }
    return 0;
}

LRESULT TabBarHost::OnTimer(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    if(wParam == ID_TIMER_SELECTTAB) {
        ATLTRACE(L"TabBarHost::OnTimer selectTab\n");
    } else if(wParam == ID_TIMER_CONTEXTMENU) {
        ATLTRACE(L"TabBarHost::OnTimer contextMenu\n");
    } else if(wParam == ID_TIMER_SUBDIRTIP) {
        ATLTRACE(L"TabBarHost::OnTimer subDirTip\n");
        if(m_subDirTipTimer) {
            ::KillTimer(m_hWnd, ID_TIMER_SUBDIRTIP);
            m_subDirTipTimer = 0;
        }
        if(m_pendingSubDirTipIndex) {
            ShowSubDirTip(*m_pendingSubDirTipIndex);
        }
    }
    return 0;
}

LRESULT TabBarHost::OnContextMenu(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    bHandled = TRUE;
    POINT pt{};
    if(reinterpret_cast<HWND>(wParam) == m_hWnd) {
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
        if(pt.x == -1 && pt.y == -1) {
            RECT rc{};
            ::GetClientRect(m_hWnd, &rc);
            pt.x = rc.left;
            pt.y = rc.bottom;
            ::ClientToScreen(m_hWnd, &pt);
        }
    } else {
        ::GetCursorPos(&pt);
    }
    m_contextTabIndex.reset();
    ShowContextMenu(pt);
    return 0;
}

LRESULT TabBarHost::OnCommand(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    ExecuteCommand(LOWORD(wParam));
    return 0;
}

LRESULT TabBarHost::OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    m_hasFocus = true;
    ATLTRACE(L"TabBarHost::OnSetFocus\n");
    if(m_tabControl && m_tabControl->IsWindow()) {
        ::SetFocus(m_tabControl->m_hWnd);
    }
    m_owner.NotifyTabHostFocusChange(TRUE);
    return 0;
}

LRESULT TabBarHost::OnKillFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    m_hasFocus = false;
    ATLTRACE(L"TabBarHost::OnKillFocus\n");
    HideTabSwitcher(false);
    m_owner.NotifyTabHostFocusChange(FALSE);
    return 0;
}

LRESULT TabBarHost::OnDpiChanged(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    bHandled = TRUE;
    UINT newDpiX = LOWORD(wParam);
    UINT newDpiY = HIWORD(wParam);
    ATLTRACE(L"TabBarHost::OnDpiChanged %u %u\n", newDpiX, newDpiY);
    m_dpiX = newDpiX;
    m_dpiY = newDpiY;
    if(lParam != 0) {
        const RECT* suggested = reinterpret_cast<RECT*>(lParam);
        ::SetWindowPos(m_hWnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                       suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    return 0;
}

LRESULT TabBarHost::OnGetDlgCode(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    return DLGC_WANTARROWS | DLGC_WANTTAB | DLGC_WANTCHARS;
}

LRESULT TabBarHost::OnKeyDown(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    bHandled = TRUE;
    UINT vk = static_cast<UINT>(wParam);
    if(vk == VK_SHIFT && m_config.tips.subDirTipsWithShift) {
        if(m_pendingSubDirTipIndex) {
            if(m_subDirTipTimer) {
                ::KillTimer(m_hWnd, ID_TIMER_SUBDIRTIP);
                m_subDirTipTimer = 0;
            }
            ShowSubDirTip(*m_pendingSubDirTipIndex);
        }
    }
    if(m_tabSwitcher && m_tabSwitcher->IsVisible() && vk == VK_ESCAPE) {
        HideTabSwitcher(false);
        return 0;
    }

    UINT modifiers = CurrentModifierMask();
    bool isRepeat = (HIWORD(lParam) & KF_REPEAT) != 0;
    if(HandleTabSwitcherShortcut(vk, modifiers, isRepeat)) {
        return 0;
    }

    MSG msg{};
    msg.message = WM_KEYDOWN;
    msg.wParam = wParam;
    msg.lParam = lParam;
    if(!HandleAccelerator(&msg)) {
        bHandled = FALSE;
    }
    return 0;
}

LRESULT TabBarHost::OnKeyUp(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = FALSE;
    UINT vk = static_cast<UINT>(wParam);
    if(vk == VK_SHIFT && m_config.tips.subDirTipsWithShift) {
        HideSubDirTip();
    }
    if(!m_tabSwitcher || !m_tabSwitcher->IsVisible()) {
        return 0;
    }

    UINT modifiers = CurrentModifierMask();
    if(m_tabSwitcherAnchorModifiers != 0) {
        if((modifiers & m_tabSwitcherAnchorModifiers) == 0) {
            HideTabSwitcher(true);
            bHandled = TRUE;
        }
    } else if(vk == m_tabSwitcherTriggerKey) {
        HideTabSwitcher(true);
        bHandled = TRUE;
    }
    return 0;
}

LRESULT TabBarHost::OnSettingChange(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
    bHandled = FALSE;
    if(lParam != 0) {
        const wchar_t* name = reinterpret_cast<const wchar_t*>(lParam);
        if(name != nullptr && _wcsicmp(name, L"QTTabBar.ConfigChanged") == 0) {
            ReloadConfiguration();
            bHandled = TRUE;
        }
    }
    return 0;
}

void __stdcall TabBarHost::OnBeforeNavigate2(IDispatch* /*pDisp*/, VARIANT* url, VARIANT* /*flags*/, VARIANT* /*targetFrameName*/,
                                             VARIANT* /*postData*/, VARIANT* /*headers*/, VARIANT_BOOL* /*cancel*/) {
    std::wstring path = NormalizeUrlToPath(VariantToString(url));
    ATLTRACE(L"TabBarHost::OnBeforeNavigate2 %s\n", path.c_str());
    if(!path.empty()) {
        m_pendingNavigation = path;
    }
}

void __stdcall TabBarHost::OnNavigateComplete2(IDispatch* /*pDisp*/, VARIANT* url) {
    std::wstring path = NormalizeUrlToPath(VariantToString(url));
    ATLTRACE(L"TabBarHost::OnNavigateComplete2 %s\n", path.c_str());
    UpdateActivePath(path);
}

void __stdcall TabBarHost::OnDocumentComplete(IDispatch* /*pDisp*/, VARIANT* url) {
    std::wstring path = NormalizeUrlToPath(VariantToString(url));
    ATLTRACE(L"TabBarHost::OnDocumentComplete %s\n", path.c_str());
    UpdateActivePath(path);
}

void __stdcall TabBarHost::OnNavigateError(IDispatch* /*pDisp*/, VARIANT* url, VARIANT* /*targetFrameName*/, VARIANT* statusCode,
                                           VARIANT_BOOL* /*cancel*/) {
    std::wstring path = NormalizeUrlToPath(VariantToString(url));
    std::wstring status = VariantToString(statusCode);
    ATLTRACE(L"TabBarHost::OnNavigateError %s status=%s\n", path.c_str(), status.c_str());
}

void __stdcall TabBarHost::OnQuit() {
    ATLTRACE(L"TabBarHost::OnQuit\n");
    SaveSessionState();
}

void TabBarHost::ConnectBrowserEvents() {
    if(!m_spBrowser || m_browserCookie != 0) {
        return;
    }
    ATLTRACE(L"TabBarHost::ConnectBrowserEvents\n");
    if(SUCCEEDED(DispEventAdvise(m_spBrowser))) {
        m_browserCookie = 1;
    }
}

void TabBarHost::DisconnectBrowserEvents() {
    if(m_spBrowser && m_browserCookie != 0) {
        ATLTRACE(L"TabBarHost::DisconnectBrowserEvents\n");
        DispEventUnadvise(m_spBrowser);
        m_browserCookie = 0;
    }
}

void TabBarHost::StartTimers() {
    if(!m_visible) {
        m_visible = true;
    }
    if(m_hWnd != nullptr) {
        ::SetTimer(m_hWnd, ID_TIMER_SELECTTAB, kSelectTabTimerMs, nullptr);
        ::SetTimer(m_hWnd, ID_TIMER_CONTEXTMENU, kContextMenuTimerMs, nullptr);
    }
}

void TabBarHost::StopTimers() {
    if(m_hWnd != nullptr) {
        ::KillTimer(m_hWnd, ID_TIMER_SELECTTAB);
        ::KillTimer(m_hWnd, ID_TIMER_CONTEXTMENU);
        ::KillTimer(m_hWnd, ID_TIMER_SUBDIRTIP);
        m_subDirTipTimer = 0;
    }
}

std::wstring TabBarHost::VariantToString(const VARIANT* value) const {
    if(value == nullptr) {
        return {};
    }
    CComVariant normalized;
    if(FAILED(normalized.Copy(value))) {
        return {};
    }
    if(FAILED(normalized.ChangeType(VT_BSTR))) {
        return {};
    }
    if(normalized.bstrVal == nullptr) {
        return {};
    }
    return std::wstring(normalized.bstrVal, SysStringLen(normalized.bstrVal));
}

void TabBarHost::UpdateActivePath(const std::wstring& path) {
    if(path.empty()) {
        return;
    }
    std::wstring effectivePath = path;
    if(m_pendingNavigation.has_value()) {
        effectivePath = *m_pendingNavigation;
        m_pendingNavigation.reset();
    }

    m_currentPath = effectivePath;
    AddTab(effectivePath, true, false);
    RefreshTaskbarPreviews();
    LogTabsState(L"UpdateActivePath");
}

void TabBarHost::AddTab(const std::wstring& path, bool makeActive, bool allowDuplicate) {
    if(path.empty()) {
        return;
    }
    if(m_tabControl) {
        std::size_t index = m_tabControl->AddTab(path, makeActive, allowDuplicate);
        if(auto alias = qttabbar::AliasStoreNative::Instance().GetAlias(path)) {
            m_tabControl->SetAlias(index, *alias);
        }
    }
    if(makeActive) {
        m_currentPath = path;
    }
    RefreshTaskbarPreviews();
}

void TabBarHost::ActivateTab(std::size_t index) {
    if(!m_tabControl) {
        return;
    }
    std::wstring path = m_tabControl->ActivateTab(index);
    if(!path.empty()) {
        m_currentPath = path;
    }
    RefreshTaskbarPreviews();
    LogTabsState(L"ActivateTab");
}

void TabBarHost::ActivateNextTab() {
    if(!m_tabControl || m_tabControl->GetCount() == 0) {
        return;
    }
    std::wstring path = m_tabControl->ActivateNextTab();
    if(!path.empty()) {
        m_currentPath = path;
    }
    RefreshTaskbarPreviews();
}

void TabBarHost::ActivatePreviousTab() {
    if(!m_tabControl || m_tabControl->GetCount() == 0) {
        return;
    }
    std::wstring path = m_tabControl->ActivatePreviousTab();
    if(!path.empty()) {
        m_currentPath = path;
    }
    RefreshTaskbarPreviews();
}

void TabBarHost::ActivateFirstTab() {
    if(!m_tabControl || m_tabControl->GetCount() == 0) {
        return;
    }
    ActivateTab(0);
}

void TabBarHost::ActivateLastTab() {
    if(!m_tabControl || m_tabControl->GetCount() == 0) {
        return;
    }
    std::size_t last = m_tabControl->GetCount() - 1;
    ActivateTab(last);
}

void TabBarHost::CloseActiveTab() {
    if(!m_tabControl || m_tabControl->GetCount() == 0) {
        return;
    }
    CloseTabAt(m_tabControl->GetActiveIndex());
}

void TabBarHost::RestoreLastClosed() {
    if(m_closedHistory.empty()) {
        return;
    }
    std::wstring path = m_closedHistory.front();
    m_closedHistory.pop_front();
    ATLTRACE(L"TabBarHost::RestoreLastClosed %s\n", path.c_str());
    AddTab(path, true, true);
    PersistClosedHistory();
}

void TabBarHost::CloseTabAt(std::size_t index) {
    if(!m_tabControl || index >= m_tabControl->GetCount()) {
        return;
    }
    auto closed = m_tabControl->CloseTab(index);
    if(!closed) {
        return;
    }
    RecordClosedEntry(*closed);
    if(auto active = m_tabControl->GetActivePath()) {
        m_currentPath = *active;
    } else {
        m_currentPath.clear();
    }
    PersistClosedHistory();
    RefreshTaskbarPreviews();
    LogTabsState(L"CloseTabAt");
}

void TabBarHost::RefreshExplorer() {
    if(m_spBrowser) {
        ATLTRACE(L"TabBarHost::RefreshExplorer\n");
        m_spBrowser->Refresh();
    }
}

void TabBarHost::OpenOptions() {
    ATLTRACE(L"TabBarHost::OpenOptions\n");
    HWND ownerHwnd = m_owner.GetHostRebarWindow();
    if(ownerHwnd == nullptr) {
        ownerHwnd = m_owner.GetHostWindow();
    }
    qttabbar::OptionsDialog::Open(ownerHwnd);
}

std::vector<std::wstring> TabBarHost::GetOpenTabs() const {
    if(!m_tabControl) {
        return {};
    }
    return m_tabControl->GetTabPaths();
}

std::vector<std::wstring> TabBarHost::GetClosedTabHistory() const {
    return std::vector<std::wstring>(m_closedHistory.begin(), m_closedHistory.end());
}

void TabBarHost::ActivateTabByIndex(std::size_t index) {
    ActivateTab(index);
}

void TabBarHost::RestoreClosedTabByIndex(std::size_t index) {
    if(index >= m_closedHistory.size()) {
        return;
    }
    auto it = m_closedHistory.begin();
    std::advance(it, static_cast<std::ptrdiff_t>(index));
    std::wstring path = *it;
    m_closedHistory.erase(it);
    AddTab(path, true, true);
    PersistClosedHistory();
}

void TabBarHost::CloneActiveTab() {
    if(m_currentPath.empty()) {
        return;
    }
    AddTab(m_currentPath, true, true);
}

void TabBarHost::CloneTabAt(std::size_t index) {
    if(!m_tabControl || index >= m_tabControl->GetCount()) {
        return;
    }
    std::wstring path = m_tabControl->GetPath(index);
    if(path.empty()) {
        return;
    }
    AddTab(path, true, true);
}

void TabBarHost::TearOffTab(std::size_t index) {
    if(!m_tabControl || index >= m_tabControl->GetCount()) {
        return;
    }
    if(m_tabControl->IsLocked(index)) {
        return;
    }
    std::wstring path = m_tabControl->GetPath(index);
    if(path.empty()) {
        return;
    }
    OpenNewWindowAtPath(path);
    CloseTabAt(index);
}

void TabBarHost::ToggleLockAllTabs() {
    if(!m_tabControl || m_tabControl->GetCount() == 0) {
        return;
    }
    bool anyUnlocked = false;
    for(std::size_t i = 0; i < m_tabControl->GetCount(); ++i) {
        if(!m_tabControl->IsLocked(i)) {
            anyUnlocked = true;
            break;
        }
    }
    for(std::size_t i = 0; i < m_tabControl->GetCount(); ++i) {
        m_tabControl->SetLocked(i, anyUnlocked);
    }
}

void TabBarHost::CloseAllTabsExceptActive() {
    if(!m_tabControl || m_tabControl->GetCount() == 0) {
        return;
    }
    CloseAllTabsExcept(m_tabControl->GetActiveIndex());
}

void TabBarHost::CloseTabsToLeft() {
    if(!m_tabControl || m_tabControl->GetCount() == 0) {
        return;
    }
    CloseTabsToLeftOf(m_tabControl->GetActiveIndex());
}

void TabBarHost::CloseTabsToRight() {
    if(!m_tabControl || m_tabControl->GetCount() == 0) {
        return;
    }
    CloseTabsToRightOf(m_tabControl->GetActiveIndex());
}

void TabBarHost::CloseAllTabsExcept(std::size_t index) {
    if(!m_tabControl || index >= m_tabControl->GetCount()) {
        return;
    }
    auto closed = m_tabControl->CloseAllExcept(index);
    for(const auto& path : closed) {
        RecordClosedEntry(path);
    }
    if(!closed.empty()) {
        PersistClosedHistory();
    }
    if(auto active = m_tabControl->GetActivePath()) {
        m_currentPath = *active;
    }
    RefreshTaskbarPreviews();
    LogTabsState(L"CloseAllTabsExcept");
}

void TabBarHost::CloseTabsToLeftOf(std::size_t index) {
    if(!m_tabControl || index >= m_tabControl->GetCount()) {
        return;
    }
    auto closed = m_tabControl->CloseTabsToLeft(index);
    for(const auto& path : closed) {
        RecordClosedEntry(path);
    }
    if(!closed.empty()) {
        PersistClosedHistory();
    }
    RefreshTaskbarPreviews();
    LogTabsState(L"CloseTabsToLeftOf");
}

void TabBarHost::CloseTabsToRightOf(std::size_t index) {
    if(!m_tabControl || index >= m_tabControl->GetCount()) {
        return;
    }
    auto closed = m_tabControl->CloseTabsToRight(index);
    for(const auto& path : closed) {
        RecordClosedEntry(path);
    }
    if(!closed.empty()) {
        PersistClosedHistory();
    }
    RefreshTaskbarPreviews();
    LogTabsState(L"CloseTabsToRightOf");
}

void TabBarHost::GoUpOneLevel() {
    if(m_currentPath.empty()) {
        return;
    }
    std::wstring path = m_currentPath;
    path.push_back(L'\0');
    if(PathRemoveFileSpecW(path.data())) {
        path.resize(wcslen(path.c_str()));
        AddTab(path, true, true);
    }
}

void TabBarHost::NavigateBack() {
    if(m_spBrowser) {
        m_spBrowser->GoBack();
    }
}

void TabBarHost::NavigateForward() {
    if(m_spBrowser) {
        m_spBrowser->GoForward();
    }
}

bool TabBarHost::OpenCapturedWindow(const std::wstring& path) {
    if(path.empty()) {
        return false;
    }
    AddTab(path, true, true);
    LogTabsState(L"OpenCapturedWindow");
    return true;
}

void TabBarHost::OpenGroupByIndex(std::size_t index) {
    auto group = qttabbar::GroupsManagerNative::Instance().GetGroupByIndex(index);
    if(!group.has_value() || group->paths.empty()) {
        return;
    }
    bool first = true;
    for(const auto& path : group->paths) {
        if(path.empty()) {
            continue;
        }
        AddTab(path, first, true);
        first = false;
    }
    RefreshTaskbarPreviews();
    LogTabsState(L"OpenGroupByIndex");
}

void TabBarHost::TrimClosedHistory() {
    while(m_closedHistory.size() > kMaxClosedHistory) {
        m_closedHistory.pop_back();
    }
}

void TabBarHost::LoadClosedHistory() {
    m_closedHistory = qttabbar::ClosedTabHistoryStore::Instance().Load();
    TrimClosedHistory();
}

void TabBarHost::PersistClosedHistory() const {
    qttabbar::ClosedTabHistoryStore::Instance().Save(m_closedHistory);
}

void TabBarHost::ClearClosedHistory() {
    m_closedHistory.clear();
    qttabbar::ClosedTabHistoryStore::Instance().Clear();
}

void TabBarHost::RecordClosedEntry(const std::wstring& path) {
    if(path.empty()) {
        return;
    }
    auto it = std::remove(m_closedHistory.begin(), m_closedHistory.end(), path);
    if(it != m_closedHistory.end()) {
        m_closedHistory.erase(it, m_closedHistory.end());
    }
    m_closedHistory.push_front(path);
    TrimClosedHistory();
}

std::optional<std::size_t> TabBarHost::ResolveContextTabIndex() const {
    if(!m_tabControl) {
        return std::nullopt;
    }
    if(m_contextTabIndex.has_value() && *m_contextTabIndex < m_tabControl->GetCount()) {
        return m_contextTabIndex;
    }
    return std::nullopt;
}

std::wstring TabBarHost::GetTabPath(std::size_t index) const {
    if(!m_tabControl) {
        return {};
    }
    return m_tabControl->GetPath(index);
}

bool TabBarHost::IsTabLocked(std::size_t index) const {
    if(!m_tabControl) {
        return false;
    }
    return m_tabControl->IsLocked(index);
}

void TabBarHost::ToggleLockTab(std::size_t index) {
    if(!m_tabControl) {
        return;
    }
    bool locked = m_tabControl->IsLocked(index);
    m_tabControl->SetLocked(index, !locked);
}

void TabBarHost::SetTabAlias(std::size_t index, const std::wstring& alias) {
    if(!m_tabControl) {
        return;
    }
    m_tabControl->SetAlias(index, alias);
    RefreshTaskbarPreviews();
}

void TabBarHost::CopyPathToClipboard(const std::wstring& path) const {
    if(path.empty()) {
        return;
    }
    if(!::OpenClipboard(m_hWnd)) {
        return;
    }
    ::EmptyClipboard();
    SIZE_T bytes = (path.size() + 1) * sizeof(wchar_t);
    HGLOBAL handle = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if(handle != nullptr) {
        void* data = ::GlobalLock(handle);
        if(data != nullptr) {
            std::memcpy(data, path.c_str(), bytes);
            ::GlobalUnlock(handle);
            ::SetClipboardData(CF_UNICODETEXT, handle);
        } else {
            ::GlobalFree(handle);
        }
    }
    ::CloseClipboard();
}

void TabBarHost::CopyCurrentFolderPathToClipboard() const {
    CopyPathToClipboard(m_currentPath);
}

void TabBarHost::CopyCurrentFolderNameToClipboard() const {
    std::wstring name = ExtractLeafName(m_currentPath);
    if(name.empty()) {
        name = m_currentPath;
    }
    CopyPathToClipboard(name);
}

void TabBarHost::CopyTabPathToClipboard(std::size_t index) const {
    if(!m_tabControl || index >= m_tabControl->GetCount()) {
        return;
    }
    CopyPathToClipboard(m_tabControl->GetPath(index));
}

void TabBarHost::OpenNewWindowAtPath(const std::wstring& path) const {
    if(path.empty()) {
        return;
    }
    ::ShellExecuteW(nullptr, L"open", L"explorer.exe", path.c_str(), nullptr, SW_SHOWNORMAL);
}

void TabBarHost::NavigateToPath(const std::wstring& path) {
    if(path.empty() || !m_spBrowser) {
        return;
    }
    CComBSTR bstr(path.c_str());
    VARIANT empty;
    ::VariantInit(&empty);
    m_pendingNavigation = path;
    m_spBrowser->Navigate(bstr, &empty, &empty, &empty, &empty);
}

void TabBarHost::OpenPathFromTooltip(const std::wstring& path) {
    HideSubDirTip();
    NavigateToPath(path);
}

void TabBarHost::OpenPathInNewTabFromTooltip(const std::wstring& path) {
    if(path.empty()) {
        return;
    }
    HideSubDirTip();
    AddTab(path, true, true);
}

void TabBarHost::OpenPathInNewWindowFromTooltip(const std::wstring& path) {
    HideSubDirTip();
    OpenNewWindowAtPath(path);
}

void TabBarHost::ShowSubDirTip(std::size_t tabIndex) {
    if(!m_tabControl || tabIndex >= m_tabControl->GetCount()) {
        return;
    }
    if(!m_config.tips.showSubDirTips || !m_config.tabs.showSubDirTipOnTab) {
        return;
    }
    if(m_config.tips.subDirTipsWithShift && (::GetKeyState(VK_SHIFT) & 0x8000) == 0) {
        return;
    }
    std::wstring path = m_tabControl->GetPath(tabIndex);
    if(path.empty()) {
        return;
    }
    if(!m_subDirTipWindow) {
        m_subDirTipWindow = std::make_unique<SubDirTipWindow>(*this);
    }
    m_subDirTipWindow->ApplyConfiguration(m_config);
    m_subDirTipWindow->ShowForPath(path, m_subDirTipAnchor, false);
}

void TabBarHost::HideSubDirTip() {
    if(m_subDirTipTimer) {
        ::KillTimer(m_hWnd, ID_TIMER_SUBDIRTIP);
        m_subDirTipTimer = 0;
    }
    if(m_subDirTipWindow) {
        m_subDirTipWindow->HideTip();
    }
}

bool TabBarHost::FocusExplorerView() const {
    if(!m_spBrowser) {
        return false;
    }
    SHANDLE_PTR hwndValue = 0;
    if(FAILED(m_spBrowser->get_HWND(&hwndValue))) {
        return false;
    }
    HWND hwnd = reinterpret_cast<HWND>(hwndValue);
    if(hwnd == nullptr) {
        return false;
    }
    ::SetFocus(hwnd);
    return true;
}

std::wstring TabBarHost::ResolveTabPath(std::optional<std::size_t> tabIndex) const {
    if(tabIndex && m_tabControl && *tabIndex < m_tabControl->GetCount()) {
        return m_tabControl->GetPath(*tabIndex);
    }
    return m_currentPath;
}

bool TabBarHost::IsTabLocked(std::optional<std::size_t> tabIndex) const {
    if(!m_tabControl || m_tabControl->GetCount() == 0) {
        return false;
    }
    if(tabIndex) {
        if(*tabIndex >= m_tabControl->GetCount()) {
            return false;
        }
        return m_tabControl->IsLocked(*tabIndex);
    }
    return m_tabControl->IsLocked(m_tabControl->GetActiveIndex());
}

void TabBarHost::OpenCommandPrompt(const std::wstring& path) const {
    std::wstring directory = path.empty() ? m_currentPath : path;
    if(directory.empty()) {
        return;
    }
    ::ShellExecuteW(nullptr, L"open", L"cmd.exe", nullptr, directory.c_str(), SW_SHOWNORMAL);
}

void TabBarHost::ShowProperties(const std::wstring& path) const {
    if(path.empty()) {
        return;
    }
    SHELLEXECUTEINFOW exec{};
    exec.cbSize = sizeof(exec);
    exec.fMask = SEE_MASK_INVOKEIDLIST;
    exec.lpVerb = L"properties";
    exec.lpFile = path.c_str();
    exec.nShow = SW_SHOWNORMAL;
    ::ShellExecuteExW(&exec);
}

std::wstring TabBarHost::LoadStringResource(UINT id, const wchar_t* fallback) const {
    wchar_t buffer[256] = {};
    HINSTANCE instance = _AtlBaseModule.GetResourceInstance();
    int length = ::LoadStringW(instance, id, buffer, static_cast<int>(_countof(buffer)));
    if(length > 0) {
        return std::wstring(buffer, length);
    }
    return fallback != nullptr ? std::wstring(fallback) : std::wstring();
}

HMENU TabBarHost::BuildContextMenu(std::size_t targetIndex, std::wstring targetPath) {
    HMENU menu = ::CreatePopupMenu();
    if(menu == nullptr) {
        return nullptr;
    }

    std::size_t tabCount = m_tabControl ? m_tabControl->GetCount() : 0;
    if(tabCount == 0) {
        targetIndex = 0;
    } else if(targetIndex >= tabCount) {
        targetIndex = tabCount - 1;
    }

    bool hasTabs = m_tabControl && tabCount > 0;
    if(hasTabs) {
        targetPath = GetTabPath(targetIndex);
    } else {
        targetPath.clear();
    }
    bool locked = hasTabs && IsTabLocked(targetIndex);
    bool canCloseTab = hasTabs && m_tabControl->CanCloseTab(targetIndex);
    bool canCloseLeft = hasTabs && m_tabControl->HasClosableTabsToLeft(targetIndex);
    bool canCloseRight = hasTabs && m_tabControl->HasClosableTabsToRight(targetIndex);
    bool canCloseOthers = hasTabs && m_tabControl->HasClosableOtherTabs(targetIndex);
    bool hasHistory = !m_closedHistory.empty();

    const std::wstring newTabText = LoadStringResource(IDS_CONTEXT_MENU_NEW_TAB, L"&New Tab");
    const std::wstring closeTabText = LoadStringResource(IDS_CONTEXT_MENU_CLOSE_TAB, L"&Close Tab");
    const std::wstring closeRightText = LoadStringResource(IDS_CONTEXT_MENU_CLOSE_RIGHT, L"Close Tabs to the &Right");
    const std::wstring closeLeftText = LoadStringResource(IDS_CONTEXT_MENU_CLOSE_LEFT, L"Close Tabs to the &Left");
    const std::wstring closeOthersText = LoadStringResource(IDS_CONTEXT_MENU_CLOSE_OTHERS, L"Close &Other Tabs");
    const std::wstring restoreLastText = LoadStringResource(IDS_CONTEXT_MENU_RESTORE_LAST, L"Restore &Last Closed");
    const std::wstring groupCaption = LoadStringResource(IDS_CONTEXT_MENU_GROUP_CAPTION, L"Add to &Group");
    const std::wstring createGroupText = LoadStringResource(IDS_CONTEXT_MENU_CREATE_GROUP, L"Create &Group...");
    const std::wstring cloneTabText = LoadStringResource(IDS_CONTEXT_MENU_CLONE_TAB, L"&Clone Tab");
    const std::wstring openNewWindowText = LoadStringResource(IDS_CONTEXT_MENU_OPEN_NEW_WIN, L"Open in New &Window");
    const std::wstring copyPathText = LoadStringResource(IDS_CONTEXT_MENU_COPY_PATH, L"&Copy Path");
    const std::wstring propertiesText = LoadStringResource(IDS_CONTEXT_MENU_PROPERTIES, L"&Properties");
    const std::wstring historyText = LoadStringResource(IDS_CONTEXT_MENU_HISTORY, L"&History");
    const std::wstring commandPromptText = LoadStringResource(IDS_CONTEXT_MENU_COMMAND, L"Open &Command Prompt Here");
    const std::wstring editAliasText = LoadStringResource(IDS_CONTEXT_MENU_EDIT_ALIAS, L"Edit &Alias...");
    const std::wstring refreshText = LoadStringResource(IDS_CONTEXT_MENU_REFRESH, L"&Refresh");
    const std::wstring optionsText = LoadStringResource(IDS_CONTEXT_MENU_OPTIONS, L"&Options...");

    auto appendItem = [&](UINT id, const std::wstring& text, bool enabled = true) {
        ::AppendMenuW(menu, (enabled ? MF_STRING : MF_STRING | MF_GRAYED), id, text.c_str());
    };
    auto appendPopup = [&](HMENU subMenu, const std::wstring& text, bool enabled) {
        ::AppendMenuW(menu, (enabled ? MF_POPUP | MF_STRING : MF_POPUP | MF_STRING | MF_GRAYED),
                      reinterpret_cast<UINT_PTR>(subMenu), text.c_str());
    };

    appendItem(ID_CONTEXT_NEWTAB, newTabText, true);
    appendItem(ID_CONTEXT_CLOSETAB, closeTabText, canCloseTab);
    appendItem(ID_CONTEXT_CLOSE_RIGHT, closeRightText, canCloseRight);
    appendItem(ID_CONTEXT_CLOSE_LEFT, closeLeftText, canCloseLeft);
    appendItem(ID_CONTEXT_CLOSE_OTHERS, closeOthersText, canCloseOthers);
    appendItem(ID_CONTEXT_RESTORELASTCLOSED, restoreLastText, hasHistory);
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    HMENU groupMenu = ::CreatePopupMenu();
    if(groupMenu) {
        bool hasGroupEntries = PopulateGroupMenu(groupMenu, targetPath);
        appendPopup(groupMenu, groupCaption, hasGroupEntries && !targetPath.empty());
    }

    appendItem(ID_CONTEXT_CREATE_GROUP, createGroupText, !targetPath.empty());
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    const std::wstring lockText = locked ? LoadStringResource(IDS_CONTEXT_MENU_UNLOCK_TAB, L"&Unlock Tab")
                                         : LoadStringResource(IDS_CONTEXT_MENU_LOCK_TAB, L"&Lock Tab");
    appendItem(ID_CONTEXT_TOGGLE_LOCK, lockText, hasTabs);
    appendItem(ID_CONTEXT_CLONE_TAB, cloneTabText, !targetPath.empty());
    appendItem(ID_CONTEXT_OPEN_NEW_WINDOW, openNewWindowText, !targetPath.empty());
    appendItem(ID_CONTEXT_COPY_PATH, copyPathText, !targetPath.empty());
    appendItem(ID_CONTEXT_PROPERTIES, propertiesText, !targetPath.empty());

    HMENU historyMenu = ::CreatePopupMenu();
    if(historyMenu) {
        PopulateHistoryMenu(historyMenu);
        appendPopup(historyMenu, historyText, hasHistory);
    }

    appendItem(ID_CONTEXT_OPEN_COMMAND, commandPromptText, !targetPath.empty());
    appendItem(ID_CONTEXT_EDIT_ALIAS, editAliasText, hasTabs && !targetPath.empty());
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    appendItem(ID_CONTEXT_REFRESH, refreshText, true);
    appendItem(ID_CONTEXT_OPTIONS, optionsText, true);

    return menu;
}

bool TabBarHost::PopulateGroupMenu(HMENU menu, const std::wstring& targetPath) const {
    if(menu == nullptr) {
        return false;
    }
    if(targetPath.empty()) {
        return false;
    }
    auto groups = qttabbar::GroupsManagerNative::Instance().GetGroups();
    if(groups.empty()) {
        const std::wstring noGroups = LoadStringResource(IDS_CONTEXT_MENU_NO_GROUPS, L"(no groups)");
        ::AppendMenuW(menu, MF_GRAYED | MF_STRING, 0, noGroups.c_str());
        return false;
    }
    for(std::size_t index = 0; index < groups.size(); ++index) {
        ::AppendMenuW(menu, MF_STRING, ID_CONTEXT_GROUP_BASE + static_cast<UINT>(index), groups[index].name.c_str());
    }
    return true;
}

void TabBarHost::PopulateHistoryMenu(HMENU menu) const {
    if(menu == nullptr) {
        return;
    }
    if(m_closedHistory.empty()) {
        const std::wstring emptyText = LoadStringResource(IDS_CONTEXT_MENU_HISTORY_EMPTY, L"(empty)");
        ::AppendMenuW(menu, MF_GRAYED | MF_STRING, 0, emptyText.c_str());
        return;
    }
    for(std::size_t index = 0; index < m_closedHistory.size(); ++index) {
        std::wstring display = ExtractLeafName(m_closedHistory[index]);
        if(display.empty()) {
            display = m_closedHistory[index];
        }
        ::AppendMenuW(menu, MF_STRING, ID_CONTEXT_HISTORY_BASE + static_cast<UINT>(index), display.c_str());
    }
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    const std::wstring clearText = LoadStringResource(IDS_CONTEXT_MENU_HISTORY_CLEAR, L"&Clear History");
    ::AppendMenuW(menu, MF_STRING, ID_CONTEXT_HISTORY_CLEAR, clearText.c_str());
}

void TabBarHost::LogTabsState(const wchar_t* source) const {
    if(!m_tabControl) {
        return;
    }
    std::wstring state = JoinTabList(m_tabControl->GetTabPaths());
    ATLTRACE(L"TabBarHost::LogTabsState %s tabs='%s'\n", source, state.c_str());
}

bool TabBarHost::BrowseForFolder() {
    CComPtr<IFileOpenDialog> dialog;
    if(FAILED(dialog.CoCreateInstance(CLSID_FileOpenDialog))) {
        return false;
    }

    DWORD options = 0;
    if(SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    }

    if(!m_currentPath.empty()) {
        CComPtr<IShellItem> folder;
        if(SUCCEEDED(::SHCreateItemFromParsingName(m_currentPath.c_str(), nullptr, IID_PPV_ARGS(&folder))) && folder) {
            dialog->SetFolder(folder);
            dialog->SetDefaultFolder(folder);
        }
    }

    QTTabBarClass* ownerClass = dynamic_cast<QTTabBarClass*>(&m_owner);
    HWND explorer = ownerClass ? ownerClass->m_explorerHwnd : nullptr;
    bool restoreTopMost = false;
    if(explorer) {
        LONG_PTR exStyle = ::GetWindowLongPtrW(explorer, GWL_EXSTYLE);
        if((exStyle & WS_EX_TOPMOST) != 0) {
            restoreTopMost = true;
            ::SetWindowPos(explorer, HWND_NOTOPMOST, 0, 0, 0, 0,
                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }

    HRESULT hr = dialog->Show(m_hWnd);

    if(restoreTopMost && explorer) {
        ::SetWindowPos(explorer, HWND_TOPMOST, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    if(FAILED(hr)) {
        return false;
    }

    CComPtr<IShellItem> result;
    if(FAILED(dialog->GetResult(&result)) || !result) {
        return false;
    }

    PWSTR rawPath = nullptr;
    if(FAILED(result->GetDisplayName(SIGDN_FILESYSPATH, &rawPath)) || rawPath == nullptr) {
        return false;
    }

    std::wstring selected(rawPath);
    ::CoTaskMemFree(rawPath);

    if(selected.empty()) {
        return false;
    }

    AddTab(selected, true, true);
    LogTabsState(L"BrowseForFolder");
    return true;
}

bool TabBarHost::ShowToolbarMenu() {
    if(!m_tabControl) {
        return false;
    }
    POINT pt{};
    std::size_t count = m_tabControl->GetCount();
    if(count > 0) {
        auto bounds = m_tabControl->GetTabBounds(count - 1);
        if(bounds) {
            pt.x = bounds->right + 10;
            pt.y = bounds->bottom - 10;
        } else {
            RECT rect{};
            ::GetWindowRect(m_tabControl->m_hWnd, &rect);
            pt.x = rect.right;
            pt.y = rect.bottom;
        }
        m_contextTabIndex = count - 1;
    } else {
        RECT rect{};
        ::GetWindowRect(m_hWnd, &rect);
        pt.x = rect.left;
        pt.y = rect.bottom;
        m_contextTabIndex.reset();
    }
    ShowContextMenu(pt);
    return true;
}

bool TabBarHost::ShowTabMenuAtIndex(std::optional<std::size_t> index, const POINT& point) {
    if(!m_tabControl || m_tabControl->GetCount() == 0) {
        return false;
    }
    std::size_t target = index ? *index : m_tabControl->GetActiveIndex();
    if(target >= m_tabControl->GetCount()) {
        target = m_tabControl->GetCount() - 1;
    }

    POINT menuPoint = point;
    if(menuPoint.x == 0 && menuPoint.y == 0) {
        auto bounds = m_tabControl->GetTabBounds(target);
        if(bounds) {
            menuPoint.x = bounds->right;
            menuPoint.y = bounds->bottom;
        } else {
            RECT rect{};
            ::GetWindowRect(m_tabControl->m_hWnd, &rect);
            menuPoint.x = rect.left;
            menuPoint.y = rect.bottom;
        }
    }

    m_contextTabIndex = target;
    ShowContextMenu(menuPoint);
    return true;
}

bool TabBarHost::ShowTabMenuAtCursor() {
    if(!m_tabControl) {
        return false;
    }
    POINT pt{};
    ::GetCursorPos(&pt);
    m_contextTabIndex = m_tabControl->GetActiveIndex();
    ShowContextMenu(pt);
    return true;
}

bool TabBarHost::ShowButtonBarMenu(UINT commandId) {
    if(auto* buttonBar = GetButtonBar()) {
        return buttonBar->InvokeCommand(commandId);
    }
    return false;
}

bool TabBarHost::DoFileTools(int index) {
    switch(index) {
    case 0: {
        auto paths = GetSelectedPaths();
        if(paths.empty()) {
            return false;
        }
        std::wstring joined;
        for(size_t i = 0; i < paths.size(); ++i) {
            joined.append(paths[i]);
            if(i + 1 < paths.size()) {
                joined.append(L"\r\n");
            }
        }
        CopyPathToClipboard(joined);
        return true;
    }
    case 1: {
        auto names = GetSelectedNames();
        if(names.empty()) {
            return false;
        }
        std::wstring joined;
        for(size_t i = 0; i < names.size(); ++i) {
            joined.append(names[i]);
            if(i + 1 < names.size()) {
                joined.append(L"\r\n");
            }
        }
        CopyPathToClipboard(joined);
        return true;
    }
    case 2:
        CopyCurrentFolderPathToClipboard();
        return true;
    case 3:
        CopyCurrentFolderNameToClipboard();
        return true;
    case 4: {
        auto paths = GetSelectedPaths();
        ShowFileHashDialog(paths);
        return true;
    }
    case 5: {
        if(!m_tabControl) {
            return false;
        }
        auto paths = m_tabControl->GetTabPaths();
        if(paths.empty()) {
            return false;
        }
        std::wstring joined;
        for(size_t i = 0; i < paths.size(); ++i) {
            joined.append(paths[i]);
            if(i + 1 < paths.size()) {
                joined.append(L"\r\n");
            }
        }
        CopyPathToClipboard(joined);
        return true;
    }
    default:
        return false;
    }
}

bool TabBarHost::ToggleTopMost() {
    QTTabBarClass* ownerClass = dynamic_cast<QTTabBarClass*>(&m_owner);
    if(!ownerClass || !ownerClass->m_explorerHwnd) {
        return false;
    }
    HWND explorer = ownerClass->m_explorerHwnd;
    LONG_PTR exStyle = ::GetWindowLongPtrW(explorer, GWL_EXSTYLE);
    bool isTopMost = (exStyle & WS_EX_TOPMOST) != 0;
    HWND insertAfter = isTopMost ? HWND_NOTOPMOST : HWND_TOPMOST;
    return ::SetWindowPos(explorer, insertAfter, 0, 0, 0, 0,
                          SWP_NOMOVE | SWP_NOSIZE) != FALSE;
}

bool TabBarHost::AdjustTransparency(bool increase) {
    QTTabBarClass* ownerClass = dynamic_cast<QTTabBarClass*>(&m_owner);
    if(!ownerClass || !ownerClass->m_explorerHwnd) {
        return false;
    }
    HWND explorer = ownerClass->m_explorerHwnd;
    LONG_PTR exStyle = ::GetWindowLongPtrW(explorer, GWL_EXSTYLE);
    if((exStyle & WS_EX_LAYERED) == 0) {
        if(increase) {
            return true;
        }
        ::SetWindowLongPtrW(explorer, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
        ::SetLayeredWindowAttributes(explorer, 0, 0xFF, LWA_ALPHA);
    }

    COLORREF key = 0;
    BYTE alpha = 0xFF;
    DWORD flags = 0;
    if(!::GetLayeredWindowAttributes(explorer, &key, &alpha, &flags)) {
        alpha = 0xFF;
    }

    if(increase) {
        alpha = (alpha > 0xF3) ? 0xFF : static_cast<BYTE>(std::min<int>(0xFF, alpha + 12));
    } else {
        alpha = (alpha <= 0x20) ? 0x14 : static_cast<BYTE>(std::max<int>(0x14, alpha - 12));
    }

    if(!::SetLayeredWindowAttributes(explorer, key, alpha, LWA_ALPHA)) {
        return false;
    }

    if(alpha == 0xFF) {
        LONG_PTR current = ::GetWindowLongPtrW(explorer, GWL_EXSTYLE);
        ::SetWindowLongPtrW(explorer, GWL_EXSTYLE, current & ~static_cast<LONG_PTR>(WS_EX_LAYERED));
    }
    return true;
}

bool TabBarHost::FocusSearchBand() {
    return FocusExplorerSearch();
}

bool TabBarHost::FocusButtonBarSearch() {
    if(auto* buttonBar = GetButtonBar()) {
        return buttonBar->InvokeCommand(ID_BUTTONBAR_SEARCH_FOCUS);
    }
    return false;
}

bool TabBarHost::ShowSelectedSubDirTip() {
    if(!m_config.tips.showSubDirTips) {
        return false;
    }
    auto selections = GetSelectedPaths();
    if(selections.size() != 1) {
        return false;
    }
    std::wstring target = selections.front();
    if(target.rfind(L"::", 0) == 0) {
        return false;
    }
    auto resolved = ResolveFolderPathForAction(target);
    if(!resolved) {
        return false;
    }
    target = *resolved;
    DWORD attr = ::GetFileAttributesW(target.c_str());
    if(attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return false;
    }
    if(PathIsNetworkPathW(target.c_str())) {
        return false;
    }
    if(!m_subDirTipWindow) {
        m_subDirTipWindow = std::make_unique<SubDirTipWindow>(*this);
    }
    m_subDirTipWindow->ApplyConfiguration(m_config);
    POINT anchor{};
    ::GetCursorPos(&anchor);
    return m_subDirTipWindow->ShowAndExecute(target, anchor, SubDirTipWindow::Command::Open, true);
}

bool TabBarHost::SendWindowToTray() {
    QTTabBarClass* ownerClass = dynamic_cast<QTTabBarClass*>(&m_owner);
    if(!ownerClass || !ownerClass->m_explorerHwnd || !m_tabControl) {
        return false;
    }
    InstanceManager::TabSnapshot snapshot;
    snapshot.currentPath = m_currentPath;
    snapshot.tabPaths = m_tabControl->GetTabPaths();
    snapshot.tabNames = m_tabControl->GetTabDisplayNames();
    InstanceManager::Instance().PushTabList(ownerClass->m_explorerHwnd, ownerClass->m_hWnd, snapshot);
    return true;
}

bool TabBarHost::MergeAllWindows() {
    QTTabBarClass* ownerClass = dynamic_cast<QTTabBarClass*>(&m_owner);
    if(!ownerClass || !m_tabControl) {
        return false;
    }
    auto tabBars = InstanceManagerNative::Instance().EnumerateTabBars();
    bool merged = false;
    for(auto* tabBar : tabBars) {
        if(tabBar == nullptr || tabBar == ownerClass) {
            continue;
        }
        if(!tabBar->m_tabHost || !tabBar->m_tabHost->m_tabControl) {
            continue;
        }
        auto* otherControl = tabBar->m_tabHost->m_tabControl.get();
        if(!otherControl) {
            continue;
        }
        for(std::size_t i = 0; i < otherControl->GetCount(); ++i) {
            std::wstring path = otherControl->GetPath(i);
            if(path.empty()) {
                continue;
            }
            std::size_t before = m_tabControl->GetCount();
            AddTab(path, false, true);
            std::size_t after = m_tabControl->GetCount();
            if(after > before && otherControl->IsLocked(i)) {
                m_tabControl->SetLocked(after - 1, true);
            }
            merged = true;
        }
        HWND explorer = tabBar->m_explorerHwnd;
        if(explorer) {
            ::PostMessageW(explorer, WM_CLOSE, 0, 0);
        }
    }
    if(merged) {
        LogTabsState(L"MergeAllWindows");
    }
    return merged;
}

bool TabBarHost::CreateNewItem(bool isFolder) {
    if(m_currentPath.empty() || ::GetFileAttributesW(m_currentPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    std::wstring baseName = isFolder ? L"New Folder" : L"New Text Document";
    std::wstring extension = isFolder ? L"" : L".txt";
    std::wstring candidate = m_currentPath + L"\\" + baseName + extension;
    int counter = 2;
    while(::GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
        candidate = m_currentPath + L"\\" + baseName + L" (" + std::to_wstring(counter) + L")" + extension;
        ++counter;
    }

    bool created = false;
    if(isFolder) {
        created = ::CreateDirectoryW(candidate.c_str(), nullptr) != FALSE;
    } else {
        HANDLE file = ::CreateFileW(candidate.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        if(file != INVALID_HANDLE_VALUE) {
            ::CloseHandle(file);
            created = true;
        }
    }

    if(!created) {
        return false;
    }

    if(auto view = GetShellView()) {
        view->Refresh();
    }
    SelectInExplorer(candidate, true);
    return true;
}

bool TabBarHost::ExecuteItemCommand(BindAction action) {
    switch(action) {
    case BindAction::ItemOpenInNewTab:
        return OpenSelectedInNewTab(true);
    case BindAction::ItemOpenInNewTabNoSel:
    case BindAction::ItemsOpenInNewTabNoSel:
        return OpenSelectedInNewTab(false);
    case BindAction::ItemOpenInNewWindow:
        return OpenSelectedInNewWindow();
    case BindAction::ItemCut:
        return InvokeShellVerb(L"cut");
    case BindAction::ItemCopy:
        return InvokeShellVerb(L"copy");
    case BindAction::ItemDelete:
        return InvokeShellVerb(L"delete");
    case BindAction::ItemProperties: {
        auto paths = GetSelectedPaths();
        if(paths.empty()) {
            return false;
        }
        ShowProperties(paths.front());
        return true;
    }
    case BindAction::CopyItemPath:
        return DoFileTools(0);
    case BindAction::CopyItemName:
        return DoFileTools(1);
    case BindAction::ChecksumItem: {
        auto paths = GetSelectedPaths();
        if(paths.empty()) {
            return false;
        }
        ShowFileHashDialog(paths);
        return true;
    }
    default:
        return false;
    }
}

bool TabBarHost::OpenSelectedInNewTab(bool activate) {
    auto paths = GetSelectedPaths();
    if(paths.empty()) {
        return false;
    }
    bool opened = false;
    bool first = true;
    for(const auto& path : paths) {
        auto target = ResolveFolderPathForAction(path);
        if(!target) {
            continue;
        }
        OpenFolderInNewTab(*target, activate && first);
        opened = true;
        first = false;
    }
    return opened;
}

bool TabBarHost::OpenSelectedInNewWindow() {
    auto paths = GetSelectedPaths();
    if(paths.empty()) {
        return false;
    }
    bool opened = false;
    for(const auto& path : paths) {
        auto target = ResolveFolderPathForAction(path);
        if(!target) {
            continue;
        }
        OpenNewWindowAtPath(*target);
        opened = true;
    }
    return opened;
}

bool TabBarHost::InvokeShellVerb(const wchar_t* verb) {
    if(!verb) {
        return false;
    }
    CComPtr<IContextMenu> menu = GetSelectionContextMenu();
    if(!menu) {
        return false;
    }
    CMINVOKECOMMANDINFOEX info{};
    info.cbSize = sizeof(info);
    info.fMask = CMIC_MASK_UNICODE;
    info.hwnd = m_hWnd;
    info.lpVerbW = verb;
    info.nShow = SW_SHOWNORMAL;
    return SUCCEEDED(menu->InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&info)));
}

void TabBarHost::ShowFileHashDialog(const std::vector<std::wstring>& paths) const {
    std::wstring content;
    if(paths.empty()) {
        content = L"No files selected.";
    } else {
        for(size_t i = 0; i < paths.size(); ++i) {
            content.append(paths[i]);
            content.append(L"\n    ");
            if(auto hash = ComputeFileHash(paths[i])) {
                content.append(*hash);
            } else {
                content.append(L"(error)");
            }
            if(i + 1 < paths.size()) {
                content.append(L"\n\n");
            }
        }
    }

    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.hwndParent = m_owner.GetHostWindow();
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
    config.dwCommonButtons = TDCBF_OK_BUTTON;
    config.pszWindowTitle = L"QTTabBar";
    config.pszMainInstruction = L"MD5 Checksum";
    config.pszContent = content.c_str();
    if(FAILED(::TaskDialogIndirect(&config, nullptr, nullptr, nullptr))) {
        ::MessageBoxW(m_hWnd, content.c_str(), L"MD5 Checksum", MB_OK | MB_ICONINFORMATION);
    }
}

std::optional<std::wstring> TabBarHost::ComputeFileHash(const std::wstring& path) const {
    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if(file == INVALID_HANDLE_VALUE) {
        DWORD error = ::GetLastError();
        InstanceManager::Instance().Log(L"ComputeFileHash(CreateFile) failed for '%s' (err=%lu)",
                                        path.c_str(), error);
        return std::nullopt;
    }

    HCRYPTPROV provider = 0;
    if(!::CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        DWORD error = ::GetLastError();
        InstanceManager::Instance().Log(L"ComputeFileHash(CryptAcquireContext) failed for '%s' (err=%lu)",
                                        path.c_str(), error);
        ::CloseHandle(file);
        return std::nullopt;
    }

    HCRYPTHASH hash = 0;
    if(!::CryptCreateHash(provider, CALG_MD5, 0, 0, &hash)) {
        DWORD error = ::GetLastError();
        InstanceManager::Instance().Log(L"ComputeFileHash(CryptCreateHash) failed for '%s' (err=%lu)",
                                        path.c_str(), error);
        ::CryptReleaseContext(provider, 0);
        ::CloseHandle(file);
        return std::nullopt;
    }

    std::array<BYTE, 4096> buffer{};
    DWORD read = 0;
    bool failed = false;
    while(true) {
        if(!::ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
            DWORD error = ::GetLastError();
            InstanceManager::Instance().Log(L"ComputeFileHash(ReadFile) failed for '%s' (err=%lu)",
                                            path.c_str(), error);
            failed = true;
            break;
        }
        if(read == 0) {
            break;
        }
        if(!::CryptHashData(hash, buffer.data(), read, 0)) {
            failed = true;
            DWORD error = ::GetLastError();
            InstanceManager::Instance().Log(L"ComputeFileHash(CryptHashData) failed for '%s' (err=%lu)",
                                            path.c_str(), error);
            break;
        }
    }

    std::wstring result;
    if(!failed) {
        DWORD hashSize = 0;
        DWORD size = sizeof(hashSize);
        if(::CryptGetHashParam(hash, HP_HASHSIZE, reinterpret_cast<BYTE*>(&hashSize), &size, 0) && hashSize > 0) {
            std::vector<BYTE> digest(hashSize);
            if(::CryptGetHashParam(hash, HP_HASHVAL, digest.data(), &hashSize, 0)) {
                wchar_t chunk[3];
                for(DWORD i = 0; i < hashSize; ++i) {
                    swprintf_s(chunk, L"%02X", digest[i]);
                    result.append(chunk);
                }
            } else {
                DWORD error = ::GetLastError();
                InstanceManager::Instance().Log(L"ComputeFileHash(CryptGetHashParam HASHVAL) failed for '%s' (err=%lu)",
                                                path.c_str(), error);
                failed = true;
            }
        } else {
            DWORD error = ::GetLastError();
            InstanceManager::Instance().Log(L"ComputeFileHash(CryptGetHashParam HASHSIZE) failed for '%s' (err=%lu)",
                                            path.c_str(), error);
            failed = true;
        }
    }

    ::CryptDestroyHash(hash);
    ::CryptReleaseContext(provider, 0);
    ::CloseHandle(file);

    if(result.empty()) {
        if(!failed) {
            InstanceManager::Instance().Log(L"ComputeFileHash produced empty result for '%s'", path.c_str());
        }
        return std::nullopt;
    }
    return result;
}

void TabBarHost::RefreshTaskbarPreviews() {
    if(!m_taskbarPreviews || !m_tabControl) {
        return;
    }

    HWND ownerHwnd = m_owner.GetHostWindow();
    m_taskbarPreviews->SetOwner(ownerHwnd,
                                [this](std::size_t index) { ActivateTabByIndex(index); },
                                [this](std::size_t index) { CloseTabAt(index); });
    m_taskbarPreviews->SetEnabled(m_config.misc.taskbarThumbnails);

    if(!m_config.misc.taskbarThumbnails) {
        return;
    }

    auto switchEntries = m_tabControl->GetSwitchEntries();
    std::size_t activeIndex = m_tabControl->GetActiveIndex();
    std::vector<TaskbarTabPreviewManager::Entry> previewEntries;
    previewEntries.reserve(switchEntries.size());
    for(std::size_t index = 0; index < switchEntries.size(); ++index) {
        TaskbarTabPreviewManager::Entry entry;
        entry.title = switchEntries[index].display.empty() ? switchEntries[index].path : switchEntries[index].display;
        entry.path = switchEntries[index].path;
        entry.icon = switchEntries[index].icon;
        entry.active = index == activeIndex;
        previewEntries.push_back(std::move(entry));
    }
    m_taskbarPreviews->Update(previewEntries);
}

std::vector<std::wstring> TabBarHost::GetSelectedPaths() const {
    std::vector<std::wstring> result;
    CComPtr<IDataObject> data = GetSelectionDataObject();
    if(!data) {
        return result;
    }
    FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium{};
    if(FAILED(data->GetData(&format, &medium))) {
        return result;
    }
    HDROP drop = static_cast<HDROP>(::GlobalLock(medium.hGlobal));
    if(drop) {
        UINT count = ::DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for(UINT i = 0; i < count; ++i) {
            UINT length = ::DragQueryFileW(drop, i, nullptr, 0);
            if(length == 0) {
                continue;
            }
            std::wstring path;
            path.resize(length + 1);
            UINT copied = ::DragQueryFileW(drop, i, path.data(), length + 1);
            if(copied > 0) {
                path.resize(copied);
                result.push_back(std::move(path));
            }
        }
        ::GlobalUnlock(medium.hGlobal);
    }
    ::ReleaseStgMedium(&medium);
    return result;
}

std::vector<std::wstring> TabBarHost::GetSelectedNames() const {
    auto paths = GetSelectedPaths();
    std::vector<std::wstring> names;
    names.reserve(paths.size());
    for(const auto& path : paths) {
        const wchar_t* name = PathFindFileNameW(path.c_str());
        names.emplace_back(name && *name ? name : path);
    }
    return names;
}

std::optional<std::wstring> TabBarHost::ResolveShortcutTarget(const std::wstring& path) const {
    if(path.empty()) {
        return std::nullopt;
    }
    CComPtr<IShellLinkW> link;
    if(FAILED(::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) || !link) {
        return std::nullopt;
    }
    CComPtr<IPersistFile> persist;
    if(FAILED(link->QueryInterface(IID_PPV_ARGS(&persist))) || !persist) {
        return std::nullopt;
    }
    if(FAILED(persist->Load(path.c_str(), STGM_READ))) {
        return std::nullopt;
    }
    wchar_t resolved[MAX_PATH] = {};
    WIN32_FIND_DATAW data{};
    if(SUCCEEDED(link->GetPath(resolved, ARRAYSIZE(resolved), &data, SLGP_RAWPATH)) && resolved[0] != L'\0') {
        return std::wstring(resolved);
    }
    return std::nullopt;
}

std::optional<std::wstring> TabBarHost::ResolveFolderPathForAction(const std::wstring& path) const {
    if(path.empty()) {
        return std::nullopt;
    }
    std::wstring candidate = path;
    DWORD attr = ::GetFileAttributesW(candidate.c_str());
    if(attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        if(_wcsicmp(PathFindExtensionW(candidate.c_str()), L".lnk") == 0) {
            if(auto resolved = ResolveShortcutTarget(candidate)) {
                candidate = *resolved;
                attr = ::GetFileAttributesW(candidate.c_str());
            }
        }
    }
    if(attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return std::nullopt;
    }
    return candidate;
}

CComPtr<IShellBrowser> TabBarHost::GetShellBrowser() const {
    if(!m_spBrowser) {
        return nullptr;
    }
    CComPtr<IServiceProvider> provider;
    if(FAILED(m_spBrowser->QueryInterface(IID_PPV_ARGS(&provider))) || !provider) {
        return nullptr;
    }
    CComPtr<IShellBrowser> browser;
    provider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser));
    return browser;
}

CComPtr<IShellView> TabBarHost::GetShellView() const {
    CComPtr<IShellBrowser> browser = GetShellBrowser();
    if(!browser) {
        return nullptr;
    }
    CComPtr<IShellView> view;
    browser->QueryActiveShellView(&view);
    return view;
}

CComPtr<IDataObject> TabBarHost::GetSelectionDataObject() const {
    CComPtr<IShellView> view = GetShellView();
    if(!view) {
        return nullptr;
    }
    CComPtr<IDataObject> data;
    view->GetItemObject(SVGIO_SELECTION, IID_PPV_ARGS(&data));
    return data;
}

CComPtr<IContextMenu> TabBarHost::GetSelectionContextMenu() const {
    CComPtr<IShellView> view = GetShellView();
    if(!view) {
        return nullptr;
    }
    CComPtr<IContextMenu> menu;
    view->GetItemObject(SVGIO_SELECTION, IID_PPV_ARGS(&menu));
    return menu;
}

QTButtonBar* TabBarHost::GetButtonBar() const {
    QTTabBarClass* ownerClass = dynamic_cast<QTTabBarClass*>(&m_owner);
    if(!ownerClass) {
        return nullptr;
    }
    return InstanceManagerNative::Instance().FindButtonBar(ownerClass->m_explorerHwnd);
}

bool TabBarHost::SelectInExplorer(const std::wstring& path, bool edit) const {
    CComPtr<IShellView> view = GetShellView();
    if(!view) {
        return false;
    }
    PIDLIST_ABSOLUTE pidl = nullptr;
    if(FAILED(::SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr)) || !pidl) {
        return false;
    }
    PCUITEMID_CHILD child = ILFindLastID(pidl);
    if(child == nullptr) {
        ::CoTaskMemFree(pidl);
        return false;
    }
    UINT flags = SVSI_SELECT | SVSI_DESELECTOTHERS | SVSI_ENSUREVISIBLE;
    if(edit) {
        flags |= SVSI_EDIT;
    }
    HRESULT hr = view->SelectItem(child, flags);
    ::CoTaskMemFree(pidl);
    return SUCCEEDED(hr);
}

bool TabBarHost::FocusExplorerSearch() {
    QTTabBarClass* ownerClass = dynamic_cast<QTTabBarClass*>(&m_owner);
    if(!ownerClass || !ownerClass->m_explorerHwnd) {
        return false;
    }
    ::SetForegroundWindow(ownerClass->m_explorerHwnd);
    INPUT inputs[4] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'E';
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'E';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    return ::SendInput(static_cast<UINT>(std::size(inputs)), inputs, sizeof(INPUT)) == std::size(inputs);
}

bool TabBarHost::ShowTabSubfolderMenu(std::size_t index) {
    if(!m_tabControl || index >= m_tabControl->GetCount()) {
        return false;
    }
    std::wstring path = m_tabControl->GetPath(index);
    if(path.empty()) {
        return false;
    }
    if(!m_subDirTipWindow) {
        m_subDirTipWindow = std::make_unique<SubDirTipWindow>(*this);
    }
    m_subDirTipWindow->ApplyConfiguration(m_config);
    POINT anchor{};
    if(auto bounds = m_tabControl->GetTabBounds(index)) {
        anchor.x = bounds->left;
        anchor.y = bounds->bottom;
    } else {
        anchor = m_subDirTipAnchor;
    }
    return m_subDirTipWindow->ShowAndExecute(path, anchor, SubDirTipWindow::Command::Open, true);
}

void TabBarHost::OnTabControlTabSelected(std::size_t index) {
    ATLTRACE(L"TabBarHost::OnTabControlTabSelected index=%u\n", static_cast<UINT>(index));
    ActivateTab(index);
}

void TabBarHost::OnTabControlCloseRequested(std::size_t index) {
    ATLTRACE(L"TabBarHost::OnTabControlCloseRequested index=%u\n", static_cast<UINT>(index));
    CloseTabAt(index);
}

void TabBarHost::OnTabControlContextMenuRequested(std::optional<std::size_t> index, const POINT& screenPoint) {
    ATLTRACE(L"TabBarHost::OnTabControlContextMenuRequested (%ld,%ld)\n", screenPoint.x, screenPoint.y);
    m_contextTabIndex = index;
    ShowContextMenu(screenPoint);
}

void TabBarHost::OnTabControlNewTabRequested() {
    ATLTRACE(L"TabBarHost::OnTabControlNewTabRequested\n");
    AddTab(m_currentPath, true, true);
}

void TabBarHost::OnTabControlBeginDrag(std::size_t index, const POINT& screenPoint) {
    ATLTRACE(L"TabBarHost::OnTabControlBeginDrag index=%u (%ld,%ld)\n", static_cast<UINT>(index), screenPoint.x, screenPoint.y);
}

void TabBarHost::OnTabControlHoverChanged(std::optional<std::size_t> index, const POINT& screenPoint) {
    if(!index) {
        m_pendingSubDirTipIndex.reset();
        HideSubDirTip();
        return;
    }
    if(!m_tabControl || !m_config.tips.showSubDirTips || !m_config.tabs.showSubDirTipOnTab) {
        HideSubDirTip();
        return;
    }

    m_subDirTipAnchor = screenPoint;
    m_pendingSubDirTipIndex = index;
    if(m_config.tips.subDirTipsWithShift && (::GetKeyState(VK_SHIFT) & 0x8000) == 0) {
        HideSubDirTip();
        return;
    }

    if(m_subDirTipTimer) {
        ::KillTimer(m_hWnd, ID_TIMER_SUBDIRTIP);
    }
    m_subDirTipTimer = ::SetTimer(m_hWnd, ID_TIMER_SUBDIRTIP, kSubDirTipTimerMs, nullptr);
}

void TabBarHost::EnsureTabSwitcher() {
    if(!m_tabSwitcher) {
        m_tabSwitcher = std::make_unique<TabSwitchOverlay>();
        m_tabSwitcher->SetCommitCallback([this](std::size_t index) {
            CommitTabSwitcher(index);
        });
    }
}

bool TabBarHost::HandleTabSwitcherShortcut(UINT vk, UINT modifiers, bool /*isRepeat*/) {
    bool matchNext = ShortcutMatches(m_nextTabShortcut, vk, modifiers);
    bool matchPrev = ShortcutMatches(m_prevTabShortcut, vk, modifiers);
    if(!matchNext && !matchPrev) {
        return false;
    }

    if(!m_tabControl) {
        return true;
    }

    if(!m_useTabSwitcher || m_tabControl->GetCount() < 2) {
        if(matchPrev) {
            ActivatePreviousTab();
        } else {
            ActivateNextTab();
        }
        return true;
    }

    EnsureTabSwitcher();
    if(!m_tabSwitcher) {
        if(matchPrev) {
            ActivatePreviousTab();
        } else {
            ActivateNextTab();
        }
        return true;
    }

    if(!m_tabSwitcher->IsVisible()) {
        auto entries = m_tabControl->GetSwitchEntries();
        if(entries.size() < 2) {
            if(matchPrev) {
                ActivatePreviousTab();
            } else {
                ActivateNextTab();
            }
            return true;
        }
        std::size_t activeIndex = m_tabControl->GetActiveIndex();
        std::vector<TabSwitchOverlay::Entry> overlayEntries;
        overlayEntries.reserve(entries.size());
        for(auto& entry : entries) {
            overlayEntries.push_back({std::move(entry.display), std::move(entry.path), entry.icon, entry.locked});
        }
        if(!m_tabSwitcher->Show(m_owner.GetHostWindow(), std::move(overlayEntries), activeIndex, activeIndex)) {
            if(matchPrev) {
                ActivatePreviousTab();
            } else {
                ActivateNextTab();
            }
            return true;
        }
        m_tabSwitcherActive = true;
        m_tabSwitcherAnchorModifiers = matchPrev ? m_prevTabShortcut.modifiers : m_nextTabShortcut.modifiers;
        m_tabSwitcherTriggerKey = vk;
        m_tabSwitcher->Cycle(matchPrev);
    } else {
        if(matchPrev || matchNext) {
            m_tabSwitcherAnchorModifiers = matchPrev ? m_prevTabShortcut.modifiers : m_nextTabShortcut.modifiers;
            m_tabSwitcherTriggerKey = vk;
            m_tabSwitcher->Cycle(matchPrev);
        }
    }
    return true;
}

void TabBarHost::HideTabSwitcher(bool commit, std::optional<std::size_t> forcedIndex) {
    if(!m_tabSwitcher) {
        m_tabSwitcherActive = false;
        m_tabSwitcherAnchorModifiers = 0;
        m_tabSwitcherTriggerKey = 0;
        return;
    }
    std::size_t index = forcedIndex.value_or(m_tabSwitcher->SelectedIndex());
    m_tabSwitcher->Hide(commit);
    m_tabSwitcherActive = false;
    m_tabSwitcherAnchorModifiers = 0;
    m_tabSwitcherTriggerKey = 0;
    if(commit && m_tabControl) {
        if(index < m_tabControl->GetCount()) {
            ActivateTab(index);
        }
    }
}

void TabBarHost::CommitTabSwitcher(std::size_t index) {
    HideTabSwitcher(true, index);
}

UINT TabBarHost::CurrentModifierMask() const {
    UINT mods = 0;
    if(::GetKeyState(VK_CONTROL) & 0x8000) {
        mods |= MOD_CONTROL;
    }
    if(::GetKeyState(VK_SHIFT) & 0x8000) {
        mods |= MOD_SHIFT;
    }
    if(::GetKeyState(VK_MENU) & 0x8000) {
        mods |= MOD_ALT;
    }
    if((::GetKeyState(VK_LWIN) & 0x8000) || (::GetKeyState(VK_RWIN) & 0x8000)) {
        mods |= MOD_WIN;
    }
    return mods;
}

void TabBarHost::ReloadConfiguration() {
    qttabbar::ConfigData config = qttabbar::LoadConfigFromRegistry();
    m_config = config;
    if(m_tabControl) {
        m_tabControl->ApplyConfiguration(m_config);
    }
    if(m_subDirTipWindow) {
        m_subDirTipWindow->ApplyConfiguration(m_config);
    }

    m_useTabSwitcher = config.keys.useTabSwitcher;
    std::size_t nextIndex = static_cast<std::size_t>(qttabbar::BindAction::NextTab);
    std::size_t prevIndex = static_cast<std::size_t>(qttabbar::BindAction::PreviousTab);
    if(config.keys.shortcuts.size() <= nextIndex || config.keys.shortcuts.size() <= prevIndex) {
        config.keys.shortcuts.resize(static_cast<std::size_t>(qttabbar::BindAction::KEYBOARD_ACTION_COUNT));
    }
    m_nextTabShortcut = DecodeShortcut(config.keys.shortcuts[nextIndex]);
    m_prevTabShortcut = DecodeShortcut(config.keys.shortcuts[prevIndex]);

    m_keyboardBindings.clear();
    std::size_t maxActions = (std::min)(config.keys.shortcuts.size(),
                                      static_cast<std::size_t>(qttabbar::BindAction::KEYBOARD_ACTION_COUNT));
    for(std::size_t index = 0; index < maxActions; ++index) {
        auto action = static_cast<BindAction>(index);
        ShortcutKey shortcut = DecodeShortcut(config.keys.shortcuts[index]);
        if(!shortcut.enabled || shortcut.key == 0) {
            continue;
        }
        uint32_t composed = ComposeShortcutKey(shortcut.key, shortcut.modifiers);
        m_keyboardBindings[composed] = action;
    }

    m_globalMouseActions = config.mouse.globalMouseActions;
    m_tabMouseActions = config.mouse.tabActions;
    m_barMouseActions = config.mouse.barActions;
    m_linkMouseActions = config.mouse.linkActions;
    m_itemMouseActions = config.mouse.itemActions;
    m_marginMouseActions = config.mouse.marginActions;

    if(!m_useTabSwitcher) {
        HideTabSwitcher(false);
    }
    if(!m_config.tips.showSubDirTips) {
        HideSubDirTip();
    }
    RefreshTaskbarPreviews();
}

TabBarHost::ShortcutKey TabBarHost::DecodeShortcut(int value) {
    ShortcutKey shortcut;
    constexpr int kEnabledFlag = 1 << 30;
    constexpr int kLegacyFlag = 0x100000;
    if((value & kEnabledFlag) != 0 || (value & kLegacyFlag) != 0) {
        shortcut.enabled = true;
    }
    int sanitized = value & ~(kEnabledFlag | kLegacyFlag);
    shortcut.key = static_cast<UINT>(sanitized & 0xFFFF);
    shortcut.modifiers = static_cast<UINT>((static_cast<unsigned int>(sanitized) >> 16) & 0x01FF);
    return shortcut;
}

bool TabBarHost::ShortcutMatches(const ShortcutKey& shortcut, UINT vk, UINT modifiers) const {
    if(!shortcut.enabled || shortcut.key == 0) {
        return false;
    }
    return shortcut.key == vk && shortcut.modifiers == modifiers;
}

std::optional<BindAction> TabBarHost::LookupMouseAction(MouseTarget target, MouseChord chord) const {
    const qttabbar::MouseActionMap* map = nullptr;
    switch(target) {
    case MouseTarget::Anywhere:
        map = &m_globalMouseActions;
        break;
    case MouseTarget::Tab:
        map = &m_tabMouseActions;
        break;
    case MouseTarget::TabBarBackground:
        map = &m_barMouseActions;
        break;
    case MouseTarget::FolderLink:
        map = &m_linkMouseActions;
        break;
    case MouseTarget::ExplorerItem:
        map = &m_itemMouseActions;
        break;
    case MouseTarget::ExplorerBackground:
        map = &m_marginMouseActions;
        break;
    default:
        break;
    }
    if(map != nullptr) {
        auto it = map->find(chord);
        if(it != map->end()) {
            return it->second;
        }
    }
    return std::nullopt;
}

std::optional<BindAction> TabBarHost::LookupKeyboardAction(UINT vk, UINT modifiers) const {
    uint32_t composed = ComposeShortcutKey(vk, modifiers);
    auto it = m_keyboardBindings.find(composed);
    if(it != m_keyboardBindings.end()) {
        return it->second;
    }
    return std::nullopt;
}

uint32_t TabBarHost::ComposeShortcutKey(UINT vk, UINT modifiers) {
    return (modifiers << 16) | (vk & 0xFFFFu);
}

bool TabBarHost::IsRepeatAllowed(BindAction action) {
    switch(action) {
    case BindAction::GoBack:
    case BindAction::GoForward:
    case BindAction::TransparencyPlus:
    case BindAction::TransparencyMinus:
        return true;
    default:
        return false;
    }
}
