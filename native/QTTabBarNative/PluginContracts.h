#pragma once

#include <windows.h>

#include <cstddef>

namespace qttabbar::plugins {

// Mirrors QTPlugin.EndCode from the managed implementation.
enum class PluginEndCode : int {
    WindowClosed = 0,
    Unloaded = 1,
    Removed = 2,
    Hidden = 3,
};

// Mirrors QTPlugin.MenuType from the managed implementation.
enum class PluginMenuType : unsigned int {
    None = 0,
    Tab = 1,
    Bar = 2,
    Both = 4,
};

inline PluginMenuType operator|(PluginMenuType lhs, PluginMenuType rhs) {
    return static_cast<PluginMenuType>(static_cast<unsigned int>(lhs) | static_cast<unsigned int>(rhs));
}

inline bool operator&(PluginMenuType lhs, PluginMenuType rhs) {
    return (static_cast<unsigned int>(lhs) & static_cast<unsigned int>(rhs)) != 0u;
}

// Mirrors QTPlugin.PluginType.
enum class PluginType : unsigned int {
    Interactive = 0,
    Background = 1,
    BackgroundMultiple = 2,
    Static = 3,
};

struct PluginMetadataNative {
    wchar_t id[128];
    wchar_t name[128];
    wchar_t author[128];
    wchar_t description[512];
    wchar_t version[32];
    wchar_t libraryPath[MAX_PATH];
    PluginType type;
    BOOL hasOptions;
    BOOL enabled;
    BOOL managedFallback;  // Indicates the plugin still relies on the legacy managed loader.
};

struct PluginClientVTable {
    void(__stdcall* Close)(void* instance, PluginEndCode endCode);
    void(__stdcall* OnMenuItemClick)(void* instance, PluginMenuType menuType, const wchar_t* menuText, void* tabContext);
    void(__stdcall* OnOption)(void* instance);
    void(__stdcall* OnShortcutKeyPressed)(void* instance, int index);
    HRESULT(__stdcall* Open)(void* instance, void* pluginServer, void* shellBrowser);
    HRESULT(__stdcall* QueryShortcutKeys)(void* instance, wchar_t*** actions, int* count);
    BOOL(__stdcall* HasOption)(void* instance);
};

using PluginCreateFn = HRESULT(__stdcall*)(void** instance, const PluginClientVTable** vtable);
using PluginDestroyFn = void(__stdcall*)(void* instance);
using PluginQueryMetadataFn = HRESULT(__stdcall*)(PluginMetadataNative* metadata);

struct PluginLibraryExports {
    PluginQueryMetadataFn queryMetadata = nullptr;
    PluginCreateFn createInstance = nullptr;
    PluginDestroyFn destroyInstance = nullptr;
};

}  // namespace qttabbar::plugins

