#include "pch.h"
#include "OptionsDialog.h"

#include <CommCtrl.h>
#include <CommDlg.h>
#include <Shobjidl.h>
#include <VersionHelpers.h>

#include <atlstr.h>
#include <windowsx.h>

#include <atomic>
#include <cwchar>
#include <memory>
#include <mutex>
#include <optional>
#include <array>
#include <utility>
#include <vector>

#include "Config.h"
#include "resource.h"

#pragma comment(lib, "Comctl32.lib")

namespace qttabbar {
namespace {

constexpr UINT kOptionsDialogCreated = WM_APP + 42;
constexpr UINT kOptionsDialogDestroy = WM_APP + 43;
constexpr UINT kOptionsDialogBringToFront = WM_APP + 44;

class OptionsDialogImpl;

std::wstring LoadStringResource(UINT stringId) {
    ATL::CStringW value;
    if(value.LoadString(stringId)) {
        return std::wstring(value.GetString());
    }
    return {};
}

std::vector<std::wstring> LoadDelimitedStrings(UINT stringId) {
    std::wstring value = LoadStringResource(stringId);
    std::vector<std::wstring> result;
    size_t start = 0;
    while(start <= value.size()) {
        size_t pos = value.find(L';', start);
        if(pos == std::wstring::npos) {
            result.emplace_back(value.substr(start));
            break;
        }
        result.emplace_back(value.substr(start, pos - start));
        start = pos + 1;
    }
    if(!value.empty() && value.back() == L';') {
        result.emplace_back(std::wstring{});
    }
    return result;
}

std::wstring PidlToParsingName(const ByteVector& pidlBytes) {
    if(pidlBytes.empty()) {
        return {};
    }
    const auto* pidlData = reinterpret_cast<const ITEMIDLIST*>(pidlBytes.data());
    PIDLIST_ABSOLUTE clone = ILCloneFull(pidlData);
    if(clone == nullptr) {
        return {};
    }
    PWSTR name = nullptr;
    std::wstring result;
    if(SUCCEEDED(SHGetNameFromIDList(clone, SIGDN_DESKTOPABSOLUTEPARSING, &name)) && name != nullptr) {
        result.assign(name);
        CoTaskMemFree(name);
    }
    CoTaskMemFree(clone);
    return result;
}

class OptionsDialogPage {
public:
    OptionsDialogPage(UINT dialogId, const wchar_t* title)
        : m_dialogId(dialogId)
        , m_title(title) {}

    virtual ~OptionsDialogPage() = default;

    UINT DialogId() const noexcept { return m_dialogId; }
    const wchar_t* Title() const noexcept { return m_title; }
    HWND Hwnd() const noexcept { return m_hwnd; }

    void Create(HWND parent, OptionsDialogImpl* owner) {
        ATLASSERT(m_hwnd == nullptr);
        m_hwnd = ::CreateDialogParamW(
            _AtlBaseModule.GetModuleInstance(),
            MAKEINTRESOURCEW(m_dialogId),
            parent,
            &OptionsDialogPage::DialogProc,
            reinterpret_cast<LPARAM>(this));
        if(m_hwnd != nullptr) {
            ::SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(owner));
            OnInitDialog(owner);
        }
    }

    void Destroy() {
        if(m_hwnd != nullptr) {
            ::DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
        }
    }

    void Show(bool visible) {
        if(m_hwnd != nullptr) {
            ::ShowWindow(m_hwnd, visible ? SW_SHOW : SW_HIDE);
        }
    }

    virtual void SyncFromConfig(const ConfigData& config) = 0;
    virtual bool SyncToConfig(ConfigData& config) const = 0;

protected:
    virtual void OnInitDialog(OptionsDialogImpl* /*owner*/) {}
    virtual BOOL OnCommand(WORD /*notifyCode*/, WORD /*commandId*/, HWND /*control*/, OptionsDialogImpl* /*owner*/) {
        return FALSE;
    }

protected:
    HWND m_hwnd = nullptr;

private:
    static INT_PTR CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if(msg == WM_INITDIALOG) {
            auto* page = reinterpret_cast<OptionsDialogPage*>(lParam);
            ::SetWindowLongPtrW(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(page));
            page->m_hwnd = hwnd;
            return TRUE;
        }

        auto* page = reinterpret_cast<OptionsDialogPage*>(::GetWindowLongPtrW(hwnd, DWLP_USER));
        if(page == nullptr) {
            return FALSE;
        }

        switch(msg) {
        case WM_COMMAND: {
            WORD notifyCode = HIWORD(wParam);
            WORD commandId = LOWORD(wParam);
            HWND control = reinterpret_cast<HWND>(lParam);
            auto* owner = reinterpret_cast<OptionsDialogImpl*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if(owner != nullptr) {
                return page->OnCommand(notifyCode, commandId, control, owner);
            }
            break;
        }
        default:
            break;
        }
        return FALSE;
    }

    UINT m_dialogId;
    const wchar_t* m_title;
};

ColorValue ColorRefToColorValue(COLORREF color) {
    return ColorValue((0xFFu << 24) | (GetRValue(color) << 16) | (GetGValue(color) << 8) | GetBValue(color));
}

COLORREF ColorValueToColorRef(ColorValue value) {
    return RGB((value.argb >> 16) & 0xFF, (value.argb >> 8) & 0xFF, value.argb & 0xFF);
}

class WindowOptionsPage final : public OptionsDialogPage {
public:
    WindowOptionsPage()
        : OptionsDialogPage(IDD_OPTIONS_WINDOW, L"Window") {}

    void SyncFromConfig(const ConfigData& config) override {
        const WindowSettings& window = config.window;
        CheckRadioButton(Hwnd(), IDC_CAPTURE_ENABLE, IDC_CAPTURE_DISABLE,
                         window.captureNewWindows ? IDC_CAPTURE_ENABLE : IDC_CAPTURE_DISABLE);
        SetDefaultLocation(window.defaultLocation);

        if(!window.restoreSession) {
            CheckRadioButton(Hwnd(), IDC_SESSION_NEW, IDC_SESSION_RESTORE_LOCKED, IDC_SESSION_NEW);
        } else if(window.restoreOnlyLocked) {
            CheckRadioButton(Hwnd(), IDC_SESSION_NEW, IDC_SESSION_RESTORE_LOCKED, IDC_SESSION_RESTORE_LOCKED);
        } else {
            CheckRadioButton(Hwnd(), IDC_SESSION_NEW, IDC_SESSION_RESTORE_LOCKED, IDC_SESSION_RESTORE_ALL);
        }

        m_closeMode = DetermineCloseMode(window);
        CheckRadioButton(Hwnd(), IDC_CLOSE_ACTION_WINDOW, IDC_CLOSE_ACTION_UNLOCKED,
                         CloseModeToRadio(m_closeMode));

        SetCheckbox(IDC_TRAY_ON_CLOSE, window.trayOnClose);
        SetCheckbox(IDC_TRAY_ON_MINIMIZE, window.trayOnMinimize);
        SetCheckbox(IDC_AUTO_HOOK_WINDOW, window.autoHookWindow);
        SetCheckbox(IDC_SHOW_FAIL_NAV_MSG, window.showFailNavMsg);
        SetCheckbox(IDC_CAPTURE_WECHAT_SELECTION, window.captureWeChatSelection);

        UpdateCloseRadiosEnabled();
    }

