#pragma once

#include <windows.h>

#include <optional>

namespace qttabbar::hooks {

struct HookCallbacks {
    void(__stdcall* hookResult)(int hookId, int retcode, void* context) = nullptr;
    BOOL(__stdcall* newWindow)(PCIDLIST_ABSOLUTE pidl, void* context) = nullptr;
    void* context = nullptr;
};

class HookLibraryBridge {
public:
    static HookLibraryBridge& Instance();

    HRESULT Initialize(const HookCallbacks& callbacks, const wchar_t* libraryPath);
    HRESULT InitializeBackground(const HookCallbacks& callbacks, const wchar_t* libraryPath);
    void Shutdown();
    HRESULT InitShellBrowserHook(IUnknown* shellBrowser);
    HRESULT RegisterBackgroundWindow(HWND window);

private:
    HookLibraryBridge() = default;

    HRESULT InitializeEntry(const HookCallbacks& callbacks,
                            const wchar_t* libraryPath,
                            const char* entryPoint);

    static void __cdecl ForwardHookResult(int hookId, int retcode);
    static bool __cdecl ForwardNewWindow(LPCITEMIDLIST pidl);

    HookCallbacks callbacks_{};
    HMODULE module_ = nullptr;
    bool initializationFailed_ = false;
};

}  // namespace qttabbar::hooks
