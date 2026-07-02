#include "pch.h"

#include "PluginLibrary.h"

#include <shlwapi.h>

#include <cstring>
#include <cwchar>
#include <memory>

#pragma comment(lib, "Shlwapi.lib")

namespace qttabbar::plugins {

namespace {
constexpr char kQueryMetadataProc[] = "QTPlugin_QueryMetadata";
constexpr char kCreateProc[] = "QTPlugin_CreateInstance";
constexpr char kDestroyProc[] = "QTPlugin_DestroyInstance";
}  // namespace

PluginLibrary::PluginLibrary(std::wstring path, bool enabled)
    : path_(std::move(path)) {
    ResetMetadata();
    metadata_.enabled = enabled ? TRUE : FALSE;
}

PluginLibrary::PluginLibrary(PluginLibrary&& other) noexcept {
    *this = std::move(other);
}

PluginLibrary& PluginLibrary::operator=(PluginLibrary&& other) noexcept {
    if (this != &other) {
        Unload();
        path_ = std::move(other.path_);
        module_ = other.module_;
        other.module_ = nullptr;
        metadata_ = other.metadata_;
        exports_ = other.exports_;
        other.ResetMetadata();
    }
    return *this;
}

PluginLibrary::~PluginLibrary() {
    Unload();
}

void PluginLibrary::ResetMetadata() {
    std::memset(&metadata_, 0, sizeof(metadata_));
    metadata_.type = PluginType::Interactive;
}

bool PluginLibrary::Load() {
    if (module_ != nullptr) {
        return true;
    }
    ResetMetadata();
    exports_ = {};

    if (path_.empty() || !PathFileExistsW(path_.c_str())) {
        return false;
    }

    module_ = ::LoadLibraryExW(path_.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (module_ == nullptr) {
        return false;
    }

    exports_.queryMetadata = reinterpret_cast<PluginQueryMetadataFn>(
        ::GetProcAddress(module_, kQueryMetadataProc));
    exports_.createInstance = reinterpret_cast<PluginCreateFn>(
        ::GetProcAddress(module_, kCreateProc));
    exports_.destroyInstance = reinterpret_cast<PluginDestroyFn>(
        ::GetProcAddress(module_, kDestroyProc));

    if (exports_.queryMetadata == nullptr) {
        Unload();
        return false;
    }

    if (FAILED(exports_.queryMetadata(&metadata_))) {
        Unload();
        return false;
    }

    if (metadata_.libraryPath[0] == L'\0') {
        wcsncpy_s(metadata_.libraryPath, path_.c_str(), _TRUNCATE);
    }

    return true;
}

void PluginLibrary::Unload() {
    if (module_ != nullptr) {
        ::FreeLibrary(module_);
        module_ = nullptr;
    }
    exports_ = {};
    ResetMetadata();
}

std::optional<PluginLibraryExports> PluginLibrary::Exports() const {
    if (module_ == nullptr) {
        return std::nullopt;
    }
    return exports_;
}

bool PluginLibrary::SupportsInstantiation() const {
    return exports_.createInstance != nullptr && exports_.destroyInstance != nullptr;
}

HRESULT PluginLibrary::CreateInstance(void** instance, const PluginClientVTable** vtable) {
    if (instance == nullptr || vtable == nullptr) {
        return E_POINTER;
    }

    *instance = nullptr;
    *vtable = nullptr;

    if (module_ == nullptr) {
        return E_FAIL;
    }

    if (!SupportsInstantiation()) {
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    }

    HRESULT hr = exports_.createInstance(instance, vtable);
    if (FAILED(hr)) {
        *instance = nullptr;
        *vtable = nullptr;
        return hr;
    }

    if (*instance == nullptr || *vtable == nullptr) {
        *instance = nullptr;
        *vtable = nullptr;
        return E_UNEXPECTED;
    }

    return hr;
}

void PluginLibrary::DestroyInstance(void* instance) {
    if (module_ == nullptr || instance == nullptr) {
        return;
    }
    if (exports_.destroyInstance != nullptr) {
        exports_.destroyInstance(instance);
    }
}

}  // namespace qttabbar::plugins
