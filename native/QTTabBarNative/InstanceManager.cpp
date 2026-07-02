#include "pch.h"
#include "InstanceManager.h"

#include "AutoLoaderNative.h"
#include "QTButtonBar.h"
#include "QTDesktopTool.h"
#include "QTTabBarClass.h"

#include <ShlObj.h>
#include <Shellapi.h>
#include <strsafe.h>

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <sstream>

namespace {
constexpr UINT WM_TRAYICON = WM_APP + 0x100;
constexpr UINT WM_TRAYCOMMAND = WM_APP + 0x101;
constexpr UINT ID_TRAYICON_BASE = 0x5000;
constexpr UINT ID_RESTORE_ALL = 0x5100;
constexpr UINT ID_CLOSE_ALL = 0x5101;

static_assert(sizeof(InstanceManager::MessageType) == sizeof(uint32_t),
    "MessageType must remain a 32-bit enum for IPC serialization.");

std::vector<uint8_t> PackActionPayload(const std::wstring& payload, const std::vector<uint8_t>& binary) {
    uint32_t length = static_cast<uint32_t>(payload.size());
    size_t payloadBytes = static_cast<size_t>(length) * sizeof(wchar_t);
    std::vector<uint8_t> buffer(sizeof(length) + payloadBytes + binary.size());
    std::memcpy(buffer.data(), &length, sizeof(length));
    if(payloadBytes > 0) {
        std::memcpy(buffer.data() + sizeof(length), payload.data(), payloadBytes);
    }
    if(!binary.empty()) {
        std::memcpy(buffer.data() + sizeof(length) + payloadBytes, binary.data(), binary.size());
    }
    return buffer;
}

void UnpackActionPayload(const std::vector<uint8_t>& buffer, std::wstring& payload, std::vector<uint8_t>& binary) {
    if(buffer.size() < sizeof(uint32_t)) {
        payload.clear();
        binary.clear();
        return;
    }
    uint32_t length = 0;
    std::memcpy(&length, buffer.data(), sizeof(length));
    size_t offset = sizeof(length);
    size_t payloadBytes = static_cast<size_t>(length) * sizeof(wchar_t);
    if(offset + payloadBytes > buffer.size()) {
        payloadBytes = buffer.size() - offset;
        length = static_cast<uint32_t>(payloadBytes / sizeof(wchar_t));
    }
    payload.assign(reinterpret_cast<const wchar_t*>(buffer.data() + offset), length);
    offset += payloadBytes;
    if(offset < buffer.size()) {
        binary.assign(buffer.begin() + static_cast<ptrdiff_t>(offset), buffer.end());
    } else {
        binary.clear();
    }
}
}

// -------------------------------------------------------------------------------------------------
// Tray icon host
// -------------------------------------------------------------------------------------------------
class InstanceManager::TrayIconManager {
public:
    explicit TrayIconManager(InstanceManager& owner)
        : m_owner(owner) {}

    ~TrayIconManager() { Shutdown(); }

    void EnsureThread();
    void Shutdown();

    void AddOrUpdate(const TrayInstance& instance);
    void Remove(HWND tabBar);
    void RestoreAll();
    void Show(HWND tabBar, bool show);

private:
    enum class CommandType {
        AddOrUpdate,
        Remove,
        RestoreAll,
        Show
    };

    struct Command {
        CommandType type;
        TrayInstance instance;
        HWND tabBar{};
        bool show{};
    };

    static LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void ThreadProc(std::promise<HWND> readySignal);
    void Post(std::unique_ptr<Command> command);
    void ProcessCommand(Command& command);
    void HandleTrayNotify(WPARAM wParam, LPARAM lParam);
    void HandleContextMenu();
    void HandleCommand(UINT commandId);
    void EnsureIcon();
    void UpdateIconVisibility();
    void UpdateToolTip();
    void ShowExplorerWindow(TrayInstance& instance, bool show);

    InstanceManager& m_owner;
    std::once_flag m_once;
    std::thread m_thread;
    HWND m_hwnd{};
    DWORD m_threadId{};
    bool m_iconVisible{false};
    HICON m_icon{};
    NOTIFYICONDATAW m_nid{};
    std::mutex m_mutex;
    std::unordered_map<HWND, TrayInstance> m_instances;
    std::unordered_map<UINT, std::pair<HWND, int>> m_tabSelectionMap;
};

