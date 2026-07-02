#include <windows.h>

#include <cstdio>

#pragma comment(linker, "/manifestdependency:\"type='win32' " \
                        "name='Microsoft.Windows.Common-Controls' " \
                        "version='6.0.0.0' processorArchitecture='*' " \
                        "publicKeyToken='6595b64144ccf1df' language='*'\"")

struct HookCallbacks {
    void(__stdcall* hookResult)(int, int, void*);
    BOOL(__stdcall* newWindow)(const void*, void*);
    void* context;
};

using InitializeHookLibraryFn = int(__stdcall*)(const HookCallbacks*, const wchar_t*);

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        return 2;
    }

    HMODULE bridge = LoadLibraryW(argv[1]);
    if (bridge == nullptr) {
        wprintf(L"LoadLibrary failed: %lu\n", GetLastError());
        return 3;
    }

    auto initialize = reinterpret_cast<InitializeHookLibraryFn>(
        GetProcAddress(bridge, "QTTabBarNative_InitializeHookLibrary"));
    if (initialize == nullptr) {
        FreeLibrary(bridge);
        return 4;
    }

    HookCallbacks callbacks{};
    const int result = initialize(&callbacks, argv[2]);
    wprintf(L"Initialize returned 0x%08X\n", static_cast<unsigned int>(result));
    FreeLibrary(bridge);
    return static_cast<unsigned int>(result) == EXCEPTION_ACCESS_VIOLATION ? 0 : 5;
}
