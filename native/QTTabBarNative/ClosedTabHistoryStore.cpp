#include "pch.h"
#include "ClosedTabHistoryStore.h"

#include <atlbase.h>

namespace {
constexpr const wchar_t kHistoryRoot[] = L"Software\\QTTabBar\\RecentlyClosed";
}

namespace qttabbar {

ClosedTabHistoryStore& ClosedTabHistoryStore::Instance() {
    static ClosedTabHistoryStore instance;
    return instance;
}

std::deque<std::wstring> ClosedTabHistoryStore::Load() const {
    std::deque<std::wstring> history;
    CRegKey key;
    if(key.Open(HKEY_CURRENT_USER, kHistoryRoot, KEY_READ) != ERROR_SUCCESS) {
        return history;
    }
    for(DWORD index = 0;; ++index) {
        wchar_t valueName[16] = {};
        _snwprintf_s(valueName, std::size(valueName), L"%u", index);
        ULONG chars = 0;
        if(key.QueryStringValue(valueName, nullptr, &chars) != ERROR_SUCCESS || chars == 0) {
            break;
        }
        std::wstring path(chars, L'\0');
        if(key.QueryStringValue(valueName, path.data(), &chars) == ERROR_SUCCESS) {
            if(!path.empty() && path.back() == L'\0') {
                path.pop_back();
            }
            if(!path.empty()) {
                history.push_back(std::move(path));
            }
        }
    }
    return history;
}

void ClosedTabHistoryStore::Save(const std::deque<std::wstring>& history) const {
    RegDeleteTreeW(HKEY_CURRENT_USER, kHistoryRoot);
    CRegKey key;
    if(key.Create(HKEY_CURRENT_USER, kHistoryRoot) != ERROR_SUCCESS) {
        return;
    }
    DWORD index = 0;
    for(const auto& path : history) {
        if(path.empty()) {
            continue;
        }
        wchar_t valueName[16] = {};
        _snwprintf_s(valueName, std::size(valueName), L"%u", index++);
        key.SetStringValue(valueName, path.c_str());
    }
}

void ClosedTabHistoryStore::Clear() const {
    RegDeleteTreeW(HKEY_CURRENT_USER, kHistoryRoot);
}

} // namespace qttabbar

