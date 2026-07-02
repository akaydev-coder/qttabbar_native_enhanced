#pragma once

#include <atlbase.h>
#include <shobjidl.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

class TaskbarTabPreviewManager final {
public:
    struct Entry {
        std::wstring title;
        std::wstring path;
        HICON icon = nullptr;
        bool active = false;
    };

    TaskbarTabPreviewManager() noexcept;
    ~TaskbarTabPreviewManager();

    void SetOwner(HWND ownerHwnd,
                  std::function<void(std::size_t)> activateCallback,
                  std::function<void(std::size_t)> closeCallback);
    void SetEnabled(bool enabled);
    void Update(const std::vector<Entry>& entries);
    void Clear();

private:
    struct ProxyWindow {
        HWND hwnd = nullptr;
        HICON icon = nullptr;
        std::size_t index = 0;
        bool active = false;
        std::wstring title;
        std::wstring path;
        TaskbarTabPreviewManager* owner = nullptr;
    };

    static constexpr UINT kWmDwmSendIconicThumbnail = 0x0323;
    static constexpr UINT kWmDwmSendIconicLivePreviewBitmap = 0x0326;

    HRESULT EnsureTaskbar();
    HWND ResolveOwnerWindow() const;
    bool EnsureProxyCount(std::size_t count);
    bool CreateProxy(std::size_t index);
    void DestroyProxy(std::unique_ptr<ProxyWindow>& proxy);
    void RegisterProxy(ProxyWindow& proxy, HWND previous);
    void UpdateProxy(ProxyWindow& proxy, const Entry& entry, std::size_t index);
    void UpdateTaskbarOrderAndActive();
    void ActivateProxy(std::size_t index);
    void CloseProxy(std::size_t index);
    HBITMAP CreatePreviewBitmap(const ProxyWindow& proxy, int width, int height) const;
    void SetProxyIcon(ProxyWindow& proxy, HICON icon);

    static ATOM RegisterProxyClass();
    static LRESULT CALLBACK ProxyWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleProxyMessage(ProxyWindow& proxy, UINT message, WPARAM wParam, LPARAM lParam);

    CComPtr<ITaskbarList3> m_taskbar;
    HWND m_ownerHwnd = nullptr;
    bool m_enabled = false;
    bool m_taskbarReady = false;
    std::function<void(std::size_t)> m_activateCallback;
    std::function<void(std::size_t)> m_closeCallback;
    std::vector<std::unique_ptr<ProxyWindow>> m_proxies;
};