void InstanceManager::TrayIconManager::EnsureThread() {
    std::call_once(m_once, [this]() {
        std::promise<HWND> ready;
        auto future = ready.get_future();
        m_thread = std::thread(&TrayIconManager::ThreadProc, this, std::move(ready));
        m_hwnd = future.get();
    });
}

void InstanceManager::TrayIconManager::Shutdown() {
    if(m_thread.joinable()) {
        PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
        m_thread.join();
    }
    m_hwnd = nullptr;
    m_threadId = 0;
}

void InstanceManager::TrayIconManager::AddOrUpdate(const TrayInstance& instance) {
    auto command = std::make_unique<Command>();
    command->type = CommandType::AddOrUpdate;
    command->instance = instance;
    Post(std::move(command));
}

void InstanceManager::TrayIconManager::Remove(HWND tabBar) {
    auto command = std::make_unique<Command>();
    command->type = CommandType::Remove;
    command->tabBar = tabBar;
    Post(std::move(command));
}

void InstanceManager::TrayIconManager::RestoreAll() {
    auto command = std::make_unique<Command>();
    command->type = CommandType::RestoreAll;
    Post(std::move(command));
}

void InstanceManager::TrayIconManager::Show(HWND tabBar, bool show) {
    auto command = std::make_unique<Command>();
    command->type = CommandType::Show;
    command->tabBar = tabBar;
    command->show = show;
    Post(std::move(command));
}

void InstanceManager::TrayIconManager::Post(std::unique_ptr<Command> command) {
    EnsureThread();
    if(!m_hwnd) {
        return;
    }
    PostMessageW(m_hwnd, WM_TRAYCOMMAND, reinterpret_cast<WPARAM>(command.release()), 0);
}

void InstanceManager::TrayIconManager::ThreadProc(std::promise<HWND> readySignal) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"QTTabBar.TrayIcon";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, this);
    m_hwnd = hwnd;
    m_threadId = GetCurrentThreadId();

    readySignal.set_value(hwnd);

    MSG msg{};
    while(GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if(msg.message == WM_TRAYCOMMAND) {
            std::unique_ptr<Command> command(reinterpret_cast<Command*>(msg.wParam));
            if(command) {
                ProcessCommand(*command);
            }
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if(m_iconVisible) {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_iconVisible = false;
    }
    if(m_icon) {
        DestroyIcon(m_icon);
        m_icon = nullptr;
    }

    CoUninitialize();
}

LRESULT CALLBACK InstanceManager::TrayIconManager::TrayWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    TrayIconManager* self = nullptr;
    if(message == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<TrayIconManager*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<TrayIconManager*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if(!self) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    switch(message) {
    case WM_TRAYICON:
        self->HandleTrayNotify(wParam, lParam);
        return 0;
    case WM_COMMAND:
        self->HandleCommand(LOWORD(wParam));
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

void InstanceManager::TrayIconManager::ProcessCommand(Command& command) {
    std::lock_guard lock(m_mutex);
    switch(command.type) {
    case CommandType::AddOrUpdate: {
        TrayInstance instance = command.instance;
        instance.showWindowCode = ::IsZoomed(instance.explorer) ? SW_MAXIMIZE : SW_SHOWNORMAL;
        m_instances[instance.tabBar] = std::move(instance);
        EnsureIcon();
        UpdateIconVisibility();
        UpdateToolTip();
        auto& stored = m_instances[command.instance.tabBar];
        ShowExplorerWindow(stored, false);
        break;
    }
    case CommandType::Remove: {
        auto it = m_instances.find(command.tabBar);
        if(it != m_instances.end()) {
            ShowExplorerWindow(it->second, true);
            m_instances.erase(it);
            UpdateIconVisibility();
            UpdateToolTip();
        }
        break;
    }
    case CommandType::RestoreAll:
        for(auto& [_, instance] : m_instances) {
            ShowExplorerWindow(instance, true);
        }
        m_instances.clear();
        UpdateIconVisibility();
        UpdateToolTip();
        break;
    case CommandType::Show: {
        auto it = m_instances.find(command.tabBar);
        if(it != m_instances.end()) {
            ShowExplorerWindow(it->second, command.show);
            if(command.show) {
                m_instances.erase(it);
                UpdateIconVisibility();
                UpdateToolTip();
            }
        }
        break;
    }
    }
}

void InstanceManager::TrayIconManager::HandleTrayNotify(WPARAM /*wParam*/, LPARAM lParam) {
    switch(lParam) {
    case WM_LBUTTONDBLCLK:
        RestoreAll();
        break;
    case WM_CONTEXTMENU:
    case WM_RBUTTONUP:
        HandleContextMenu();
        break;
    default:
        break;
    }
}

void InstanceManager::TrayIconManager::HandleContextMenu() {
    std::vector<std::pair<HWND, TrayInstance>> entries;
    entries.reserve(m_instances.size());
    {
        std::lock_guard lock(m_mutex);
        if(m_instances.empty()) {
            return;
        }
        for(const auto& [tabBar, instance] : m_instances) {
            entries.emplace_back(tabBar, instance);
        }
    }

    POINT pt{};
    GetCursorPos(&pt);

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_RESTORE_ALL, L"Restore all");
    AppendMenuW(menu, MF_STRING, ID_CLOSE_ALL, L"Close all");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    UINT currentId = ID_TRAYICON_BASE;
    std::unordered_map<UINT, std::pair<HWND, int>> commandMap;
    for(const auto& [tabBar, instance] : entries) {
        HMENU subMenu = CreatePopupMenu();
        for(size_t i = 0; i < instance.tabNames.size(); ++i) {
            const auto& name = instance.tabNames[i];
            AppendMenuW(subMenu, MF_STRING, currentId, name.c_str());
            commandMap[currentId] = {tabBar, static_cast<int>(i)};
            ++currentId;
        }
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(subMenu), instance.currentPath.c_str());
    }

    {
        std::lock_guard lock(m_mutex);
        m_tabSelectionMap = std::move(commandMap);
    }

    TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);
}

