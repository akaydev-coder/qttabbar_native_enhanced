#include "pch.h"
#include "HookManagerNative.h"

#include "Config.h"
#include "HookLibraryBridge.h"
#include "HookMessages.h"
#include "InstanceManagerNative.h"
#include "QTTabBarClass.h"

#include <Shlobj.h>
#include <VersionHelpers.h>

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <system_error>

#pragma comment(lib, "Shell32.lib")

namespace qttabbar::hooks {

namespace {
constexpr SHORT kKeyPressedMask = static_cast<SHORT>(0x8000);
}  // namespace

HookManagerNative& HookManagerNative::Instance() {
    static HookManagerNative instance;
    return instance;
}

HookManagerNative::HookManagerNative()
    : libraryLoaded_(false)
    , autoHookEnabled_(false)
    , captureNewWindowsEnabled_(true)
    , serverBlocked_(false)
    , serverWarningLogged_(false)
    , missingLibraryLogged_(false) {
    for(auto& status : hookStatus_) {
        status.store(-1, std::memory_order_relaxed);
    }
}

void HookManagerNative::OnTabBarSiteAssigned(QTTabBarClass* /*tabBar*/) {
    ReloadConfiguration();
}

void HookManagerNative::OnTabBarSiteCleared(QTTabBarClass* /*tabBar*/) {
    std::lock_guard guard(mutex_);
    ApplyHookStateLocked();
}

void HookManagerNative::ReloadConfiguration() {
    ConfigData config = LoadConfigFromRegistry();
    ReloadConfiguration(config);
}

void HookManagerNative::ReloadConfiguration(const ConfigData& config) {
    std::lock_guard guard(mutex_);
    autoHookEnabled_.store(config.window.autoHookWindow, std::memory_order_release);
    captureNewWindowsEnabled_.store(config.window.captureNewWindows, std::memory_order_release);
    ApplyHookStateLocked();
}

void HookManagerNative::RegisterSink(IHookEventSink* sink) {
    if(sink == nullptr) {
        return;
    }
    std::lock_guard guard(mutex_);
    if(std::find(sinks_.begin(), sinks_.end(), sink) == sinks_.end()) {
        sinks_.push_back(sink);
        ApplyHookStateLocked();
    }
}

void HookManagerNative::UnregisterSink(IHookEventSink* sink) {
    if(sink == nullptr) {
        return;
    }
    std::lock_guard guard(mutex_);
    sinks_.erase(std::remove(sinks_.begin(), sinks_.end(), sink), sinks_.end());
    ApplyHookStateLocked();
}

void HookManagerNative::ApplyHookStateLocked() {
    const bool isServer = ::IsWindowsServer();
    serverBlocked_.store(isServer, std::memory_order_release);
    if(isServer) {
        if(!serverWarningLogged_) {
            LogError(L"Hook installation blocked on Windows Server.");
            serverWarningLogged_ = true;
        }
    } else {
        serverWarningLogged_ = false;
    }

    const bool shouldLoad = (autoHookEnabled_.load(std::memory_order_acquire) || !sinks_.empty()) && !isServer;
    if(!shouldLoad) {
        if(libraryLoaded_.exchange(false, std::memory_order_acq_rel)) {
            HookLibraryBridge::Instance().Shutdown();
        }
        return;
    }

    std::wstring libraryPath = ResolveHookLibraryPathUnlocked();
    if(libraryPath.empty()) {
        if(libraryLoaded_.exchange(false, std::memory_order_acq_rel)) {
            HookLibraryBridge::Instance().Shutdown();
        }
        return;
    }

    HookCallbacks callbacks{};
    callbacks.hookResult = &HookManagerNative::HookResultThunk;
    callbacks.newWindow = &HookManagerNative::NewWindowThunk;
    callbacks.context = this;

    HRESULT hr = HookLibraryBridge::Instance().Initialize(callbacks, libraryPath.c_str());
    if(FAILED(hr)) {
        std::wstringstream stream;
        stream << L"Failed to initialize hook library. hr=0x" << std::hex << hr;
        LogError(stream.str());
        libraryLoaded_.store(false, std::memory_order_release);
        return;
    }

    libraryLoaded_.store(true, std::memory_order_release);
}

std::wstring HookManagerNative::ResolveHookLibraryPathUnlocked() {
    wchar_t buffer[MAX_PATH] = {};
    if(FAILED(::SHGetFolderPathW(nullptr, CSIDL_COMMON_APPDATA, nullptr, SHGFP_TYPE_CURRENT, buffer))) {
        LogError(L"Failed to locate ProgramData folder.");
        return {};
    }

    std::filesystem::path path(buffer);
    path /= L"QTTabBar";
    path /= (sizeof(void*) == 8 ? L"QTHookLib64.dll" : L"QTHookLib32.dll");

    std::error_code ec;
    if(std::filesystem::exists(path, ec)) {
        missingLibraryLogged_ = false;
        return path.wstring();
    }

    wchar_t modulePath[MAX_PATH] = {};
    if(::GetModuleFileNameW(_AtlBaseModule.GetModuleInstance(), modulePath, _countof(modulePath)) != 0) {
        std::filesystem::path fallback(modulePath);
        fallback.replace_filename(L"QTHookLib.dll");
        if(std::filesystem::exists(fallback, ec)) {
            missingLibraryLogged_ = false;
            return fallback.wstring();
        }
    }

    if(!missingLibraryLogged_) {
        std::wstringstream stream;
        stream << L"Hook library not found: " << path.wstring();
        LogError(stream.str());
        missingLibraryLogged_ = true;
    }
    return {};
}

std::wstring HookManagerNative::PidlToPath(PCIDLIST_ABSOLUTE pidl) const {
    if(pidl == nullptr) {
        return {};
    }

    PWSTR buffer = nullptr;
    std::wstring result;
    if(SUCCEEDED(::SHGetNameFromIDList(pidl, SIGDN_FILESYSPATH, &buffer))) {
        result.assign(buffer);
        ::CoTaskMemFree(buffer);
        return result;
    }

    if(SUCCEEDED(::SHGetNameFromIDList(pidl, SIGDN_DESKTOPABSOLUTEPARSING, &buffer))) {
        result.assign(buffer);
        ::CoTaskMemFree(buffer);
    }

    return result;
}

void HookManagerNative::LogError(const std::wstring& message) const {
    ATLTRACE(L"HookManagerNative: %s\n", message.c_str());
    std::wstring formatted = L"[QTTabBarNative] " + message + L"\n";
    ::OutputDebugStringW(formatted.c_str());
}

void __stdcall HookManagerNative::HookResultThunk(int hookId, int retcode, void* context) {
    if(auto* self = static_cast<HookManagerNative*>(context)) {
        self->HandleHookResult(hookId, retcode);
    }
}

BOOL __stdcall HookManagerNative::NewWindowThunk(PCIDLIST_ABSOLUTE pidl, void* context) {
    if(auto* self = static_cast<HookManagerNative*>(context)) {
        return self->HandleNewWindow(pidl);
    }
    return FALSE;
}

void HookManagerNative::HandleHookResult(int hookId, int retcode) {
    std::vector<IHookEventSink*> sinksCopy;
    {
        std::lock_guard guard(mutex_);
        if(hookId >= 0 && hookId < static_cast<int>(hookStatus_.size())) {
            hookStatus_[static_cast<size_t>(hookId)].store(retcode, std::memory_order_release);
        }
        sinksCopy = sinks_;
    }
    for(IHookEventSink* sink : sinksCopy) {
        if(sink) {
            sink->OnHookResult(hookId, retcode);
        }
    }
}

BOOL HookManagerNative::HandleNewWindow(PCIDLIST_ABSOLUTE pidl) {
    if(serverBlocked_.load(std::memory_order_acquire) || !libraryLoaded_.load(std::memory_order_acquire)) {
        return FALSE;
    }

    BOOL handled = FALSE;

    if(autoHookEnabled_.load(std::memory_order_acquire) && captureNewWindowsEnabled_.load(std::memory_order_acquire) &&
       InstanceManagerNative::Instance().GetTabBarCount() > 0 &&
       (::GetAsyncKeyState(VK_CONTROL) & kKeyPressedMask) == 0) {
        std::wstring path = PidlToPath(pidl);
        if(!path.empty()) {
            auto tabBars = InstanceManagerNative::Instance().EnumerateTabBars();
            for(QTTabBarClass* tabBar : tabBars) {
                if(tabBar == nullptr) {
                    continue;
                }
                HWND hwnd = tabBar->GetWindowHandle();
                if(hwnd == nullptr || !::IsWindow(hwnd)) {
                    continue;
                }
                CaptureNewWindowRequest request{};
                request.path = path.c_str();
                LRESULT result = ::SendMessageW(hwnd, WM_APP_CAPTURE_NEW_WINDOW, reinterpret_cast<WPARAM>(&request), 0);
                if(result != 0 && request.handled) {
                    handled = TRUE;
                    break;
                }
            }
        }
    }

    std::vector<IHookEventSink*> sinksCopy;
    {
        std::lock_guard guard(mutex_);
        sinksCopy = sinks_;
    }
    for(IHookEventSink* sink : sinksCopy) {
        if(sink && sink->OnHookNewWindow(pidl)) {
            handled = TRUE;
        }
    }

    return handled;
}

}  // namespace qttabbar::hooks

