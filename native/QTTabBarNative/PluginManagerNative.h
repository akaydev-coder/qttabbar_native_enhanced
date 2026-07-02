#pragma once

#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "PluginLibrary.h"

namespace qttabbar::plugins {

class PluginManagerNative {
public:
    static PluginManagerNative& Instance();

    void Refresh();
    std::vector<PluginMetadataNative> EnumerateMetadata() const;
    bool SetEnabled(const std::wstring& pluginId, bool enabled);
    bool IsEnabled(const std::wstring& pluginId) const;
    HRESULT CreateInstance(const std::wstring& pluginId, void** instance, const PluginClientVTable** vtable);
    void DestroyInstance(void* instance);
    bool DispatchMenuClick(void* instance, PluginMenuType menuType, const wchar_t* menuText, void* tabContext);
    bool DispatchOption(void* instance);
    bool DispatchShortcut(void* instance, int index);
    HRESULT DispatchOpen(void* instance, void* pluginServer, void* shellBrowser);
    HRESULT DispatchQueryShortcuts(void* instance, wchar_t*** actions, int* count);
    bool DispatchHasOption(void* instance) const;
    void DispatchClose(void* instance, PluginEndCode endCode);

private:
    PluginManagerNative();

    struct InstanceRecord {
        void* instance = nullptr;
        const PluginClientVTable* clientVTable = nullptr;
        PluginLibrary* library = nullptr;
        std::wstring pluginId;
    };

    void LoadFromRegistry();
    void LoadEnabledList();
    void PersistEnabledList() const;
    void PersistPluginPaths() const;
    void EnsureInitializedLocked() const;
    void DestroyAllInstancesLocked();
    PluginLibrary* FindLibraryById(const std::wstring& pluginId);
    const PluginLibrary* FindLibraryById(const std::wstring& pluginId) const;
    InstanceRecord* FindInstance(void* instance);
    const InstanceRecord* FindInstance(void* instance) const;

    std::wstring MakePluginId(const PluginMetadataNative& metadata) const;

    mutable std::mutex mutex_;
    mutable bool initialized_ = false;
    mutable bool pluginPathsDirty_ = false;
    mutable bool enabledListDirty_ = false;
    mutable std::vector<PluginLibrary> libraries_;
    mutable std::vector<std::wstring> pluginPaths_;
    mutable std::vector<std::wstring> enabledIds_;
    mutable std::unordered_map<void*, InstanceRecord> liveInstances_;
};

}  // namespace qttabbar::plugins

