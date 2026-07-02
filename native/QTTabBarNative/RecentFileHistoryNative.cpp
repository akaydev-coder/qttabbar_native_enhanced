#include "pch.h"
#include "RecentFileHistoryNative.h"

#include <Shlwapi.h>

#include <algorithm>

#include "InstanceManagerNative.h"

namespace {
constexpr const wchar_t kRecentFilesRoot[] = L"Software\\QTTabBar\\RecentFiles";

bool CaseInsensitiveEquals(const std::wstring& lhs, const std::wstring& rhs) {
    return _wcsicmp(lhs.c_str(), rhs.c_str()) == 0;
}

}  // namespace

namespace qttabbar {

RecentFileHistoryNative& RecentFileHistoryNative::Instance() {
    static RecentFileHistoryNative instance;
    return instance;
}

RecentFileHistoryNative::RecentFileHistoryNative()
    : capacity_(15) {
    LoadFromRegistry();
}

void RecentFileHistoryNative::Reload(int capacity) {
    std::lock_guard guard(mutex_);
    capacity_ = (std::max)(1, capacity);
    LoadFromRegistry();
    TrimToCapacity();
}

void RecentFileHistoryNative::Add(const std::wstring& path) {
    if(path.empty()) {
        return;
    }
    std::wstring trimmed = path;
    trimmed.erase(std::remove(trimmed.begin(), trimmed.end(), L'\r'), trimmed.end());
    trimmed.erase(std::remove(trimmed.begin(), trimmed.end(), L'\n'), trimmed.end());
    if(trimmed.empty()) {
        return;
    }

    std::lock_guard guard(mutex_);
    history_.erase(std::remove_if(history_.begin(), history_.end(), [&](const std::wstring& value) {
                          return CaseInsensitiveEquals(value, trimmed);
                      }),
                  history_.end());
    history_.push_back(trimmed);
    TrimToCapacity();
    SaveToRegistry();
    InstanceManagerNative::Instance().SetDesktopRecentFiles(history_);
}

void RecentFileHistoryNative::Clear() {
    std::lock_guard guard(mutex_);
    history_.clear();
    SaveToRegistry();
    InstanceManagerNative::Instance().SetDesktopRecentFiles(history_);
}

std::vector<std::wstring> RecentFileHistoryNative::GetRecentFiles() const {
    std::lock_guard guard(mutex_);
    return history_;
}

void RecentFileHistoryNative::LoadFromRegistry() {
    history_.clear();
    CRegKey root;
    if(root.Open(HKEY_CURRENT_USER, kRecentFilesRoot, KEY_READ) != ERROR_SUCCESS) {
        return;
    }
    for(DWORD index = 0;; ++index) {
        wchar_t valueName[16] = {};
        _snwprintf_s(valueName, std::size(valueName), L"%u", index);
        ULONG chars = 0;
        if(root.QueryStringValue(valueName, nullptr, &chars) != ERROR_SUCCESS || chars == 0) {
            break;
        }
        std::wstring value(chars, L'\0');
        if(root.QueryStringValue(valueName, value.data(), &chars) == ERROR_SUCCESS) {
            if(!value.empty() && value.back() == L'\0') {
                value.pop_back();
            }
            if(!value.empty()) {
                history_.push_back(std::move(value));
            }
        }
    }
}

void RecentFileHistoryNative::SaveToRegistry() const {
    RegDeleteTreeW(HKEY_CURRENT_USER, kRecentFilesRoot);
    CRegKey root;
    if(root.Create(HKEY_CURRENT_USER, kRecentFilesRoot) != ERROR_SUCCESS) {
        return;
    }
    DWORD index = 0;
    for(const auto& entry : history_) {
        wchar_t valueName[16] = {};
        _snwprintf_s(valueName, std::size(valueName), L"%u", index++);
        root.SetStringValue(valueName, entry.c_str());
    }
}

void RecentFileHistoryNative::TrimToCapacity() {
    if(capacity_ <= 0) {
        capacity_ = 1;
    }
    if(history_.size() <= static_cast<size_t>(capacity_)) {
        return;
    }
    history_.erase(history_.begin(), history_.begin() + static_cast<ptrdiff_t>(history_.size() - capacity_));
}

}  // namespace qttabbar