void InstanceManager::TrayIconManager::HandleCommand(UINT commandId) {
    if(commandId == ID_RESTORE_ALL) {
        RestoreAll();
        return;
    }
    if(commandId == ID_CLOSE_ALL) {
        std::lock_guard lock(m_mutex);
        for(const auto& [_, instance] : m_instances) {
            PostMessageW(instance.explorer, WM_CLOSE, 0, 0);
        }
        m_instances.clear();
        m_tabSelectionMap.clear();
        UpdateToolTip();
        UpdateIconVisibility();
        return;
    }
    std::pair<HWND, int> selection{};
    {
        std::lock_guard lock(m_mutex);
        auto it = m_tabSelectionMap.find(commandId);
        if(it != m_tabSelectionMap.end()) {
            selection = it->second;
        }
    }
    if(selection.first != nullptr) {
        Show(selection.first, true);
        m_owner.NotifySelectionChanged(selection.first, selection.second);
    }
}

void InstanceManager::TrayIconManager::EnsureIcon() {
    if(m_icon) {
        return;
    }
    m_icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APPLICATION));
    if(!m_icon) {
        m_icon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    ZeroMemory(&m_nid, sizeof(m_nid));
    m_nid.cbSize = sizeof(m_nid);
    m_nid.hWnd = m_hwnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAYICON;
    m_nid.hIcon = m_icon;
}

void InstanceManager::TrayIconManager::UpdateIconVisibility() {
    if(!m_hwnd) {
        return;
    }
    if(m_instances.empty()) {
        if(m_iconVisible) {
            Shell_NotifyIconW(NIM_DELETE, &m_nid);
            m_iconVisible = false;
        }
    } else {
        EnsureIcon();
        if(!m_iconVisible) {
            Shell_NotifyIconW(NIM_ADD, &m_nid);
            m_iconVisible = true;
        } else {
            Shell_NotifyIconW(NIM_MODIFY, &m_nid);
        }
    }
}

