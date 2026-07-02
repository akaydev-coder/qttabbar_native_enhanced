#pragma once

#include <windows.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class QTTabBarClass;
class QTButtonBar;
class QTDesktopTool;
class AutoLoaderNative;

// The native instance manager mirrors the managed InstanceManager.cs behaviour.
// It exposes registration APIs for UI components, marshals cross-process messages
// through a lightweight named-pipe based pub/sub layer and owns the native tray icon.
class InstanceManager {
public:
    struct TabSnapshot {
        std::wstring currentPath;
        std::vector<std::wstring> tabNames;
        std::vector<std::wstring> tabPaths;
    };

    enum class MessageType : uint32_t {
        Subscribe = 1,
        Unsubscribe,
        RegisterTabBar,
        UnregisterTabBar,
        RegisterButtonBar,
        UnregisterButtonBar,
        TabListChanged,
        SelectionChanged,
        ExecuteAction,
        Broadcast
    };

    struct Message {
        MessageType type{};
        HWND explorerHwnd{};
        HWND tabBarHwnd{};
        std::wstring payload;
        std::vector<uint8_t> binaryPayload;
        int intValue{};
    };

    using SubscriptionId = uint64_t;
    using MessageCallback = std::function<void(const Message&)>;
    using ActionHandler = std::function<void(const std::wstring&, const std::vector<uint8_t>&)>;
    using SelectionCallback = std::function<void(HWND, int)>;

    static InstanceManager& Instance();

    void EnsureInitialized();
    void Shutdown();

    // Registration APIs -----------------------------------------------------
    void RegisterTabHost(HWND explorerHwnd, QTTabBarClass* tabBar);
    void UnregisterTabHost(QTTabBarClass* tabBar);

    void RegisterButtonBar(HWND explorerHwnd, QTButtonBar* buttonBar);
    void UnregisterButtonBar(QTButtonBar* buttonBar);

    void RegisterDesktopTool(QTDesktopTool* desktopTool);
    void UnregisterDesktopTool(QTDesktopTool* desktopTool);

    void RegisterAutoLoader(AutoLoaderNative* loader);
    void UnregisterAutoLoader(AutoLoaderNative* loader);

    // Tray synchronisation --------------------------------------------------
    void PushTabList(HWND explorerHwnd, HWND tabBarHwnd, const TabSnapshot& snapshot);
    void RemoveFromTray(HWND tabBarHwnd);
    void NotifyExplorerVisibility(HWND tabBarHwnd, bool isVisible);
    void NotifySelectionChanged(HWND tabBarHwnd, int index);

    // Cross-process coordination -------------------------------------------
    SubscriptionId Subscribe(MessageCallback callback);
    void Unsubscribe(SubscriptionId token);
    void Publish(const Message& message);

    void RegisterActionHandler(const std::wstring& action, ActionHandler handler);
    void UnregisterActionHandler(const std::wstring& action);

    bool ExecuteOnMainProcess(const std::wstring& action, const std::wstring& payload,
                              const std::vector<uint8_t>& binaryPayload, bool doAsync);
    void ExecuteOnServerProcess(const std::wstring& action, const std::wstring& payload,
                                const std::vector<uint8_t>& binaryPayload, bool doAsync);
    void Broadcast(const std::wstring& action, const std::wstring& payload,
                   const std::vector<uint8_t>& binaryPayload);

    // Shared state ----------------------------------------------------------
    std::vector<std::wstring> GetSelectedTabs(HWND explorerHwnd) const;
    void SetSelectedTabs(HWND explorerHwnd, const std::vector<std::wstring>& selections);

    void SetSelectionCallback(SelectionCallback callback);

    void Log(const wchar_t* format, ...) const;

private:
    struct PipeMessageHeader {
        uint32_t magic;
        uint32_t version;
        MessageType type;
        uint64_t explorerHwnd;
        uint64_t tabBarHwnd;
        int32_t intValue;
        uint32_t payloadSize;
        uint32_t binarySize;
    };

    struct ClientContext {
        HANDLE pipe = INVALID_HANDLE_VALUE;
        std::atomic<bool> connected{false};
    };

    struct TabRegistration {
        HWND explorer{};
        QTTabBarClass* tabBar{};
    };

    struct ButtonBarRegistration {
        HWND explorer{};
        QTButtonBar* buttonBar{};
    };

    struct DesktopToolRegistration {
        QTDesktopTool* tool{};
    };

    struct AutoLoaderRegistration {
        AutoLoaderNative* loader{};
    };

    struct TrayInstance {
        HWND explorer{};
        HWND tabBar{};
        std::wstring currentPath;
        std::vector<std::wstring> tabNames;
        std::vector<std::wstring> tabPaths;
        int showWindowCode{};
    };

    class TrayIconManager;

    InstanceManager();
    ~InstanceManager();

    InstanceManager(const InstanceManager&) = delete;
    InstanceManager& operator=(const InstanceManager&) = delete;

    void StartServerThread();
    void StopServerThread();
    void ServerThreadProc();
    void HandleClient(HANDLE pipe);
    void HandlePipeMessage(const Message& message, HANDLE senderPipe);
    void SendPipeMessage(HANDLE pipe, const Message& message);
    void BroadcastPipeMessage(const Message& message, HANDLE senderPipe);

    Message DeserializeMessage(const PipeMessageHeader& header, const std::vector<uint8_t>& payload,
                               const std::vector<uint8_t>& binaryPayload) const;

    std::wstring FormatExplorerKey(HWND explorerHwnd) const;

    void UpdateTrayIcon(std::function<void(TrayIconManager&)> action);

    static constexpr uint32_t kPipeMagic = 0x51545442; // 'QTTB'
    static constexpr uint32_t kPipeVersion = 1;
    static constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\QTTabBar_InstanceManager";
    static constexpr wchar_t kServerMutexName[] = L"Global\\QTTabBar_InstanceManager_Server";

    mutable std::shared_mutex m_tabLock;
    std::unordered_map<QTTabBarClass*, TabRegistration> m_tabRegistrations;
    std::unordered_map<QTButtonBar*, ButtonBarRegistration> m_buttonRegistrations;
    std::vector<DesktopToolRegistration> m_desktopTools;
    std::vector<AutoLoaderRegistration> m_autoLoaders;

    mutable std::shared_mutex m_selectionLock;
    std::unordered_map<std::wstring, std::vector<std::wstring>> m_selectedTabs;

    mutable std::mutex m_subscriptionMutex;
    std::vector<std::pair<SubscriptionId, MessageCallback>> m_subscribers;
    SubscriptionId m_nextSubscriptionId = 1;

    mutable std::mutex m_actionMutex;
    std::unordered_map<std::wstring, ActionHandler> m_actionHandlers;

    mutable std::mutex m_clientMutex;
    std::vector<std::unique_ptr<ClientContext>> m_clients;

    std::once_flag m_initOnce;
    HANDLE m_serverMutex = nullptr;
    std::thread m_serverThread;
    std::atomic<bool> m_stopRequested{false};
    bool m_isServer = false;

    std::unique_ptr<TrayIconManager> m_trayIconManager;

    SelectionCallback m_selectionCallback;
    mutable std::mutex m_selectionCallbackMutex;

    void EnsureTray();
};

