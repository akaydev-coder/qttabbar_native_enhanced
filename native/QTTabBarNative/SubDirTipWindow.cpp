#include "pch.h"
#include "SubDirTipWindow.h"

#include <CommCtrl.h>
#include <Shlobj.h>
#include <Shlwapi.h>
#include <shellapi.h>

#include <algorithm>
#include <filesystem>

#include "TabBarHost.h"
#include "ThumbnailTooltipWindow.h"

#pragma comment(lib, "Shlwapi.lib")

namespace {
constexpr int kListViewPadding = 12;
constexpr int kDefaultWidth = 340;
constexpr int kDefaultHeight = 260;
constexpr UINT kListViewStyles = LVS_REPORT | LVS_SHOWSELALWAYS | LVS_AUTOARRANGE;
constexpr DWORD kListViewExStyles = LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_LABELTIP;
constexpr UINT kContextOpen = 1;
constexpr UINT kContextOpenNewTab = 2;
constexpr UINT kContextOpenNewWindow = 3;

std::wstring FormatTimestamp(const FILETIME& ft) {
    if(ft.dwLowDateTime == 0 && ft.dwHighDateTime == 0) {
        return {};
    }
    SYSTEMTIME st{};
    if(!::FileTimeToSystemTime(&ft, &st)) {
        return {};
    }
    wchar_t buffer[64] = {};
    if(::GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, buffer, std::size(buffer))) {
        std::wstring formatted(buffer);
        formatted.push_back(L' ');
        if(::GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, nullptr, buffer, std::size(buffer))) {
            formatted.append(buffer);
        }
        return formatted;
    }
    return {};
}

int GetSmallIconIndex(const std::wstring& path, bool isDirectory) {
    SHFILEINFOW sfi{};
    UINT flags = SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES;
    DWORD attributes = isDirectory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    if(::SHGetFileInfoW(path.c_str(), attributes, &sfi, sizeof(sfi), flags)) {
        return static_cast<int>(sfi.iIcon);
    }
    return -1;
}

class DropSource final : public IDropSource {
public:
    DropSource() : m_refs(1) {}
    IFACEMETHODIMP QueryInterface(REFIID riid, void** object) override {
        if(!object) {
            return E_POINTER;
        }
        if(riid == IID_IUnknown || riid == IID_IDropSource) {
            *object = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++m_refs; }
    IFACEMETHODIMP_(ULONG) Release() override {
        ULONG refs = --m_refs;
        if(refs == 0) {
            delete this;
        }
        return refs;
    }
    IFACEMETHODIMP QueryContinueDrag(BOOL escapePressed, DWORD keyState) override {
        if(escapePressed) {
            return DRAGDROP_S_CANCEL;
        }
        if((keyState & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) == 0) {
            return DRAGDROP_S_DROP;
        }
        return S_OK;
    }
    IFACEMETHODIMP GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }

private:
    ULONG m_refs;
};

std::wstring ToLowerCopy(const std::wstring& value) {
    std::wstring result = value;
    std::transform(result.begin(), result.end(), result.begin(), [](wchar_t ch) { return static_cast<wchar_t>(::towlower(ch)); });
    return result;
}

bool AllowFile(const std::wstring& extension, const qttabbar::ConfigData& config) {
    if(!config.tips.subDirTipsFiles) {
        return false;
    }
    if(config.tips.textExt.empty() && config.tips.imageExt.empty()) {
        return true;
    }
    std::wstring lowered = ToLowerCopy(extension);
    if(ThumbnailTooltipWindow::IsSupportedTextExtension(lowered, config.tips)) {
        return true;
    }
    if(ThumbnailTooltipWindow::IsSupportedImageExtension(lowered, config.tips)) {
        return true;
    }
    return false;
}

} // namespace

SubDirTipWindow::SubDirTipWindow(TabBarHost& owner) noexcept
    : m_owner(owner) {
    ::SetRectEmpty(&m_anchorRect);
}

SubDirTipWindow::~SubDirTipWindow() {
    HideTip();
}

void SubDirTipWindow::ApplyConfiguration(const qttabbar::ConfigData& config) {
    m_config = config;
    if(m_thumbnailTooltip) {
        m_thumbnailTooltip->ApplyConfiguration(config.tips);
    }
    UpdateColumns();
}

