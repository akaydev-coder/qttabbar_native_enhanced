#include "pch.h"

#include "HookLibraryBridge.h"

#include <shlwapi.h>

#include <mutex>

#pragma comment(lib, "Shlwapi.lib")

namespace qttabbar::hooks {

namespace {
struct CallbackStruct {
    void(__cdecl* hookResult)(int, int);
    bool(__cdecl* newWindow)(LPCITEMIDLIST);
};

using InitializeFn = int(__cdecl*)(CallbackStruct*);
using DisposeFn = int(__cdecl*)();
using InitShellBrowserHookFn = int(__cdecl*)(IShellBrowser*);
using RegisterBackgroundWindowFn = int(__cdecl*)(HWND);

int InitializeWithSeh(InitializeFn initialize, CallbackStruct* callbacks, DWORD& exceptionCode) noexcept {
    exceptionCode = ERROR_SUCCESS;
    __try {
        return initialize(callbacks);
    }
    __except ((exceptionCode = GetExceptionCode()), EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

bool DisposeWithSeh(DisposeFn dispose, DWORD& exceptionCode) noexcept {
    exceptionCode = ERROR_SUCCESS;
    __try {
        dispose();
        return true;
    }
    __except ((exceptionCode = GetExceptionCode()), EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::mutex g_mutex;
}  // namespace

HookLibraryBridge& HookLibraryBridge::Instance() {
    static HookLibraryBridge instance;
    return instance;
}

HRESULT HookLibraryBridge::Initialize(const HookCallbacks& callbacks, const wchar_t* libraryPath) {
    return InitializeEntry(callbacks, libraryPath, "Initialize");
}

HRESULT HookLibraryBridge::InitializeBackground(const HookCallbacks& callbacks,
                                                const wchar_t* libraryPath) {
    return InitializeEntry(callbacks, libraryPath, "InitializeBackground");
}

HRESULT HookLibraryBridge::InitializeEntry(const HookCallbacks& callbacks,
                                           const wchar_t* libraryPath,
                                           const char* entryPoint) {
    std::scoped_lock lock(g_mutex);

    if (initializationFailed_) {
        return E_FAIL;
    }

    if (module_ != nullptr) {
        callbacks_ = callbacks;
        return S_OK;
    }

    if (libraryPath == nullptr || !PathFileExistsW(libraryPath)) {
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }

    module_ = ::LoadLibraryExW(libraryPath, nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (module_ == nullptr) {
        return HRESULT_FROM_WIN32(::GetLastError());
    }

    auto initialize = reinterpret_cast<InitializeFn>(::GetProcAddress(module_, entryPoint));
    if (initialize == nullptr) {
        ::FreeLibrary(module_);
        module_ = nullptr;
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }

    CallbackStruct callbacksNative{};
    callbacksNative.hookResult = &HookLibraryBridge::ForwardHookResult;
    callbacksNative.newWindow = &HookLibraryBridge::ForwardNewWindow;

    DWORD exceptionCode = ERROR_SUCCESS;
    int result = InitializeWithSeh(initialize, &callbacksNative, exceptionCode);
    if (result != 0) {
        DWORD cleanupExceptionCode = ERROR_SUCCESS;
        auto dispose = reinterpret_cast<DisposeFn>(::GetProcAddress(module_, "Dispose"));
        const bool cleanupSucceeded = dispose != nullptr && DisposeWithSeh(dispose, cleanupExceptionCode);
        if (cleanupSucceeded) {
            ::FreeLibrary(module_);
            module_ = nullptr;
        } else {
            // Keep the module mapped if rollback failed. Installed detours must never
            // point into an unloaded QTHookLib image.
            initializationFailed_ = true;
        }
        callbacks_ = {};
        if (exceptionCode != ERROR_SUCCESS) {
            return static_cast<HRESULT>(exceptionCode);
        }
        return HRESULT_FROM_WIN32(ERROR_INVALID_FUNCTION);
    }

    callbacks_ = callbacks;
    initializationFailed_ = false;
    return S_OK;
}

void HookLibraryBridge::Shutdown() {
    std::scoped_lock lock(g_mutex);
    if (module_ != nullptr) {
        if (auto dispose = reinterpret_cast<DisposeFn>(::GetProcAddress(module_, "Dispose"))) {
            DWORD exceptionCode = ERROR_SUCCESS;
            if (!DisposeWithSeh(dispose, exceptionCode)) {
                callbacks_ = {};
                initializationFailed_ = true;
                return;
            }
        }
        ::FreeLibrary(module_);
        module_ = nullptr;
    }
    callbacks_ = {};
    initializationFailed_ = false;
}

HRESULT HookLibraryBridge::InitShellBrowserHook(IUnknown* shellBrowser) {
    if (module_ == nullptr || shellBrowser == nullptr) {
        return E_FAIL;
    }
    auto initHook = reinterpret_cast<InitShellBrowserHookFn>(::GetProcAddress(module_, "InitShellBrowserHook"));
    if (initHook == nullptr) {
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }
    CComPtr<IShellBrowser> spBrowser;
    HRESULT hr = shellBrowser->QueryInterface(IID_PPV_ARGS(&spBrowser));
    if (FAILED(hr)) {
        return hr;
    }
    return initHook(spBrowser) == 0 ? S_OK : E_FAIL;
}

HRESULT HookLibraryBridge::RegisterBackgroundWindow(HWND window) {
    if (module_ == nullptr || !::IsWindow(window)) {
        return E_FAIL;
    }
    auto registerWindow = reinterpret_cast<RegisterBackgroundWindowFn>(
        ::GetProcAddress(module_, "RegisterBackgroundWindow"));
    if (registerWindow == nullptr) {
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }
    return registerWindow(window) == 0 ? S_OK : E_FAIL;
}

void __cdecl HookLibraryBridge::ForwardHookResult(int hookId, int retcode) {
    HookLibraryBridge& bridge = HookLibraryBridge::Instance();
    if (bridge.callbacks_.hookResult != nullptr) {
        bridge.callbacks_.hookResult(hookId, retcode, bridge.callbacks_.context);
    }
}

bool __cdecl HookLibraryBridge::ForwardNewWindow(LPCITEMIDLIST pidl) {
    HookLibraryBridge& bridge = HookLibraryBridge::Instance();
    if (bridge.callbacks_.newWindow != nullptr) {
        return bridge.callbacks_.newWindow(pidl, bridge.callbacks_.context) != FALSE;
    }
    return false;
}

}  // namespace qttabbar::hooks
