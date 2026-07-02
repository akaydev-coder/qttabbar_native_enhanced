#include "pch.h"

#include "PluginManagerNative.h"

#include <shlwapi.h>

#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <functional>
#include <string_view>

#include "PluginContracts.h"

#pragma comment(lib, "Shlwapi.lib")

namespace qttabbar::plugins {

namespace {
constexpr wchar_t kRegistryRoot[] = L"Software\\QTTabBar";
constexpr wchar_t kPluginPathsKey[] = L"Plugins\\Paths";
constexpr wchar_t kPluginEnabledKey[] = L"Plugins\\Enabled";

bool CaseInsensitiveEquals(const std::wstring& lhs, const std::wstring& rhs) {
    return _wcsicmp(lhs.c_str(), rhs.c_str()) == 0;
}

std::wstring ReadRegistryString(HKEY key, const wchar_t* name) {
    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS) {
        return {};
    }
    if (type != REG_SZ && type != REG_EXPAND_SZ) {
        return {};
    }
    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(value.data()), &size) != ERROR_SUCCESS) {
        return {};
    }
    if (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    return value;
}

void WriteRegistryString(HKEY key, const wchar_t* name, const std::wstring& value) {
    DWORD size = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), size);
}

void ClearRegistryValues(HKEY key) {
    wchar_t name[260];
    DWORD nameLength = ARRAYSIZE(name);
    while (RegEnumValueW(key, 0, name, &nameLength, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        RegDeleteValueW(key, name);
        nameLength = ARRAYSIZE(name);
    }
}

std::vector<std::wstring> ReadRegistryStrings(HKEY key) {
    DWORD index = 0;
    std::vector<std::wstring> values;
    wchar_t name[260];
    DWORD nameLength = ARRAYSIZE(name);
    while (RegEnumValueW(key, index++, name, &nameLength, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        values.emplace_back(ReadRegistryString(key, name));
        nameLength = ARRAYSIZE(name);
    }
    return values;
}

void WriteRegistryStrings(HKEY key, const std::vector<std::wstring>& values) {
    DWORD index = 0;
    for (const auto& value : values) {
        wchar_t name[32];
        _snwprintf_s(name, _TRUNCATE, L"%u", index++);
        WriteRegistryString(key, name, value);
    }
}

bool IsQtTabBarInfrastructureLibrary(const std::filesystem::path& path) {
    std::wstring fileName = path.filename().wstring();
    return _wcsicmp(fileName.c_str(), L"Interop.SHDocVw.dll") == 0 ||
           _wcsicmp(fileName.c_str(), L"BandObjectLib.dll") == 0 ||
           _wcsicmp(fileName.c_str(), L"QTPluginLib.dll") == 0 ||
           _wcsicmp(fileName.c_str(), L"QTTabBar.dll") == 0 ||
           _wcsicmp(fileName.c_str(), L"QTTabBarNative.dll") == 0 ||
           _wcsicmp(fileName.c_str(), L"QTHookLib32.dll") == 0 ||
           _wcsicmp(fileName.c_str(), L"QTHookLib64.dll") == 0 ||
           _wcsicmp(fileName.c_str(), L"ExplorerBgTool.dll") == 0;
}

std::vector<std::wstring> DiscoverProgramDataPlugins() {
    std::vector<std::wstring> results;
    wchar_t buffer[MAX_PATH];
    DWORD length = GetEnvironmentVariableW(L"ProgramData", buffer, ARRAYSIZE(buffer));
    if (length == 0 || length >= ARRAYSIZE(buffer)) {
        return results;
    }

    std::filesystem::path base(std::wstring(buffer, length));
    base /= L"QTTabBar";

    std::error_code ec;
    if (!std::filesystem::exists(base, ec) || !std::filesystem::is_directory(base, ec)) {
        return results;
    }

    for (const auto& entry : std::filesystem::directory_iterator(base, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        std::filesystem::path path = entry.path();
        if (_wcsicmp(path.extension().c_str(), L".dll") != 0) {
            continue;
        }
        if (IsQtTabBarInfrastructureLibrary(path)) {
            continue;
        }
        results.push_back(path.wstring());
    }

    std::sort(results.begin(), results.end(), [](const std::wstring& lhs, const std::wstring& rhs) {
        return _wcsicmp(lhs.c_str(), rhs.c_str()) < 0;
    });

    results.erase(std::unique(results.begin(), results.end(), CaseInsensitiveEquals), results.end());
    return results;
}

bool ContainsPath(const std::vector<std::wstring>& list, const std::wstring& candidate) {
    return std::any_of(list.begin(), list.end(), [&](const std::wstring& value) {
        return CaseInsensitiveEquals(value, candidate);
    });
}

}  // namespace

PluginManagerNative& PluginManagerNative::Instance() {
    static PluginManagerNative instance;
    return instance;
}

PluginManagerNative::PluginManagerNative() = default;

void PluginManagerNative::Refresh() {
    std::scoped_lock lock(mutex_);
    DestroyAllInstancesLocked();
    LoadFromRegistry();
    LoadEnabledList();
    initialized_ = true;
    if (pluginPathsDirty_) {
        PersistPluginPaths();
    }
    if (enabledListDirty_) {
        PersistEnabledList();
    }
}

std::vector<PluginMetadataNative> PluginManagerNative::EnumerateMetadata() const {
    std::scoped_lock lock(mutex_);
    EnsureInitializedLocked();
    std::vector<PluginMetadataNative> metadata;
    metadata.reserve(libraries_.size());
    for (const auto& lib : libraries_) {
        metadata.push_back(lib.Metadata());
    }
    return metadata;
}

bool PluginManagerNative::SetEnabled(const std::wstring& pluginId, bool enabled) {
    std::scoped_lock lock(mutex_);
    EnsureInitializedLocked();
    PluginLibrary* library = FindLibraryById(pluginId);
    if (library == nullptr) {
        return false;
    }
    library->SetEnabled(enabled);
    auto it = std::find_if(enabledIds_.begin(), enabledIds_.end(), [&](const std::wstring& id) {
        return CaseInsensitiveEquals(id, pluginId);
    });
    if (enabled) {
        if (it == enabledIds_.end()) {
            enabledIds_.push_back(pluginId);
            enabledListDirty_ = true;
        }
    } else {
        if (it != enabledIds_.end()) {
            enabledIds_.erase(it);
            enabledListDirty_ = true;
        }
    }
    if (enabledListDirty_) {
        PersistEnabledList();
    }
    return true;
}

bool PluginManagerNative::IsEnabled(const std::wstring& pluginId) const {
    std::scoped_lock lock(mutex_);
    EnsureInitializedLocked();
    return std::any_of(enabledIds_.begin(), enabledIds_.end(), [&](const std::wstring& id) {
        return CaseInsensitiveEquals(id, pluginId);
    });
}

void PluginManagerNative::LoadFromRegistry() {
    libraries_.clear();
    pluginPaths_.clear();
    pluginPathsDirty_ = false;

    HKEY root = nullptr;
    std::vector<std::wstring> registryPaths;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryRoot, 0, KEY_READ, &root) == ERROR_SUCCESS) {
        HKEY pathsKey = nullptr;
        if (RegOpenKeyExW(root, kPluginPathsKey, 0, KEY_READ, &pathsKey) == ERROR_SUCCESS) {
            registryPaths = ReadRegistryStrings(pathsKey);
            RegCloseKey(pathsKey);
        }
        RegCloseKey(root);
    }

    std::vector<std::wstring> discoveredPaths = DiscoverProgramDataPlugins();

    for (const auto& path : registryPaths) {
        if (path.empty()) {
            continue;
        }
        if (!PathFileExistsW(path.c_str())) {
            pluginPathsDirty_ = true;
            continue;
        }
        if (IsQtTabBarInfrastructureLibrary(std::filesystem::path(path))) {
            pluginPathsDirty_ = true;
            continue;
        }
        if (!ContainsPath(pluginPaths_, path)) {
            pluginPaths_.push_back(path);
        }
    }

    for (const auto& path : discoveredPaths) {
        if (!ContainsPath(pluginPaths_, path)) {
            pluginPaths_.push_back(path);
            pluginPathsDirty_ = true;
        }
    }

    for (const auto& path : pluginPaths_) {
        PluginLibrary library(path, false);
        if (library.Load()) {
            libraries_.push_back(std::move(library));
        } else {
            pluginPathsDirty_ = true;
        }
    }
}