bool SubDirTipWindow::ShowForPath(const std::wstring& path, const POINT& anchor, bool /*byKeyboard*/) {
    if(path.empty()) {
        return false;
    }

    if(!IsWindow()) {
        DWORD style = WS_POPUP | WS_CAPTION | WS_THICKFRAME;
        DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
        if(Create(nullptr, CWindow::rcDefault, L"", style, exStyle) == nullptr) {
            return false;
        }
        EnsureListView();
    }

    ApplyConfiguration(m_config);

    m_currentPath = path;
    PopulateItems(path);
    if(m_items.empty()) {
        HideTip();
        return false;
    }

    UpdateAnchorRect();
    RECT workArea{};
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    int width = kDefaultWidth;
    int height = (std::min)(static_cast<int>((std::max<std::size_t>)(m_items.size(), 1u) * 22 + 60), kDefaultHeight * 2);
    int x = anchor.x;
    int y = anchor.y;
    if(x + width > workArea.right) {
        x = workArea.right - width;
    }
    if(y + height > workArea.bottom) {
        y = anchor.y - height;
    }
    if(x < workArea.left) {
        x = workArea.left;
    }
    if(y < workArea.top) {
        y = workArea.top;
    }

    SetWindowPos(HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW);
    m_showing = true;
    return true;
}

bool SubDirTipWindow::ShowAndExecute(const std::wstring& path, const POINT& anchor, Command command, bool byKeyboard) {
    if(!ShowForPath(path, anchor, byKeyboard)) {
        return false;
    }
    if(m_items.empty()) {
        return false;
    }
    std::vector<int> indices;
    indices.push_back(0);
    ExecuteCommand(indices, command);
    return true;
}

void SubDirTipWindow::HideTip() {
    m_showing = false;
    if(IsWindow()) {
        ShowWindow(SW_HIDE);
    }
    if(m_thumbnailTooltip) {
        m_thumbnailTooltip->HideTooltip();
    }
}

void SubDirTipWindow::ClearThumbnailCache() {
    if(m_thumbnailTooltip) {
        m_thumbnailTooltip->ClearCache();
    }
}

LRESULT SubDirTipWindow::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    EnsureListView();
    return 0;
}

LRESULT SubDirTipWindow::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    if(m_listView.IsWindow()) {
        m_listView.DestroyWindow();
    }
    if(m_thumbnailTooltip) {
        m_thumbnailTooltip->DestroyWindow();
        m_thumbnailTooltip.reset();
    }
    return 0;
}

LRESULT SubDirTipWindow::OnShowWindow(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    if(!wParam && m_thumbnailTooltip) {
        m_thumbnailTooltip->HideTooltip();
    }
    return 0;
}

LRESULT SubDirTipWindow::OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
    bHandled = TRUE;
    if(!m_listView.IsWindow()) {
        return 0;
    }
    int width = LOWORD(lParam) - kListViewPadding * 2;
    int height = HIWORD(lParam) - kListViewPadding * 2;
    if(width < 100) {
        width = 100;
    }
    if(height < 80) {
        height = 80;
    }
    ::MoveWindow(m_listView, kListViewPadding, kListViewPadding, width, height, TRUE);
    return 0;
}

LRESULT SubDirTipWindow::OnKillFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    HideTip();
    return 0;
}

LRESULT SubDirTipWindow::OnNotify(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    bHandled = FALSE;
    if(reinterpret_cast<HWND>(wParam) != m_listView.m_hWnd) {
        return 0;
    }
    NMHDR* header = reinterpret_cast<NMHDR*>(lParam);
    if(header->code == LVN_ITEMACTIVATE) {
        NMLISTVIEW* view = reinterpret_cast<NMLISTVIEW*>(header);
        std::vector<int> indices = {view->iItem};
        ExecuteCommand(indices, Command::Open);
        bHandled = TRUE;
    } else if(header->code == NM_RCLICK) {
        POINT point{};
        ::GetCursorPos(&point);
        ShowContextMenu(point);
        bHandled = TRUE;
    } else if(header->code == LVN_BEGINDRAG) {
        NMLISTVIEW* view = reinterpret_cast<NMLISTVIEW*>(header);
        std::vector<int> indices = {view->iItem};
        BeginDragOperation(indices);
        bHandled = TRUE;
    } else if(header->code == LVN_ITEMCHANGED) {
        NMLISTVIEW* view = reinterpret_cast<NMLISTVIEW*>(header);
        if((view->uChanged & LVIF_STATE) != 0 && (view->uNewState & LVIS_SELECTED) != 0) {
            UpdateThumbnailPreview(view->iItem);
        }
    } else if(header->code == LVN_HOTTRACK) {
        NMLISTVIEW* view = reinterpret_cast<NMLISTVIEW*>(header);
        UpdateThumbnailPreview(view->iItem);
    }
    return 0;
}

LRESULT SubDirTipWindow::OnBeginDrag(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    bHandled = FALSE;
    if(uMsg == WM_LBUTTONDOWN && (wParam & MK_SHIFT) != 0) {
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        LVHITTESTINFO info{};
        info.pt = pt;
        if(::SendMessageW(m_listView, LVM_HITTEST, 0, reinterpret_cast<LPARAM>(&info)) >= 0 && info.iItem >= 0) {
            std::vector<int> indices = {info.iItem};
            BeginDragOperation(indices);
            bHandled = TRUE;
        }
    }
    return 0;
}