    bool SyncToConfig(ConfigData& config) const override {
        WindowSettings& window = config.window;
        window.captureNewWindows = IsRadioChecked(IDC_CAPTURE_ENABLE);

        std::wstring defaultLocation = GetDefaultLocationText();
        if(defaultLocation.empty()) {
            window.defaultLocation.clear();
        } else {
            ByteVector pidl = qttabbar::PidlFromDisplayName(defaultLocation.c_str());
            if(pidl.empty()) {
                ShowInvalidLocationMessage();
                return false;
            }
            window.defaultLocation = std::move(pidl);
        }

        int sessionSelection = GetCheckedRadio(IDC_SESSION_NEW, IDC_SESSION_RESTORE_LOCKED);
        if(sessionSelection == IDC_SESSION_NEW) {
            window.restoreSession = false;
            window.restoreOnlyLocked = false;
        } else if(sessionSelection == IDC_SESSION_RESTORE_LOCKED) {
            window.restoreSession = true;
            window.restoreOnlyLocked = true;
        } else {
            window.restoreSession = true;
            window.restoreOnlyLocked = false;
        }

        bool trayClose = IsChecked(IDC_TRAY_ON_CLOSE);
        window.trayOnClose = trayClose;
        window.trayOnMinimize = IsChecked(IDC_TRAY_ON_MINIMIZE);
        window.autoHookWindow = IsChecked(IDC_AUTO_HOOK_WINDOW);
        window.showFailNavMsg = IsChecked(IDC_SHOW_FAIL_NAV_MSG);
        window.captureWeChatSelection = IsChecked(IDC_CAPTURE_WECHAT_SELECTION);

        if(!trayClose) {
            m_closeMode = RadioToCloseMode(GetCheckedRadio(IDC_CLOSE_ACTION_WINDOW, IDC_CLOSE_ACTION_UNLOCKED));
        }
        ApplyCloseMode(window);

        return true;
    }

protected:
    void OnInitDialog(OptionsDialogImpl* /*owner*/) override {
        HFONT font = reinterpret_cast<HFONT>(::SendMessageW(GetParent(m_hwnd), WM_GETFONT, 0, 0));
        if(font != nullptr) {
            ::SendMessageW(m_hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
        LoadStrings();
        UpdateCloseRadiosEnabled();
    }

    BOOL OnCommand(WORD notifyCode, WORD commandId, HWND /*control*/, OptionsDialogImpl* /*owner*/) override {
        if(notifyCode == BN_CLICKED) {
            switch(commandId) {
            case IDC_DEFAULT_LOCATION_BROWSE:
                BrowseForDefaultLocation();
                return TRUE;
            case IDC_TRAY_ON_CLOSE:
                UpdateCloseRadiosEnabled();
                return TRUE;
            case IDC_CLOSE_ACTION_WINDOW:
            case IDC_CLOSE_ACTION_TAB:
            case IDC_CLOSE_ACTION_UNLOCKED:
                m_closeMode = RadioToCloseMode(commandId);
                return TRUE;
            default:
                break;
            }
        }
        return FALSE;
    }

private:
    enum class CloseMode {
        Window,
        CurrentTab,
        Unlockeds
    };

    void LoadStrings() {
        auto strings = LoadDelimitedStrings(IDS_OPTIONS_PAGE01_WINDOW);
        if(strings.size() >= 16) {
            SetDlgItemTextW(Hwnd(), IDC_WINDOW_HEADER, strings[0].c_str());
            SetDlgItemTextW(Hwnd(), IDC_WINDOW_SINGLE_LABEL, strings[1].c_str());
            SetDlgItemTextW(Hwnd(), IDC_CAPTURE_ENABLE, strings[2].c_str());
            SetDlgItemTextW(Hwnd(), IDC_CAPTURE_DISABLE, strings[3].c_str());
            SetDlgItemTextW(Hwnd(), IDC_DEFAULT_LOCATION_LABEL, strings[4].c_str());
            SetDlgItemTextW(Hwnd(), IDC_SESSION_LABEL, strings[5].c_str());
            SetDlgItemTextW(Hwnd(), IDC_SESSION_NEW, strings[6].c_str());
            SetDlgItemTextW(Hwnd(), IDC_SESSION_RESTORE_ALL, strings[7].c_str());
            SetDlgItemTextW(Hwnd(), IDC_SESSION_RESTORE_LOCKED, strings[8].c_str());
            SetDlgItemTextW(Hwnd(), IDC_CLOSE_BUTTON_LABEL, strings[9].c_str());
            SetDlgItemTextW(Hwnd(), IDC_CLOSE_ACTION_WINDOW, strings[10].c_str());
            SetDlgItemTextW(Hwnd(), IDC_CLOSE_ACTION_TAB, strings[11].c_str());
            SetDlgItemTextW(Hwnd(), IDC_CLOSE_ACTION_UNLOCKED, strings[12].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TRAY_LABEL, strings[13].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TRAY_ON_CLOSE, strings[14].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TRAY_ON_MINIMIZE, strings[15].c_str());
        }
        std::wstring others = LoadStringResource(IDS_OPTIONS_PAGE01_WINDOW_OTHERS);
        if(!others.empty()) {
            SetDlgItemTextW(Hwnd(), IDC_WINDOW_OTHERS_LABEL, others.c_str());
        }
        std::wstring autoHook = LoadStringResource(IDS_OPTIONS_WINDOW_AUTOHOOK);
        if(!autoHook.empty()) {
            SetDlgItemTextW(Hwnd(), IDC_AUTO_HOOK_WINDOW, autoHook.c_str());
        }
        std::wstring failMsg = LoadStringResource(IDS_OPTIONS_WINDOW_FAILMSG);
        if(!failMsg.empty()) {
            SetDlgItemTextW(Hwnd(), IDC_SHOW_FAIL_NAV_MSG, failMsg.c_str());
        }
        std::wstring captureSel = LoadStringResource(IDS_OPTIONS_WINDOW_CAPTURESEL);
        if(!captureSel.empty()) {
            SetDlgItemTextW(Hwnd(), IDC_CAPTURE_WECHAT_SELECTION, captureSel.c_str());
        }
    }

    void SetCheckbox(int controlId, bool value) {
        ::SendDlgItemMessageW(Hwnd(), controlId, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    bool IsChecked(int controlId) const {
        return ::SendDlgItemMessageW(Hwnd(), controlId, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    bool IsRadioChecked(int controlId) const {
        return ::SendDlgItemMessageW(Hwnd(), controlId, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    void SetDefaultLocation(const ByteVector& pidl) {
        std::wstring value = PidlToParsingName(pidl);
        SetDlgItemTextW(Hwnd(), IDC_DEFAULT_LOCATION_EDIT, value.c_str());
    }

    std::wstring GetDefaultLocationText() const {
        wchar_t buffer[1024];
        int length = ::GetDlgItemTextW(Hwnd(), IDC_DEFAULT_LOCATION_EDIT, buffer, _countof(buffer));
        if(length <= 0) {
            return {};
        }
        return std::wstring(buffer, buffer + length);
    }

    void UpdateCloseRadiosEnabled() const {
        bool enabled = !IsChecked(IDC_TRAY_ON_CLOSE);
        EnableWindow(::GetDlgItem(Hwnd(), IDC_CLOSE_ACTION_WINDOW), enabled);
        EnableWindow(::GetDlgItem(Hwnd(), IDC_CLOSE_ACTION_TAB), enabled);
        EnableWindow(::GetDlgItem(Hwnd(), IDC_CLOSE_ACTION_UNLOCKED), enabled);
    }

    static CloseMode DetermineCloseMode(const WindowSettings& settings) {
        if(settings.closeBtnClosesUnlocked) {
            return CloseMode::Unlockeds;
        }
        if(settings.closeBtnClosesSingleTab) {
            return CloseMode::CurrentTab;
        }
        return CloseMode::Window;
    }

    static int CloseModeToRadio(CloseMode mode) {
        switch(mode) {
        case CloseMode::CurrentTab:
            return IDC_CLOSE_ACTION_TAB;
        case CloseMode::Unlockeds:
            return IDC_CLOSE_ACTION_UNLOCKED;
        default:
            return IDC_CLOSE_ACTION_WINDOW;
        }
    }

    static CloseMode RadioToCloseMode(int radioId) {
        switch(radioId) {
        case IDC_CLOSE_ACTION_TAB:
            return CloseMode::CurrentTab;
        case IDC_CLOSE_ACTION_UNLOCKED:
            return CloseMode::Unlockeds;
        default:
            return CloseMode::Window;
        }
    }

    void ApplyCloseMode(WindowSettings& settings) const {
        switch(m_closeMode) {
        case CloseMode::CurrentTab:
            settings.closeBtnClosesSingleTab = true;
            settings.closeBtnClosesUnlocked = false;
            break;
        case CloseMode::Unlockeds:
            settings.closeBtnClosesSingleTab = false;
            settings.closeBtnClosesUnlocked = true;
            break;
        default:
            settings.closeBtnClosesSingleTab = false;
            settings.closeBtnClosesUnlocked = false;
            break;
        }
    }

    int GetCheckedRadio(int firstId, int lastId) const {
        for(int id = firstId; id <= lastId; ++id) {
            if(IsRadioChecked(id)) {
                return id;
            }
        }
        return firstId;
    }

    void CheckRadioButton(HWND hwnd, int firstId, int lastId, int checkId) {
        ::CheckRadioButton(hwnd, firstId, lastId, checkId);
    }

    bool BrowseForDefaultLocation() {
        CComPtr<IFileDialog> dialog;
        if(FAILED(dialog.CoCreateInstance(CLSID_FileOpenDialog))) {
            return false;
        }
        DWORD options = 0;
        if(SUCCEEDED(dialog->GetOptions(&options))) {
            dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_DONTADDTORECENT);
        }
        if(FAILED(dialog->Show(Hwnd()))) {
            return false;
        }
        CComPtr<IShellItem> result;
        if(FAILED(dialog->GetResult(&result))) {
            return false;
        }
        PWSTR path = nullptr;
        if(FAILED(result->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &path))) {
            return false;
        }
        std::wstring value(path);
        CoTaskMemFree(path);
        SetDlgItemTextW(Hwnd(), IDC_DEFAULT_LOCATION_EDIT, value.c_str());
        return true;
    }

    void ShowInvalidLocationMessage() const {
        std::wstring message = LoadStringResource(IDS_OPTIONS_INVALID_LOCATION);
        if(message.empty()) {
            message = L"Enter a valid shell path.";
        }
        std::wstring title = LoadStringResource(IDS_PROJNAME);
        if(title.empty()) {
            title = L"QTTabBar";
        }
        MessageBeep(MB_ICONWARNING);
        MessageBoxW(Hwnd(), message.c_str(), title.c_str(), MB_ICONWARNING | MB_OK);
        ::SetFocus(::GetDlgItem(Hwnd(), IDC_DEFAULT_LOCATION_EDIT));
    }

    mutable CloseMode m_closeMode = CloseMode::Window;
};

class TweaksOptionsPage final : public OptionsDialogPage {
public:
    TweaksOptionsPage()
        : OptionsDialogPage(IDD_OPTIONS_TWEAKS, L"Tweaks") {}

    void SyncFromConfig(const ConfigData& config) override {
        const TweaksSettings& tweaks = config.tweaks;
        SetCheckbox(IDC_TWEAKS_ALWAYS_HEADERS, tweaks.alwaysShowHeaders);
        SetCheckbox(IDC_TWEAKS_REDIRECT_LIBRARY, tweaks.redirectLibraryFolders);
        SetCheckbox(IDC_TWEAKS_KILL_EXT, tweaks.killExtWhileRenaming);
        SetCheckbox(IDC_TWEAKS_F2_SELECTION, tweaks.f2Selection);
        SetCheckbox(IDC_TWEAKS_WRAP_ARROW, tweaks.wrapArrowKeySelection);
        SetCheckbox(IDC_TWEAKS_BACKSPACE_UP, tweaks.backspaceUpLevel);
        SetCheckbox(IDC_TWEAKS_HORIZONTAL_SCROLL, tweaks.horizontalScroll);
        SetCheckbox(IDC_TWEAKS_FORCE_SYSLISTVIEW, tweaks.forceSysListView);
        SetCheckbox(IDC_TWEAKS_FULL_ROW_SELECT, tweaks.toggleFullRowSelect);
        SetCheckbox(IDC_TWEAKS_GRID_LINES, tweaks.detailsGridLines);
        SetCheckbox(IDC_TWEAKS_ALT_ROW_COLORS, tweaks.alternateRowColors);

        m_foregroundColor = tweaks.altRowForegroundColor;
        m_backgroundColor = tweaks.altRowBackgroundColor;
        UpdateColorText(IDC_TWEAKS_FOREGROUND_VALUE, m_foregroundColor);
        UpdateColorText(IDC_TWEAKS_BACKGROUND_VALUE, m_backgroundColor);

        UpdateForceSysListDependencies();
        UpdateColorControlsEnabled();
    }

    bool SyncToConfig(ConfigData& config) const override {
        TweaksSettings& tweaks = config.tweaks;
        tweaks.alwaysShowHeaders = IsChecked(IDC_TWEAKS_ALWAYS_HEADERS);
        tweaks.redirectLibraryFolders = IsChecked(IDC_TWEAKS_REDIRECT_LIBRARY);
        tweaks.killExtWhileRenaming = IsChecked(IDC_TWEAKS_KILL_EXT);
        tweaks.f2Selection = IsChecked(IDC_TWEAKS_F2_SELECTION);
        tweaks.wrapArrowKeySelection = IsChecked(IDC_TWEAKS_WRAP_ARROW);
        tweaks.backspaceUpLevel = IsChecked(IDC_TWEAKS_BACKSPACE_UP);
        tweaks.horizontalScroll = IsChecked(IDC_TWEAKS_HORIZONTAL_SCROLL);
        tweaks.forceSysListView = IsChecked(IDC_TWEAKS_FORCE_SYSLISTVIEW);
        tweaks.toggleFullRowSelect = IsChecked(IDC_TWEAKS_FULL_ROW_SELECT);
        tweaks.detailsGridLines = IsChecked(IDC_TWEAKS_GRID_LINES);
        tweaks.alternateRowColors = IsChecked(IDC_TWEAKS_ALT_ROW_COLORS);
        tweaks.altRowForegroundColor = m_foregroundColor;
        tweaks.altRowBackgroundColor = m_backgroundColor;
        return true;
    }

protected:
    void OnInitDialog(OptionsDialogImpl* /*owner*/) override {
        HFONT font = reinterpret_cast<HFONT>(::SendMessageW(GetParent(m_hwnd), WM_GETFONT, 0, 0));
        if(font != nullptr) {
            ::SendMessageW(m_hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
        LoadStrings();
        ApplyOsEnabling();
        UpdateForceSysListDependencies();
        UpdateColorControlsEnabled();
        UpdateColorText(IDC_TWEAKS_FOREGROUND_VALUE, m_foregroundColor);
        UpdateColorText(IDC_TWEAKS_BACKGROUND_VALUE, m_backgroundColor);
    }

    BOOL OnCommand(WORD notifyCode, WORD commandId, HWND /*control*/, OptionsDialogImpl* /*owner*/) override {
        if(notifyCode == BN_CLICKED) {
            switch(commandId) {
            case IDC_TWEAKS_FORCE_SYSLISTVIEW:
                UpdateForceSysListDependencies();
                UpdateColorControlsEnabled();
                return TRUE;
            case IDC_TWEAKS_ALT_ROW_COLORS:
                UpdateColorControlsEnabled();
                return TRUE;
            case IDC_TWEAKS_FOREGROUND_BUTTON:
                if(ChooseColor(m_foregroundColor, IDC_TWEAKS_FOREGROUND_VALUE)) {
                    return TRUE;
                }
                break;
            case IDC_TWEAKS_BACKGROUND_BUTTON:
                if(ChooseColor(m_backgroundColor, IDC_TWEAKS_BACKGROUND_VALUE)) {
                    return TRUE;
                }
                break;
            default:
                break;
            }
        }
        return FALSE;
    }

private:
    void LoadStrings() {
        auto strings = LoadDelimitedStrings(IDS_OPTIONS_PAGE03_TWEAKS);
        if(strings.size() >= 16) {
            SetDlgItemTextW(Hwnd(), IDC_TWEAKS_HEADER, strings[0].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TWEAKS_GENERAL_LABEL, strings[1].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TWEAKS_ALWAYS_HEADERS, strings[2].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TWEAKS_REDIRECT_LIBRARY, strings[3].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TWEAKS_KILL_EXT, strings[4].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TWEAKS_F2_SELECTION, strings[5].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TWEAKS_WRAP_ARROW, strings[6].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TWEAKS_BACKSPACE_UP, strings[7].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TWEAKS_HORIZONTAL_SCROLL, strings[8].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TWEAKS_OLD_LIST_LABEL, strings[9].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TWEAKS_FORCE_SYSLISTVIEW, strings[10].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TWEAKS_FULL_ROW_SELECT, strings[11].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TWEAKS_GRID_LINES, strings[12].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TWEAKS_ALT_ROW_COLORS, strings[13].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TWEAKS_FOREGROUND_LABEL, strings[14].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TWEAKS_BACKGROUND_LABEL, strings[15].c_str());
        }
    }

    void SetCheckbox(int controlId, bool value) {
        ::SendDlgItemMessageW(Hwnd(), controlId, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    bool IsChecked(int controlId) const {
        return ::SendDlgItemMessageW(Hwnd(), controlId, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    void ApplyOsEnabling() {
        bool isXpOrOlder = !::IsWindowsVistaOrGreater();
        bool isWin7OrGreater = ::IsWindows7OrGreater();

        EnableControl(IDC_TWEAKS_ALWAYS_HEADERS, !isXpOrOlder);
        EnableControl(IDC_TWEAKS_BACKSPACE_UP, !isXpOrOlder);
        EnableControl(IDC_TWEAKS_REDIRECT_LIBRARY, isWin7OrGreater);
        EnableControl(IDC_TWEAKS_FORCE_SYSLISTVIEW, isWin7OrGreater);
        EnableControl(IDC_TWEAKS_KILL_EXT, isXpOrOlder);
    }

    void EnableControl(int controlId, bool enabled) {
        HWND control = ::GetDlgItem(Hwnd(), controlId);
        if(control != nullptr) {
            ::EnableWindow(control, enabled);
        }
    }

    bool IsControlEnabled(int controlId) const {
        HWND control = ::GetDlgItem(Hwnd(), controlId);
        return control != nullptr && ::IsWindowEnabled(control);
    }

    void UpdateForceSysListDependencies() {
        bool baseEnabled = IsControlEnabled(IDC_TWEAKS_FORCE_SYSLISTVIEW) && IsChecked(IDC_TWEAKS_FORCE_SYSLISTVIEW);
        EnableControl(IDC_TWEAKS_FULL_ROW_SELECT, baseEnabled);
        EnableControl(IDC_TWEAKS_GRID_LINES, baseEnabled);
        EnableControl(IDC_TWEAKS_ALT_ROW_COLORS, baseEnabled);
    }

    void UpdateColorControlsEnabled() {
        bool colorsEnabled = IsControlEnabled(IDC_TWEAKS_ALT_ROW_COLORS) && IsChecked(IDC_TWEAKS_ALT_ROW_COLORS);
        EnableControl(IDC_TWEAKS_FOREGROUND_LABEL, colorsEnabled);
        EnableControl(IDC_TWEAKS_FOREGROUND_VALUE, colorsEnabled);
        EnableControl(IDC_TWEAKS_FOREGROUND_BUTTON, colorsEnabled);
        EnableControl(IDC_TWEAKS_BACKGROUND_LABEL, colorsEnabled);
        EnableControl(IDC_TWEAKS_BACKGROUND_VALUE, colorsEnabled);
        EnableControl(IDC_TWEAKS_BACKGROUND_BUTTON, colorsEnabled);
    }

    bool ChooseColor(ColorValue& color, int valueControlId) {
        COLORREF current = ColorValueToColorRef(color);
        CHOOSECOLORW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = Hwnd();
        dialog.lpCustColors = m_customColors.data();
        dialog.rgbResult = current;
        dialog.Flags = CC_RGBINIT | CC_FULLOPEN;
        if(::ChooseColorW(&dialog)) {
            color = ColorRefToColorValue(dialog.rgbResult);
            UpdateColorText(valueControlId, color);
            return true;
        }
        return false;
    }

    void UpdateColorText(int controlId, ColorValue color) const {
        wchar_t buffer[16];
        _snwprintf_s(buffer, _countof(buffer), L"#%02X%02X%02X", (color.argb >> 16) & 0xFF, (color.argb >> 8) & 0xFF,
                     color.argb & 0xFF);
        ::SetDlgItemTextW(Hwnd(), controlId, buffer);
    }

    ColorValue m_foregroundColor{ColorValue(0xFF000000)};
    ColorValue m_backgroundColor{ColorValue(0xFFFFFFFF)};
    std::array<COLORREF, 16> m_customColors{};
};

class TabsOptionsPage final : public OptionsDialogPage {
public:
    TabsOptionsPage()
        : OptionsDialogPage(IDD_OPTIONS_TABS, L"Tabs") {}

    void SyncFromConfig(const ConfigData& config) override {
        const TabsSettings& tabs = config.tabs;
        SetComboSelection(IDC_TABS_NEW_TAB_COMBO, tabs.newTabPosition);
        SetComboSelection(IDC_TABS_CLOSE_SWITCH_COMBO, tabs.nextAfterClosed);
        SetCheckbox(IDC_TABS_ACTIVATE_NEW, tabs.activateNewTab);
        SetCheckbox(IDC_TABS_NEVER_OPEN_SAME, tabs.neverOpenSame);
        SetCheckbox(IDC_TABS_RENAME_AMBIGUOUS, tabs.renameAmbTabs);
        CheckRadioButton(IDC_TABS_DRAG_SWITCH, IDC_TABS_DRAG_SUBMENU,
                         tabs.dragOverTabOpensSDT ? IDC_TABS_DRAG_SUBMENU : IDC_TABS_DRAG_SWITCH);
        SetCheckbox(IDC_TABS_SHOW_FOLDER_ICONS, tabs.showFolderIcon);
        SetCheckbox(IDC_TABS_SUBDIR_ICON_ACTION, tabs.showSubDirTipOnTab);
        SetCheckbox(IDC_TABS_SHOW_DRIVE_LETTER, tabs.showDriveLetters);
        SetCheckbox(IDC_TABS_CLOSE_BUTTONS, tabs.showCloseButtons);
        SetCheckbox(IDC_TABS_CLOSE_ALT_ONLY, tabs.closeBtnsWithAlt);
        SetCheckbox(IDC_TABS_CLOSE_HOVER_ONLY, tabs.closeBtnsOnHover);
        SetCheckbox(IDC_TABS_SHOW_NAV_BUTTONS, tabs.showNavButtons);
        CheckRadioButton(IDC_TABS_NAV_LEFT, IDC_TABS_NAV_RIGHT,
                         tabs.navButtonsOnRight ? IDC_TABS_NAV_RIGHT : IDC_TABS_NAV_LEFT);
        SetCheckbox(IDC_TABS_ALLOW_MULTI_ROWS, tabs.multipleTabRows);
        SetCheckbox(IDC_TABS_ACTIVE_ROW_BOTTOM, tabs.activeTabOnBottomRow);
        SetCheckbox(IDC_TABS_NEED_PLUS_BUTTON, tabs.needPlusButton);
        UpdateIconControlsEnabled();
        UpdateCloseControlsEnabled();
        UpdateNavButtonControls();
        UpdateMultiRowControls();
    }

    bool SyncToConfig(ConfigData& config) const override {
        TabsSettings& tabs = config.tabs;
        tabs.newTabPosition = GetComboSelection(IDC_TABS_NEW_TAB_COMBO).value_or(TabPos::Rightmost);
        tabs.nextAfterClosed = GetComboSelection(IDC_TABS_CLOSE_SWITCH_COMBO).value_or(TabPos::LastActive);
        tabs.activateNewTab = IsChecked(IDC_TABS_ACTIVATE_NEW);
        tabs.neverOpenSame = IsChecked(IDC_TABS_NEVER_OPEN_SAME);
        tabs.renameAmbTabs = IsChecked(IDC_TABS_RENAME_AMBIGUOUS);
        tabs.dragOverTabOpensSDT = IsRadioChecked(IDC_TABS_DRAG_SUBMENU);
        tabs.showFolderIcon = IsChecked(IDC_TABS_SHOW_FOLDER_ICONS);
        tabs.showSubDirTipOnTab = IsChecked(IDC_TABS_SUBDIR_ICON_ACTION);
        tabs.showDriveLetters = IsChecked(IDC_TABS_SHOW_DRIVE_LETTER);
        tabs.showCloseButtons = IsChecked(IDC_TABS_CLOSE_BUTTONS);
        tabs.closeBtnsWithAlt = IsChecked(IDC_TABS_CLOSE_ALT_ONLY);
        tabs.closeBtnsOnHover = IsChecked(IDC_TABS_CLOSE_HOVER_ONLY);
        tabs.showNavButtons = IsChecked(IDC_TABS_SHOW_NAV_BUTTONS);
        tabs.navButtonsOnRight = IsRadioChecked(IDC_TABS_NAV_RIGHT);
        tabs.multipleTabRows = IsChecked(IDC_TABS_ALLOW_MULTI_ROWS);
        tabs.activeTabOnBottomRow = IsChecked(IDC_TABS_ACTIVE_ROW_BOTTOM);
        tabs.needPlusButton = IsChecked(IDC_TABS_NEED_PLUS_BUTTON);
        return true;
    }

protected:
    void OnInitDialog(OptionsDialogImpl* /*owner*/) override {
        HFONT font = reinterpret_cast<HFONT>(::SendMessageW(GetParent(m_hwnd), WM_GETFONT, 0, 0));
        if(font != nullptr) {
            ::SendMessageW(m_hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
        LoadStrings();
        PopulateCombos();
        UpdateIconControlsEnabled();
        UpdateCloseControlsEnabled();
        UpdateNavButtonControls();
        UpdateMultiRowControls();
    }

    BOOL OnCommand(WORD notifyCode, WORD commandId, HWND /*control*/, OptionsDialogImpl* /*owner*/) override {
        if(notifyCode == BN_CLICKED) {
            switch(commandId) {
            case IDC_TABS_SHOW_FOLDER_ICONS:
                UpdateIconControlsEnabled();
                return TRUE;
            case IDC_TABS_CLOSE_BUTTONS:
                UpdateCloseControlsEnabled();
                return TRUE;
            case IDC_TABS_SHOW_NAV_BUTTONS:
                UpdateNavButtonControls();
                return TRUE;
            case IDC_TABS_ALLOW_MULTI_ROWS:
                UpdateMultiRowControls();
                return TRUE;
            default:
                break;
            }
        }
        return FALSE;
    }

private:
    void LoadStrings() {
        auto strings = LoadDelimitedStrings(IDS_OPTIONS_PAGE02_TABS);
        if(strings.size() >= 33) {
            SetDlgItemTextW(Hwnd(), IDC_TABS_HEADER, strings[0].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TABS_OPEN_LABEL, strings[1].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TABS_CLOSE_SWITCH_LABEL, strings[2].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TABS_ACTIVATE_NEW, strings[3].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TABS_NEVER_OPEN_SAME, strings[4].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TABS_RENAME_AMBIGUOUS, strings[5].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TABS_DRAG_HOVER_LABEL, strings[6].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TABS_DRAG_SWITCH, strings[7].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TABS_DRAG_SUBMENU, strings[8].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TABS_ICON_HEADER, strings[9].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TABS_SHOW_FOLDER_ICONS, strings[10].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TABS_SUBDIR_ICON_ACTION, strings[11].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TABS_SHOW_DRIVE_LETTER, strings[12].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TABS_CLOSE_HEADER, strings[13].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TABS_CLOSE_BUTTONS, strings[14].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TABS_CLOSE_ALT_ONLY, strings[15].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TABS_CLOSE_HOVER_ONLY, strings[16].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TABS_BAR_HEADER, strings[17].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TABS_SHOW_NAV_BUTTONS, strings[18].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TABS_NAV_LEFT, strings[19].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TABS_NAV_RIGHT, strings[20].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TABS_ALLOW_MULTI_ROWS, strings[21].c_str());
            SetDlgItemTextW(Hwnd(), IDC_TABS_ACTIVE_ROW_BOTTOM, strings[22].c_str());
            m_newTabItems = {strings[23], strings[24], strings[25], strings[26]};
            m_afterCloseItems = {strings[27], strings[28], strings[29], strings[30], strings[31]};
        }
        auto windowStrings = LoadDelimitedStrings(IDS_OPTIONS_PAGE01_WINDOW);
        if(windowStrings.size() > 16) {
            SetDlgItemTextW(Hwnd(), IDC_TABS_NEED_PLUS_BUTTON, windowStrings[16].c_str());
        }
    }

    void PopulateCombos() {
        struct ComboDef {
            int controlId;
            std::initializer_list<std::pair<TabPos, size_t>> items;
        };

        const ComboDef combos[] = {
            {IDC_TABS_NEW_TAB_COMBO,
             {{TabPos::Left, 0}, {TabPos::Right, 1}, {TabPos::Leftmost, 2}, {TabPos::Rightmost, 3}}},
            {IDC_TABS_CLOSE_SWITCH_COMBO,
             {{TabPos::Left, 0}, {TabPos::Right, 1}, {TabPos::Leftmost, 2}, {TabPos::Rightmost, 3}, {TabPos::LastActive, 4}}},
        };

        for(const auto& combo : combos) {
            HWND control = ::GetDlgItem(Hwnd(), combo.controlId);
            if(control == nullptr) {
                continue;
            }
            ::SendMessageW(control, CB_RESETCONTENT, 0, 0);
            for(auto [value, stringIndex] : combo.items) {
                const std::wstring& text = combo.controlId == IDC_TABS_NEW_TAB_COMBO
                                                ? m_newTabItems[stringIndex]
                                                : m_afterCloseItems[stringIndex];
                int idx = static_cast<int>(::SendMessageW(control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str())));
                if(idx != CB_ERR && idx != CB_ERRSPACE) {
                    ::SendMessageW(control, CB_SETITEMDATA, idx,
                                    static_cast<LPARAM>(static_cast<uint32_t>(value)));
                }
            }
            ::SendMessageW(control, CB_SETCURSEL, 0, 0);
        }
    }

    void SetCheckbox(int controlId, bool checked) {
        ::SendDlgItemMessageW(Hwnd(), controlId, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    bool IsChecked(int controlId) const {
        return ::SendDlgItemMessageW(Hwnd(), controlId, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    bool IsRadioChecked(int controlId) const {
        return ::SendDlgItemMessageW(Hwnd(), controlId, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    void CheckRadioButton(int firstId, int lastId, int checkId) {
        ::CheckRadioButton(Hwnd(), firstId, lastId, checkId);
    }

    void UpdateIconControlsEnabled() const {
        bool enabled = IsChecked(IDC_TABS_SHOW_FOLDER_ICONS);
        EnableWindow(::GetDlgItem(Hwnd(), IDC_TABS_SUBDIR_ICON_ACTION), enabled);
        EnableWindow(::GetDlgItem(Hwnd(), IDC_TABS_SHOW_DRIVE_LETTER), enabled);
    }

    void UpdateCloseControlsEnabled() const {
        bool enabled = IsChecked(IDC_TABS_CLOSE_BUTTONS);
        EnableWindow(::GetDlgItem(Hwnd(), IDC_TABS_CLOSE_ALT_ONLY), enabled);
        EnableWindow(::GetDlgItem(Hwnd(), IDC_TABS_CLOSE_HOVER_ONLY), enabled);
    }

    void UpdateNavButtonControls() const {
        bool enabled = IsChecked(IDC_TABS_SHOW_NAV_BUTTONS);
        EnableWindow(::GetDlgItem(Hwnd(), IDC_TABS_NAV_LEFT), enabled);
        EnableWindow(::GetDlgItem(Hwnd(), IDC_TABS_NAV_RIGHT), enabled);
    }

    void UpdateMultiRowControls() const {
        bool enabled = IsChecked(IDC_TABS_ALLOW_MULTI_ROWS);
        EnableWindow(::GetDlgItem(Hwnd(), IDC_TABS_ACTIVE_ROW_BOTTOM), enabled);
    }

    std::optional<TabPos> GetComboSelection(int controlId) const {
        HWND control = ::GetDlgItem(Hwnd(), controlId);
        if(control == nullptr) {
            return std::nullopt;
        }
        int sel = static_cast<int>(::SendMessageW(control, CB_GETCURSEL, 0, 0));
        if(sel == CB_ERR) {
            return std::nullopt;
        }
        LRESULT data = ::SendMessageW(control, CB_GETITEMDATA, sel, 0);
        uint32_t raw = static_cast<uint32_t>(static_cast<LONG_PTR>(data));
        return static_cast<TabPos>(raw);
    }

    void SetComboSelection(int controlId, TabPos value) {
        HWND control = ::GetDlgItem(Hwnd(), controlId);
        if(control == nullptr) {
            return;
        }
        int count = static_cast<int>(::SendMessageW(control, CB_GETCOUNT, 0, 0));
        for(int i = 0; i < count; ++i) {
            LRESULT data = ::SendMessageW(control, CB_GETITEMDATA, i, 0);
            uint32_t raw = static_cast<uint32_t>(static_cast<LONG_PTR>(data));
            if(static_cast<TabPos>(raw) == value) {
                ::SendMessageW(control, CB_SETCURSEL, i, 0);
                return;
            }
        }
        ::SendMessageW(control, CB_SETCURSEL, 0, 0);
    }

    std::array<std::wstring, 4> m_newTabItems{};
    std::array<std::wstring, 5> m_afterCloseItems{};
};

class PlaceholderOptionsPage final : public OptionsDialogPage {
public:
    PlaceholderOptionsPage(UINT dialogId, const wchar_t* title)
        : OptionsDialogPage(dialogId, title) {}

    void SyncFromConfig(const ConfigData& /*config*/) override {}
    bool SyncToConfig(ConfigData& /*config*/) const override { return true; }
};

class OptionsDialogImpl {
public:
    explicit OptionsDialogImpl(HWND explorerWindow)
        : m_explorerWindow(explorerWindow) {
        m_originalConfig = LoadConfigFromRegistry();
        m_workingConfig = m_originalConfig;
    }

    ~OptionsDialogImpl() {
        for(auto& page : m_pages) {
            page->Destroy();
        }
    }

    HWND hwnd() const noexcept { return m_hwnd; }

    void CreateDialogWindow() {
        ATLASSERT(m_hwnd == nullptr);
        HWND owner = m_explorerWindow;
        m_hwnd = ::CreateDialogParamW(
            _AtlBaseModule.GetModuleInstance(),
            MAKEINTRESOURCEW(IDD_OPTIONS_DIALOG),
            owner,
            &OptionsDialogImpl::DialogProc,
            reinterpret_cast<LPARAM>(this));
        if(m_hwnd != nullptr) {
            ::SetWindowLongPtrW(m_hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(this));
        }
    }

    void RunMessageLoop() {
        MSG msg;
        while(::GetMessageW(&msg, nullptr, 0, 0)) {
            if(m_hwnd == nullptr || !::IsDialogMessageW(m_hwnd, &msg)) {
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
            }
        }
    }

    void Show() {
        if(m_hwnd != nullptr) {
            ::ShowWindow(m_hwnd, SW_SHOW);
            ::SetForegroundWindow(m_hwnd);
        }
    }

    void Close() {
        if(m_hwnd != nullptr) {
            ::DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
        }
    }

    void OnInitDialog(HWND hwnd) {
        m_hwnd = hwnd;
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        InitPages();
        SyncPagesFromConfig();
        HWND tab = ::GetDlgItem(m_hwnd, IDC_OPTIONS_TAB);
        if(tab != nullptr) {
            TabCtrl_SetCurSel(tab, 0);
        }
        SelectPage(0);
    }

    void OnDestroyDialog() {
        m_hwnd = nullptr;
        ::PostQuitMessage(0);
    }

    BOOL OnCommand(WORD notifyCode, WORD commandId, HWND control) {
        switch(commandId) {
        case IDOK:
            ApplyChanges(true);
            return TRUE;
        case IDCANCEL:
            CancelChanges();
            return TRUE;
        case IDC_APPLY_BUTTON:
            ApplyChanges(false);
            return TRUE;
        case IDC_RESET_BUTTON:
            ResetCurrentPage();
            return TRUE;
        default:
            break;
        }

        if(control != nullptr && ::IsWindow(control)) {
            HWND page = CurrentPageWindow();
            if(page != nullptr) {
                if(::SendMessageW(page, WM_COMMAND, MAKEWPARAM(commandId, notifyCode), reinterpret_cast<LPARAM>(control))) {
                    return TRUE;
                }
            }
        }
        return FALSE;
    }

    LRESULT OnNotify(int controlId, LPNMHDR header) {
        if(controlId == IDC_OPTIONS_TAB && header->code == TCN_SELCHANGE) {
            int index = TabCtrl_GetCurSel(header->hwndFrom);
            SelectPage(index);
            return TRUE;
        }
        return FALSE;
    }

    void BringToFront() {
        if(m_hwnd != nullptr) {
            if(::IsIconic(m_hwnd)) {
                ::ShowWindow(m_hwnd, SW_RESTORE);
            }
            ::SetForegroundWindow(m_hwnd);
        }
    }

private:
    static INT_PTR CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if(msg == WM_INITDIALOG) {
            auto* self = reinterpret_cast<OptionsDialogImpl*>(lParam);
            if(self != nullptr) {
                self->OnInitDialog(hwnd);
                return TRUE;
            }
            return FALSE;
        }

        auto* self = reinterpret_cast<OptionsDialogImpl*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if(self == nullptr) {
            return FALSE;
        }

        switch(msg) {
        case WM_COMMAND:
            return self->OnCommand(HIWORD(wParam), LOWORD(wParam), reinterpret_cast<HWND>(lParam));
        case WM_NOTIFY:
            return self->OnNotify(static_cast<int>(wParam), reinterpret_cast<LPNMHDR>(lParam));
        case WM_CLOSE:
            self->CancelChanges();
            return TRUE;
        case WM_DESTROY:
            self->OnDestroyDialog();
            return TRUE;
        case WM_DPICHANGED:
            self->OnDpiChanged(reinterpret_cast<RECT*>(lParam));
            return TRUE;
        case kOptionsDialogCreated:
            self->Show();
            return TRUE;
        case kOptionsDialogDestroy:
            self->CancelChanges();
            return TRUE;
        case kOptionsDialogBringToFront:
            self->BringToFront();
            return TRUE;
        default:
            break;
        }
        return FALSE;
    }

    void InitPages() {
        HWND tab = ::GetDlgItem(m_hwnd, IDC_OPTIONS_TAB);
        ATLASSERT(tab != nullptr);
        ::SendMessageW(tab, WM_SETFONT, ::SendMessageW(m_hwnd, WM_GETFONT, 0, 0), TRUE);

        m_pages.emplace_back(std::make_unique<WindowOptionsPage>());
        m_pages.emplace_back(std::make_unique<TabsOptionsPage>());
        m_pages.emplace_back(std::make_unique<TweaksOptionsPage>());
        m_pages.emplace_back(std::make_unique<PlaceholderOptionsPage>(IDD_OPTIONS_TOOLTIPS, L"Tooltips"));
        m_pages.emplace_back(std::make_unique<PlaceholderOptionsPage>(IDD_OPTIONS_GENERAL, L"General"));
        m_pages.emplace_back(std::make_unique<PlaceholderOptionsPage>(IDD_OPTIONS_APPEARANCE, L"Appearance"));
        m_pages.emplace_back(std::make_unique<PlaceholderOptionsPage>(IDD_OPTIONS_MOUSE, L"Mouse"));
        m_pages.emplace_back(std::make_unique<PlaceholderOptionsPage>(IDD_OPTIONS_KEYS, L"Keys"));
        m_pages.emplace_back(std::make_unique<PlaceholderOptionsPage>(IDD_OPTIONS_GROUPS, L"Groups"));
        m_pages.emplace_back(std::make_unique<PlaceholderOptionsPage>(IDD_OPTIONS_APPS, L"Apps"));
        m_pages.emplace_back(std::make_unique<PlaceholderOptionsPage>(IDD_OPTIONS_BUTTONBAR, L"Button Bar"));
        m_pages.emplace_back(std::make_unique<PlaceholderOptionsPage>(IDD_OPTIONS_PLUGINS, L"Plugins"));
        m_pages.emplace_back(std::make_unique<PlaceholderOptionsPage>(IDD_OPTIONS_LANGUAGE, L"Language"));
        m_pages.emplace_back(std::make_unique<PlaceholderOptionsPage>(IDD_OPTIONS_ABOUT, L"About"));

        RECT pageRect = GetPageRect();
        for(size_t index = 0; index < m_pages.size(); ++index) {
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            item.pszText = const_cast<wchar_t*>(m_pages[index]->Title());
            TabCtrl_InsertItem(tab, static_cast<int>(index), &item);
            m_pages[index]->Create(m_hwnd, this);
            if(m_pages[index]->Hwnd() != nullptr) {
                ::SetWindowPos(m_pages[index]->Hwnd(), nullptr, pageRect.left, pageRect.top,
                               pageRect.right - pageRect.left, pageRect.bottom - pageRect.top,
                               SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
    }

    void SyncPagesFromConfig() {
        for(auto& page : m_pages) {
            page->SyncFromConfig(m_workingConfig);
        }
    }

    bool SyncPagesToConfig() {
        for(auto& page : m_pages) {
            if(!page->SyncToConfig(m_workingConfig)) {
                return false;
            }
        }
        return true;
    }

    void SelectPage(int index) {
        if(index < 0 || index >= static_cast<int>(m_pages.size())) {
            return;
        }
        if(m_activePage == index) {
            return;
        }
        if(m_activePage >= 0) {
            m_pages[m_activePage]->Show(false);
        }
        m_activePage = index;
        m_pages[m_activePage]->Show(true);
    }

    HWND CurrentPageWindow() const {
        if(m_activePage < 0 || m_activePage >= static_cast<int>(m_pages.size())) {
            return nullptr;
        }
        return m_pages[m_activePage]->Hwnd();
    }

    void ApplyChanges(bool closeAfterApply) {
        if(!SyncPagesToConfig()) {
            return;
        }
        WriteConfigToRegistry(m_workingConfig, false);
        UpdateConfigSideEffects(m_workingConfig, true);
        m_originalConfig = m_workingConfig;
        if(closeAfterApply) {
            Close();
        }
    }

    void CancelChanges() {
        m_workingConfig = m_originalConfig;
        Close();
    }

    void ResetCurrentPage() {
        if(m_activePage < 0 || m_activePage >= static_cast<int>(m_pages.size())) {
            return;
        }
        m_pages[m_activePage]->SyncFromConfig(m_originalConfig);
        m_pages[m_activePage]->SyncToConfig(m_workingConfig);
    }

    void OnDpiChanged(RECT* suggestedRect) {
        if(suggestedRect != nullptr) {
            ::SetWindowPos(m_hwnd, nullptr, suggestedRect->left, suggestedRect->top,
                           suggestedRect->right - suggestedRect->left,
                           suggestedRect->bottom - suggestedRect->top,
                           SWP_NOZORDER | SWP_NOACTIVATE);
            RECT pageRect = GetPageRect();
            for(auto& page : m_pages) {
                if(page->Hwnd() != nullptr) {
                    ::SetWindowPos(page->Hwnd(), nullptr, pageRect.left, pageRect.top,
                                   pageRect.right - pageRect.left, pageRect.bottom - pageRect.top,
                                   SWP_NOZORDER | SWP_NOACTIVATE);
                }
            }
        }
    }

    RECT GetPageRect() const {
        RECT tabRect{};
        HWND tab = ::GetDlgItem(m_hwnd, IDC_OPTIONS_TAB);
        if(tab == nullptr) {
            return tabRect;
        }
        ::GetClientRect(tab, &tabRect);
        TabCtrl_AdjustRect(tab, FALSE, &tabRect);
        POINT points[2] = {{tabRect.left, tabRect.top}, {tabRect.right, tabRect.bottom}};
        ::MapWindowPoints(tab, m_hwnd, points, 2);
        RECT result{points[0].x, points[0].y, points[1].x, points[1].y};
        return result;
    }

    HWND m_explorerWindow;
    HWND m_hwnd = nullptr;
    std::vector<std::unique_ptr<OptionsDialogPage>> m_pages;
    int m_activePage = -1;
    ConfigData m_originalConfig;
    ConfigData m_workingConfig;
};

struct DialogThreadState {
    std::mutex mutex;
    HANDLE thread = nullptr;
    std::atomic<HWND> hwnd{nullptr};
    std::atomic<bool> running{false};
};

DialogThreadState& GetThreadState() {
    static DialogThreadState state;
    return state;
}

DWORD WINAPI DialogThreadProc(LPVOID param) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool coInitialized = SUCCEEDED(hr);
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_TAB_CLASSES | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    ::InitCommonControlsEx(&icc);
    std::shared_ptr<OptionsDialogImpl>* initParam = reinterpret_cast<std::shared_ptr<OptionsDialogImpl>*>(param);
    std::shared_ptr<OptionsDialogImpl> impl = *initParam;
    delete initParam;

    impl->CreateDialogWindow();
    HWND hwnd = impl->hwnd();
    if(hwnd != nullptr) {
        GetThreadState().hwnd.store(hwnd, std::memory_order_release);
        ::PostMessageW(hwnd, kOptionsDialogCreated, 0, 0);
    } else {
        GetThreadState().running.store(false, std::memory_order_release);
        if(coInitialized) {
            CoUninitialize();
        }
        return 0;
    }

    impl->RunMessageLoop();
    GetThreadState().hwnd.store(nullptr, std::memory_order_release);
    if(coInitialized) {
        CoUninitialize();
    }
    GetThreadState().running.store(false, std::memory_order_release);
    return 0;
}

void EnsureThreadStarted(HWND explorerWindow) {
    DialogThreadState& state = GetThreadState();
    std::scoped_lock lock(state.mutex);
    if(state.thread != nullptr) {
        DWORD exitCode = 0;
        if(::GetExitCodeThread(state.thread, &exitCode) && exitCode != STILL_ACTIVE) {
            ::CloseHandle(state.thread);
            state.thread = nullptr;
        }
    }
    if(state.running.load(std::memory_order_acquire)) {
        HWND hwnd = state.hwnd.load(std::memory_order_acquire);
        if(hwnd != nullptr) {
            ::PostMessageW(hwnd, kOptionsDialogBringToFront, 0, 0);
        }
        return;
    }

    auto impl = std::make_shared<OptionsDialogImpl>(explorerWindow);
    auto* implPtr = new std::shared_ptr<OptionsDialogImpl>(impl);
    HANDLE thread = ::CreateThread(nullptr, 0, DialogThreadProc, implPtr, 0, nullptr);
    if(thread != nullptr) {
        state.thread = thread;
        state.running.store(true, std::memory_order_release);
    } else {
        delete implPtr;
    }
}

}  // namespace

void OptionsDialog::Open(HWND explorerWindow) {
    EnsureThreadStarted(explorerWindow);
}

void OptionsDialog::ForceClose() {
    DialogThreadState& state = GetThreadState();
    HWND hwnd = state.hwnd.load(std::memory_order_acquire);
    if(hwnd != nullptr) {
        ::PostMessageW(hwnd, kOptionsDialogDestroy, 0, 0);
    }
}

bool OptionsDialog::IsOpen() {
    return GetThreadState().running.load(std::memory_order_acquire) &&
           GetThreadState().hwnd.load(std::memory_order_acquire) != nullptr;
}

}  // namespace qttabbar
