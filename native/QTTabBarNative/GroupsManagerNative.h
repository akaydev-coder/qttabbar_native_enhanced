#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <vector>

struct DesktopGroupInfo;

namespace qttabbar {

struct GroupEntry {
    std::wstring name;
    std::vector<std::wstring> paths;
    bool startup = false;
    UINT shortcut = 0;
};

class GroupsManagerNative {
public:
    static GroupsManagerNative& Instance();

    const std::vector<GroupEntry>& GetGroups() const noexcept { return groups_; }
    std::optional<GroupEntry> GetGroupByName(const std::wstring& name) const;
    std::optional<GroupEntry> GetGroupByIndex(std::size_t index) const;

    bool AddGroup(const std::wstring& name, const std::vector<std::wstring>& paths, bool startup = false,
                  UINT shortcut = 0);
    bool RenameGroup(const std::wstring& oldName, const std::wstring& newName);
    bool RemoveGroup(const std::wstring& name);
    bool AppendPaths(const std::wstring& name, const std::vector<std::wstring>& paths);
    void Reorder(const std::vector<std::wstring>& orderedNames);

    void Reload();

private:
    GroupsManagerNative();

    void LoadFromRegistry();
    void SaveToRegistry() const;
    void NotifyObservers() const;
    std::vector<std::wstring> NormalizePaths(const std::vector<std::wstring>& paths) const;

    std::vector<GroupEntry> groups_;
};

} // namespace qttabbar