void PluginManagerNative::LoadEnabledList() {
    enabledIds_.clear();
    enabledListDirty_ = false;

    HKEY root = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryRoot, 0, KEY_READ, &root) != ERROR_SUCCESS) {
        return;
    }

    HKEY enabledKey = nullptr;
    if (RegOpenKeyExW(root, kPluginEnabledKey, 0, KEY_READ, &enabledKey) != ERROR_SUCCESS) {
        RegCloseKey(root);
        return;
    }

    std::vector<std::wstring> ids = ReadRegistryStrings(enabledKey);
    RegCloseKey(enabledKey);
    RegCloseKey(root);

    for (const auto& id : ids) {
        if (FindLibraryById(id) != nullptr) {
            enabledIds_.push_back(id);
        } else {
            enabledListDirty_ = true;
        }
    }

    for (auto& library : libraries_) {
        std::wstring id = MakePluginId(library.Metadata());
        bool enabled = std::any_of(enabledIds_.begin(), enabledIds_.end(), [&](const std::wstring& stored) {
            return CaseInsensitiveEquals(stored, id);
        });
        library.SetEnabled(enabled);
    }
}

void PluginManagerNative::PersistEnabledList() const {
    HKEY root = nullptr;
    RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryRoot, 0, nullptr, 0, KEY_WRITE, nullptr, &root, nullptr);
    if (root == nullptr) {
        return;
    }
    HKEY enabledKey = nullptr;
    RegCreateKeyExW(root, kPluginEnabledKey, 0, nullptr, 0, KEY_WRITE, nullptr, &enabledKey, nullptr);
    if (enabledKey != nullptr) {
        ClearRegistryValues(enabledKey);
        WriteRegistryStrings(enabledKey, enabledIds_);
        RegCloseKey(enabledKey);
    }
    RegCloseKey(root);
    enabledListDirty_ = false;
}

