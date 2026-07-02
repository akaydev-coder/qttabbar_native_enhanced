#include "pch.h"
#include "AppsManagerNative.h"

#include <ShlObj.h>
#include <Shellapi.h>
#include <Shlwapi.h>

#include <algorithm>
#include <array>
#include <cstring>

#include "Config.h"
#include "InstanceManagerNative.h"
#include "RecentFileHistoryNative.h"

namespace {
constexpr const wchar_t kAppsRoot[] = L"Software\\QTTabBar\\UserApps";

bool CaseInsensitiveEquals(const std::wstring& lhs, const std::wstring& rhs) {
    return _wcsicmp(lhs.c_str(), rhs.c_str()) == 0;
}

std::wstring ToLower(const std::wstring& value) {
    std::wstring result(value);
    std::transform(result.begin(), result.end(), result.begin(), ::towlower);
    return result;
}

size_t FindCaseInsensitive(const std::wstring& haystack, const std::wstring& needle, size_t start) {
    if(needle.empty() || haystack.empty() || start >= haystack.size()) {
        return std::wstring::npos;
    }
    std::wstring loweredHaystack = ToLower(haystack);
    std::wstring loweredNeedle = ToLower(needle);
    return loweredHaystack.find(loweredNeedle, start);
}

std::wstring ReplaceCaseInsensitive(const std::wstring& input, const std::wstring& token, const std::wstring& replacement) {
    if(token.empty()) {
        return input;
    }
    std::wstring result;
    size_t position = 0;
    while(position < input.size()) {
        size_t found = FindCaseInsensitive(input, token, position);
        if(found == std::wstring::npos) {
            result.append(input.substr(position));
            break;
        }
        result.append(input.substr(position, found - position));
        result.append(replacement);
        position = found + token.size();
    }
    return result;
}

}  // namespace

