#include "pch.h"
#include "QTTabBarClass.h"
#include "HookManagerNative.h"
#include "HookMessages.h"
#include "OptionsDialog.h"
#include "TabBarHost.h"
#include "BreadcrumbBar.h"

#include <Shldisp.h>
#include <ShlObj.h>

#include "InstanceManagerNative.h"

using qttabbar::BindAction;

constexpr DWORD kRebarMaskStyle = RBBIM_STYLE | RBBIM_CHILD;

std::wstring PathFromPidl(PCIDLIST_ABSOLUTE pidl) {
    if(pidl == nullptr) {
        return {};
    }
    PWSTR buffer = nullptr;
    std::wstring path;
    if(SUCCEEDED(::SHGetNameFromIDList(pidl, SIGDN_FILESYSPATH, &buffer)) && buffer != nullptr) {
        path.assign(buffer);
        ::CoTaskMemFree(buffer);
        return path;
    }
    if(SUCCEEDED(::SHGetNameFromIDList(pidl, SIGDN_DESKTOPABSOLUTEPARSING, &buffer)) && buffer != nullptr) {
        path.assign(buffer);
        ::CoTaskMemFree(buffer);
    }
    return path;
}

class RebarBreakFixer {
public:
    RebarBreakFixer(HWND hwndRebar, QTTabBarClass* parent)
        : m_hwnd(hwndRebar)
        , m_parent(parent)
        , m_prevProc(nullptr)
        , m_monitorSetInfo(true)
        , m_enabled(true) {
        ATLASSERT(::IsWindow(hwndRebar));
        std::scoped_lock lock(s_mapMutex);
        m_prevProc = reinterpret_cast<WNDPROC>(::SetWindowLongPtrW(m_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&RebarBreakFixer::SubclassProc)));
        s_instances[m_hwnd] = this;
    }

    ~RebarBreakFixer() {
        std::scoped_lock lock(s_mapMutex);
        if(::IsWindow(m_hwnd) && m_prevProc != nullptr) {
            ::SetWindowLongPtrW(m_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(m_prevProc));
        }
        s_instances.erase(m_hwnd);
    }

    RebarBreakFixer(const RebarBreakFixer&) = delete;
    RebarBreakFixer& operator=(const RebarBreakFixer&) = delete;

    void SetMonitorSetInfo(bool enabled) noexcept { m_monitorSetInfo = enabled; }
    void SetEnabled(bool enabled) noexcept { m_enabled = enabled; }

private:
    static RebarBreakFixer* Lookup(HWND hwnd) {
        std::scoped_lock lock(s_mapMutex);
        auto it = s_instances.find(hwnd);
        return it != s_instances.end() ? it->second : nullptr;
    }

    static LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        RebarBreakFixer* self = Lookup(hwnd);
        if(self == nullptr) {
            return ::DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        if(!self->m_enabled) {
            return ::CallWindowProcW(self->m_prevProc, hwnd, msg, wParam, lParam);
        }

        if(msg == RB_SETBANDINFO && self->m_monitorSetInfo && lParam != 0) {
            auto* bandInfo = reinterpret_cast<REBARBANDINFOW*>(lParam);
            if((bandInfo->fMask & RBBIM_STYLE) != 0 && bandInfo->hwndChild == self->m_parent->m_hWnd) {
                if(self->m_parent->ShouldHaveBreak()) {
                    bandInfo->fStyle |= RBBS_BREAK;
                } else {
                    bandInfo->fStyle &= ~RBBS_BREAK;
                }
            }
        } else if(msg == RB_DELETEBAND) {
            int deleteIndex = static_cast<int>(wParam);
            int count = self->m_parent->ActiveRebarCount();
            for(int i = 0; i < count; ++i) {
                REBARBANDINFOW info = self->m_parent->GetRebarBand(i, kRebarMaskStyle | RBBIM_STYLE);
                if(info.hwndChild == self->m_parent->m_hWnd) {
                    LRESULT result = ::CallWindowProcW(self->m_prevProc, hwnd, msg, wParam, lParam);
                    if(i == deleteIndex) {
                        return result;
                    }

                    REBARBANDINFOW restore = {};
                    restore.cbSize = sizeof(restore);
                    restore.fMask = RBBIM_STYLE;
                    restore.fStyle = info.fStyle;

                    bool previousMonitor = self->m_monitorSetInfo;
                    self->m_monitorSetInfo = false;
                    ::SendMessageW(hwnd, RB_SETBANDINFO, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(&restore));
                    self->m_monitorSetInfo = previousMonitor;
                    return result;
                }
            }
        }

        return ::CallWindowProcW(self->m_prevProc, hwnd, msg, wParam, lParam);
    }

    HWND m_hwnd;
    QTTabBarClass* m_parent;
    WNDPROC m_prevProc;
    bool m_monitorSetInfo;
    bool m_enabled;

    static std::mutex s_mapMutex;
    static std::unordered_map<HWND, RebarBreakFixer*> s_instances;
};

