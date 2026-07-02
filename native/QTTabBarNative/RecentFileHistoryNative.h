#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace qttabbar {

class RecentFileHistoryNative {
public:
    static RecentFileHistoryNative& Instance();

    void Reload(int capacity);
    void Add(const std::wstring& path);
    void Clear();
    std::vector<std::wstring> GetRecentFiles() const;

private:
    RecentFileHistoryNative();

    void LoadFromRegistry();
    void SaveToRegistry() const;
    void TrimToCapacity();

    mutable std::mutex mutex_;
    int capacity_;
    std::vector<std::wstring> history_;
};

}  // namespace qttabbar

