#include "pch.h"
#include "QTSecondViewBar.h"

#include <ShlObj.h>
#include <Shlwapi.h>
#include <strsafe.h>

#include <algorithm>
#include <cmath>

#pragma comment(lib, "Shlwapi.lib")

namespace {
constexpr int kDefaultTabHeight = 28;
constexpr int kSplitterHeight = 6;
constexpr float kDefaultPaneRatio = 0.55f;

RECT MakeRect(LONG left, LONG top, LONG right, LONG bottom) {
    RECT rc{};
    rc.left = left;
    rc.top = top;
    rc.right = right;
    rc.bottom = bottom;
    return rc;
}

} // namespace

ExplorerBrowserHost::ExplorerBrowserHost(QTSecondViewBar& owner) noexcept
    : m_owner(owner) {
}

ExplorerBrowserHost::~ExplorerBrowserHost() {
    if(m_spBrowser) {
        m_spBrowser->Destroy();
        m_spBrowser.Release();
    }
}

LRESULT ExplorerBrowserHost::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;

    RECT rc{};
    GetClientRect(&rc);

    FOLDERSETTINGS settings{};
    settings.ViewMode = FVM_DETAILS;
    settings.fFlags = FWF_NOWEBVIEW | FWF_NOCLIENTEDGE;

    HRESULT hr = m_spBrowser.CoCreateInstance(CLSID_ExplorerBrowser, nullptr, CLSCTX_INPROC_SERVER);
    if(FAILED(hr) || !m_spBrowser) {
        return -1;
    }

    hr = m_spBrowser->Initialize(m_hWnd, &rc, &settings);
    if(FAILED(hr)) {
        m_spBrowser.Release();
        return -1;
    }

    m_spBrowser->SetOptions(EBO_SHOWFRAMES | EBO_NAVIGATEONCE);
    NavigateToDefaultFolder();
    return 0;
}

LRESULT ExplorerBrowserHost::OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
    bHandled = TRUE;
    if(!m_spBrowser) {
        return 0;
    }
    RECT rc{};
    GetClientRect(&rc);
    m_spBrowser->SetRect(nullptr, rc);
    return 0;
}

LRESULT ExplorerBrowserHost::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    if(m_spBrowser) {
        m_spBrowser->Destroy();
        m_spBrowser.Release();
    }
    return 0;
}

void ExplorerBrowserHost::NavigateToDefaultFolder() {
    if(!m_spBrowser) {
        return;
    }
    PIDLIST_ABSOLUTE pidl{};
    if(SUCCEEDED(SHGetKnownFolderIDList(FOLDERID_Desktop, 0, nullptr, &pidl)) && pidl != nullptr) {
        m_spBrowser->BrowseToIDList(pidl, SBSP_ABSOLUTE);
        CoTaskMemFree(pidl);
    }
}

SplitterBar::SplitterBar(QTSecondViewBar& owner) noexcept
    : m_owner(owner)
    , m_dragging(false) {
}

LRESULT SplitterBar::OnLButtonDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
    bHandled = TRUE;
    SetCapture();
    m_dragging = true;
    POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    ClientToScreen(&pt);
    m_owner.BeginDragResize();
    UpdatePositionFromPoint(pt);
    return 0;
}

LRESULT SplitterBar::OnLButtonUp(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
    bHandled = TRUE;
    if(m_dragging) {
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ClientToScreen(&pt);
        UpdatePositionFromPoint(pt);
    }
    m_dragging = false;
    ReleaseCapture();
    m_owner.EndDragResize();
    return 0;
}

LRESULT SplitterBar::OnMouseMove(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    bHandled = TRUE;
    if(!(wParam & MK_LBUTTON) || !m_dragging) {
        return 0;
    }
    POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    ClientToScreen(&pt);
    UpdatePositionFromPoint(pt);
    return 0;
}

LRESULT SplitterBar::OnSetCursor(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    ::SetCursor(::LoadCursorW(nullptr, IDC_SIZENS));
    return TRUE;
}

