#include "pch.h"
#include "InstanceManagerNative.h"

#include "InstanceManager.h"

#include "QTButtonBar.h"
#include "HookMessages.h"
#include "QTDesktopTool.h"
#include "QTTabBarClass.h"
#include "AppsManagerNative.h"
#include "RecentFileHistoryNative.h"
#include "Config.h"

#include <algorithm>
#include <cstddef>

InstanceManagerNative& InstanceManagerNative::Instance() {
    static InstanceManagerNative instance;
    return instance;
}

InstanceManagerNative::InstanceManagerNative() {
    InstanceManager::Instance().EnsureInitialized();
    InstanceManager::Instance().SetSelectionCallback([this](HWND tabBarHwnd, int index) {
        OnSelectionRequested(tabBarHwnd, index);
    });

    qttabbar::ConfigData config = qttabbar::LoadConfigFromRegistry();
    qttabbar::AppsManagerNative::Instance().Reload();
    SetDesktopApplications(qttabbar::AppsManagerNative::Instance().BuildDesktopApplications());
    qttabbar::RecentFileHistoryNative::Instance().Reload(config.misc.fileHistoryCount);
    SetDesktopRecentFiles(qttabbar::RecentFileHistoryNative::Instance().GetRecentFiles());
}

void InstanceManagerNative::RegisterTabBar(HWND explorerHwnd, QTTabBarClass* tabBar) {
    if(explorerHwnd == nullptr || tabBar == nullptr) {
        return;
    }
    std::scoped_lock lock(mutex_);
    Entry& entry = map_[explorerHwnd];
    entry.tabBar = tabBar;

    InstanceManager::Instance().RegisterTabHost(explorerHwnd, tabBar);
}

void InstanceManagerNative::UnregisterTabBar(QTTabBarClass* tabBar) {
    if(tabBar == nullptr) {
        return;
    }
    InstanceManager::Instance().UnregisterTabHost(tabBar);
    std::scoped_lock lock(mutex_);
    for(auto it = map_.begin(); it != map_.end();) {
        if(it->second.tabBar == tabBar) {
            it->second.tabBar = nullptr;
            if(it->second.buttonBar == nullptr) {
                it = map_.erase(it);
                continue;
            }
        }
        ++it;
    }
}

void InstanceManagerNative::RegisterButtonBar(HWND explorerHwnd, QTButtonBar* buttonBar) {
    if(explorerHwnd == nullptr || buttonBar == nullptr) {
        return;
    }
    std::scoped_lock lock(mutex_);
    Entry& entry = map_[explorerHwnd];
    entry.buttonBar = buttonBar;

    InstanceManager::Instance().RegisterButtonBar(explorerHwnd, buttonBar);
}

void InstanceManagerNative::UnregisterButtonBar(QTButtonBar* buttonBar) {
    if(buttonBar == nullptr) {
        return;
    }
    InstanceManager::Instance().UnregisterButtonBar(buttonBar);
    std::scoped_lock lock(mutex_);
    for(auto it = map_.begin(); it != map_.end();) {
        if(it->second.buttonBar == buttonBar) {
            it->second.buttonBar = nullptr;
            if(it->second.tabBar == nullptr) {
                it = map_.erase(it);
                continue;
            }
        }
        ++it;
    }
}

void InstanceManagerNative::RegisterDesktopTool(QTDesktopTool* desktopTool) {
    if(desktopTool == nullptr) {
        return;
    }
    std::scoped_lock lock(desktopMutex_);
    if(std::find(desktopTools_.begin(), desktopTools_.end(), desktopTool) == desktopTools_.end()) {
        desktopTools_.push_back(desktopTool);
    }

    InstanceManager::Instance().RegisterDesktopTool(desktopTool);
}

void InstanceManagerNative::UnregisterDesktopTool(QTDesktopTool* desktopTool) {
    if(desktopTool == nullptr) {
        return;
    }
    std::scoped_lock lock(desktopMutex_);
    desktopTools_.erase(std::remove(desktopTools_.begin(), desktopTools_.end(), desktopTool), desktopTools_.end());

    InstanceManager::Instance().UnregisterDesktopTool(desktopTool);
}