void InstanceManager::TrayIconManager::UpdateToolTip() {
    if(!m_iconVisible) {
        return;
    }
    std::wstring tip;
    size_t count = m_instances.size();
    if(count == 0) {
        tip = L"QTTabBar";
    } else if(count == 1) {
        tip = L"QTTabBar - 1 window";
    } else {
        tip = L"QTTabBar - " + std::to_wstring(count) + L" windows";
    }
    StringCchCopyW(m_nid.szTip, ARRAYSIZE(m_nid.szTip), tip.c_str());
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

void InstanceManager::TrayIconManager::ShowExplorerWindow(TrayInstance& instance, bool show) {
    if(show) {
        ShowWindow(instance.explorer, instance.showWindowCode);
        SetForegroundWindow(instance.explorer);
    } else {
        ShowWindow(instance.explorer, SW_HIDE);
    }
}

// -------------------------------------------------------------------------------------------------
// Instance manager
// -------------------------------------------------------------------------------------------------

InstanceManager& InstanceManager::Instance() {
    static InstanceManager instance;
    return instance;
}

InstanceManager::InstanceManager() = default;
InstanceManager::~InstanceManager() { Shutdown(); }

void InstanceManager::EnsureInitialized() {
    std::call_once(m_initOnce, [this]() {
        m_serverMutex = CreateMutexW(nullptr, TRUE, kServerMutexName);
        if(m_serverMutex) {
            DWORD error = GetLastError();
            if(error == ERROR_ALREADY_EXISTS) {
                m_isServer = false;
                ReleaseMutex(m_serverMutex);
            } else {
                m_isServer = true;
                StartServerThread();
            }
        }
    });
}

void InstanceManager::Shutdown() {
    m_stopRequested = true;
    {
        std::lock_guard lock(m_clientMutex);
        for(auto& client : m_clients) {
            if(client->pipe != INVALID_HANDLE_VALUE) {
                CancelIoEx(client->pipe, nullptr);
                CloseHandle(client->pipe);
                client->pipe = INVALID_HANDLE_VALUE;
            }
        }
        m_clients.clear();
    }
    if(m_serverThread.joinable()) {
        HANDLE pipe = CreateFileW(kPipeName, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if(pipe != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe);
        }
        m_serverThread.join();
    }
    if(m_serverMutex) {
        CloseHandle(m_serverMutex);
        m_serverMutex = nullptr;
    }
    if(m_trayIconManager) {
        m_trayIconManager->Shutdown();
        m_trayIconManager.reset();
    }
}

void InstanceManager::StartServerThread() {
    if(m_serverThread.joinable()) {
        return;
    }
    m_stopRequested = false;
    m_serverThread = std::thread(&InstanceManager::ServerThreadProc, this);
}

void InstanceManager::ServerThreadProc() {
    EnsureTray();
    while(!m_stopRequested) {
        HANDLE pipe = CreateNamedPipeW(kPipeName,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            16 * 1024,
            16 * 1024,
            0,
            nullptr);
        if(pipe == INVALID_HANDLE_VALUE) {
            Log(L"CreateNamedPipe failed: %lu", GetLastError());
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }
        BOOL connected = ConnectNamedPipe(pipe, nullptr);
        if(!connected) {
            DWORD error = GetLastError();
            if(error != ERROR_PIPE_CONNECTED) {
                CloseHandle(pipe);
                continue;
            }
        }

        auto context = std::make_unique<ClientContext>();
        context->pipe = pipe;
        context->connected = true;

        {
            std::lock_guard lock(m_clientMutex);
            m_clients.push_back(std::move(context));
        }

        std::thread worker(&InstanceManager::HandleClient, this, pipe);
        worker.detach();
    }
}

void InstanceManager::HandleClient(HANDLE pipe) {
    while(!m_stopRequested) {
        PipeMessageHeader header{};
        DWORD bytesRead = 0;
        BOOL result = ReadFile(pipe, &header, sizeof(header), &bytesRead, nullptr);
        if(!result || bytesRead == 0) {
            break;
        }
        if(bytesRead != sizeof(header) || header.magic != kPipeMagic) {
            break;
        }
        std::vector<uint8_t> payload(header.payloadSize);
        std::vector<uint8_t> binary(header.binarySize);
        if(header.payloadSize > 0) {
            DWORD payloadRead = 0;
            if(!ReadFile(pipe, payload.data(), header.payloadSize, &payloadRead, nullptr)) {
                break;
            }
        }
        if(header.binarySize > 0) {
            DWORD binaryRead = 0;
            if(!ReadFile(pipe, binary.data(), header.binarySize, &binaryRead, nullptr)) {
                break;
            }
        }
        Message message = DeserializeMessage(header, payload, binary);
        HandlePipeMessage(message, pipe);
    }

    {
        std::lock_guard lock(m_clientMutex);
        auto it = std::remove_if(m_clients.begin(), m_clients.end(), [pipe](const std::unique_ptr<ClientContext>& ctx) {
            return ctx->pipe == pipe;
        });
        m_clients.erase(it, m_clients.end());
    }
    CloseHandle(pipe);
}

void InstanceManager::HandlePipeMessage(const Message& message, HANDLE senderPipe) {
    Publish(message);

    switch(message.type) {
    case MessageType::TabListChanged: {
        TrayInstance instance;
        instance.explorer = message.explorerHwnd;
        instance.tabBar = message.tabBarHwnd;
        instance.currentPath = message.payload;
        if(!message.binaryPayload.empty()) {
            const wchar_t* data = reinterpret_cast<const wchar_t*>(message.binaryPayload.data());
            size_t total = message.binaryPayload.size() / sizeof(wchar_t);
            size_t index = 0;
            while(index < total) {
                const wchar_t* namePtr = data + index;
                size_t nameLen = wcslen(namePtr);
                if(nameLen == 0) {
                    break;
                }
                index += nameLen + 1;
                if(index >= total) {
                    break;
                }
                const wchar_t* pathPtr = data + index;
                size_t pathLen = wcslen(pathPtr);
                instance.tabNames.emplace_back(namePtr, nameLen);
                instance.tabPaths.emplace_back(pathPtr, pathLen);
                index += pathLen + 1;
            }
        }
        UpdateTrayIcon([&](TrayIconManager& tray) { tray.AddOrUpdate(instance); });
        break;
    }
    case MessageType::SelectionChanged:
        NotifySelectionChanged(message.tabBarHwnd, message.intValue);
        break;
    case MessageType::RegisterTabBar:
    case MessageType::UnregisterTabBar:
    case MessageType::RegisterButtonBar:
    case MessageType::UnregisterButtonBar:
        break;
    case MessageType::ExecuteAction:
    case MessageType::Broadcast: {
        ActionHandler handler;
        {
            std::lock_guard lock(m_actionMutex);
            auto it = m_actionHandlers.find(message.payload);
            if(it != m_actionHandlers.end()) {
                handler = it->second;
            }
        }
        std::wstring textPayload;
        std::vector<uint8_t> binary;
        UnpackActionPayload(message.binaryPayload, textPayload, binary);
        if(handler) {
            handler(textPayload, binary);
        }
        if(message.type == MessageType::Broadcast) {
            BroadcastPipeMessage(message, senderPipe);
        }
        break;
    }
    case MessageType::Subscribe:
    case MessageType::Unsubscribe:
        break;
    }
}

void InstanceManager::SendPipeMessage(HANDLE pipe, const Message& message) {
    PipeMessageHeader header{};
    header.magic = kPipeMagic;
    header.version = kPipeVersion;
    header.type = message.type;
    header.explorerHwnd = reinterpret_cast<uint64_t>(message.explorerHwnd);
    header.tabBarHwnd = reinterpret_cast<uint64_t>(message.tabBarHwnd);
    header.intValue = message.intValue;
    header.payloadSize = static_cast<uint32_t>(message.payload.size() * sizeof(wchar_t));
    header.binarySize = static_cast<uint32_t>(message.binaryPayload.size());

    DWORD written = 0;
    WriteFile(pipe, &header, sizeof(header), &written, nullptr);
    if(header.payloadSize > 0) {
        WriteFile(pipe, message.payload.data(), header.payloadSize, &written, nullptr);
    }
    if(header.binarySize > 0) {
        WriteFile(pipe, message.binaryPayload.data(), header.binarySize, &written, nullptr);
    }
}

void InstanceManager::BroadcastPipeMessage(const Message& message, HANDLE senderPipe) {
    std::lock_guard lock(m_clientMutex);
    for(auto& client : m_clients) {
        if(client->pipe != senderPipe && client->pipe != INVALID_HANDLE_VALUE) {
            SendPipeMessage(client->pipe, message);
        }
    }
}

InstanceManager::Message InstanceManager::DeserializeMessage(const PipeMessageHeader& header,
    const std::vector<uint8_t>& payload,
    const std::vector<uint8_t>& binaryPayload) const {
    Message message;
    message.type = header.type;
    message.explorerHwnd = reinterpret_cast<HWND>(header.explorerHwnd);
    message.tabBarHwnd = reinterpret_cast<HWND>(header.tabBarHwnd);
    message.intValue = header.intValue;
    if(header.payloadSize > 0) {
        message.payload.assign(reinterpret_cast<const wchar_t*>(payload.data()), payload.size() / sizeof(wchar_t));
    }
    message.binaryPayload = binaryPayload;
    return message;
}

void InstanceManager::RegisterTabHost(HWND explorerHwnd, QTTabBarClass* tabBar) {
    EnsureInitialized();
    if(!explorerHwnd || !tabBar) {
        return;
    }
    std::unique_lock lock(m_tabLock);
    m_tabRegistrations[tabBar] = {explorerHwnd, tabBar};

    Message message;
    message.type = MessageType::RegisterTabBar;
    message.explorerHwnd = explorerHwnd;
    message.tabBarHwnd = reinterpret_cast<HWND>(tabBar);
    Publish(message);
}

void InstanceManager::UnregisterTabHost(QTTabBarClass* tabBar) {
    EnsureInitialized();
    if(!tabBar) {
        return;
    }
    std::unique_lock lock(m_tabLock);
    m_tabRegistrations.erase(tabBar);
    lock.unlock();

    Message message;
    message.type = MessageType::UnregisterTabBar;
    message.tabBarHwnd = reinterpret_cast<HWND>(tabBar);
    Publish(message);
}

void InstanceManager::RegisterButtonBar(HWND explorerHwnd, QTButtonBar* buttonBar) {
    EnsureInitialized();
    if(!explorerHwnd || !buttonBar) {
        return;
    }
    std::unique_lock lock(m_tabLock);
    m_buttonRegistrations[buttonBar] = {explorerHwnd, buttonBar};

    Message message;
    message.type = MessageType::RegisterButtonBar;
    message.explorerHwnd = explorerHwnd;
    message.tabBarHwnd = reinterpret_cast<HWND>(buttonBar);
    Publish(message);
}

void InstanceManager::UnregisterButtonBar(QTButtonBar* buttonBar) {
    EnsureInitialized();
    if(!buttonBar) {
        return;
    }
    std::unique_lock lock(m_tabLock);
    m_buttonRegistrations.erase(buttonBar);
    lock.unlock();

    Message message;
    message.type = MessageType::UnregisterButtonBar;
    message.tabBarHwnd = reinterpret_cast<HWND>(buttonBar);
    Publish(message);
}

void InstanceManager::RegisterDesktopTool(QTDesktopTool* desktopTool) {
    EnsureInitialized();
    if(!desktopTool) {
        return;
    }
    std::unique_lock lock(m_tabLock);
    m_desktopTools.push_back({desktopTool});
}

void InstanceManager::UnregisterDesktopTool(QTDesktopTool* desktopTool) {
    EnsureInitialized();
    std::unique_lock lock(m_tabLock);
    m_desktopTools.erase(std::remove_if(m_desktopTools.begin(), m_desktopTools.end(),
        [desktopTool](const DesktopToolRegistration& entry) { return entry.tool == desktopTool; }), m_desktopTools.end());
}

void InstanceManager::RegisterAutoLoader(AutoLoaderNative* loader) {
    EnsureInitialized();
    if(!loader) {
        return;
    }
    std::unique_lock lock(m_tabLock);
    auto it = std::find_if(m_autoLoaders.begin(), m_autoLoaders.end(), [loader](const AutoLoaderRegistration& entry) {
        return entry.loader == loader;
    });
    if(it == m_autoLoaders.end()) {
        m_autoLoaders.push_back({loader});
    }
}

void InstanceManager::UnregisterAutoLoader(AutoLoaderNative* loader) {
    EnsureInitialized();
    std::unique_lock lock(m_tabLock);
    m_autoLoaders.erase(std::remove_if(m_autoLoaders.begin(), m_autoLoaders.end(),
        [loader](const AutoLoaderRegistration& entry) { return entry.loader == loader; }), m_autoLoaders.end());
}

void InstanceManager::PushTabList(HWND explorerHwnd, HWND tabBarHwnd, const TabSnapshot& snapshot) {
    EnsureInitialized();
    TrayInstance instance;
    instance.explorer = explorerHwnd;
    instance.tabBar = tabBarHwnd;
    instance.currentPath = snapshot.currentPath;
    instance.tabNames = snapshot.tabNames;
    instance.tabPaths = snapshot.tabPaths;
    UpdateTrayIcon([&](TrayIconManager& tray) { tray.AddOrUpdate(instance); });

    SetSelectedTabs(explorerHwnd, snapshot.tabPaths);

    Message message;
    message.type = MessageType::TabListChanged;
    message.explorerHwnd = explorerHwnd;
    message.tabBarHwnd = tabBarHwnd;
    message.payload = snapshot.currentPath;

    // Flatten the tab list into a UTF-16 payload: name\0path\0...
    if(!snapshot.tabNames.empty() && snapshot.tabNames.size() == snapshot.tabPaths.size()) {
        std::wstring buffer;
        for(size_t i = 0; i < snapshot.tabNames.size(); ++i) {
            buffer.append(snapshot.tabNames[i]);
            buffer.push_back(L'\0');
            buffer.append(snapshot.tabPaths[i]);
            buffer.push_back(L'\0');
        }
        const wchar_t terminator = L'\0';
        buffer.push_back(terminator);
        const auto* begin = reinterpret_cast<const uint8_t*>(buffer.data());
        message.binaryPayload.assign(begin, begin + buffer.size() * sizeof(wchar_t));
    }

    Publish(message);
}

void InstanceManager::RemoveFromTray(HWND tabBarHwnd) {
    EnsureInitialized();
    UpdateTrayIcon([&](TrayIconManager& tray) { tray.Remove(tabBarHwnd); });
}

void InstanceManager::NotifyExplorerVisibility(HWND tabBarHwnd, bool isVisible) {
    EnsureInitialized();
    UpdateTrayIcon([&](TrayIconManager& tray) { tray.Show(tabBarHwnd, isVisible); });
}

void InstanceManager::NotifySelectionChanged(HWND tabBarHwnd, int index) {
    EnsureInitialized();
    SelectionCallback callback;
    {
        std::lock_guard lock(m_selectionCallbackMutex);
        callback = m_selectionCallback;
    }
    if(callback) {
        callback(tabBarHwnd, index);
    }
    Message message;
    message.type = MessageType::SelectionChanged;
    message.tabBarHwnd = tabBarHwnd;
    message.intValue = index;
    Publish(message);
}

InstanceManager::SubscriptionId InstanceManager::Subscribe(MessageCallback callback) {
    std::lock_guard lock(m_subscriptionMutex);
    SubscriptionId id = m_nextSubscriptionId++;
    m_subscribers.emplace_back(id, std::move(callback));
    return id;
}

void InstanceManager::Unsubscribe(SubscriptionId token) {
    std::lock_guard lock(m_subscriptionMutex);
    m_subscribers.erase(std::remove_if(m_subscribers.begin(), m_subscribers.end(),
        [token](const auto& entry) { return entry.first == token; }), m_subscribers.end());
}

void InstanceManager::Publish(const Message& message) {
    std::vector<MessageCallback> callbacks;
    {
        std::lock_guard lock(m_subscriptionMutex);
        for(const auto& [id, callback] : m_subscribers) {
            if(callback) {
                callbacks.push_back(callback);
            }
        }
    }
    for(auto& callback : callbacks) {
        callback(message);
    }
}

void InstanceManager::RegisterActionHandler(const std::wstring& action, ActionHandler handler) {
    std::lock_guard lock(m_actionMutex);
    if(handler) {
        m_actionHandlers[action] = std::move(handler);
    } else {
        m_actionHandlers.erase(action);
    }
}

void InstanceManager::UnregisterActionHandler(const std::wstring& action) {
    std::lock_guard lock(m_actionMutex);
    m_actionHandlers.erase(action);
}

bool InstanceManager::ExecuteOnMainProcess(const std::wstring& action, const std::wstring& payload,
    const std::vector<uint8_t>& binaryPayload, bool doAsync) {
    EnsureInitialized();
    if(m_isServer) {
        auto invoke = [this, action, payload, binaryPayload]() {
            ActionHandler handler;
            {
                std::lock_guard lock(m_actionMutex);
                auto it = m_actionHandlers.find(action);
                if(it != m_actionHandlers.end()) {
                    handler = it->second;
                }
            }
            if(handler) {
                handler(payload, binaryPayload);
            }
        };
        if(doAsync) {
            std::thread(std::move(invoke)).detach();
        } else {
            invoke();
        }
        return true;
    }

    HANDLE pipe = CreateFileW(kPipeName, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if(pipe == INVALID_HANDLE_VALUE) {
        return false;
    }
    Message message;
    message.type = MessageType::ExecuteAction;
    message.payload = action;
    message.binaryPayload = PackActionPayload(payload, binaryPayload);
    SendPipeMessage(pipe, message);
    CloseHandle(pipe);
    return false;
}

void InstanceManager::ExecuteOnServerProcess(const std::wstring& action, const std::wstring& payload,
    const std::vector<uint8_t>& binaryPayload, bool doAsync) {
    EnsureInitialized();
    if(m_isServer) {
        ExecuteOnMainProcess(action, payload, binaryPayload, doAsync);
        return;
    }
    HANDLE pipe = CreateFileW(kPipeName, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if(pipe == INVALID_HANDLE_VALUE) {
        return;
    }
    Message message;
    message.type = MessageType::ExecuteAction;
    message.payload = action;
    message.binaryPayload = PackActionPayload(payload, binaryPayload);
    SendPipeMessage(pipe, message);
    CloseHandle(pipe);
}

void InstanceManager::Broadcast(const std::wstring& action, const std::wstring& payload,
    const std::vector<uint8_t>& binaryPayload) {
    EnsureInitialized();
    Message message;
    message.type = MessageType::Broadcast;
    message.payload = action;
    message.binaryPayload = PackActionPayload(payload, binaryPayload);
    Publish(message);
    if(m_isServer) {
        BroadcastPipeMessage(message, nullptr);
    } else {
        HANDLE pipe = CreateFileW(kPipeName, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if(pipe != INVALID_HANDLE_VALUE) {
            SendPipeMessage(pipe, message);
            CloseHandle(pipe);
        }
    }
}

std::vector<std::wstring> InstanceManager::GetSelectedTabs(HWND explorerHwnd) const {
    std::shared_lock lock(m_selectionLock);
    auto it = m_selectedTabs.find(FormatExplorerKey(explorerHwnd));
    if(it != m_selectedTabs.end()) {
        return it->second;
    }
    return {};
}

void InstanceManager::SetSelectedTabs(HWND explorerHwnd, const std::vector<std::wstring>& selections) {
    std::unique_lock lock(m_selectionLock);
    m_selectedTabs[FormatExplorerKey(explorerHwnd)] = selections;
}

void InstanceManager::SetSelectionCallback(SelectionCallback callback) {
    std::lock_guard lock(m_selectionCallbackMutex);
    m_selectionCallback = std::move(callback);
}

std::wstring InstanceManager::FormatExplorerKey(HWND explorerHwnd) const {
    std::wstringstream stream;
    stream << L"explorer:" << reinterpret_cast<uintptr_t>(explorerHwnd);
    return stream.str();
}

void InstanceManager::EnsureTray() {
    if(!m_isServer) {
        return;
    }
    if(!m_trayIconManager) {
        m_trayIconManager = std::make_unique<TrayIconManager>(*this);
    }
    m_trayIconManager->EnsureThread();
}

void InstanceManager::UpdateTrayIcon(std::function<void(TrayIconManager&)> action) {
    if(!m_isServer) {
        return;
    }
    EnsureTray();
    if(m_trayIconManager && action) {
        action(*m_trayIconManager);
    }
}

void InstanceManager::Log(const wchar_t* format, ...) const {
    wchar_t buffer[512];
    va_list args;
    va_start(args, format);
    StringCchVPrintfW(buffer, ARRAYSIZE(buffer), format, args);
    va_end(args);
    OutputDebugStringW(buffer);
    OutputDebugStringW(L"\n");
}

