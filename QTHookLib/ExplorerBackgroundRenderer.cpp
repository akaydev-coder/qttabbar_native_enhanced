#include "ExplorerBackgroundRenderer.h"

#include <shlwapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <memory>
#include <mutex>
#include <new>
#include <limits>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "Msimg32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Windowscodecs.lib")

namespace qttabbar {
namespace background {
namespace {

constexpr wchar_t kRegisteredWindowProperty[] =
    L"QTTabBar.NativeEnhanced.BackgroundWindow";

struct ImageAsset {
    ~ImageAsset() {
        if(memoryDc != nullptr && previousBitmap != nullptr && previousBitmap != HGDI_ERROR) {
            ::SelectObject(memoryDc, previousBitmap);
        }
        if(bitmap != nullptr) {
            ::DeleteObject(bitmap);
        }
        if(memoryDc != nullptr) {
            ::DeleteDC(memoryDc);
        }
    }

    std::wstring path;
    std::wstring fileName;
    HDC memoryDc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previousBitmap = nullptr;
    SIZE size{};
    mutable std::mutex drawMutex;
};

struct ConfigSnapshot {
    std::wstring configPath;
    std::wstring imageFolder;
    int positionMode = 3;
    BYTE alpha = 255;
    bool random = false;
    bool customByPath = false;
    std::vector<std::shared_ptr<ImageAsset>> images;
};

struct WindowState {
    SIZE lastSize{};
    std::size_t imageIndex = 0;
    std::wstring path;
};

std::wstring ToLower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return value;
}

std::wstring FileNameOf(const std::wstring& path) {
    const std::wstring::size_type slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

bool FileExists(const std::wstring& path) {
    const DWORD attributes = ::GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool DirectoryExists(const std::wstring& path) {
    const DWORD attributes = ::GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::wstring ModuleDirectory(HMODULE module) {
    std::vector<wchar_t> buffer(MAX_PATH);
    for(;;) {
        const DWORD length = ::GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if(length == 0) {
            return {};
        }
        if(length < buffer.size() - 1) {
            std::wstring path(buffer.data(), length);
            const std::wstring::size_type slash = path.find_last_of(L"\\/");
            return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring ReadIni(const std::wstring& file, const std::wstring& section,
                     const wchar_t* key, const wchar_t* fallback = L"") {
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD length = ::GetPrivateProfileStringW(section.c_str(), key, fallback,
        buffer.data(), static_cast<DWORD>(buffer.size()), file.c_str());
    return std::wstring(buffer.data(), length);
}

bool ReadBool(const std::wstring& file, const wchar_t* section, const wchar_t* key,
              bool fallback) {
    const std::wstring value = ToLower(ReadIni(file, section, key, fallback ? L"true" : L"false"));
    return value == L"true" || value == L"1" || value == L"yes" || value == L"on";
}

int ReadInt(const std::wstring& file, const wchar_t* section, const wchar_t* key,
            int fallback, int minimum, int maximum) {
    const std::wstring value = ReadIni(file, section, key, L"");
    if(value.empty()) {
        return fallback;
    }
    wchar_t* end = nullptr;
    const long parsed = std::wcstol(value.c_str(), &end, 10);
    if(end == value.c_str() || *end != L'\0') {
        return fallback;
    }
    const long clamped = parsed < minimum ? minimum : (parsed > maximum ? maximum : parsed);
    return static_cast<int>(clamped);
}

std::wstring ExpandPath(const std::wstring& value, const std::wstring& baseDirectory) {
    if(value.empty()) {
        return {};
    }
    const DWORD required = ::ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
    std::wstring expanded = value;
    if(required > 0) {
        std::vector<wchar_t> buffer(required, L'\0');
        if(::ExpandEnvironmentStringsW(value.c_str(), buffer.data(), required) != 0) {
            expanded.assign(buffer.data());
        }
    }
    if(::PathIsRelativeW(expanded.c_str())) {
        return baseDirectory + L"\\" + expanded;
    }
    return expanded;
}

bool IsSupportedImage(const std::wstring& name) {
    const std::wstring lower = ToLower(name);
    return lower.size() > 4 &&
        (lower.compare(lower.size() - 4, 4, L".png") == 0 ||
         lower.compare(lower.size() - 4, 4, L".bmp") == 0 ||
         lower.compare(lower.size() - 4, 4, L".jpg") == 0 ||
         (lower.size() > 5 && lower.compare(lower.size() - 5, 5, L".jpeg") == 0));
}

void EnumerateImages(const std::wstring& directory, std::vector<std::wstring>& paths) {
    if(!DirectoryExists(directory)) {
        return;
    }
    WIN32_FIND_DATAW data{};
    const std::wstring pattern = directory + L"\\*";
    HANDLE find = ::FindFirstFileW(pattern.c_str(), &data);
    if(find == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 && IsSupportedImage(data.cFileName)) {
            paths.push_back(directory + L"\\" + data.cFileName);
        }
    } while(::FindNextFileW(find, &data));
    ::FindClose(find);
    std::sort(paths.begin(), paths.end(), [](const std::wstring& left, const std::wstring& right) {
        return ToLower(left) < ToLower(right);
    });
}

std::shared_ptr<ImageAsset> LoadImage(const std::wstring& path) {
    using Microsoft::WRL::ComPtr;

    ComPtr<IWICImagingFactory> factory;
    HRESULT result = ::CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if(FAILED(result)) {
        return {};
    }

    ComPtr<IWICBitmapDecoder> decoder;
    result = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, &decoder);
    if(FAILED(result)) {
        return {};
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    result = decoder->GetFrame(0, &frame);
    if(FAILED(result)) {
        return {};
    }

    UINT width = 0;
    UINT height = 0;
    result = frame->GetSize(&width, &height);
    if(FAILED(result) || width == 0 || height == 0 ||
       width > static_cast<UINT>((std::numeric_limits<LONG>::max)()) ||
       height > static_cast<UINT>((std::numeric_limits<LONG>::max)()) ||
       width > (std::numeric_limits<UINT>::max)() / 4) {
        return {};
    }

    const UINT stride = width * 4;
    if(height > (std::numeric_limits<UINT>::max)() / stride) {
        return {};
    }
    const UINT bufferSize = stride * height;

    ComPtr<IWICFormatConverter> converter;
    result = factory->CreateFormatConverter(&converter);
    if(FAILED(result)) {
        return {};
    }
    result = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if(FAILED(result)) {
        return {};
    }

    auto image = std::make_shared<ImageAsset>();
    image->path = path;
    image->fileName = FileNameOf(path);
    image->size.cx = static_cast<LONG>(width);
    image->size.cy = static_cast<LONG>(height);
    image->memoryDc = ::CreateCompatibleDC(nullptr);
    if(image->memoryDc == nullptr) {
        return {};
    }
    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = image->size.cx;
    bitmapInfo.bmiHeader.biHeight = -image->size.cy;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    void* destinationBits = nullptr;
    image->bitmap = ::CreateDIBSection(image->memoryDc, &bitmapInfo, DIB_RGB_COLORS,
        &destinationBits, nullptr, 0);
    if(image->bitmap == nullptr || destinationBits == nullptr) {
        return {};
    }

    result = converter->CopyPixels(nullptr, stride, bufferSize,
        static_cast<BYTE*>(destinationBits));
    if(FAILED(result)) {
        return {};
    }

    image->previousBitmap = ::SelectObject(image->memoryDc, image->bitmap);
    if(image->previousBitmap == nullptr || image->previousBitmap == HGDI_ERROR) {
        return {};
    }
    return image;
}

bool SameFileTime(const FILETIME& left, const FILETIME& right) {
    return left.dwLowDateTime == right.dwLowDateTime && left.dwHighDateTime == right.dwHighDateTime;
}

FILETIME LastWriteTime(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if(::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return data.ftLastWriteTime;
    }
    return {};
}

class Renderer {
public:
    Renderer() noexcept : random_(0x51545442u) {
    }

    HRESULT Initialize(HMODULE module) {
        std::lock_guard<std::mutex> loadGuard(loadMutex_);
        if(active_.load(std::memory_order_acquire)) {
            return S_OK;
        }
        if((::GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0) {
            return HRESULT_FROM_WIN32(ERROR_CANCELLED);
        }
        moduleDirectory_ = ModuleDirectory(module);
        if(moduleDirectory_.empty()) {
            return E_FAIL;
        }
        configPath_ = moduleDirectory_ + L"\\config.ini";
        if(!FileExists(configPath_) || !ReadBool(configPath_, L"image", L"enabled", true)) {
            return S_FALSE;
        }

        std::shared_ptr<const ConfigSnapshot> snapshot = BuildSnapshot();
        if(!snapshot || snapshot->images.empty()) {
            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        }
        {
            std::lock_guard<std::mutex> stateGuard(stateMutex_);
            config_ = std::move(snapshot);
            windows_.clear();
            dcWindows_.clear();
            configWriteTime_ = LastWriteTime(configPath_);
        }
        active_.store(true, std::memory_order_release);
        return S_OK;
    }

    void Shutdown() noexcept {
        std::lock_guard<std::mutex> loadGuard(loadMutex_);
        active_.store(false, std::memory_order_release);
        {
            std::lock_guard<std::mutex> stateGuard(stateMutex_);
            for(const auto& entry : windows_) {
                if(::IsWindow(entry.first) &&
                   ::GetPropW(entry.first, kRegisteredWindowProperty) == this) {
                    ::RemovePropW(entry.first, kRegisteredWindowProperty);
                }
            }
            windows_.clear();
            dcWindows_.clear();
            config_.reset();
        }
    }

    bool IsActive() const noexcept {
        return active_.load(std::memory_order_acquire);
    }

    HRESULT RegisterWindow(HWND window, const wchar_t* path) {
        if(!IsActive() || !::IsWindow(window)) {
            return E_FAIL;
        }
        wchar_t className[64]{};
        ::GetClassNameW(window, className, ARRAYSIZE(className));
        if(std::wcscmp(className, L"DirectUIHWND") != 0) {
            return E_INVALIDARG;
        }
        ReloadIfChanged();

        const std::wstring currentPath = path == nullptr ? std::wstring() : path;
        std::lock_guard<std::mutex> stateGuard(stateMutex_);
        if(!config_ || config_->images.empty()) {
            return E_FAIL;
        }
        if(!::SetPropW(window, kRegisteredWindowProperty, this)) {
            const DWORD error = ::GetLastError();
            return HRESULT_FROM_WIN32(error == ERROR_SUCCESS ? ERROR_NOT_ENOUGH_MEMORY : error);
        }
        WindowState& state = windows_[window];
        state.path = currentPath;
        state.imageIndex = SelectImageIndex(*config_, currentPath);
        state.lastSize = {};
        ::InvalidateRect(window, nullptr, TRUE);
        return S_OK;
    }

    HRESULT UpdateWindow(HWND window, const wchar_t* path) {
        if(!IsActive() || !::IsWindow(window)) {
            return E_FAIL;
        }
        if(::GetPropW(window, kRegisteredWindowProperty) != this) {
            return E_INVALIDARG;
        }
        ReloadIfChanged();
        const std::wstring currentPath = path == nullptr ? std::wstring() : path;
        std::lock_guard<std::mutex> stateGuard(stateMutex_);
        auto found = windows_.find(window);
        if(found == windows_.end() || !config_ || config_->images.empty()) {
            return E_FAIL;
        }
        found->second.path = currentPath;
        found->second.imageIndex = SelectImageIndex(*config_, currentPath);
        ::InvalidateRect(window, nullptr, TRUE);
        return S_OK;
    }

    void OnWindowDcAcquired(HWND window, HDC dc) noexcept {
        if(!IsActive() || window == nullptr || dc == nullptr) {
            return;
        }
        std::lock_guard<std::mutex> guard(stateMutex_);
        dcWindows_.erase(dc);
        if(windows_.find(window) != windows_.end() &&
           ::IsWindow(window) &&
           ::GetPropW(window, kRegisteredWindowProperty) == this) {
            dcWindows_[dc] = window;
        }
    }

    void OnCompatibleDcCreated(HDC sourceDc, HDC compatibleDc) noexcept {
        if(!IsActive() || sourceDc == nullptr || compatibleDc == nullptr) {
            return;
        }
        std::lock_guard<std::mutex> guard(stateMutex_);
        dcWindows_.erase(compatibleDc);
        if(dcWindows_.size() > 2048) {
            dcWindows_.clear();
        }
        const HWND window = ResolveWindowLocked(sourceDc);
        if(window != nullptr) {
            dcWindows_[compatibleDc] = window;
        }
    }

    void OnDcDeleted(HDC dc) noexcept {
        if(dc == nullptr) {
            return;
        }
        std::lock_guard<std::mutex> guard(stateMutex_);
        dcWindows_.erase(dc);
    }

    void OnFillRect(HDC targetDc, const RECT* paintedRect) noexcept {
        if(!IsActive() || targetDc == nullptr || paintedRect == nullptr) {
            return;
        }

        HWND window = nullptr;
        SIZE previousSize{};
        std::shared_ptr<const ConfigSnapshot> config;
        std::shared_ptr<ImageAsset> image;
        {
            std::lock_guard<std::mutex> guard(stateMutex_);
            window = ResolveWindowLocked(targetDc);
            auto found = windows_.find(window);
            if(found != windows_.end()) {
                const WindowState& state = found->second;
                previousSize = state.lastSize;
                config = config_;
                if(config && !config->images.empty()) {
                    const std::size_t index = state.imageIndex < config->images.size() ? state.imageIndex : 0;
                    image = config->images[index];
                }
            }
        }
        if(window == nullptr || !image || !config || image->memoryDc == nullptr ||
           image->size.cx <= 0 || image->size.cy <= 0) {
            return;
        }

        RECT client{};
        if(!::GetClientRect(window, &client)) {
            return;
        }
        const SIZE windowSize{client.right - client.left, client.bottom - client.top};
        if(windowSize.cx <= 0 || windowSize.cy <= 0) {
            return;
        }
        if(config->positionMode != 0 &&
           (previousSize.cx != windowSize.cx || previousSize.cy != windowSize.cy)) {
            ::InvalidateRect(window, nullptr, TRUE);
        }

        POINT position{};
        SIZE destination = image->size;
        CalculatePlacement(config->positionMode, windowSize, image->size, position, destination);
        if(destination.cx <= 0 || destination.cy <= 0) {
            return;
        }

        const int saved = ::SaveDC(targetDc);
        if(saved == 0) {
            return;
        }
        const int clipResult = ::IntersectClipRect(targetDc, paintedRect->left, paintedRect->top,
            paintedRect->right, paintedRect->bottom);
        if(clipResult == NULLREGION || clipResult == ERROR) {
            ::RestoreDC(targetDc, saved);
            return;
        }
        const BLENDFUNCTION blend{AC_SRC_OVER, 0, config->alpha, AC_SRC_ALPHA};
        {
            std::lock_guard<std::mutex> drawGuard(image->drawMutex);
            ::AlphaBlend(targetDc, position.x, position.y, destination.cx, destination.cy,
                image->memoryDc, 0, 0, image->size.cx, image->size.cy, blend);
        }
        ::RestoreDC(targetDc, saved);

        std::lock_guard<std::mutex> guard(stateMutex_);
        auto found = windows_.find(window);
        if(found != windows_.end()) {
            found->second.lastSize = windowSize;
        }
    }

private:
    HWND ResolveWindowLocked(HDC dc) {
        const auto mapped = dcWindows_.find(dc);
        if(mapped != dcWindows_.end()) {
            if(windows_.find(mapped->second) != windows_.end() && ::IsWindow(mapped->second) &&
               ::GetPropW(mapped->second, kRegisteredWindowProperty) == this) {
                return mapped->second;
            }
            dcWindows_.erase(mapped);
        }

        HWND candidate = ::WindowFromDC(dc);
        for(int depth = 0; candidate != nullptr && depth < 8; ++depth) {
            if(windows_.find(candidate) != windows_.end() &&
               ::GetPropW(candidate, kRegisteredWindowProperty) == this) {
                return candidate;
            }
            candidate = ::GetParent(candidate);
        }
        return nullptr;
    }

    std::shared_ptr<const ConfigSnapshot> BuildSnapshot() const {
        auto snapshot = std::make_shared<ConfigSnapshot>();
        snapshot->configPath = configPath_;
        snapshot->positionMode = ReadInt(configPath_, L"image", L"posType", 3, 0, 6);
        snapshot->alpha = static_cast<BYTE>(ReadInt(configPath_, L"image", L"imgAlpha", 255, 0, 255));
        snapshot->random = ReadBool(configPath_, L"image", L"random", false);
        snapshot->customByPath = ReadBool(configPath_, L"image", L"custom", false);

        const std::wstring configuredFolder = ExpandPath(
            ReadIni(configPath_, L"image", L"folder", L""), moduleDirectory_);
        snapshot->imageFolder = configuredFolder.empty() ? moduleDirectory_ + L"\\Image" : configuredFolder;

        std::vector<std::wstring> paths;
        const std::wstring defaultImage = ExpandPath(
            ReadIni(configPath_, L"image", L"imgPath", L""), moduleDirectory_);
        if(FileExists(defaultImage) && IsSupportedImage(defaultImage)) {
            paths.push_back(defaultImage);
        }
        EnumerateImages(snapshot->imageFolder, paths);

        std::unordered_set<std::wstring> seen;
        for(const std::wstring& path : paths) {
            const std::wstring key = ToLower(path);
            if(!seen.insert(key).second) {
                continue;
            }
            std::shared_ptr<ImageAsset> image = LoadImage(path);
            if(image) {
                snapshot->images.push_back(std::move(image));
            }
        }
        return snapshot;
    }

    void ReloadIfChanged() {
        const FILETIME currentWriteTime = LastWriteTime(configPath_);
        {
            std::lock_guard<std::mutex> guard(stateMutex_);
            if(SameFileTime(currentWriteTime, configWriteTime_)) {
                return;
            }
        }
        std::lock_guard<std::mutex> loadGuard(loadMutex_);
        {
            std::lock_guard<std::mutex> guard(stateMutex_);
            if(SameFileTime(currentWriteTime, configWriteTime_)) {
                return;
            }
        }
        std::shared_ptr<const ConfigSnapshot> replacement = BuildSnapshot();
        if(!replacement || replacement->images.empty()) {
            return;
        }
        std::vector<HWND> invalidate;
        {
            std::lock_guard<std::mutex> guard(stateMutex_);
            config_ = std::move(replacement);
            configWriteTime_ = currentWriteTime;
            for(auto& entry : windows_) {
                entry.second.imageIndex = SelectImageIndex(*config_, entry.second.path);
                invalidate.push_back(entry.first);
            }
        }
        for(HWND window : invalidate) {
            if(::IsWindow(window)) {
                ::InvalidateRect(window, nullptr, TRUE);
            }
        }
    }

    std::size_t SelectImageIndex(const ConfigSnapshot& config, const std::wstring& path) {
        if(config.customByPath && !path.empty()) {
            const std::wstring requested = ToLower(FileNameOf(
                ReadIni(config.configPath, path, L"img", L"")));
            if(!requested.empty()) {
                for(std::size_t i = 0; i < config.images.size(); ++i) {
                    if(ToLower(config.images[i]->fileName) == requested) {
                        return i;
                    }
                }
            }
        }
        if(config.random && config.images.size() > 1) {
            std::uniform_int_distribution<std::size_t> distribution(0, config.images.size() - 1);
            return distribution(random_);
        }
        return 0;
    }

    static void CalculatePlacement(int mode, const SIZE& window, const SIZE& image,
                                   POINT& position, SIZE& destination) {
        destination = image;
        switch(mode) {
            case 0:
                position = {0, 0};
                break;
            case 1:
                position = {window.cx - image.cx, 0};
                break;
            case 2:
                position = {0, window.cy - image.cy};
                break;
            case 3:
                position = {window.cx - image.cx, window.cy - image.cy};
                break;
            case 4:
                position = {(window.cx - image.cx) / 2, (window.cy - image.cy) / 2};
                break;
            case 5:
                position = {0, 0};
                destination = window;
                break;
            case 6: {
                const double scaleX = static_cast<double>(window.cx) / image.cx;
                const double scaleY = static_cast<double>(window.cy) / image.cy;
                const double scale = scaleX > scaleY ? scaleX : scaleY;
                const LONG scaledWidth = static_cast<LONG>(image.cx * scale + 0.5);
                const LONG scaledHeight = static_cast<LONG>(image.cy * scale + 0.5);
                destination.cx = scaledWidth > 0 ? scaledWidth : 1;
                destination.cy = scaledHeight > 0 ? scaledHeight : 1;
                position.x = (window.cx - destination.cx) / 2;
                position.y = (window.cy - destination.cy) / 2;
                break;
            }
            default:
                position = {window.cx - image.cx, window.cy - image.cy};
                break;
        }
    }

    std::atomic<bool> active_{false};
    std::mutex stateMutex_;
    std::mutex loadMutex_;
    std::unordered_map<HWND, WindowState> windows_;
    std::unordered_map<HDC, HWND> dcWindows_;
    std::shared_ptr<const ConfigSnapshot> config_;
    std::wstring moduleDirectory_;
    std::wstring configPath_;
    FILETIME configWriteTime_{};
    std::mt19937 random_;
};

Renderer& GetRenderer() {
    // Explicit Shutdown releases GDI objects before the DLL is unloaded. The tiny
    // coordinator intentionally survives to avoid destructor work under loader lock.
    static Renderer* renderer = new Renderer();
    return *renderer;
}

}  // namespace

HRESULT Initialize(HMODULE module) {
    try {
        return GetRenderer().Initialize(module);
    }
    catch(const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch(...) {
        return E_FAIL;
    }
}

void Shutdown() noexcept {
    GetRenderer().Shutdown();
}

bool IsActive() noexcept {
    return GetRenderer().IsActive();
}

HRESULT RegisterWindow(HWND window, const wchar_t* path) {
    try {
        return GetRenderer().RegisterWindow(window, path);
    }
    catch(const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch(...) {
        return E_FAIL;
    }
}

HRESULT UpdateWindow(HWND window, const wchar_t* path) {
    try {
        return GetRenderer().UpdateWindow(window, path);
    }
    catch(const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch(...) {
        return E_FAIL;
    }
}

void OnCompatibleDcCreated(HDC sourceDc, HDC compatibleDc) noexcept {
    GetRenderer().OnCompatibleDcCreated(sourceDc, compatibleDc);
}

void OnWindowDcAcquired(HWND window, HDC dc) noexcept {
    GetRenderer().OnWindowDcAcquired(window, dc);
}

void OnDcDeleted(HDC dc) noexcept {
    GetRenderer().OnDcDeleted(dc);
}

void OnFillRect(HDC targetDc, const RECT* paintedRect) noexcept {
    GetRenderer().OnFillRect(targetDc, paintedRect);
}

}  // namespace background
}  // namespace qttabbar
