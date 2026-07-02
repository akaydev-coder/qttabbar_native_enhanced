#include "pch.h"
#include "AliasStoreNative.h"

#include <atlbase.h>
#include <algorithm>

namespace {
constexpr const wchar_t kAliasRoot[] = L"Software\\QTTabBar\\TabAliases";
}

namespace qttabbar {

AliasStoreNative& AliasStoreNative::Instance() {
    static AliasStoreNative instance;
    return instance;
}

std::optional<std::wstring> AliasStoreNative::GetAlias(const std::wstring& path) const {
    std::wstring key = Normalize(path);
    if(key.empty()) {
        return std::nullopt;
    }
    CRegKey reg;
    if(reg.Open(HKEY_CURRENT_USER, kAliasRoot, KEY_READ) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    ULONG chars = 0;
    if(reg.QueryStringValue(key.c_str(), nullptr, &chars) != ERROR_SUCCESS || chars == 0) {
        return std::nullopt;
    }
    std::wstring value(chars, L'\0');
    if(reg.QueryStringValue(key.c_str(), value.data(), &chars) == ERROR_SUCCESS) {
        if(!value.empty() && value.back() == L'\0') {
            value.pop_back();
        }
        if(!value.empty()) {
            return value;
        }
    }
    return std::nullopt;
}

void AliasStoreNative::SetAlias(const std::wstring& path, const std::wstring& alias) {
    std::wstring key = Normalize(path);
    if(key.empty()) {
        return;
    }
    if(alias.empty()) {
        ClearAlias(path);
        return;
    }
    CRegKey reg;
    if(reg.Create(HKEY_CURRENT_USER, kAliasRoot) != ERROR_SUCCESS) {
        return;
    }
    reg.SetStringValue(key.c_str(), alias.c_str());
}

void AliasStoreNative::ClearAlias(const std::wstring& path) {
    std::wstring key = Normalize(path);
    if(key.empty()) {
        return;
    }
    CRegKey reg;
    if(reg.Open(HKEY_CURRENT_USER, kAliasRoot, KEY_SET_VALUE) != ERROR_SUCCESS) {
        return;
    }
    reg.DeleteValue(key.c_str());
}

std::wstring AliasStoreNative::Normalize(const std::wstring& path) const {
    std::wstring result = path;
    std::transform(result.begin(), result.end(), result.begin(), ::towlower);
    return result;
}

} // namespace qttabbar

