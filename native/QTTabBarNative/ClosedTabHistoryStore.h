#pragma once

#include <windows.h>

#include <deque>
#include <string>

namespace qttabbar {

class ClosedTabHistoryStore {
public:
    static ClosedTabHistoryStore& Instance();

    std::deque<std::wstring> Load() const;
    void Save(const std::deque<std::wstring>& history) const;
    void Clear() const;

private:
    ClosedTabHistoryStore() = default;
};

} // namespace qttabbar