void SubDirTipWindow::EnsureListView() {
    if(m_listView.IsWindow()) {
        return;
    }
    RECT rc{};
    ::SetRect(&rc, 0, 0, kDefaultWidth, kDefaultHeight);
    HWND hwnd = ::CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | kListViewStyles,
                                  rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, m_hWnd, nullptr, nullptr, nullptr);
    m_listView.Attach(hwnd);
    ListView_SetExtendedListViewStyle(m_listView, kListViewExStyles);
    UpdateColumns();

    SHFILEINFOW sfi{};
    HIMAGELIST sysImageList = reinterpret_cast<HIMAGELIST>(::SHGetFileInfoW(L"", FILE_ATTRIBUTE_DIRECTORY, &sfi, sizeof(sfi),
                                                                            SHGFI_SYSICONINDEX | SHGFI_SMALLICON));
    if(sysImageList != nullptr) {
        m_imageList = sysImageList;
        ListView_SetImageList(m_listView, m_imageList, LVSIL_SMALL);
    }

    if(!m_thumbnailTooltip) {
        m_thumbnailTooltip = std::make_unique<ThumbnailTooltipWindow>();
        m_thumbnailTooltip->ApplyConfiguration(m_config.tips);
    }
}

void SubDirTipWindow::PopulateItems(const std::wstring& path) {
    if(!m_listView.IsWindow()) {
        return;
    }
    ListView_DeleteAllItems(m_listView);
    m_items.clear();

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::directory_iterator it(path, fs::directory_options::skip_permission_denied, ec);
    if(ec) {
        return;
    }

    for(const auto& entry : it) {
        std::wstring name = entry.path().filename().wstring();
        std::wstring fullPath = entry.path().wstring();
        Item item{};
        item.name = name;
        item.path = fullPath;
        item.isDirectory = entry.is_directory();
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if(::GetFileAttributesExW(fullPath.c_str(), GetFileExInfoStandard, &data)) {
            item.modified = data.ftLastWriteTime;
        }
        if(item.isDirectory || AllowFile(entry.path().extension().wstring(), m_config)) {
            m_items.push_back(std::move(item));
        }
    }

    std::sort(m_items.begin(), m_items.end(), [](const Item& lhs, const Item& rhs) {
        if(lhs.isDirectory != rhs.isDirectory) {
            return lhs.isDirectory && !rhs.isDirectory;
        }
        return _wcsicmp(lhs.name.c_str(), rhs.name.c_str()) < 0;
    });

    for(std::size_t index = 0; index < m_items.size(); ++index) {
        AddItemToList(index, m_items[index]);
    }
}

void SubDirTipWindow::AddItemToList(std::size_t index, const Item& item) {
    LVITEMW lvi{};
    lvi.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
    lvi.iItem = static_cast<int>(index);
    lvi.pszText = const_cast<wchar_t*>(item.name.c_str());
    lvi.lParam = static_cast<LPARAM>(index);
    lvi.iImage = GetSmallIconIndex(item.path, item.isDirectory);
    int inserted = ListView_InsertItem(m_listView.m_hWnd, &lvi);
    if(inserted >= 0) {
        std::wstring typeText = item.isDirectory ? L"Folder" : L"File";
        ListView_SetItemText(m_listView, inserted, 1, const_cast<wchar_t*>(typeText.c_str()));
        std::wstring modified = FormatTimestamp(item.modified);
        if(!modified.empty()) {
            ListView_SetItemText(m_listView, inserted, 2, const_cast<wchar_t*>(modified.c_str()));
        }
    }
}

void SubDirTipWindow::UpdateColumns() {
    if(!m_listView.IsWindow()) {
        return;
    }
    while(ListView_DeleteColumn(m_listView, 0)) {
    }
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    column.pszText = const_cast<wchar_t*>(L"Name");
    column.cx = 180;
    ListView_InsertColumn(m_listView, 0, &column);

    column.pszText = const_cast<wchar_t*>(L"Type");
    column.cx = 70;
    column.iSubItem = 1;
    ListView_InsertColumn(m_listView, 1, &column);

    column.pszText = const_cast<wchar_t*>(L"Modified");
    column.cx = 130;
    column.iSubItem = 2;
    ListView_InsertColumn(m_listView, 2, &column);
}

