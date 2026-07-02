#include "pch.h"

#include "AutoLoaderNative.h"
#include "PluginManagerNative.h"
#include "PluginContracts.h"

#include "HookLibraryBridge.h"
#include "FluentGlassBridge.h"

using namespace qttabbar::plugins;

extern "C" {

__declspec(dllexport) void __stdcall QTTabBarNative_RefreshPlugins() {
    PluginManagerNative::Instance().Refresh();
}

__declspec(dllexport) size_t __stdcall QTTabBarNative_GetPluginCount() {
    return PluginManagerNative::Instance().EnumerateMetadata().size();
}

__declspec(dllexport) BOOL __stdcall QTTabBarNative_GetPluginMetadata(size_t index, PluginMetadataNative* metadata) {
    if (metadata == nullptr) {
        return FALSE;
    }
    auto list = PluginManagerNative::Instance().EnumerateMetadata();
    if (index >= list.size()) {
        return FALSE;
    }
    *metadata = list[index];
    return TRUE;
}

__declspec(dllexport) BOOL __stdcall QTTabBarNative_SetPluginEnabled(const wchar_t* pluginId, BOOL enabled) {
    if (pluginId == nullptr) {
        return FALSE;
    }
    return PluginManagerNative::Instance().SetEnabled(pluginId, enabled != FALSE) ? TRUE : FALSE;
}

__declspec(dllexport) BOOL __stdcall QTTabBarNative_IsPluginEnabled(const wchar_t* pluginId) {
    if (pluginId == nullptr) {
        return FALSE;
    }
    return PluginManagerNative::Instance().IsEnabled(pluginId) ? TRUE : FALSE;
}

__declspec(dllexport) HRESULT __stdcall QTTabBarNative_CreatePluginInstance(const wchar_t* pluginId,
                                                                            void** instance,
                                                                            const PluginClientVTable** vtable) {
    if (pluginId == nullptr || instance == nullptr || vtable == nullptr) {
        return E_POINTER;
    }
    return PluginManagerNative::Instance().CreateInstance(pluginId, instance, vtable);
}

__declspec(dllexport) void __stdcall QTTabBarNative_DestroyPluginInstance(void* instance) {
    PluginManagerNative::Instance().DestroyInstance(instance);
}

__declspec(dllexport) BOOL __stdcall QTTabBarNative_PluginOnMenuClick(void* instance,
                                                                      PluginMenuType menuType,
                                                                      const wchar_t* menuText,
                                                                      void* tabContext) {
    return PluginManagerNative::Instance().DispatchMenuClick(instance, menuType, menuText, tabContext) ? TRUE : FALSE;
}

__declspec(dllexport) BOOL __stdcall QTTabBarNative_PluginOnOption(void* instance) {
    return PluginManagerNative::Instance().DispatchOption(instance) ? TRUE : FALSE;
}

__declspec(dllexport) BOOL __stdcall QTTabBarNative_PluginOnShortcut(void* instance, int index) {
    return PluginManagerNative::Instance().DispatchShortcut(instance, index) ? TRUE : FALSE;
}

__declspec(dllexport) HRESULT __stdcall QTTabBarNative_PluginOpen(void* instance, void* pluginServer, void* shellBrowser) {
    return PluginManagerNative::Instance().DispatchOpen(instance, pluginServer, shellBrowser);
}

__declspec(dllexport) HRESULT __stdcall QTTabBarNative_PluginQueryShortcuts(void* instance,
                                                                            wchar_t*** actions,
                                                                            int* count) {
    return PluginManagerNative::Instance().DispatchQueryShortcuts(instance, actions, count);
}

__declspec(dllexport) BOOL __stdcall QTTabBarNative_PluginHasOption(void* instance) {
    return PluginManagerNative::Instance().DispatchHasOption(instance) ? TRUE : FALSE;
}

__declspec(dllexport) void __stdcall QTTabBarNative_PluginClose(void* instance, PluginEndCode endCode) {
    PluginManagerNative::Instance().DispatchClose(instance, endCode);
}

__declspec(dllexport) int __stdcall QTTabBarNative_InitializeHookLibrary(const qttabbar::hooks::HookCallbacks* callbacks,
                                                                         const wchar_t* libraryPath) {
    if (callbacks == nullptr) {
        return E_POINTER;
    }
    qttabbar::hooks::HookCallbacks local = *callbacks;
    return qttabbar::hooks::HookLibraryBridge::Instance().Initialize(local, libraryPath);
}

__declspec(dllexport) int __stdcall QTTabBarNative_InitializeBackgroundLibrary(
        const qttabbar::hooks::HookCallbacks* callbacks,
        const wchar_t* libraryPath) {
    if (callbacks == nullptr) {
        return E_POINTER;
    }
    qttabbar::hooks::HookCallbacks local = *callbacks;
    return qttabbar::hooks::HookLibraryBridge::Instance().InitializeBackground(local, libraryPath);
}

__declspec(dllexport) void __stdcall QTTabBarNative_ShutdownHookLibrary() {
    qttabbar::hooks::HookLibraryBridge::Instance().Shutdown();
}

__declspec(dllexport) HRESULT __stdcall QTTabBarNative_ApplyFluentGlass(HWND explorerWindow,
                                                                        HWND tabBarWindow,
                                                                        HWND sideBarWindow,
                                                                        HWND sideBarRebarWindow,
                                                                        int addressMode,
                                                                        int addressExtraPixels,
                                                                        DWORD inactiveColorArgb,
                                                                        BOOL applyCaptionColor,
                                                                        BOOL applyBorderColor,
                                                                        BOOL suppressQtChildErase) {
    qttabbar::fluent::FluentGlassSettings settings{};
    settings.addressMode = addressMode;
    settings.addressExtraPixels = addressExtraPixels;
    settings.inactiveColorArgb = inactiveColorArgb;
    settings.applyCaptionColor = applyCaptionColor;
    settings.applyBorderColor = applyBorderColor;
    settings.suppressQtChildErase = suppressQtChildErase;
    return qttabbar::fluent::ApplyFluentGlass(
        explorerWindow, tabBarWindow, sideBarWindow, sideBarRebarWindow, settings);
}

__declspec(dllexport) HRESULT __stdcall QTTabBarNative_DisableFluentGlass(HWND explorerWindow) {
    return qttabbar::fluent::DisableFluentGlass(explorerWindow);
}

__declspec(dllexport) int __stdcall QTTabBarNative_InitShellBrowserHook(IUnknown* shellBrowser) {
    if (shellBrowser == nullptr) {
        return E_POINTER;
    }
    return qttabbar::hooks::HookLibraryBridge::Instance().InitShellBrowserHook(shellBrowser);
}

__declspec(dllexport) int __stdcall QTTabBarNative_RegisterBackgroundWindow(HWND window) {
    return qttabbar::hooks::HookLibraryBridge::Instance().RegisterBackgroundWindow(window);
}

__declspec(dllexport) HRESULT __stdcall QTTabBarNative_ActivateAutoLoader(IUnknown* site) {
    if (site == nullptr) {
        return E_POINTER;
    }

    CComPtr<IWebBrowser2> browser;
    HRESULT hr = site->QueryInterface(IID_PPV_ARGS(&browser));
    if (FAILED(hr) || browser == nullptr) {
        CComPtr<IServiceProvider> serviceProvider;
        hr = site->QueryInterface(IID_PPV_ARGS(&serviceProvider));
        if (SUCCEEDED(hr) && serviceProvider) {
            hr = serviceProvider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser));
        }
    }

    if (FAILED(hr) || browser == nullptr) {
        return FAILED(hr) ? hr : E_NOINTERFACE;
    }

    return AutoLoaderNative::ActivateForBrowser(browser);
}

}  // extern "C"