void PluginManagerNative::PersistPluginPaths() const {
    HKEY root = nullptr;
    RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryRoot, 0, nullptr, 0, KEY_WRITE, nullptr, &root, nullptr);
    if (root == nullptr) {
        return;
    }
    HKEY pathsKey = nullptr;
    RegCreateKeyExW(root, kPluginPathsKey, 0, nullptr, 0, KEY_WRITE, nullptr, &pathsKey, nullptr);
    if (pathsKey != nullptr) {
        ClearRegistryValues(pathsKey);
        WriteRegistryStrings(pathsKey, pluginPaths_);
        RegCloseKey(pathsKey);
    }
    RegCloseKey(root);
    pluginPathsDirty_ = false;
}

void PluginManagerNative::EnsureInitializedLocked() const {
    if (initialized_) {
        return;
    }
    auto* self = const_cast<PluginManagerNative*>(this);
    self->LoadFromRegistry();
    self->LoadEnabledList();
    initialized_ = true;
    if (pluginPathsDirty_) {
        self->PersistPluginPaths();
    }
    if (enabledListDirty_) {
        self->PersistEnabledList();
    }
}

void PluginManagerNative::DestroyAllInstancesLocked() {
    for (auto& entry : liveInstances_) {
        if (entry.second.library != nullptr && entry.second.instance != nullptr) {
            entry.second.library->DestroyInstance(entry.second.instance);
        }
    }
    liveInstances_.clear();
}