void SubDirTipWindow::ExecuteCommand(const std::vector<int>& indices, Command command) {
    if(indices.empty()) {
        return;
    }
    std::vector<std::wstring> paths;
    for(int index : indices) {
        if(index < 0 || static_cast<std::size_t>(index) >= m_items.size()) {
            continue;
        }
        paths.push_back(m_items[index].path);
    }
    if(paths.empty()) {
        return;
    }

    switch(command) {
    case Command::Open:
        m_owner.OpenPathFromTooltip(paths.front());
        break;
    case Command::OpenNewTab:
        for(const auto& p : paths) {
            m_owner.OpenPathInNewTabFromTooltip(p);
        }
        break;
    case Command::OpenNewWindow:
        for(const auto& p : paths) {
            m_owner.OpenPathInNewWindowFromTooltip(p);
        }
        break;
    }
}

void SubDirTipWindow::ShowContextMenu(const POINT& screenPoint) {
    HMENU menu = ::CreatePopupMenu();
    if(!menu) {
        return;
    }
    AppendMenuW(menu, MF_STRING, kContextOpen, L"Open");
    AppendMenuW(menu, MF_STRING, kContextOpenNewTab, L"Open in New Tab");
    AppendMenuW(menu, MF_STRING, kContextOpenNewWindow, L"Open in New Window");

    UINT command = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, screenPoint.x, screenPoint.y, 0, m_hWnd, nullptr);
    ::DestroyMenu(menu);

    if(command == 0) {
        return;
    }

    std::vector<int> indices;
    int item = -1;
    while((item = ListView_GetNextItem(m_listView, item, LVNI_SELECTED)) != -1) {
        indices.push_back(item);
    }
    if(indices.empty()) {
        POINT cursor{};
        ::GetCursorPos(&cursor);
        ::ScreenToClient(m_listView, &cursor);
        LVHITTESTINFO info{};
        info.pt = cursor;
        if(ListView_HitTest(m_listView, &info) >= 0 && info.iItem >= 0) {
            indices.push_back(info.iItem);
        }
    }

    if(indices.empty()) {
        return;
    }

    switch(command) {
    case kContextOpen:
        ExecuteCommand(indices, Command::Open);
        break;
    case kContextOpenNewTab:
        ExecuteCommand(indices, Command::OpenNewTab);
        break;
    case kContextOpenNewWindow:
        ExecuteCommand(indices, Command::OpenNewWindow);
        break;
    default:
        break;
    }
}

void SubDirTipWindow::BeginDragOperation(const std::vector<int>& indices) {
    std::vector<std::wstring> paths;
    for(int index : indices) {
        if(index < 0 || static_cast<std::size_t>(index) >= m_items.size()) {
            continue;
        }
        paths.push_back(m_items[index].path);
    }
    if(paths.empty()) {
        return;
    }

    std::vector<LPITEMIDLIST> pidls;
    for(const auto& path : paths) {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if(SUCCEEDED(::SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr))) {
            pidls.push_back(pidl);
        }
    }
    if(pidls.empty()) {
        return;
    }

    CComPtr<IDataObject> dataObject;
    if(FAILED(::SHCreateDataObject(nullptr, static_cast<UINT>(pidls.size()), const_cast<LPCITEMIDLIST*>(pidls.data()), nullptr,
                                   IID_PPV_ARGS(&dataObject)))) {
        for(auto pidl : pidls) {
            ::CoTaskMemFree(pidl);
        }
        return;
    }

    DropSource* source = new DropSource();
    DWORD effect = DROPEFFECT_NONE;
    ::DoDragDrop(dataObject, source, DROPEFFECT_COPY | DROPEFFECT_MOVE, &effect);
    source->Release();
    for(auto pidl : pidls) {
        ::CoTaskMemFree(pidl);
    }
}

void SubDirTipWindow::UpdateThumbnailPreview(int hotItem) {
    if(!m_thumbnailTooltip) {
        return;
    }
    if(hotItem < 0 || static_cast<std::size_t>(hotItem) >= m_items.size()) {
        m_thumbnailTooltip->HideTooltip();
        return;
    }
    if(!m_config.tips.subDirTipsPreview) {
        m_thumbnailTooltip->HideTooltip();
        return;
    }

    RECT itemRect{};
    if(!ListView_GetItemRect(m_listView, hotItem, &itemRect, LVIR_BOUNDS)) {
        return;
    }
    POINT topLeft{itemRect.left, itemRect.top};
    POINT bottomRight{itemRect.right, itemRect.bottom};
    ::ClientToScreen(m_listView, &topLeft);
    ::ClientToScreen(m_listView, &bottomRight);
    RECT reference{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
    m_thumbnailTooltip->ShowForPath(m_items[hotItem].path, reference, m_hWnd);
}

void SubDirTipWindow::UpdateAnchorRect() {
    if(!m_listView.IsWindow()) {
        return;
    }
    RECT rect{};
    ::GetWindowRect(m_listView, &rect);
    m_anchorRect = rect;
}
