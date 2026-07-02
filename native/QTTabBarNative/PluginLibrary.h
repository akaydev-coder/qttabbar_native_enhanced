#pragma once

#include <windows.h>

#include <optional>
#include <string>

#include "PluginContracts.h"

namespace qttabbar::plugins {

class PluginLibrary {
public:
    PluginLibrary() = default;
    PluginLibrary(std::wstring path, bool enabled);
    PluginLibrary(const PluginLibrary&) = delete;
    PluginLibrary& operator=(const PluginLibrary&) = delete;
    PluginLibrary(PluginLibrary&& other) noexcept;
    PluginLibrary& operator=(PluginLibrary&& other) noexcept;
    ~PluginLibrary();

    bool Load();
    void Unload();

    const PluginMetadataNative& Metadata() const { return metadata_; }
    bool IsLoaded() const { return module_ != nullptr; }
    const std::wstring& Path() const { return path_; }

    bool ManagedFallback() const { return metadata_.managedFallback != FALSE; }
    void SetEnabled(bool enabled) { metadata_.enabled = enabled ? TRUE : FALSE; }

    std::optional<PluginLibraryExports> Exports() const;
    bool SupportsInstantiation() const;
    HRESULT CreateInstance(void** instance, const PluginClientVTable** vtable);
    void DestroyInstance(void* instance);

private:
    void ResetMetadata();

    std::wstring path_;
    HMODULE module_ = nullptr;
    PluginMetadataNative metadata_{};
    PluginLibraryExports exports_{};
};

}  // namespace qttabbar::plugins

