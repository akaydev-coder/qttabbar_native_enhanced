#pragma once

#include <windows.h>

#include <mutex>
#include <unordered_map>
#include <vector>
#include <string>

class QTTabBarClass;
class QTButtonBar;
class QTDesktopTool;

struct DesktopGroupInfo {
    std::wstring name;
    std::vector<std::wstring> items;
};

struct DesktopApplicationInfo {
    std::wstring name;
    std::wstring command;
    std::wstring arguments;
    std::wstring workingDirectory;
};

class InstanceManagerNative {
public:
    static InstanceManagerNative& Instance();

    void RegisterTabBar(HWND explorerHwnd, QTTabBarClass* tabBar);
    void UnregisterTabBar(QTTabBarClass* tabBar);
    void RegisterButtonBar(HWND explorerHwnd, QTButtonBar* buttonBar);
    void UnregisterButtonBar(QTButtonBar* buttonBar);
    void RegisterDesktopTool(QTDesktopTool* desktopTool);
    void UnregisterDesktopTool(QTDesktopTool* desktopTool);

    QTTabBarClass* FindTabBar(HWND explorerHwnd) const;
    QTButtonBar* FindButtonBar(HWND explorerHwnd) const;

    void NotifyButtonCommand(HWND explorerHwnd, UINT commandId);

    size_t GetTabBarCount() const;
    std::vector<QTTabBarClass*> EnumerateTabBars() const;

    void SetDesktopGroups(std::vector<DesktopGroupInfo> groups);
    void SetDesktopApplications(std::vector<DesktopApplicationInfo> applications);
    void SetDesktopRecentFiles(std::vector<std::wstring> files);

    std::vector<DesktopGroupInfo> GetDesktopGroups() const;
    std::vector<DesktopApplicationInfo> GetDesktopApplications() const;
    std::vector<std::wstring> GetDesktopRecentFiles() const;

private:
    InstanceManagerNative();

    void OnSelectionRequested(HWND tabBarHwnd, int index);

    struct Entry {
        QTTabBarClass* tabBar = nullptr;
        QTButtonBar* buttonBar = nullptr;
    };

    mutable std::mutex mutex_;
    std::unordered_map<HWND, Entry> map_;

    mutable std::mutex desktopMutex_;
    std::vector<DesktopGroupInfo> desktopGroups_;
    std::vector<DesktopApplicationInfo> desktopApplications_;
    std::vector<std::wstring> desktopRecentFiles_;
    std::vector<QTDesktopTool*> desktopTools_;
};

