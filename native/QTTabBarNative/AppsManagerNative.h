#pragma once

#include <windows.h>

#include <mutex>
#include <string>
#include <vector>


struct DesktopApplicationInfo;

namespace qttabbar {

struct UserAppEntryNative {
    std::wstring name;
    std::wstring path;
    std::wstring arguments;
    std::wstring workingDirectory;
    UINT shortcutKey = 0;
    int childrenCount = -1;

    bool IsFolder() const noexcept { return childrenCount >= 0; }
};

struct UserAppMenuNodeNative {
    UserAppEntryNative app;
    bool isFolder = false;
    size_t sequentialIndex = 0;
    size_t span = 1;
    std::vector<UserAppMenuNodeNative> children;
};

struct AppExecutionContextNative {
    std::wstring currentDirectory;
    std::vector<std::wstring> selectedFiles;
    std::vector<std::wstring> selectedDirectories;
    HWND parentWindow = nullptr;
};

class AppsManagerNative {
public:
    static AppsManagerNative& Instance();

    void Reload();
    std::vector<UserAppMenuNodeNative> GetRootNodes() const;
    bool MoveRootNodeUp(size_t rootIndex);
    bool MoveRootNodeDown(size_t rootIndex);
    bool Execute(const UserAppEntryNative& entry, const AppExecutionContextNative& context) const;

    std::vector<DesktopApplicationInfo> BuildDesktopApplications() const;

private:
    AppsManagerNative();

    void LoadFromRegistry();
    void SaveToRegistry() const;

    std::vector<UserAppMenuNodeNative> BuildNodes(size_t& offset, size_t maxCount) const;

    static std::wstring ExpandEnvironment(const std::wstring& value);
    static std::wstring ReplaceVariables(const std::wstring& input, const std::wstring& currentDirectory,
                                         const std::wstring& directories, const std::wstring& files,
                                         const std::wstring& both);
    static std::wstring ReplaceVariables(const std::wstring& input, const std::wstring& currentDirectory,
                                         const std::wstring& firstDirectory);

    mutable std::mutex mutex_;
    std::vector<UserAppEntryNative> apps_;
};

}  // namespace qttabbar