std::mutex RebarBreakFixer::s_mapMutex;
std::unordered_map<HWND, RebarBreakFixer*> RebarBreakFixer::s_instances;

QTTabBarClass::QTTabBarClass() noexcept
    : m_hwndRebar(nullptr)
    , m_explorerHwnd(nullptr)
    , m_minSize{16, 26}
    , m_maxSize{-1, -1}
    , m_closed(false)
    , m_visible(false)
    , m_vertical(false)
    , m_bandId(0) {
}

QTTabBarClass::~QTTabBarClass() {
}

HRESULT QTTabBarClass::FinalConstruct() {
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES | ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES;
    ::InitCommonControlsEx(&icc);
    return S_OK;
}

void QTTabBarClass::FinalRelease() {
    InstanceManagerNative::Instance().UnregisterTabBar(this);
    DestroyTimers();
    ReleaseRebarSubclass();
    ResetBreadcrumbBar();
    if(m_tabHost) {
        m_tabHost->OnParentDestroyed();
        if(m_tabHost->IsWindow()) {
            m_tabHost->DestroyWindow();
        }
        m_tabHost.reset();
    }
    if(m_hWnd != nullptr && ::IsWindow(m_hWnd)) {
        ::DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }
    PersistBreakPreference();
    m_spExplorer.Release();
    m_spServiceProvider.Release();
    m_spInputObjectSite.Release();
    m_spSite.Release();
}

