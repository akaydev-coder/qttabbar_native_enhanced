#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include <shlobj.h>

namespace qttabbar {
struct ConfigData;
}  // namespace qttabbar

class QTTabBarClass;

namespace qttabbar::hooks {

class IHookEventSink {
public:
    virtual ~IHookEventSink() = default;
    virtual void OnHookResult(int hookId, int retcode) = 0;
    virtual BOOL OnHookNewWindow(PCIDLIST_ABSOLUTE pidl) = 0;
};

class HookManagerNative {
public:
    static HookManagerNative& Instance();

    void OnTabBarSiteAssigned(QTTabBarClass* tabBar);
    void OnTabBarSiteCleared(QTTabBarClass* tabBar);

    void ReloadConfiguration();
    void ReloadConfiguration(const ConfigData& config);

    void RegisterSink(IHookEventSink* sink);
    void UnregisterSink(IHookEventSink* sink);

private:
    HookManagerNative();

    static constexpr size_t kHookCount = 19;

    static void __stdcall HookResultThunk(int hookId, int retcode, void* context);
    static BOOL __stdcall NewWindowThunk(PCIDLIST_ABSOLUTE pidl, void* context);

    void HandleHookResult(int hookId, int retcode);
    BOOL HandleNewWindow(PCIDLIST_ABSOLUTE pidl);

    void ApplyHookStateLocked();
    std::wstring ResolveHookLibraryPathUnlocked();
    std::wstring PidlToPath(PCIDLIST_ABSOLUTE pidl) const;
    void LogError(const std::wstring& message) const;

    std::mutex mutex_;
    std::array<std::atomic<int>, kHookCount> hookStatus_;
    std::atomic<bool> libraryLoaded_;
    std::atomic<bool> autoHookEnabled_;
    std::atomic<bool> captureNewWindowsEnabled_;
    std::atomic<bool> serverBlocked_;
    bool serverWarningLogged_;
    bool missingLibraryLogged_;
    std::vector<IHookEventSink*> sinks_;
};

}  // namespace qttabbar::hooks

