#pragma once

#include <windows.h>

#include <optional>
#include <string>

namespace qttabbar {

class AliasStoreNative {
public:
    static AliasStoreNative& Instance();

    std::optional<std::wstring> GetAlias(const std::wstring& path) const;
    void SetAlias(const std::wstring& path, const std::wstring& alias);
    void ClearAlias(const std::wstring& path);

private:
    AliasStoreNative() = default;
    std::wstring Normalize(const std::wstring& path) const;
};

} // namespace qttabbar