HRESULT QTTabBarClass::EnsureWindow() {
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

void QTTabBarClass::InitializeTimers() {
    if(m_hWnd == nullptr) {
        return;
    }
    ::SetTimer(m_hWnd, ID_TIMER_SELECTTAB, kSelectTabTimerMs, nullptr);
    ::SetTimer(m_hWnd, ID_TIMER_CONTEXTMENU, kShowMenuTimerMs, nullptr);
}

void QTTabBarClass::DestroyTimers() {
    if(m_hWnd != nullptr) {
        ::KillTimer(m_hWnd, ID_TIMER_SELECTTAB);
        ::KillTimer(m_hWnd, ID_TIMER_CONTEXTMENU);
    }
}

void QTTabBarClass::EnsureRebarSubclass() {
    if(m_hwndRebar == nullptr) {
        return;
    }
    if(!m_rebarSubclass) {
        m_rebarSubclass = std::make_unique<RebarBreakFixer>(m_hwndRebar, this);
    }
    if(m_rebarSubclass) {
        m_rebarSubclass->SetEnabled(true);
        m_rebarSubclass->SetMonitorSetInfo(true);
    }
}

void QTTabBarClass::ReleaseRebarSubclass() {
    if(m_rebarSubclass) {
        m_rebarSubclass->SetEnabled(false);
        m_rebarSubclass.reset();
    }
}

void QTTabBarClass::UpdateVisibility(BOOL fShow) {
    m_visible = fShow != FALSE;
    if(m_hWnd != nullptr) {
        ::ShowWindow(m_hWnd, fShow ? SW_SHOW : SW_HIDE);
        if(fShow) {
            ::SetWindowPos(m_hWnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    if(m_tabHost && m_tabHost->IsWindow()) {
        ::ShowWindow(m_tabHost->m_hWnd, fShow ? SW_SHOW : SW_HIDE);
    }
    if(m_tabHost) {
        m_tabHost->OnBandVisibilityChanged(fShow != FALSE);
    }
    if(fShow) {
        EnsureRebarSubclass();
        StartDeferredRebarReset();
    }
}

void QTTabBarClass::NotifyFocusChange(BOOL hasFocus) {
    if(m_spInputObjectSite != nullptr && !m_closed) {
        m_spInputObjectSite->OnFocusChangeIS(static_cast<IUnknown*>(static_cast<IQTTabBarClass*>(this)), hasFocus);
    }
}

void QTTabBarClass::StartDeferredRebarReset() {
    if(m_hWnd != nullptr) {
        ::PostMessageW(m_hWnd, WM_APP_UNSUBCLASS, 0, 0);
    }
}

void QTTabBarClass::PersistBreakPreference() const {
    CRegKey key;
    if(key.Create(HKEY_CURRENT_USER, L"Software\\QTTabBar\\") == ERROR_SUCCESS) {
        key.SetDWORDValue(L"BreakTabBar", BandHasBreak() ? 1u : 0u);
    }
}

void QTTabBarClass::InitializeBreadcrumbBar() {
    if(m_breadcrumbBar || m_explorerHwnd == nullptr) {
        return;
    }
    HWND hwndParent = ::FindWindowExW(m_explorerHwnd, nullptr, L"Breadcrumb Parent", nullptr);
    if(hwndParent == nullptr) {
        return;
    }
    HWND hwndToolbar = ::FindWindowExW(hwndParent, nullptr, L"ToolbarWindow32", nullptr);
    if(hwndToolbar == nullptr) {
        return;
    }
    auto helper = std::make_unique<BreadcrumbBarHelper>(hwndToolbar);
    helper->SetItemClickedCallback([this](PCIDLIST_ABSOLUTE pidl, UINT modifiers, bool middle) {
        return this->HandleBreadcrumbClick(pidl, modifiers, middle);
    });
    m_breadcrumbBar = std::move(helper);
}

void QTTabBarClass::ResetBreadcrumbBar() {
    if(m_breadcrumbBar) {
        m_breadcrumbBar->Reset();
        m_breadcrumbBar.reset();
    }
}

bool QTTabBarClass::HandleBreadcrumbClick(PCIDLIST_ABSOLUTE pidl, UINT modifiers, bool middle) {
    if(!m_tabHost) {
        return false;
    }
    std::wstring path = PathFromPidl(pidl);
    if(path.empty()) {
        return false;
    }

    qttabbar::MouseChord chord = middle ? qttabbar::MouseChord::Middle : qttabbar::MouseChord::Left;
    if((modifiers & BreadcrumbBarHelper::kModifierShift) != 0) {
        chord |= qttabbar::MouseChord::Shift;
    }
    if((modifiers & BreadcrumbBarHelper::kModifierCtrl) != 0) {
        chord |= qttabbar::MouseChord::Ctrl;
    }
    if((modifiers & BreadcrumbBarHelper::kModifierAlt) != 0) {
        chord |= qttabbar::MouseChord::Alt;
    }

    auto action = m_tabHost->ResolveFolderLinkAction(chord);
    if(!action) {
        return false;
    }

    return m_tabHost->HandleFolderLinkAction(*action, path);
}

bool QTTabBarClass::ShouldHaveBreak() const {
    CRegKey key;
    DWORD value = 1;
    if(key.Open(HKEY_CURRENT_USER, L"Software\\QTTabBar\\", KEY_READ) == ERROR_SUCCESS) {
        key.QueryDWORDValue(L"BreakTabBar", value);
    }
    return value != 0;
}

int QTTabBarClass::ActiveRebarCount() const {
    if(m_hwndRebar == nullptr) {
        return 0;
    }
    return static_cast<int>(::SendMessageW(m_hwndRebar, RB_GETBANDCOUNT, 0, 0));
}

REBARBANDINFOW QTTabBarClass::GetRebarBand(int index, UINT mask) const {
    REBARBANDINFOW info = {};
    if(m_hwndRebar != nullptr) {
        info.cbSize = sizeof(info);
        info.fMask = mask;
        ::SendMessageW(m_hwndRebar, RB_GETBANDINFO, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(&info));
    }
    return info;
}

bool QTTabBarClass::BandHasBreak() const {
    int count = ActiveRebarCount();
    for(int i = 0; i < count; ++i) {
        REBARBANDINFOW info = GetRebarBand(i, kRebarMaskStyle | RBBIM_STYLE);
        if(info.hwndChild == m_hWnd) {
            return (info.fStyle & RBBS_BREAK) != 0;
        }
    }
    return true;
}

LRESULT QTTabBarClass::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;

    if(!m_tabHost) {
        m_tabHost = std::make_unique<TabBarHost>(*this);
    }
    if(m_tabHost) {
        RECT rc = {0, 0, m_minSize.cx, m_minSize.cy};
        HWND hwndHost = m_tabHost->Create(m_hWnd, rc, L"",
                                          WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
        if(hwndHost == nullptr) {
            return -1;
        }
        m_tabHost->Initialize();
        if(m_spExplorer) {
            m_tabHost->SetExplorer(m_spExplorer);
        }
    }
    InitializeBreadcrumbBar();
    InitializeTimers();
    return 0;
}

LRESULT QTTabBarClass::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    DestroyTimers();
    ResetBreadcrumbBar();
    if(m_tabHost) {
        m_tabHost->OnParentDestroyed();
        if(m_tabHost->IsWindow()) {
            m_tabHost->DestroyWindow();
        }
        m_tabHost.reset();
    }
    return 0;
}

LRESULT QTTabBarClass::OnTimer(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    if(wParam == ID_TIMER_SELECTTAB) {
        EnsureRebarSubclass();
    } else if(wParam == ID_TIMER_CONTEXTMENU) {
        StartDeferredRebarReset();
    }
    return 0;
}

LRESULT QTTabBarClass::OnContextMenu(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    bHandled = TRUE;
    if(!m_tabHost) {
        return 0;
    }

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

    m_tabHost->ShowContextMenu(pt);
    return 0;
}

LRESULT QTTabBarClass::OnCommand(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    if(m_tabHost) {
        m_tabHost->ExecuteCommand(LOWORD(wParam));
    }
    return 0;
}

LRESULT QTTabBarClass::OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    if(m_tabHost && m_tabHost->IsWindow()) {
        ::SetFocus(m_tabHost->m_hWnd);
    } else {
        NotifyFocusChange(TRUE);
    }
    return 0;
}

LRESULT QTTabBarClass::OnKillFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    NotifyFocusChange(FALSE);
    return 0;
}

LRESULT QTTabBarClass::OnUnsetRebarMonitor(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    if(m_rebarSubclass) {
        m_rebarSubclass->SetMonitorSetInfo(false);
    }
    return 0;
}

LRESULT QTTabBarClass::OnCaptureNewWindow(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    auto* request = reinterpret_cast<qttabbar::hooks::CaptureNewWindowRequest*>(wParam);
    if(request == nullptr || request->path == nullptr) {
        return FALSE;
    }
    bool handled = false;
    if(m_tabHost) {
        handled = m_tabHost->OpenCapturedWindow(std::wstring(request->path));
    }
    request->handled = handled ? TRUE : FALSE;
    return handled ? TRUE : FALSE;
}

LRESULT QTTabBarClass::OnTraySelection(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    int index = static_cast<int>(wParam);
    if(index >= 0) {
        ActivateTabByIndex(static_cast<std::size_t>(index));
    }
    return 0;
}

IFACEMETHODIMP QTTabBarClass::GetWindow(HWND* phwnd) {
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

IFACEMETHODIMP QTTabBarClass::ContextSensitiveHelp(BOOL /*fEnterMode*/) {
    return E_NOTIMPL;
}

IFACEMETHODIMP QTTabBarClass::ShowDW(BOOL fShow) {
    HRESULT hr = EnsureWindow();
    if(FAILED(hr)) {
        return hr;
    }
    UpdateVisibility(fShow);
    if(!fShow) {
        PersistBreakPreference();
        if(m_tabHost) {
            m_tabHost->SaveSessionState();
        }
    }
    return S_OK;
}

IFACEMETHODIMP QTTabBarClass::CloseDW(DWORD /*dwReserved*/) {
    m_closed = true;
    ShowDW(FALSE);
    DestroyTimers();
    ReleaseRebarSubclass();
    if(m_tabHost) {
        m_tabHost->OnParentDestroyed();
        if(m_tabHost->IsWindow()) {
            m_tabHost->DestroyWindow();
        }
        m_tabHost.reset();
    }
    if(m_hWnd != nullptr && ::IsWindow(m_hWnd)) {
        ::DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }
    if(m_spExplorer) {
        m_spExplorer.Release();
    }
    if(m_spInputObjectSite) {
        m_spInputObjectSite.Release();
    }
    if(m_spServiceProvider) {
        m_spServiceProvider.Release();
    }
    m_spSite.Release();
    m_hwndRebar = nullptr;
    return S_OK;
}

IFACEMETHODIMP QTTabBarClass::ResizeBorderDW(LPCRECT /*prcBorder*/, IUnknown* /*punkToolbarSite*/, BOOL /*fReserved*/) {
    return E_NOTIMPL;
}

IFACEMETHODIMP QTTabBarClass::GetBandInfo(DWORD dwBandID, DWORD dwViewMode, DESKBANDINFO* pdbi) {
    if(pdbi == nullptr) {
        return E_POINTER;
    }

    m_bandId = dwBandID;
    m_vertical = (dwViewMode & DBIF_VIEWMODE_VERTICAL) != 0;

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
        ::StringCchCopyW(pdbi->wszTitle, ARRAYSIZE(pdbi->wszTitle), L"QTTabBar");
    }

    return S_OK;
}

IFACEMETHODIMP QTTabBarClass::UIActivateIO(BOOL fActivate, MSG* /*pMsg*/) {
    if(fActivate) {
        EnsureWindow();
        if(m_tabHost && m_tabHost->IsWindow()) {
            ::SetFocus(m_tabHost->m_hWnd);
        } else if(m_hWnd != nullptr) {
            ::SetFocus(m_hWnd);
        }
    }
    return S_OK;
}

IFACEMETHODIMP QTTabBarClass::HasFocusIO() {
    if(m_hWnd == nullptr) {
        return S_FALSE;
    }
    HWND focus = ::GetFocus();
    if(focus == m_hWnd || ::IsChild(m_hWnd, focus)) {
        return S_OK;
    }
    if(m_tabHost && m_tabHost->IsWindow()) {
        if(focus == m_tabHost->m_hWnd || ::IsChild(m_tabHost->m_hWnd, focus)) {
            return S_OK;
        }
    }
    return S_FALSE;
}

IFACEMETHODIMP QTTabBarClass::TranslateAcceleratorIO(MSG* pMsg) {
    if(pMsg == nullptr) {
        return E_POINTER;
    }
    if(m_tabHost && m_tabHost->HandleAccelerator(pMsg)) {
        return S_OK;
    }
    if(pMsg->message == WM_KEYDOWN && (pMsg->wParam == VK_TAB || pMsg->wParam == VK_F6)) {
        if(m_tabHost && m_tabHost->IsWindow()) {
            ::SetFocus(m_tabHost->m_hWnd);
            return S_OK;
        }
        if(m_hWnd != nullptr) {
            ::SetFocus(m_hWnd);
            return S_OK;
        }
    }
    return S_FALSE;
}

void QTTabBarClass::HandleButtonCommand(UINT commandId) {
    if(!m_tabHost) {
        return;
    }

    switch(commandId) {
    case ID_BUTTONBAR_NAVIGATION_BACK:
        ExecuteBindAction(BindAction::GoBack);
        break;
    case ID_BUTTONBAR_NAVIGATION_FORWARD:
        ExecuteBindAction(BindAction::GoForward);
        break;
    case ID_BUTTONBAR_NEW_WINDOW:
        ExecuteBindAction(BindAction::NewWindow);
        break;
    case ID_BUTTONBAR_CLONE_TAB:
        ExecuteBindAction(BindAction::CloneCurrent);
        break;
    case ID_BUTTONBAR_CLOSE_TAB:
        ExecuteBindAction(BindAction::CloseCurrent);
        break;
    case ID_BUTTONBAR_CLOSE_OTHERS:
        ExecuteBindAction(BindAction::CloseAllButCurrent);
        break;
    case ID_BUTTONBAR_CLOSE_LEFT:
        ExecuteBindAction(BindAction::CloseLeft);
        break;
    case ID_BUTTONBAR_CLOSE_RIGHT:
        ExecuteBindAction(BindAction::CloseRight);
        break;
    case ID_BUTTONBAR_CLOSE_WINDOW:
        ExecuteBindAction(BindAction::CloseWindow);
        break;
    case ID_BUTTONBAR_GO_UP:
        ExecuteBindAction(BindAction::UpOneLevel);
        break;
    case ID_BUTTONBAR_REFRESH:
        ExecuteBindAction(BindAction::Refresh);
        break;
    case ID_BUTTONBAR_OPTIONS:
        ExecuteBindAction(BindAction::ShowOptions);
        break;
    case ID_BUTTONBAR_LOCK_TAB:
    case ID_BUTTONBAR_TOPMOST:
    case ID_BUTTONBAR_WINDOW_OPACITY:
    case ID_BUTTONBAR_FILTER_BAR:
        ATLTRACE(L"QTTabBarClass::HandleButtonCommand command %u not yet implemented\n", commandId);
        break;
    default:
        ATLTRACE(L"QTTabBarClass::HandleButtonCommand unknown command %u\n", commandId);
        break;
    }
}

std::vector<std::wstring> QTTabBarClass::GetOpenTabs() const {
    if(!m_tabHost) {
        return {};
    }
    return m_tabHost->GetOpenTabs();
}

std::wstring QTTabBarClass::GetCurrentPath() const {
    if(!m_tabHost) {
        return {};
    }
    return m_tabHost->GetCurrentPath();
}

bool QTTabBarClass::ExecuteBindAction(qttabbar::BindAction action, bool isRepeat,
                                      std::optional<std::size_t> tabIndex) {
    if(!m_tabHost) {
        return false;
    }
    return m_tabHost->ExecuteBindAction(action, isRepeat, tabIndex);
}

std::vector<std::wstring> QTTabBarClass::GetClosedTabHistory() const {
    if(!m_tabHost) {
        return {};
    }
    return m_tabHost->GetClosedTabHistory();
}

void QTTabBarClass::ActivateTabByIndex(std::size_t index) {
    if(m_tabHost) {
        m_tabHost->ActivateTabByIndex(index);
    }
}

void QTTabBarClass::RestoreClosedTabByIndex(std::size_t index) {
    if(m_tabHost) {
        m_tabHost->RestoreClosedTabByIndex(index);
    }
}

void QTTabBarClass::OpenGroupByIndex(std::size_t index) {
    if(m_tabHost) {
        m_tabHost->OpenGroupByIndex(index);
    }
}

IFACEMETHODIMP QTTabBarClass::SetSite(IUnknown* pUnkSite) {
    if(pUnkSite == nullptr) {
        InstanceManagerNative::Instance().UnregisterTabBar(this);
        qttabbar::hooks::HookManagerNative::Instance().OnTabBarSiteCleared(this);
        m_spInputObjectSite.Release();
        m_spServiceProvider.Release();
        m_spExplorer.Release();
        m_spSite.Release();
        m_hwndRebar = nullptr;
        PersistBreakPreference();
        if(m_tabHost) {
            m_tabHost->SaveSessionState();
            m_tabHost->ClearExplorer();
        }
        ReleaseRebarSubclass();
        ResetBreadcrumbBar();
        m_explorerHwnd = nullptr;
        return S_OK;
    }

    InstanceManagerNative::Instance().UnregisterTabBar(this);
    ResetBreadcrumbBar();
    m_spSite = pUnkSite;
    m_spInputObjectSite.Release();
    m_spServiceProvider.Release();
    m_spExplorer.Release();

    pUnkSite->QueryInterface(IID_PPV_ARGS(&m_spInputObjectSite));
    pUnkSite->QueryInterface(IID_PPV_ARGS(&m_spServiceProvider));

    if(m_spServiceProvider) {
        m_spServiceProvider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&m_spExplorer));
    }

    if(m_tabHost) {
        m_tabHost->SetExplorer(m_spExplorer);
    }

    m_explorerHwnd = nullptr;
    if(m_spExplorer) {
        SHANDLE_PTR handle = 0;
        if(SUCCEEDED(m_spExplorer->get_HWND(&handle))) {
            m_explorerHwnd = reinterpret_cast<HWND>(handle);
            InstanceManagerNative::Instance().RegisterTabBar(m_explorerHwnd, this);
            InitializeBreadcrumbBar();
        }
    }

    qttabbar::hooks::HookManagerNative::Instance().OnTabBarSiteAssigned(this);

    CComPtr<IOleWindow> spOleWindow;
    if(SUCCEEDED(pUnkSite->QueryInterface(IID_PPV_ARGS(&spOleWindow)))) {
        spOleWindow->GetWindow(&m_hwndRebar);
    }

    EnsureRebarSubclass();
    return S_OK;
}

