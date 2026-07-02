#pragma once

#include <atlbase.h>
#include <atlwin.h>
#include <atlcom.h>
#include <exdisp.h>
#include <exdispid.h>

#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Config.h"

class SubDirTipWindow;

class NativeTabControl;
class TabSwitchOverlay;
class QTButtonBar;
class TaskbarTabPreviewManager;

interface IShellBrowser;
interface IShellView;
interface IDataObject;
interface IContextMenu;

class ITabBarHostOwner {
public:
    virtual ~ITabBarHostOwner() = default;
    virtual HWND GetHostWindow() const noexcept = 0;
    virtual HWND GetHostRebarWindow() const noexcept = 0;
    virtual void NotifyTabHostFocusChange(BOOL hasFocus) = 0;
};

class TabBarHost final
    : public CWindowImpl<TabBarHost, CWindow, CControlWinTraits>
    , public IDispEventSimpleImpl<1, TabBarHost, &DIID_DWebBrowserEvents2> {
public:
    DECLARE_WND_CLASS_EX(L"QTTabBarNative_TabBarHost", CS_DBLCLKS, COLOR_WINDOW);

    explicit TabBarHost(ITabBarHostOwner& owner) noexcept;
    ~TabBarHost() override;

    void Initialize();
    void SetExplorer(IWebBrowser2* browser);
    void ClearExplorer();
    void ExecuteCommand(UINT commandId);
    void ShowContextMenu(const POINT& screenPoint);
    void SaveSessionState() const;
    void RestoreSessionState();
    void OnBandVisibilityChanged(bool visible);
    bool HandleAccelerator(MSG* pMsg);
    bool HandleMouseAction(qttabbar::MouseTarget target, qttabbar::MouseChord chord,
                           std::optional<std::size_t> tabIndex = std::nullopt);
    bool HasFocus() const noexcept { return m_hasFocus; }
    void OnParentDestroyed();

    std::vector<std::wstring> GetOpenTabs() const;
    std::vector<std::wstring> GetClosedTabHistory() const;
    void ActivateTabByIndex(std::size_t index);
    void RestoreClosedTabByIndex(std::size_t index);
    void CloneActiveTab();
    void CloseAllTabsExceptActive();
    void CloseTabsToLeft();
    void CloseTabsToRight();
    void GoUpOneLevel();
    void NavigateBack();
    void NavigateForward();
    bool OpenCapturedWindow(const std::wstring& path);
    std::wstring GetCurrentPath() const { return m_currentPath; }
    void OpenGroupByIndex(std::size_t index);

    bool ExecuteBindAction(qttabbar::BindAction action, bool isRepeat = false,
                           std::optional<std::size_t> tabIndex = std::nullopt);

    std::optional<qttabbar::BindAction> ResolveFolderLinkAction(qttabbar::MouseChord chord) const;
    bool HandleFolderLinkAction(qttabbar::BindAction action, const std::wstring& path);
    void OpenFolderInNewTab(const std::wstring& path, bool activate);
    void OpenFolderInNewWindow(const std::wstring& path);
    void OpenPathFromTooltip(const std::wstring& path);
    void OpenPathInNewTabFromTooltip(const std::wstring& path);
    void OpenPathInNewWindowFromTooltip(const std::wstring& path);

    BEGIN_MSG_MAP(TabBarHost)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
        MESSAGE_HANDLER(WM_COMMAND, OnCommand)
        MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
        MESSAGE_HANDLER(WM_KILLFOCUS, OnKillFocus)
        MESSAGE_HANDLER(WM_DPICHANGED, OnDpiChanged)
        MESSAGE_HANDLER(WM_GETDLGCODE, OnGetDlgCode)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
        MESSAGE_HANDLER(WM_SYSKEYDOWN, OnKeyDown)
        MESSAGE_HANDLER(WM_KEYUP, OnKeyUp)
        MESSAGE_HANDLER(WM_SYSKEYUP, OnKeyUp)
        MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
    END_MSG_MAP()

    BEGIN_SINK_MAP(TabBarHost)
        SINK_ENTRY_INFO(1, DIID_DWebBrowserEvents2, DISPID_BEFORENAVIGATE2, OnBeforeNavigate2, &kBeforeNavigate2Info)
        SINK_ENTRY_INFO(1, DIID_DWebBrowserEvents2, DISPID_NAVIGATECOMPLETE2, OnNavigateComplete2, &kNavigateComplete2Info)
        SINK_ENTRY_INFO(1, DIID_DWebBrowserEvents2, DISPID_DOCUMENTCOMPLETE, OnDocumentComplete, &kDocumentCompleteInfo)
        SINK_ENTRY_INFO(1, DIID_DWebBrowserEvents2, DISPID_NAVIGATEERROR, OnNavigateError, &kNavigateErrorInfo)
        SINK_ENTRY_INFO(1, DIID_DWebBrowserEvents2, DISPID_ONQUIT, OnQuit, &kOnQuitInfo)
    END_SINK_MAP()

private:
    static constexpr wchar_t kRegistryRoot[] = L"Software\\QTTabBar\\";
    static constexpr wchar_t kTabsValueName[] = L"TabsOnLastClosedWindow";
    static constexpr UINT kMaxClosedHistory = 20;
    static constexpr UINT kSelectTabTimerMs = 5000;
    static constexpr UINT kContextMenuTimerMs = 0x4B0; // 1200ms
    static constexpr UINT kSubDirTipTimerMs = 450;

    static _ATL_FUNC_INFO kBeforeNavigate2Info;
    static _ATL_FUNC_INFO kNavigateComplete2Info;
    static _ATL_FUNC_INFO kDocumentCompleteInfo;
    static _ATL_FUNC_INFO kNavigateErrorInfo;
    static _ATL_FUNC_INFO kOnQuitInfo;

    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnContextMenu(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCommand(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSetFocus(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnKillFocus(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDpiChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnGetDlgCode(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnKeyUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSettingChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    void __stdcall OnBeforeNavigate2(IDispatch* pDisp, VARIANT* url, VARIANT* flags, VARIANT* targetFrameName,
                                     VARIANT* postData, VARIANT* headers, VARIANT_BOOL* cancel);
    void __stdcall OnNavigateComplete2(IDispatch* pDisp, VARIANT* url);
    void __stdcall OnDocumentComplete(IDispatch* pDisp, VARIANT* url);
    void __stdcall OnNavigateError(IDispatch* pDisp, VARIANT* url, VARIANT* targetFrameName, VARIANT* statusCode,
                                   VARIANT_BOOL* cancel);
    void __stdcall OnQuit();

    void ConnectBrowserEvents();
    void DisconnectBrowserEvents();
    void StartTimers();
    void StopTimers();
    std::wstring VariantToString(const VARIANT* value) const;
    void UpdateActivePath(const std::wstring& path);
    void AddTab(const std::wstring& path, bool makeActive, bool allowDuplicate);
    void ActivateTab(std::size_t index);
    void ActivateNextTab();
    void ActivatePreviousTab();
    void CloseActiveTab();
    void CloseTabAt(std::size_t index);
    void RestoreLastClosed();
    void RefreshExplorer();
    void OpenOptions();
    void TrimClosedHistory();
    void LoadClosedHistory();
    void PersistClosedHistory() const;
    void ClearClosedHistory();
    void RecordClosedEntry(const std::wstring& path);
    void CloseAllTabsExcept(std::size_t index);
    void CloseTabsToLeftOf(std::size_t index);
    void CloseTabsToRightOf(std::size_t index);
    std::optional<std::size_t> ResolveContextTabIndex() const;
    std::wstring GetTabPath(std::size_t index) const;
    bool IsTabLocked(std::size_t index) const;
    void ToggleLockTab(std::size_t index);
    void SetTabAlias(std::size_t index, const std::wstring& alias);
    void CopyPathToClipboard(const std::wstring& path) const;
    void OpenCommandPrompt(const std::wstring& path) const;
    void ShowProperties(const std::wstring& path) const;
    std::wstring LoadStringResource(UINT id, const wchar_t* fallback) const;
    HMENU BuildContextMenu(std::size_t targetIndex, std::wstring targetPath);
    bool PopulateGroupMenu(HMENU menu, const std::wstring& targetPath) const;
    void PopulateHistoryMenu(HMENU menu) const;
    void LogTabsState(const wchar_t* source) const;

    void OnTabControlTabSelected(std::size_t index);
    void OnTabControlCloseRequested(std::size_t index);
    void OnTabControlContextMenuRequested(std::optional<std::size_t> index, const POINT& screenPoint);
    void OnTabControlNewTabRequested();
    void OnTabControlBeginDrag(std::size_t index, const POINT& screenPoint);
    void OnTabControlHoverChanged(std::optional<std::size_t> index, const POINT& screenPoint);

    struct ShortcutKey {
        UINT key = 0;
        UINT modifiers = 0;
        bool enabled = false;
    };

    void EnsureTabSwitcher();
    bool HandleTabSwitcherShortcut(UINT vk, UINT modifiers, bool isRepeat);
    void HideTabSwitcher(bool commit, std::optional<std::size_t> forcedIndex = std::nullopt);
    void CommitTabSwitcher(std::size_t index);
    UINT CurrentModifierMask() const;
    void ReloadConfiguration();
    static ShortcutKey DecodeShortcut(int value);
    bool ShortcutMatches(const ShortcutKey& shortcut, UINT vk, UINT modifiers) const;
    std::optional<qttabbar::BindAction> LookupMouseAction(qttabbar::MouseTarget target,
                                                          qttabbar::MouseChord chord) const;
    std::optional<qttabbar::BindAction> LookupKeyboardAction(UINT vk, UINT modifiers) const;
    static uint32_t ComposeShortcutKey(UINT vk, UINT modifiers);
    static bool IsRepeatAllowed(qttabbar::BindAction action);
    void ActivateFirstTab();
    void ActivateLastTab();
    void CloneTabAt(std::size_t index);
    void TearOffTab(std::size_t index);
    void ToggleLockAllTabs();
    void CopyCurrentFolderPathToClipboard() const;
    void CopyCurrentFolderNameToClipboard() const;
    void CopyTabPathToClipboard(std::size_t index) const;
    void OpenNewWindowAtPath(const std::wstring& path) const;
    bool FocusExplorerView() const;
    std::wstring ResolveTabPath(std::optional<std::size_t> tabIndex) const;
    bool IsTabLocked(std::optional<std::size_t> tabIndex) const;
    void NavigateToPath(const std::wstring& path);
    void ShowSubDirTip(std::size_t tabIndex);
    void HideSubDirTip();
    bool BrowseForFolder();
    bool ShowToolbarMenu();
    bool ShowTabMenuAtIndex(std::optional<std::size_t> index, const POINT& point);
    bool ShowTabMenuAtCursor();
    bool ShowButtonBarMenu(UINT commandId);
    bool DoFileTools(int index);
    bool ToggleTopMost();
    bool AdjustTransparency(bool increase);
    bool FocusSearchBand();
    bool FocusButtonBarSearch();
    bool ShowSelectedSubDirTip();
    bool SendWindowToTray();
    bool MergeAllWindows();
    bool CreateNewItem(bool isFolder);
    bool ExecuteItemCommand(qttabbar::BindAction action);
    bool OpenSelectedInNewTab(bool activate);
    bool OpenSelectedInNewWindow();
    bool InvokeShellVerb(const wchar_t* verb);
    void ShowFileHashDialog(const std::vector<std::wstring>& paths) const;
    std::optional<std::wstring> ComputeFileHash(const std::wstring& path) const;
    void RefreshTaskbarPreviews();
    std::vector<std::wstring> GetSelectedPaths() const;
    std::vector<std::wstring> GetSelectedNames() const;
    std::optional<std::wstring> ResolveShortcutTarget(const std::wstring& path) const;
    std::optional<std::wstring> ResolveFolderPathForAction(const std::wstring& path) const;
    CComPtr<IShellBrowser> GetShellBrowser() const;
    CComPtr<IShellView> GetShellView() const;
    CComPtr<IDataObject> GetSelectionDataObject() const;
    CComPtr<IContextMenu> GetSelectionContextMenu() const;
    QTButtonBar* GetButtonBar() const;
    bool SelectInExplorer(const std::wstring& path, bool edit) const;
    bool FocusExplorerSearch();
    bool ShowTabSubfolderMenu(std::size_t index);

    ITabBarHostOwner& m_owner;
    CComPtr<IWebBrowser2> m_spBrowser;
    DWORD m_browserCookie;
    std::unique_ptr<NativeTabControl> m_tabControl;
    std::deque<std::wstring> m_closedHistory;
    std::optional<std::wstring> m_pendingNavigation;
    std::wstring m_currentPath;
    UINT m_dpiX;
    UINT m_dpiY;
    bool m_hasFocus;
    bool m_visible;
    std::optional<std::size_t> m_contextTabIndex;
    std::unique_ptr<TabSwitchOverlay> m_tabSwitcher;
    ShortcutKey m_nextTabShortcut;
    ShortcutKey m_prevTabShortcut;
    bool m_useTabSwitcher = true;
    bool m_tabSwitcherActive = false;
    UINT m_tabSwitcherAnchorModifiers = 0;
    UINT m_tabSwitcherTriggerKey = 0;
    std::unordered_map<uint32_t, qttabbar::BindAction> m_keyboardBindings;
    qttabbar::MouseActionMap m_globalMouseActions;
    qttabbar::MouseActionMap m_tabMouseActions;
    qttabbar::MouseActionMap m_barMouseActions;
    qttabbar::MouseActionMap m_marginMouseActions;
    qttabbar::MouseActionMap m_linkMouseActions;
    qttabbar::MouseActionMap m_itemMouseActions;
    qttabbar::ConfigData m_config{};
    std::unique_ptr<SubDirTipWindow> m_subDirTipWindow;
    std::unique_ptr<TaskbarTabPreviewManager> m_taskbarPreviews;
    UINT_PTR m_subDirTipTimer = 0;
    std::optional<std::size_t> m_pendingSubDirTipIndex;
    POINT m_subDirTipAnchor{};
    friend class NativeTabControl;
    friend class QTTabBarClass;
};