namespace qttabbar {

AppsManagerNative& AppsManagerNative::Instance() {
    static AppsManagerNative instance;
    return instance;
}

AppsManagerNative::AppsManagerNative() {
    Reload();
}

void AppsManagerNative::Reload() {
    std::lock_guard guard(mutex_);
    LoadFromRegistry();
}

std::vector<UserAppMenuNodeNative> AppsManagerNative::GetRootNodes() const {
    std::lock_guard guard(mutex_);
    size_t offset = 0;
    return BuildNodes(offset, apps_.size());
}

bool AppsManagerNative::MoveRootNodeUp(size_t rootIndex) {
    std::lock_guard guard(mutex_);
    size_t offset = 0;
    auto nodes = BuildNodes(offset, apps_.size());
    if(rootIndex == 0 || rootIndex >= nodes.size()) {
        return false;
    }
    const auto& current = nodes[rootIndex];
    const auto& previous = nodes[rootIndex - 1];
    auto dest = apps_.begin() + static_cast<ptrdiff_t>(previous.sequentialIndex);
    auto first = apps_.begin() + static_cast<ptrdiff_t>(current.sequentialIndex);
    auto last = first + static_cast<ptrdiff_t>(current.span);
    std::rotate(dest, first, last);
    SaveToRegistry();
    InstanceManagerNative::Instance().SetDesktopApplications(BuildDesktopApplications());
    return true;
}

bool AppsManagerNative::MoveRootNodeDown(size_t rootIndex) {
    std::lock_guard guard(mutex_);
    size_t offset = 0;
    auto nodes = BuildNodes(offset, apps_.size());
    if(rootIndex + 1 >= nodes.size()) {
        return false;
    }
    const auto& current = nodes[rootIndex];
    const auto& next = nodes[rootIndex + 1];
    auto first = apps_.begin() + static_cast<ptrdiff_t>(current.sequentialIndex);
    auto last = first + static_cast<ptrdiff_t>(current.span);
    auto dest = apps_.begin() + static_cast<ptrdiff_t>(next.sequentialIndex + next.span);
    std::rotate(first, last, dest);
    SaveToRegistry();
    InstanceManagerNative::Instance().SetDesktopApplications(BuildDesktopApplications());
    return true;
}

bool AppsManagerNative::Execute(const UserAppEntryNative& entry, const AppExecutionContextNative& context) const {
    std::wstring command = ExpandEnvironment(entry.path);
    std::wstring arguments = ExpandEnvironment(entry.arguments);
    std::wstring workingDirectory = ExpandEnvironment(entry.workingDirectory);

    std::wstring currentDirectory = context.currentDirectory;
    if(currentDirectory.empty()) {
        currentDirectory = L"";
    }

    std::wstring filesJoined;
    for(size_t i = 0; i < context.selectedFiles.size(); ++i) {
        filesJoined.append(context.selectedFiles[i]);
        if(i + 1 < context.selectedFiles.size()) {
            filesJoined.append(L" ");
        }
    }

    std::wstring dirsJoined;
    for(size_t i = 0; i < context.selectedDirectories.size(); ++i) {
        dirsJoined.append(context.selectedDirectories[i]);
        if(i + 1 < context.selectedDirectories.size()) {
            dirsJoined.append(L" ");
        }
    }

    std::wstring bothJoined = filesJoined;
    if(!filesJoined.empty() && !dirsJoined.empty()) {
        bothJoined.append(L" ");
    }
    bothJoined.append(dirsJoined);

    arguments = ReplaceVariables(arguments, currentDirectory, dirsJoined, filesJoined, bothJoined);
    std::wstring firstDir = context.selectedDirectories.empty() ? currentDirectory : context.selectedDirectories.front();
    workingDirectory = ReplaceVariables(workingDirectory, currentDirectory, firstDir);

    SHELLEXECUTEINFOW exec{};
    exec.cbSize = sizeof(exec);
    exec.fMask = SEE_MASK_DOENVSUBST | SEE_MASK_FLAG_NO_UI | SEE_MASK_ASYNCOK;
    exec.hwnd = context.parentWindow;
    exec.lpFile = command.c_str();
    exec.lpParameters = arguments.empty() ? nullptr : arguments.c_str();
    exec.lpDirectory = workingDirectory.empty() ? nullptr : workingDirectory.c_str();
    exec.nShow = SW_SHOWNORMAL;

    if(!::ShellExecuteExW(&exec)) {
        DWORD error = ::GetLastError();
        wchar_t buffer[256] = {};
        _snwprintf_s(buffer, std::size(buffer), L"Failed to launch '%s' (error %lu)", command.c_str(), error);
        if(context.parentWindow != nullptr) {
            ::MessageBoxW(context.parentWindow, buffer, L"QTTabBar", MB_ICONERROR | MB_OK);
        }
        return false;
    }

    qttabbar::RecentFileHistoryNative::Instance().Add(command);
    return true;
}

std::vector<DesktopApplicationInfo> AppsManagerNative::BuildDesktopApplications() const {
    std::lock_guard guard(mutex_);
    std::vector<DesktopApplicationInfo> apps;
    apps.reserve(apps_.size());
    for(const auto& entry : apps_) {
        if(entry.IsFolder()) {
            continue;
        }
        DesktopApplicationInfo info;
        info.name = entry.name;
        info.command = entry.path;
        info.arguments = entry.arguments;
        info.workingDirectory = entry.workingDirectory;
        apps.push_back(std::move(info));
    }
    return apps;
}

void AppsManagerNative::LoadFromRegistry() {
    apps_.clear();

    CRegKey root;
    if(root.Open(HKEY_CURRENT_USER, kAppsRoot, KEY_READ) != ERROR_SUCCESS) {
        return;
    }

    for(DWORD index = 0;; ++index) {
        wchar_t subName[16] = {};
        _snwprintf_s(subName, std::size(subName), L"%u", index);
        CRegKey key;
        if(key.Open(root, subName, KEY_READ) != ERROR_SUCCESS) {
            break;
        }
        ULONG chars = 0;
        if(key.QueryStringValue(L"", nullptr, &chars) != ERROR_SUCCESS || chars == 0) {
            continue;
        }
        std::wstring name(chars, L'\0');
        if(key.QueryStringValue(L"", name.data(), &chars) != ERROR_SUCCESS) {
            continue;
        }
        if(!name.empty() && name.back() == L'\0') {
            name.pop_back();
        }

        UserAppEntryNative entry;
        entry.name = std::move(name);
        ULONG children = 0;
        if(key.QueryDWORDValue(L"children", children) == ERROR_SUCCESS) {
            entry.childrenCount = static_cast<int>(children);
        } else {
            entry.childrenCount = -1;
            chars = 0;
            if(key.QueryStringValue(L"path", nullptr, &chars) == ERROR_SUCCESS && chars > 0) {
                std::wstring path(chars, L'\0');
                if(key.QueryStringValue(L"path", path.data(), &chars) == ERROR_SUCCESS) {
                    if(!path.empty() && path.back() == L'\0') {
                        path.pop_back();
                    }
                    entry.path = std::move(path);
                }
            }
            chars = 0;
            if(key.QueryStringValue(L"args", nullptr, &chars) == ERROR_SUCCESS && chars > 0) {
                std::wstring args(chars, L'\0');
                if(key.QueryStringValue(L"args", args.data(), &chars) == ERROR_SUCCESS) {
                    if(!args.empty() && args.back() == L'\0') {
                        args.pop_back();
                    }
                    entry.arguments = std::move(args);
                }
            }
            chars = 0;
            if(key.QueryStringValue(L"wdir", nullptr, &chars) == ERROR_SUCCESS && chars > 0) {
                std::wstring wdir(chars, L'\0');
                if(key.QueryStringValue(L"wdir", wdir.data(), &chars) == ERROR_SUCCESS) {
                    if(!wdir.empty() && wdir.back() == L'\0') {
                        wdir.pop_back();
                    }
                    entry.workingDirectory = std::move(wdir);
                }
            }
            DWORD shortcut = 0;
            if(key.QueryDWORDValue(L"key", shortcut) == ERROR_SUCCESS) {
                entry.shortcutKey = shortcut;
            }
        }
        apps_.push_back(std::move(entry));
    }
}

void AppsManagerNative::SaveToRegistry() const {
    RegDeleteTreeW(HKEY_CURRENT_USER, kAppsRoot);

    CRegKey root;
    if(root.Create(HKEY_CURRENT_USER, kAppsRoot) != ERROR_SUCCESS) {
        return;
    }

    DWORD index = 0;
    for(const auto& entry : apps_) {
        wchar_t subName[16] = {};
        _snwprintf_s(subName, std::size(subName), L"%u", index++);
        CRegKey key;
        if(key.Create(root, subName) != ERROR_SUCCESS) {
            continue;
        }
        key.SetStringValue(L"", entry.name.c_str());
        if(entry.IsFolder()) {
            key.SetDWORDValue(L"children", static_cast<DWORD>(entry.childrenCount));
        } else {
            key.SetStringValue(L"path", entry.path.c_str());
            key.SetStringValue(L"args", entry.arguments.c_str());
            key.SetStringValue(L"wdir", entry.workingDirectory.c_str());
            if(entry.shortcutKey != 0) {
                key.SetDWORDValue(L"key", entry.shortcutKey);
            }
        }
    }
}

std::vector<UserAppMenuNodeNative> AppsManagerNative::BuildNodes(size_t& offset, size_t maxCount) const {
    std::vector<UserAppMenuNodeNative> nodes;
    size_t processed = 0;
    while(offset < apps_.size() && processed < maxCount) {
        const auto& entry = apps_[offset];
        UserAppMenuNodeNative node;
        node.app = entry;
        node.isFolder = entry.IsFolder();
        node.sequentialIndex = offset;
        ++offset;
        if(entry.IsFolder()) {
            node.children = BuildNodes(offset, static_cast<size_t>((std::max)(entry.childrenCount, 0)));
        }
        node.span = offset - node.sequentialIndex;
        nodes.push_back(std::move(node));
        ++processed;
    }
    return nodes;
}

std::wstring AppsManagerNative::ExpandEnvironment(const std::wstring& value) {
    if(value.empty()) {
        return value;
    }
    DWORD needed = ::ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
    if(needed == 0) {
        return value;
    }
    std::wstring buffer(needed, L'\0');
    if(::ExpandEnvironmentStringsW(value.c_str(), buffer.data(), needed) == 0) {
        return value;
    }
    if(!buffer.empty() && buffer.back() == L'\0') {
        buffer.pop_back();
    }
    return buffer;
}

std::wstring AppsManagerNative::ReplaceVariables(const std::wstring& input, const std::wstring& currentDirectory,
                                                 const std::wstring& directories, const std::wstring& files,
                                                 const std::wstring& both) {
    std::wstring result = input;
    result = ReplaceCaseInsensitive(result, L"%cd%", directories.empty() ? currentDirectory : directories);
    result = ReplaceCaseInsensitive(result, L"%c%", currentDirectory);
    result = ReplaceCaseInsensitive(result, L"%d%", directories);
    result = ReplaceCaseInsensitive(result, L"%f%", files);
    result = ReplaceCaseInsensitive(result, L"%s%", both);
    return result;
}

std::wstring AppsManagerNative::ReplaceVariables(const std::wstring& input, const std::wstring& currentDirectory,
                                                 const std::wstring& firstDirectory) {
    std::wstring result = input;
    std::wstring directory = firstDirectory.empty() ? currentDirectory : firstDirectory;
    result = ReplaceCaseInsensitive(result, L"%cd%", directory);
    result = ReplaceCaseInsensitive(result, L"%c%", currentDirectory);
    result = ReplaceCaseInsensitive(result, L"%d%", directory);
    result = ReplaceCaseInsensitive(result, L"%f%", L"");
    result = ReplaceCaseInsensitive(result, L"%s%", directory);
    return result;
}

}  // namespace qttabbar