HRESULT PluginManagerNative::CreateInstance(const std::wstring& pluginId, void** instance, const PluginClientVTable** vtable) {
    if (instance == nullptr || vtable == nullptr) {
        return E_POINTER;
    }

    *instance = nullptr;
    *vtable = nullptr;

    std::scoped_lock lock(mutex_);
    EnsureInitializedLocked();

    PluginLibrary* library = FindLibraryById(pluginId);
    if (library == nullptr) {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    if (!library->Metadata().enabled) {
        return HRESULT_FROM_WIN32(ERROR_ACCESS_DISABLED_BY_POLICY);
    }
    if (library->ManagedFallback() || !library->SupportsInstantiation()) {
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    }

    HRESULT hr = library->CreateInstance(instance, vtable);
    if (FAILED(hr)) {
        return hr;
    }

    InstanceRecord record;
    record.instance = *instance;
    record.clientVTable = *vtable;
    record.library = library;
    record.pluginId = pluginId;
    liveInstances_[record.instance] = record;
    return hr;
}

void PluginManagerNative::DestroyInstance(void* instance) {
    if (instance == nullptr) {
        return;
    }
    std::scoped_lock lock(mutex_);
    auto it = liveInstances_.find(instance);
    if (it == liveInstances_.end()) {
        return;
    }
    if (it->second.library != nullptr) {
        it->second.library->DestroyInstance(instance);
    }
    liveInstances_.erase(it);
}

PluginManagerNative::InstanceRecord* PluginManagerNative::FindInstance(void* instance) {
    auto it = liveInstances_.find(instance);
    if (it == liveInstances_.end()) {
        return nullptr;
    }
    return &it->second;
}

const PluginManagerNative::InstanceRecord* PluginManagerNative::FindInstance(void* instance) const {
    auto it = liveInstances_.find(instance);
    if (it == liveInstances_.end()) {
        return nullptr;
    }
    return &it->second;
}

bool PluginManagerNative::DispatchMenuClick(void* instance, PluginMenuType menuType, const wchar_t* menuText, void* tabContext) {
    std::scoped_lock lock(mutex_);
    auto* record = FindInstance(instance);
    if (record == nullptr || record->clientVTable == nullptr || record->clientVTable->OnMenuItemClick == nullptr) {
        return false;
    }
    record->clientVTable->OnMenuItemClick(record->instance, menuType, menuText, tabContext);
    return true;
}

bool PluginManagerNative::DispatchOption(void* instance) {
    std::scoped_lock lock(mutex_);
    auto* record = FindInstance(instance);
    if (record == nullptr || record->clientVTable == nullptr || record->clientVTable->OnOption == nullptr) {
        return false;
    }
    record->clientVTable->OnOption(record->instance);
    return true;
}

bool PluginManagerNative::DispatchShortcut(void* instance, int index) {
    std::scoped_lock lock(mutex_);
    auto* record = FindInstance(instance);
    if (record == nullptr || record->clientVTable == nullptr || record->clientVTable->OnShortcutKeyPressed == nullptr) {
        return false;
    }
    record->clientVTable->OnShortcutKeyPressed(record->instance, index);
    return true;
}

HRESULT PluginManagerNative::DispatchOpen(void* instance, void* pluginServer, void* shellBrowser) {
    std::scoped_lock lock(mutex_);
    auto* record = FindInstance(instance);
    if (record == nullptr || record->clientVTable == nullptr || record->clientVTable->Open == nullptr) {
        return E_POINTER;
    }
    return record->clientVTable->Open(record->instance, pluginServer, shellBrowser);
}

HRESULT PluginManagerNative::DispatchQueryShortcuts(void* instance, wchar_t*** actions, int* count) {
    if (actions == nullptr || count == nullptr) {
        return E_POINTER;
    }
    std::scoped_lock lock(mutex_);
    auto* record = FindInstance(instance);
    if (record == nullptr || record->clientVTable == nullptr || record->clientVTable->QueryShortcutKeys == nullptr) {
        return E_POINTER;
    }
    return record->clientVTable->QueryShortcutKeys(record->instance, actions, count);
}

bool PluginManagerNative::DispatchHasOption(void* instance) const {
    std::scoped_lock lock(mutex_);
    auto* record = FindInstance(instance);
    if (record == nullptr || record->clientVTable == nullptr || record->clientVTable->HasOption == nullptr) {
        return false;
    }
    return record->clientVTable->HasOption(record->instance) != FALSE;
}

void PluginManagerNative::DispatchClose(void* instance, PluginEndCode endCode) {
    std::scoped_lock lock(mutex_);
    auto* record = FindInstance(instance);
    if (record == nullptr || record->clientVTable == nullptr || record->clientVTable->Close == nullptr) {
        return;
    }
    record->clientVTable->Close(record->instance, endCode);
}

PluginLibrary* PluginManagerNative::FindLibraryById(const std::wstring& pluginId) {
    auto it = std::find_if(libraries_.begin(), libraries_.end(), [&](PluginLibrary& library) {
        return CaseInsensitiveEquals(MakePluginId(library.Metadata()), pluginId);
    });
    return it != libraries_.end() ? &(*it) : nullptr;
}

const PluginLibrary* PluginManagerNative::FindLibraryById(const std::wstring& pluginId) const {
    auto it = std::find_if(libraries_.begin(), libraries_.end(), [&](const PluginLibrary& library) {
        return CaseInsensitiveEquals(MakePluginId(library.Metadata()), pluginId);
    });
    return it != libraries_.end() ? &(*it) : nullptr;
}

std::wstring PluginManagerNative::MakePluginId(const PluginMetadataNative& metadata) const {
    std::wstring_view pathView(metadata.libraryPath);
    size_t hashValue = std::hash<std::wstring_view>{}(pathView);
    wchar_t hashBuffer[32] = {};
    _snwprintf_s(hashBuffer, _TRUNCATE, L"%IX", static_cast<unsigned int>(hashValue));

    std::wstring id = metadata.name;
    id.append(metadata.version);
    id.push_back(L'(');
    id.append(hashBuffer);
    id.push_back(L')');
    return id;
}

}  // namespace qttabbar::plugins
