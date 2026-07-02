#include "pch.h"
#include "BreadcrumbBar.h"

#include <CommCtrl.h>
#include <ShlObj.h>
#include <ShObjIdl.h>
#include <dpa_dsa.h>

#include <mutex>
#include <unordered_map>

namespace {
std::mutex& ToolbarMapMutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<HWND, BreadcrumbBarHelper*>& ToolbarMap() {
    static std::unordered_map<HWND, BreadcrumbBarHelper*> map;
    return map;
}

std::unordered_map<HWND, BreadcrumbBarHelper*>& ParentMap() {
    static std::unordered_map<HWND, BreadcrumbBarHelper*> map;
    return map;
}

UINT RegisterBreadcrumbMessage() {
    static UINT message = ::RegisterWindowMessageW(L"QTTabBar_BreadcrumbDPA");
    return message;
}

PIDLIST_ABSOLUTE PidlFromObjectPointer(void* object) {
    if(object == nullptr) {
        return nullptr;
    }
    PIDLIST_ABSOLUTE pidl = nullptr;
    if(SUCCEEDED(::SHGetIDListFromObject(static_cast<IUnknown*>(object), &pidl))) {
        return pidl;
    }
    return nullptr;
}

} // namespace

UINT BreadcrumbBarHelper::s_breadcrumbDpaMessage = RegisterBreadcrumbMessage();

BreadcrumbBarHelper::BreadcrumbBarHelper(HWND hwndToolbar)
    : m_hwndToolbar(hwndToolbar)
    , m_hwndParent(::GetParent(hwndToolbar))
    , m_prevToolbarProc(nullptr)
    , m_prevParentProc(nullptr)
    , m_hdpa(nullptr) {
    ATLASSERT(::IsWindow(m_hwndToolbar));
    if(m_hwndToolbar == nullptr || m_hwndParent == nullptr) {
        return;
    }

    {
        std::lock_guard lock(ToolbarMapMutex());
        m_prevToolbarProc = reinterpret_cast<WNDPROC>(::SetWindowLongPtrW(m_hwndToolbar, GWLP_WNDPROC,
                                                                          reinterpret_cast<LONG_PTR>(&BreadcrumbBarHelper::ToolbarWndProc)));
        ToolbarMap()[m_hwndToolbar] = this;
    }
    {
        std::lock_guard lock(ToolbarMapMutex());
        m_prevParentProc = reinterpret_cast<WNDPROC>(::SetWindowLongPtrW(m_hwndParent, GWLP_WNDPROC,
                                                                         reinterpret_cast<LONG_PTR>(&BreadcrumbBarHelper::ParentWndProc)));
        ParentMap()[m_hwndParent] = this;
    }
}

BreadcrumbBarHelper::~BreadcrumbBarHelper() {
    Reset();
}

void BreadcrumbBarHelper::SetItemClickedCallback(ItemClickedCallback callback) {
    m_callback = std::move(callback);
}

void BreadcrumbBarHelper::Reset() {
    HWND toolbar = m_hwndToolbar;
    HWND parent = m_hwndParent;
    WNDPROC prevToolbar = m_prevToolbarProc;
    WNDPROC prevParent = m_prevParentProc;

    {
        std::lock_guard lock(ToolbarMapMutex());
        if(toolbar && prevToolbar) {
            ToolbarMap().erase(toolbar);
        }
        if(parent && prevParent) {
            ParentMap().erase(parent);
        }
    }

    if(toolbar && prevToolbar && ::IsWindow(toolbar)) {
        ::SetWindowLongPtrW(toolbar, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(prevToolbar));
    }
    if(parent && prevParent && ::IsWindow(parent)) {
        ::SetWindowLongPtrW(parent, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(prevParent));
    }

    m_hwndToolbar = nullptr;
    m_hwndParent = nullptr;
    m_prevToolbarProc = nullptr;
    m_prevParentProc = nullptr;
    m_hdpa = nullptr;
    m_callback = nullptr;
}

BreadcrumbBarHelper* BreadcrumbBarHelper::LookupToolbar(HWND hwnd) {
    std::lock_guard lock(ToolbarMapMutex());
    auto it = ToolbarMap().find(hwnd);
    return it != ToolbarMap().end() ? it->second : nullptr;
}

BreadcrumbBarHelper* BreadcrumbBarHelper::LookupParent(HWND hwnd) {
    std::lock_guard lock(ToolbarMapMutex());
    auto it = ParentMap().find(hwnd);
    return it != ParentMap().end() ? it->second : nullptr;
}