LRESULT SplitterBar::OnCaptureChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    if(m_dragging) {
        m_dragging = false;
        m_owner.EndDragResize();
    }
    return 0;
}

LRESULT SplitterBar::OnDefault(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = FALSE;
    return 0;
}

void SplitterBar::UpdatePositionFromPoint(POINT pt) {
    m_owner.UpdateSplitterFromPoint(pt);
}

QTSecondViewBar::QTSecondViewBar() noexcept
    : m_hwndRebar(nullptr)
    , m_bandId(0)
    , m_visible(false)
    , m_closed(false)
    , m_userResized(false)
    , m_paneRatio(kDefaultPaneRatio)
    , m_minSize{200, 150}
    , m_maxSize{-1, -1} {
}

QTSecondViewBar::~QTSecondViewBar() {
}

HRESULT QTSecondViewBar::FinalConstruct() {
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES | ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES;
    ::InitCommonControlsEx(&icc);
    LoadPreferences();
    return S_OK;
}

void QTSecondViewBar::FinalRelease() {
    SavePreferences();
    DestroyChildren();
    if(m_hWnd != nullptr && ::IsWindow(m_hWnd)) {
        ::DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }
    m_spExplorer.Release();
    m_spServiceProvider.Release();
    m_spInputObjectSite.Release();
    m_spSite.Release();
}

HRESULT QTSecondViewBar::EnsureWindow() {
    if(m_hWnd != nullptr) {
        return S_OK;
    }
    if(m_hwndRebar == nullptr) {
        return E_UNEXPECTED;
    }
    RECT rc = MakeRect(0, 0, m_minSize.cx, m_minSize.cy);
    HWND hwnd = Create(m_hwndRebar, rc, L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
    if(hwnd == nullptr) {
        return HRESULT_FROM_WIN32(::GetLastError());
    }
    return S_OK;
}

LRESULT QTSecondViewBar::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    EnsureTabHost();
    EnsureBrowserHost();
    UpdateLayout();
    return 0;
}

LRESULT QTSecondViewBar::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    SavePreferences();
    DestroyChildren();
    return 0;
}

LRESULT QTSecondViewBar::OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    UpdateLayout();
    return 0;
}

LRESULT QTSecondViewBar::OnContextMenu(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
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

LRESULT QTSecondViewBar::OnCommand(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    if(m_tabHost) {
        m_tabHost->ExecuteCommand(LOWORD(wParam));
    }
    return 0;
}

LRESULT QTSecondViewBar::OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    if(m_tabHost && m_tabHost->IsWindow()) {
        ::SetFocus(m_tabHost->m_hWnd);
    }
    return 0;
}

LRESULT QTSecondViewBar::OnKillFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    return 0;
}

