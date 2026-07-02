#include "pch.h"
#include "GroupsManagerNative.h"

#include "InstanceManagerNative.h"

#include <Shlwapi.h>
#include <algorithm>
#include <unordered_set>

using qttabbar::GroupsManagerNative;

namespace {

constexpr const wchar_t kGroupsRoot[] = L"Software\\QTTabBar\\Groups";

struct CaseInsensitiveHash {
    std::size_t operator()(const std::wstring& value) const noexcept {
        std::wstring lowered;
        lowered.resize(value.size());
        std::transform(value.begin(), value.end(), lowered.begin(), ::towlower);
        return std::hash<std::wstring>{}(lowered);
    }
};

struct CaseInsensitiveEqual {
    bool operator()(const std::wstring& lhs, const std::wstring& rhs) const noexcept {
        return _wcsicmp(lhs.c_str(), rhs.c_str()) == 0;
    }
};

} // namespace

namespace qttabbar {

GroupsManagerNative& GroupsManagerNative::Instance() {
    static GroupsManagerNative instance;
    return instance;
}

GroupsManagerNative::GroupsManagerNative() {
    LoadFromRegistry();
    NotifyObservers();
}

std::optional<GroupEntry> GroupsManagerNative::GetGroupByName(const std::wstring& name) const {
    auto it = std::find_if(groups_.begin(), groups_.end(), [&](const GroupEntry& group) {
        return _wcsicmp(group.name.c_str(), name.c_str()) == 0;
    });
    if(it != groups_.end()) {
        return *it;
    }
    return std::nullopt;
}

std::optional<GroupEntry> GroupsManagerNative::GetGroupByIndex(std::size_t index) const {
    if(index >= groups_.size()) {
        return std::nullopt;
    }
    return groups_[index];
}

bool GroupsManagerNative::AddGroup(const std::wstring& name, const std::vector<std::wstring>& paths, bool startup,
                                   UINT shortcut) {
    if(name.empty() || GetGroupByName(name).has_value()) {
        return false;
    }
    GroupEntry entry;
    entry.name = name;
    entry.paths = NormalizePaths(paths);
    entry.startup = startup;
    entry.shortcut = shortcut;
    groups_.push_back(std::move(entry));
    SaveToRegistry();
    NotifyObservers();
    return true;
}

bool GroupsManagerNative::RenameGroup(const std::wstring& oldName, const std::wstring& newName) {
    if(newName.empty() || _wcsicmp(oldName.c_str(), newName.c_str()) == 0) {
        return false;
    }
    if(GetGroupByName(newName).has_value()) {
        return false;
    }
    auto it = std::find_if(groups_.begin(), groups_.end(), [&](const GroupEntry& group) {
        return _wcsicmp(group.name.c_str(), oldName.c_str()) == 0;
    });
    if(it == groups_.end()) {
        return false;
    }
    it->name = newName;
    SaveToRegistry();
    NotifyObservers();
    return true;
}

bool GroupsManagerNative::RemoveGroup(const std::wstring& name) {
    auto it = std::find_if(groups_.begin(), groups_.end(), [&](const GroupEntry& group) {
        return _wcsicmp(group.name.c_str(), name.c_str()) == 0;
    });
    if(it == groups_.end()) {
        return false;
    }
    groups_.erase(it);
    SaveToRegistry();
    NotifyObservers();
    return true;
}

bool GroupsManagerNative::AppendPaths(const std::wstring& name, const std::vector<std::wstring>& paths) {
    if(paths.empty()) {
        return false;
    }
    auto it = std::find_if(groups_.begin(), groups_.end(), [&](const GroupEntry& group) {
        return _wcsicmp(group.name.c_str(), name.c_str()) == 0;
    });
    if(it == groups_.end()) {
        return false;
    }
    std::vector<std::wstring> normalized = NormalizePaths(paths);
    std::unordered_set<std::wstring, CaseInsensitiveHash, CaseInsensitiveEqual> existing(it->paths.begin(), it->paths.end());
    bool changed = false;
    for(const auto& path : normalized) {
        if(path.empty()) {
            continue;
        }
        if(existing.insert(path).second) {
            it->paths.push_back(path);
            changed = true;
        }
    }
    if(changed) {
        SaveToRegistry();
        NotifyObservers();
    }
    return changed;
}

void GroupsManagerNative::Reorder(const std::vector<std::wstring>& orderedNames) {
    std::vector<GroupEntry> reordered;
    reordered.reserve(groups_.size());
    for(const auto& name : orderedNames) {
        auto it = std::find_if(groups_.begin(), groups_.end(), [&](const GroupEntry& group) {
            return _wcsicmp(group.name.c_str(), name.c_str()) == 0;
        });
        if(it != groups_.end()) {
            reordered.push_back(*it);
        }
    }
    for(const auto& group : groups_) {
        auto it = std::find_if(reordered.begin(), reordered.end(), [&](const GroupEntry& entry) {
            return _wcsicmp(entry.name.c_str(), group.name.c_str()) == 0;
        });
        if(it == reordered.end()) {
            reordered.push_back(group);
        }
    }
    groups_.swap(reordered);
    SaveToRegistry();
    NotifyObservers();
}

void GroupsManagerNative::Reload() {
    LoadFromRegistry();
    NotifyObservers();
}

void GroupsManagerNative::LoadFromRegistry() {
    groups_.clear();

    CRegKey root;
    LONG status = root.Open(HKEY_CURRENT_USER, kGroupsRoot, KEY_READ);
    if(status != ERROR_SUCCESS) {
        ATLTRACE(L"GroupsManagerNative::LoadFromRegistry failed to open '%s': %ld\n", kGroupsRoot, status);
        return;
    }

    for(DWORD index = 0;; ++index) {
        wchar_t subName[16] = {};
        _snwprintf_s(subName, std::size(subName), L"%u", index);
        CRegKey groupKey;
        status = groupKey.Open(root, subName, KEY_READ);
        if(status != ERROR_SUCCESS) {
            if(status != ERROR_FILE_NOT_FOUND) {
                ATLTRACE(L"GroupsManagerNative::LoadFromRegistry failed to open group '%s': %ld\n", subName, status);
            }
            break;
        }

        ULONG chars = 0;
        status = groupKey.QueryStringValue(L"", nullptr, &chars);
        if(status != ERROR_SUCCESS || chars == 0) {
            ATLTRACE(L"GroupsManagerNative::LoadFromRegistry missing name for group '%s': %ld\n", subName, status);
            continue;
        }
        std::wstring name(chars, L'\0');
        status = groupKey.QueryStringValue(L"", name.data(), &chars);
        if(status != ERROR_SUCCESS) {
            ATLTRACE(L"GroupsManagerNative::LoadFromRegistry failed to read name for group '%s': %ld\n", subName, status);
            continue;
        }
        if(!name.empty() && name.back() == L'\0') {
            name.pop_back();
        }
        if(name.empty()) {
            continue;
        }

        GroupEntry entry;
        entry.name = name;

        DWORD shortcut = 0;
        if(groupKey.QueryDWORDValue(L"key", shortcut) == ERROR_SUCCESS) {
            entry.shortcut = shortcut;
        }
        WCHAR buffer[4];
        ULONG bufferChars = 4;
        if(groupKey.QueryStringValue(L"startup", buffer, &bufferChars) == ERROR_SUCCESS) {
            entry.startup = true;
        }

        for(DWORD pathIndex = 0;; ++pathIndex) {
            wchar_t valueName[16] = {};
            _snwprintf_s(valueName, std::size(valueName), L"%u", pathIndex);
            ULONG pathChars = 0;
            if(groupKey.QueryStringValue(valueName, nullptr, &pathChars) != ERROR_SUCCESS || pathChars == 0) {
                break;
            }
            std::wstring path(pathChars, L'\0');
            if(groupKey.QueryStringValue(valueName, path.data(), &pathChars) == ERROR_SUCCESS) {
                if(!path.empty() && path.back() == L'\0') {
                    path.pop_back();
                }
                if(!path.empty()) {
                    entry.paths.push_back(path);
                }
            }
        }

        entry.paths = NormalizePaths(entry.paths);
        groups_.push_back(std::move(entry));
    }
}

void GroupsManagerNative::SaveToRegistry() const {
    LONG status = RegDeleteTreeW(HKEY_CURRENT_USER, kGroupsRoot);
    if(status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
        ATLTRACE(L"GroupsManagerNative::SaveToRegistry failed to clear '%s': %ld\n", kGroupsRoot, status);
    }
    CRegKey root;
    status = root.Create(HKEY_CURRENT_USER, kGroupsRoot);
    if(status != ERROR_SUCCESS) {
        ATLTRACE(L"GroupsManagerNative::SaveToRegistry failed to create '%s': %ld\n", kGroupsRoot, status);
        return;
    }
    for(DWORD index = 0; index < groups_.size(); ++index) {
        const auto& group = groups_[index];
        wchar_t subName[16] = {};
        _snwprintf_s(subName, std::size(subName), L"%u", index);
        CRegKey groupKey;
        status = groupKey.Create(root, subName);
        if(status != ERROR_SUCCESS) {
            ATLTRACE(L"GroupsManagerNative::SaveToRegistry failed to create group '%s': %ld\n", group.name.c_str(), status);
            continue;
        }
        status = groupKey.SetStringValue(L"", group.name.c_str());
        if(status != ERROR_SUCCESS) {
            ATLTRACE(L"GroupsManagerNative::SaveToRegistry failed to write name for '%s': %ld\n", group.name.c_str(), status);
            continue;
        }
        if(group.shortcut != 0) {
            status = groupKey.SetDWORDValue(L"key", group.shortcut);
            if(status != ERROR_SUCCESS) {
                ATLTRACE(L"GroupsManagerNative::SaveToRegistry failed to write shortcut for '%s': %ld\n", group.name.c_str(),
                         status);
            }
        }
        if(group.startup) {
            status = groupKey.SetStringValue(L"startup", L"");
            if(status != ERROR_SUCCESS) {
                ATLTRACE(L"GroupsManagerNative::SaveToRegistry failed to set startup flag for '%s': %ld\n", group.name.c_str(),
                         status);
            }
        }
        DWORD pathIndex = 0;
        for(const auto& path : group.paths) {
            if(path.empty()) {
                continue;
            }
            wchar_t valueName[16] = {};
            _snwprintf_s(valueName, std::size(valueName), L"%u", pathIndex++);
            status = groupKey.SetStringValue(valueName, path.c_str());
            if(status != ERROR_SUCCESS) {
                ATLTRACE(L"GroupsManagerNative::SaveToRegistry failed to persist path %u for '%s': %ld\n", pathIndex - 1,
                         group.name.c_str(), status);
            }
        }
    }
}

void GroupsManagerNative::NotifyObservers() const {
    std::vector<DesktopGroupInfo> desktopGroups;
    desktopGroups.reserve(groups_.size());
    for(const auto& group : groups_) {
        DesktopGroupInfo info;
        info.name = group.name;
        info.items = group.paths;
        desktopGroups.push_back(std::move(info));
    }
    InstanceManagerNative::Instance().SetDesktopGroups(std::move(desktopGroups));
}

std::vector<std::wstring> GroupsManagerNative::NormalizePaths(const std::vector<std::wstring>& paths) const {
    std::vector<std::wstring> result;
    std::unordered_set<std::wstring, CaseInsensitiveHash, CaseInsensitiveEqual> seen;
    for(const auto& path : paths) {
        if(path.empty()) {
            continue;
        }
        std::wstring trimmed = path;
        while(!trimmed.empty() && iswspace(trimmed.front())) {
            trimmed.erase(trimmed.begin());
        }
        while(!trimmed.empty() && iswspace(trimmed.back())) {
            trimmed.pop_back();
        }
        if(trimmed.empty()) {
            continue;
        }
        if(seen.insert(trimmed).second) {
            result.push_back(std::move(trimmed));
        }
    }
    return result;
}

} // namespace qttabbar