LRESULT CALLBACK BreadcrumbBarHelper::ToolbarWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    BreadcrumbBarHelper* self = LookupToolbar(hwnd);
    if(self == nullptr || self->m_prevToolbarProc == nullptr) {
        return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    if(msg == WM_NCDESTROY) {
        LRESULT result = ::CallWindowProcW(self->m_prevToolbarProc, hwnd, msg, wParam, lParam);
        self->Reset();
        return result;
    }
    if(self->HandleToolbarMessage(msg, wParam, lParam)) {
        return 0;
    }
    return ::CallWindowProcW(self->m_prevToolbarProc, hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK BreadcrumbBarHelper::ParentWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    BreadcrumbBarHelper* self = LookupParent(hwnd);
    if(self == nullptr || self->m_prevParentProc == nullptr) {
        return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    if(msg == WM_NCDESTROY) {
        LRESULT result = ::CallWindowProcW(self->m_prevParentProc, hwnd, msg, wParam, lParam);
        self->Reset();
        return result;
    }
    if(self->HandleParentMessage(msg, wParam, lParam)) {
        return 0;
    }
    return ::CallWindowProcW(self->m_prevParentProc, hwnd, msg, wParam, lParam);
}

bool BreadcrumbBarHelper::HandleToolbarMessage(UINT msg, WPARAM /*wParam*/, LPARAM lParam) {
    switch(msg) {
    case WM_MBUTTONUP: {
        if(HandleMiddleClick(lParam)) {
            return true;
        }
        break;
    }
    case WM_DESTROY:
        Reset();
        break;
    default:
        break;
    }
    return false;
}

bool BreadcrumbBarHelper::HandleParentMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    if(msg == s_breadcrumbDpaMessage) {
        m_hdpa = reinterpret_cast<HDPA>(lParam);
        return true;
    }
    if(msg == WM_COMMAND) {
        if(HandleCommand(wParam)) {
            return true;
        }
    } else if(msg == WM_DESTROY) {
        Reset();
    }
    return false;
}

bool BreadcrumbBarHelper::HandleMiddleClick(LPARAM lParam) {
    if(m_hdpa == nullptr || !m_callback) {
        return false;
    }
    POINT pt{};
    pt.x = GET_X_LPARAM(lParam);
    pt.y = GET_Y_LPARAM(lParam);
    int index = HitTest(pt);
    if(index < 0) {
        return false;
    }
    UINT modifiers = CurrentModifiers();
    return InvokeForIndex(index, true, modifiers);
}

bool BreadcrumbBarHelper::HandleCommand(WPARAM wParam) {
    if(m_hdpa == nullptr || !m_callback) {
        return false;
    }
    UINT modifiers = CurrentModifiers();
    if(modifiers == 0) {
        return false;
    }
    int commandId = LOWORD(wParam);
    return InvokeForCommand(commandId, false, modifiers);
}

bool BreadcrumbBarHelper::InvokeForCommand(int commandId, bool middle, UINT modifiers) {
    int index = CommandToIndex(commandId);
    if(index < 0) {
        return false;
    }
    return InvokeForIndex(index, middle, modifiers);
}

bool BreadcrumbBarHelper::InvokeForIndex(int index, bool middle, UINT modifiers) {
    if(index < 0 || index >= ButtonCount()) {
        return false;
    }
    int commandId = IndexToCommand(index);
    if(commandId < 0) {
        return false;
    }
    LPARAM itemParam = GetButtonLParam(commandId);
    if(itemParam == 0) {
        return false;
    }
    void* object = ::DPA_GetPtr(m_hdpa, static_cast<int>(itemParam));
    if(object == nullptr) {
        return false;
    }
    PIDLIST_ABSOLUTE pidl = PidlFromObjectPointer(object);
    if(pidl == nullptr) {
        return false;
    }
    bool handled = false;
    if(m_callback) {
        handled = m_callback(pidl, modifiers, middle);
    }
    ::CoTaskMemFree(pidl);
    return handled;
}

int BreadcrumbBarHelper::ButtonCount() const {
    return static_cast<int>(::SendMessageW(m_hwndToolbar, TB_BUTTONCOUNT, 0, 0));
}

int BreadcrumbBarHelper::CommandToIndex(int commandId) const {
    return static_cast<int>(::SendMessageW(m_hwndToolbar, TB_COMMANDTOINDEX, static_cast<WPARAM>(commandId), 0));
}

int BreadcrumbBarHelper::HitTest(const POINT& pt) const {
    return static_cast<int>(::SendMessageW(m_hwndToolbar, TB_HITTEST, 0, reinterpret_cast<LPARAM>(&pt)));
}

int BreadcrumbBarHelper::IndexToCommand(int index) const {
    TBBUTTON button{};
    if(!::SendMessageW(m_hwndToolbar, TB_GETBUTTON, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(&button))) {
        return -1;
    }
    return static_cast<int>(button.idCommand);
}

LPARAM BreadcrumbBarHelper::GetButtonLParam(int commandId) const {
    TBBUTTONINFO info{};
    info.cbSize = sizeof(info);
    info.dwMask = TBIF_LPARAM | TBIF_COMMAND;
    info.idCommand = commandId;
    LRESULT result = ::SendMessageW(m_hwndToolbar, TB_GETBUTTONINFOW, static_cast<WPARAM>(commandId), reinterpret_cast<LPARAM>(&info));
    if(result == -1) {
        return 0;
    }
    return info.lParam;
}

UINT BreadcrumbBarHelper::CurrentModifiers() {
    UINT modifiers = 0;
    if(::GetKeyState(VK_CONTROL) & 0x8000) {
        modifiers |= kModifierCtrl;
    }
    if(::GetKeyState(VK_SHIFT) & 0x8000) {
        modifiers |= kModifierShift;
    }
    if(::GetKeyState(VK_MENU) & 0x8000) {
        modifiers |= kModifierAlt;
    }
    return modifiers;
}