void QTSecondViewBar::EnsureTabHost() {
    if(m_hWnd == nullptr) {
        return;
    }
    if(!m_tabHost) {
        m_tabHost = std::make_unique<TabBarHost>(*this);
    }
    if(m_tabHost && !m_tabHost->IsWindow()) {
        RECT rc = MakeRect(0, 0, m_minSize.cx, kDefaultTabHeight);
        HWND hwnd = m_tabHost->Create(m_hWnd, rc, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
        if(hwnd == nullptr) {
            return;
        }
        m_tabHost->Initialize();
        if(m_spExplorer) {
            m_tabHost->SetExplorer(m_spExplorer);
        }
    }
}

void QTSecondViewBar::EnsureBrowserHost() {
    if(m_hWnd == nullptr) {
        return;
    }
    if(!m_browserHost) {
        m_browserHost = std::make_unique<ExplorerBrowserHost>(*this);
    }
    if(m_browserHost && !m_browserHost->IsWindow()) {
        RECT rc = MakeRect(0, kDefaultTabHeight + kSplitterHeight, m_minSize.cx, m_minSize.cy);
        m_browserHost->Create(m_hWnd, rc, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
    }
    if(!m_splitter) {
        m_splitter = std::make_unique<SplitterBar>(*this);
    }
    if(m_splitter && !m_splitter->IsWindow()) {
        RECT rc = MakeRect(0, kDefaultTabHeight, m_minSize.cx, kDefaultTabHeight + kSplitterHeight);
        m_splitter->Create(m_hWnd, rc, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
    }
}

void QTSecondViewBar::DestroyChildren() {
    if(m_splitter) {
        if(m_splitter->IsWindow()) {
            m_splitter->DestroyWindow();
        }
        m_splitter.reset();
    }
    if(m_browserHost) {
        if(m_browserHost->IsWindow()) {
            m_browserHost->DestroyWindow();
        }
        m_browserHost.reset();
    }
    if(m_tabHost) {
        m_tabHost->SaveSessionState();
        m_tabHost->ClearExplorer();
        m_tabHost->OnParentDestroyed();
        if(m_tabHost->IsWindow()) {
            m_tabHost->DestroyWindow();
        }
        m_tabHost.reset();
    }
}

void QTSecondViewBar::UpdateLayout() {
    if(m_hWnd == nullptr) {
        return;
    }
    RECT rc{};
    ::GetClientRect(m_hWnd, &rc);
    LONG width = rc.right - rc.left;
    LONG height = rc.bottom - rc.top;
    if(height <= 0) {
        return;
    }

    LONG tabHeight = kDefaultTabHeight;
    LONG splitterTop = tabHeight;
    LONG splitterBottom = splitterTop + kSplitterHeight;
    LONG browserTop = splitterBottom;
    LONG available = height - tabHeight;
    if(available > kSplitterHeight) {
        LONG minBound = tabHeight + 20;
        LONG maxBound = height - 20;
        if(maxBound < minBound) {
            maxBound = minBound;
        }
        LONG adjusted = static_cast<LONG>(std::round(static_cast<float>(height) * m_paneRatio));
        adjusted = std::clamp<LONG>(adjusted, minBound, maxBound);
        splitterTop = adjusted - (kSplitterHeight / 2);
        if(splitterTop < tabHeight) {
            splitterTop = tabHeight;
        }
        splitterBottom = splitterTop + kSplitterHeight;
        if(splitterBottom > height) {
            splitterBottom = height;
        }
        browserTop = splitterBottom;
    }

    if(m_tabHost && m_tabHost->IsWindow()) {
        ::SetWindowPos(m_tabHost->m_hWnd, nullptr, 0, 0, width, tabHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if(m_splitter && m_splitter->IsWindow()) {
        ::SetWindowPos(m_splitter->m_hWnd, nullptr, 0, splitterTop, width, kSplitterHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if(m_browserHost && m_browserHost->IsWindow()) {
        LONG browserHeightActual = height - splitterBottom;
        if(browserHeightActual < 0) {
            browserHeightActual = 0;
        }
        ::SetWindowPos(m_browserHost->m_hWnd, nullptr, 0, splitterBottom, width, browserHeightActual,
                       SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void QTSecondViewBar::LoadPreferences() {
    CRegKey key;
    if(key.Open(HKEY_CURRENT_USER, kRegistryRoot, KEY_READ) == ERROR_SUCCESS) {
        DWORD ratio = 0;
        if(key.QueryDWORDValue(kPaneRatioValue, ratio) == ERROR_SUCCESS) {
            m_paneRatio = static_cast<float>(ratio) / 100.0f;
            m_paneRatio = std::clamp(m_paneRatio, 0.2f, 0.8f);
        }
        DWORD resized = 0;
        if(key.QueryDWORDValue(kUserResizedValue, resized) == ERROR_SUCCESS) {
            m_userResized = resized != 0;
        }
    }
}

void QTSecondViewBar::SavePreferences() const {
    CRegKey key;
    if(key.Create(HKEY_CURRENT_USER, kRegistryRoot) != ERROR_SUCCESS) {
        return;
    }
    DWORD ratio = static_cast<DWORD>(std::round(m_paneRatio * 100.0f));
    key.SetDWORDValue(kPaneRatioValue, ratio);
    key.SetDWORDValue(kUserResizedValue, m_userResized ? 1u : 0u);
}

void QTSecondViewBar::SetExplorer(IWebBrowser2* explorer) {
    if(m_tabHost) {
        m_tabHost->SetExplorer(explorer);
    }
}

void QTSecondViewBar::NotifyFocusChange(BOOL hasFocus) {
    if(m_spInputObjectSite) {
        m_spInputObjectSite->OnFocusChangeIS(static_cast<IDeskBand*>(this), hasFocus);
    }
}

void QTSecondViewBar::BeginDragResize() {
    m_userResized = true;
}

void QTSecondViewBar::EndDragResize() {
    SavePreferences();
}

void QTSecondViewBar::UpdateSplitterFromPoint(POINT pt) {
    if(m_hWnd == nullptr) {
        return;
    }
    RECT rc{};
    ::GetClientRect(m_hWnd, &rc);
    ::ScreenToClient(m_hWnd, &pt);
    LONG height = rc.bottom - rc.top;
    if(height <= 0) {
        return;
    }
    m_paneRatio = static_cast<float>(pt.y) / static_cast<float>(height);
    m_paneRatio = std::clamp(m_paneRatio, 0.2f, 0.8f);
    UpdateLayout();
}

IFACEMETHODIMP QTSecondViewBar::GetWindow(HWND* phwnd) {
    if(phwnd == nullptr) {
        return E_POINTER;
    }
    *phwnd = m_hWnd;
    return m_hWnd != nullptr ? S_OK : E_FAIL;
}

IFACEMETHODIMP QTSecondViewBar::ContextSensitiveHelp(BOOL /*fEnterMode*/) {
    return E_NOTIMPL;
}

IFACEMETHODIMP QTSecondViewBar::ShowDW(BOOL fShow) {
    m_visible = fShow != FALSE;
    if(fShow) {
        EnsureWindow();
        EnsureTabHost();
        EnsureBrowserHost();
        if(m_hWnd != nullptr) {
            ::ShowWindow(m_hWnd, SW_SHOW);
            UpdateLayout();
        }
    } else if(m_hWnd != nullptr) {
        ::ShowWindow(m_hWnd, SW_HIDE);
    }
    if(m_tabHost) {
        m_tabHost->OnBandVisibilityChanged(fShow != FALSE);
    }
    return S_OK;
}

IFACEMETHODIMP QTSecondViewBar::CloseDW(DWORD /*dwReserved*/) {
    m_closed = true;
    SavePreferences();
    DestroyChildren();
    return S_OK;
}

IFACEMETHODIMP QTSecondViewBar::ResizeBorderDW(LPCRECT /*prcBorder*/, IUnknown* /*punkToolbarSite*/, BOOL /*fReserved*/) {
    return S_OK;
}

IFACEMETHODIMP QTSecondViewBar::GetBandInfo(DWORD dwBandID, DWORD /*dwViewMode*/, DESKBANDINFO* pdbi) {
    if(pdbi == nullptr) {
        return E_POINTER;
    }
    m_bandId = dwBandID;
    if((pdbi->dwMask & DBIM_MINSIZE) != 0) {
        pdbi->ptMinSize = POINTL{m_minSize.cx, m_minSize.cy};
    }
    if((pdbi->dwMask & DBIM_MAXSIZE) != 0) {
        pdbi->ptMaxSize = POINTL{m_maxSize.cx, m_maxSize.cy};
    }
    if((pdbi->dwMask & DBIM_ACTUAL) != 0) {
        pdbi->ptActual = POINTL{m_minSize.cx, m_minSize.cy};
    }
    if((pdbi->dwMask & DBIM_MODEFLAGS) != 0) {
        pdbi->dwModeFlags = DBIMF_VARIABLEHEIGHT | DBIMF_USECHEVRON;
    }
    if((pdbi->dwMask & DBIM_TITLE) != 0) {
        ::StringCchCopyW(pdbi->wszTitle, ARRAYSIZE(pdbi->wszTitle), L"QT Second View");
    }
    return S_OK;
}

IFACEMETHODIMP QTSecondViewBar::UIActivateIO(BOOL fActivate, MSG* pMsg) {
    if(fActivate && m_tabHost && m_tabHost->IsWindow()) {
        ::SetFocus(m_tabHost->m_hWnd);
    }
    if(pMsg != nullptr) {
        TranslateAcceleratorIO(pMsg);
    }
    return S_OK;
}

IFACEMETHODIMP QTSecondViewBar::HasFocusIO() {
    if(!m_tabHost || !m_tabHost->IsWindow()) {
        return S_FALSE;
    }
    HWND focus = ::GetFocus();
    return (focus == m_tabHost->m_hWnd || ::IsChild(m_tabHost->m_hWnd, focus)) ? S_OK : S_FALSE;
}

IFACEMETHODIMP QTSecondViewBar::TranslateAcceleratorIO(MSG* pMsg) {
    if(pMsg == nullptr) {
        return E_POINTER;
    }
    if(m_tabHost && m_tabHost->HandleAccelerator(pMsg)) {
        return S_OK;
    }
    return S_FALSE;
}

IFACEMETHODIMP QTSecondViewBar::SetSite(IUnknown* pUnkSite) {
    if(pUnkSite == nullptr) {
        DestroyChildren();
        m_spExplorer.Release();
        m_spServiceProvider.Release();
        m_spInputObjectSite.Release();
        m_spSite.Release();
        m_hwndRebar = nullptr;
        return S_OK;
    }

    m_spExplorer.Release();
    m_spServiceProvider.Release();
    m_spInputObjectSite.Release();
    m_spSite.Release();
    m_hwndRebar = nullptr;

    m_spSite = pUnkSite;

    CComPtr<IDockingWindowSite> spDockingSite;
    if(SUCCEEDED(pUnkSite->QueryInterface(IID_PPV_ARGS(&spDockingSite))) && spDockingSite) {
        spDockingSite->GetWindow(&m_hwndRebar);
    }

    pUnkSite->QueryInterface(IID_PPV_ARGS(&m_spInputObjectSite));
    pUnkSite->QueryInterface(IID_PPV_ARGS(&m_spServiceProvider));

    if(m_spServiceProvider) {
        m_spServiceProvider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&m_spExplorer));
    }

    if(!m_spExplorer) {
        pUnkSite->QueryInterface(IID_PPV_ARGS(&m_spExplorer));
    }

    EnsureWindow();
    EnsureTabHost();
    EnsureBrowserHost();
    UpdateLayout();
    SetExplorer(m_spExplorer);

    return S_OK;
}

IFACEMETHODIMP QTSecondViewBar::GetSite(REFIID riid, void** ppvSite) {
    if(ppvSite == nullptr) {
        return E_POINTER;
    }
    if(!m_spSite) {
        *ppvSite = nullptr;
        return E_FAIL;
    }
    return m_spSite->QueryInterface(riid, ppvSite);
}

IFACEMETHODIMP QTSecondViewBar::GetClassID(CLSID* pClassID) {
    if(pClassID == nullptr) {
        return E_POINTER;
    }
    *pClassID = CLSID_QTSecondViewBar;
    return S_OK;
}

IFACEMETHODIMP QTSecondViewBar::IsDirty() {
    return S_FALSE;
}

IFACEMETHODIMP QTSecondViewBar::Load(IStream* /*pStm*/) {
    return S_OK;
}

IFACEMETHODIMP QTSecondViewBar::Save(IStream* /*pStm*/, BOOL /*fClearDirty*/) {
    SavePreferences();
    return S_OK;
}

IFACEMETHODIMP QTSecondViewBar::GetSizeMax(ULARGE_INTEGER* pcbSize) {
    if(pcbSize == nullptr) {
        return E_POINTER;
    }
    pcbSize->QuadPart = 0;
    return E_NOTIMPL;
}

HWND QTSecondViewBar::GetHostWindow() const noexcept {
    return m_hWnd;
}

HWND QTSecondViewBar::GetHostRebarWindow() const noexcept {
    return m_hwndRebar;
}

void QTSecondViewBar::NotifyTabHostFocusChange(BOOL hasFocus) {
    NotifyFocusChange(hasFocus);
}
