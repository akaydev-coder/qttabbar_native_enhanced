#include <windows.h>
#include <shlobj.h>

#include <cstdio>

struct HookCallbacks {
    void(__stdcall* hookResult)(int, int, void*) = nullptr;
    BOOL(__stdcall* newWindow)(PCIDLIST_ABSOLUTE, void*) = nullptr;
    void* context = nullptr;
};

using InitializeRendererFn = int(__stdcall*)(const HookCallbacks*);
using InstallHooksFn = int(__stdcall*)();
using ShutdownFn = void(__stdcall*)();

int wmain(int argc, wchar_t** argv) {
    if(argc != 2) {
        return 2;
    }

    HMODULE explorerFrame = ::LoadLibraryW(L"ExplorerFrame.dll");
    if(explorerFrame == nullptr) {
        std::printf("ExplorerFrame=0x%08lX\n", ::GetLastError());
        return 3;
    }

    HMODULE module = ::LoadLibraryExW(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if(module == nullptr) {
        std::printf("NativeModule=0x%08lX\n", ::GetLastError());
        return 4;
    }

    auto initialize = reinterpret_cast<InitializeRendererFn>(
        ::GetProcAddress(module, "QTTabBarNative_InitializeBackgroundRenderer"));
    auto install = reinterpret_cast<InstallHooksFn>(
        ::GetProcAddress(module, "QTTabBarNative_InstallBackgroundHooks"));
    auto shutdown = reinterpret_cast<ShutdownFn>(
        ::GetProcAddress(module, "QTTabBarNative_ShutdownHookLibrary"));
    if(initialize == nullptr || install == nullptr || shutdown == nullptr) {
        return 5;
    }

    HookCallbacks callbacks{};
    const int rendererResult = initialize(&callbacks);
    const int hookResult = rendererResult == 0 ? install() : E_UNEXPECTED;
    std::printf("Renderer=0x%08X\nHooks=0x%08X\n", rendererResult, hookResult);
    shutdown();
    ::FreeLibrary(module);
    ::FreeLibrary(explorerFrame);
    return rendererResult == 0 && hookResult == 0 ? 0 : 6;
}