QTTabBarClass* InstanceManagerNative::FindTabBar(HWND explorerHwnd) const {
    if(explorerHwnd == nullptr) {
        return nullptr;
    }
    std::scoped_lock lock(mutex_);
    auto it = map_.find(explorerHwnd);
    if(it == map_.end()) {
        return nullptr;
    }
    return it->second.tabBar;
}

QTButtonBar* InstanceManagerNative::FindButtonBar(HWND explorerHwnd) const {
    if(explorerHwnd == nullptr) {
        return nullptr;
    }
    std::scoped_lock lock(mutex_);
    auto it = map_.find(explorerHwnd);
    if(it == map_.end()) {
        return nullptr;
    }
    return it->second.buttonBar;
}

void InstanceManagerNative::NotifyButtonCommand(HWND explorerHwnd, UINT commandId) {
    if(auto* tabBar = FindTabBar(explorerHwnd); tabBar != nullptr) {
        tabBar->HandleButtonCommand(commandId);
    }
}

size_t InstanceManagerNative::GetTabBarCount() const {
    std::scoped_lock lock(mutex_);
    size_t count = 0;
    for(const auto& [_, entry] : map_) {
        if(entry.tabBar != nullptr) {
            ++count;
        }
    }
    return count;
}

std::vector<QTTabBarClass*> InstanceManagerNative::EnumerateTabBars() const {
    std::scoped_lock lock(mutex_);
    std::vector<QTTabBarClass*> result;
    result.reserve(map_.size());
    for(const auto& [_, entry] : map_) {
        if(entry.tabBar != nullptr) {
            result.push_back(entry.tabBar);
        }
    }
    return result;
}

void InstanceManagerNative::SetDesktopGroups(std::vector<DesktopGroupInfo> groups) {
    std::vector<QTDesktopTool*> listeners;
    {
        std::scoped_lock lock(desktopMutex_);
        desktopGroups_ = std::move(groups);
        listeners = desktopTools_;
    }
    InstanceManager::Instance().Broadcast(L"desktop.groups", L"", {});
    for(auto* tool : listeners) {
        if(tool) {
            tool->InvalidateModel();
        }
    }
}

void InstanceManagerNative::SetDesktopApplications(std::vector<DesktopApplicationInfo> applications) {
    std::vector<QTDesktopTool*> listeners;
    {
        std::scoped_lock lock(desktopMutex_);
        desktopApplications_ = std::move(applications);
        listeners = desktopTools_;
    }
    InstanceManager::Instance().Broadcast(L"desktop.apps", L"", {});
    for(auto* tool : listeners) {
        if(tool) {
            tool->InvalidateModel();
        }
    }
}

void InstanceManagerNative::SetDesktopRecentFiles(std::vector<std::wstring> files) {
    std::vector<QTDesktopTool*> listeners;
    {
        std::scoped_lock lock(desktopMutex_);
        desktopRecentFiles_ = std::move(files);
        listeners = desktopTools_;
    }
    InstanceManager::Instance().Broadcast(L"desktop.recent", L"", {});
    for(auto* tool : listeners) {
        if(tool) {
            tool->InvalidateModel();
        }
    }
}

void InstanceManagerNative::OnSelectionRequested(HWND tabBarHwnd, int index) {
    if(tabBarHwnd == nullptr || index < 0) {
        return;
    }
    QTTabBarClass* tabBar = nullptr;
    {
        std::scoped_lock lock(mutex_);
        for(const auto& [_, entry] : map_) {
            if(entry.tabBar != nullptr && entry.tabBar->GetWindowHandle() == tabBarHwnd) {
                tabBar = entry.tabBar;
                break;
            }
        }
    }
    if(tabBar != nullptr) {
        ::PostMessageW(tabBar->GetWindowHandle(), qttabbar::hooks::WM_APP_TRAY_SELECT, static_cast<WPARAM>(index), 0);
    }
}

std::vector<DesktopGroupInfo> InstanceManagerNative::GetDesktopGroups() const {
    std::scoped_lock lock(desktopMutex_);
    return desktopGroups_;
}

std::vector<DesktopApplicationInfo> InstanceManagerNative::GetDesktopApplications() const {
    std::scoped_lock lock(desktopMutex_);
    return desktopApplications_;
}

std::vector<std::wstring> InstanceManagerNative::GetDesktopRecentFiles() const {
    std::scoped_lock lock(desktopMutex_);
    return desktopRecentFiles_;
}