IFACEMETHODIMP QTTabBarClass::GetSite(REFIID riid, void** ppvSite) {
    if(ppvSite == nullptr) {
        return E_POINTER;
    }
    if(!m_spSite) {
        *ppvSite = nullptr;
        return E_FAIL;
    }
    return m_spSite->QueryInterface(riid, ppvSite);
}

IFACEMETHODIMP QTTabBarClass::GetClassID(CLSID* pClassID) {
    if(pClassID == nullptr) {
        return E_POINTER;
    }
    *pClassID = CLSID_QTTabBarClass;
    return S_OK;
}

IFACEMETHODIMP QTTabBarClass::IsDirty() {
    return S_FALSE;
}

IFACEMETHODIMP QTTabBarClass::Load(IStream* /*pStm*/) {
    return S_OK;
}

IFACEMETHODIMP QTTabBarClass::Save(IStream* /*pStm*/, BOOL /*fClearDirty*/) {
    return S_OK;
}

IFACEMETHODIMP QTTabBarClass::GetSizeMax(ULARGE_INTEGER* pcbSize) {
    if(pcbSize == nullptr) {
        return E_POINTER;
    }
    pcbSize->QuadPart = 0;
    return E_NOTIMPL;
}

HWND QTTabBarClass::GetHostWindow() const noexcept {
    return m_hWnd;
}

HWND QTTabBarClass::GetHostRebarWindow() const noexcept {
    return m_hwndRebar;
}

void QTTabBarClass::NotifyTabHostFocusChange(BOOL hasFocus) {
    NotifyFocusChange(hasFocus);
}
