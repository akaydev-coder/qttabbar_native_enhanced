#include "pch.h"
#include "ThumbnailTooltipWindow.h"

#include <Shlwapi.h>
#include <Shobjidl.h>

#include <algorithm>
#include <codecvt>
#include <fstream>
#include <iterator>
#include <sstream>
#include <locale>
#include <cstring>

#pragma comment(lib, "Shlwapi.lib")

namespace {
constexpr COLORREF kTooltipBackground = RGB(32, 32, 32);
constexpr COLORREF kTooltipBorder = RGB(96, 96, 96);
constexpr COLORREF kTooltipText = RGB(240, 240, 240);
constexpr UINT kTooltipPadding = 12;
constexpr UINT kTooltipTextLines = 12;

SIZE ClampSize(SIZE value, int maxWidth, int maxHeight) {
    value.cx = (std::min)(value.cx, static_cast<LONG>(maxWidth));
    value.cy = (std::min)(value.cy, static_cast<LONG>(maxHeight));
    return value;
}

std::wstring ToLowerCopy(const std::wstring& value) {
    std::wstring copy = value;
    std::transform(copy.begin(), copy.end(), copy.begin(), [](wchar_t ch) { return static_cast<wchar_t>(::towlower(ch)); });
    return copy;
}

bool HasExtension(const std::wstring& extension, const qttabbar::StringList& list) {
    if(extension.empty()) {
        return false;
    }
    std::wstring lowered = ToLowerCopy(extension);
    for(const auto& candidate : list) {
        if(ToLowerCopy(candidate) == lowered) {
            return true;
        }
    }
    return false;
}

bool ReadSmallTextFile(const std::wstring& path, std::wstring& output) {
    std::wifstream stream(path);
    if(!stream) {
        return false;
    }
    stream.imbue(std::locale(stream.getloc(), new std::codecvt_utf8_utf16<wchar_t>()));
    std::wostringstream buffer;
    std::wstring line;
    std::size_t lineCount = 0;
    while(std::getline(stream, line)) {
        buffer << line << L"\r\n";
        if(++lineCount >= kTooltipTextLines) {
            break;
        }
    }
    output = buffer.str();
    return !output.empty();
}

} // namespace

ThumbnailTooltipWindow::ThumbnailTooltipWindow() noexcept = default;

ThumbnailTooltipWindow::~ThumbnailTooltipWindow() {
    ClearCache();
}

void ThumbnailTooltipWindow::ApplyConfiguration(const qttabbar::TipsSettings& settings) {
    m_settings = settings;
}

void ThumbnailTooltipWindow::ClearCache() {
    for(auto& kvp : m_cache) {
        DestroyCacheEntry(kvp.second);
    }
    m_cache.clear();
    m_bitmap = nullptr;
    m_bitmapSize = {0, 0};
    m_textPreview.clear();
    m_mode = Mode::None;
    if(IsWindow()) {
        ShowWindow(SW_HIDE);
    }
}

void ThumbnailTooltipWindow::HideTooltip() {
    m_mode = Mode::None;
    m_bitmap = nullptr;
    m_textPreview.clear();
    if(IsWindow()) {
        ShowWindow(SW_HIDE);
    }
}

bool ThumbnailTooltipWindow::ShowForPath(const std::wstring& path, const RECT& reference, HWND owner) {
    if(path.empty()) {
        HideTooltip();
        return false;
    }

    if(!EnsureWindow()) {
        return false;
    }

    SetParent(owner);

    std::wstring extension = PathFindExtensionW(path.c_str());
    std::wstring lowered = ToLowerCopy(extension);

    CacheEntry entry{};
    auto it = m_cache.find(path);
    if(it != m_cache.end()) {
        entry = it->second;
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if(::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
            if(std::memcmp(&data.ftLastWriteTime, &entry.timestamp, sizeof(FILETIME)) != 0) {
                CacheEntry refreshed{};
                if(IsSupportedImageExtension(lowered, m_settings) && LoadImage(path, refreshed)) {
                    DestroyCacheEntry(it->second);
                    it->second = refreshed;
                    entry = refreshed;
                }
            }
        }
    } else if(IsSupportedImageExtension(lowered, m_settings)) {
        CacheEntry loaded{};
        if(LoadImage(path, loaded)) {
            entry = loaded;
            m_cache.emplace(path, loaded);
        }
    }

    if(entry.bitmap) {
        m_bitmap = entry.bitmap;
        m_bitmapSize = entry.size;
        m_mode = Mode::Image;
        m_textPreview.clear();
    } else if(IsSupportedTextExtension(lowered, m_settings)) {
        std::wstring text;
        if(LoadTextPreview(path, text)) {
            m_mode = Mode::Text;
            m_textPreview = std::move(text);
            m_bitmap = nullptr;
            m_bitmapSize = {0, 0};
        } else {
            HideTooltip();
            return false;
        }
    } else {
        HideTooltip();
        return false;
    }

    UpdateWindowPlacement(reference);
    ShowWindow(SW_SHOWNOACTIVATE);
    return true;
}

bool ThumbnailTooltipWindow::IsSupportedImageExtension(const std::wstring& extension,
                                                       const qttabbar::TipsSettings& settings) {
    if(settings.imageExt.empty()) {
        return extension == L".png" || extension == L".jpg" || extension == L".jpeg" || extension == L".bmp"
               || extension == L".gif" || extension == L".tif" || extension == L".tiff";
    }
    return HasExtension(extension, settings.imageExt);
}

bool ThumbnailTooltipWindow::IsSupportedTextExtension(const std::wstring& extension,
                                                      const qttabbar::TipsSettings& settings) {
    if(settings.textExt.empty()) {
        return extension == L".txt" || extension == L".log" || extension == L".ini" || extension == L".cfg";
    }
    return HasExtension(extension, settings.textExt);
}

LRESULT ThumbnailTooltipWindow::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    SetWindowPos(HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return 0;
}

LRESULT ThumbnailTooltipWindow::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    ClearCache();
    return 0;
}

LRESULT ThumbnailTooltipWindow::OnPaint(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
    bHandled = TRUE;
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(&ps);
    if(!hdc) {
        return 0;
    }

    RECT rc{};
    GetClientRect(&rc);
    HBRUSH background = ::CreateSolidBrush(kTooltipBackground);
    ::FillRect(hdc, &rc, background);
    ::DeleteObject(background);
    HPEN pen = ::CreatePen(PS_SOLID, 1, kTooltipBorder);
    HGDIOBJ oldPen = ::SelectObject(hdc, pen);
    ::Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);

    if(m_mode == Mode::Image && m_bitmap) {
        HDC memDC = ::CreateCompatibleDC(hdc);
        HGDIOBJ oldBmp = ::SelectObject(memDC, m_bitmap);
        BITMAP bm{};
        ::GetObject(m_bitmap, sizeof(bm), &bm);
        int destWidth = m_bitmapSize.cx;
        int destHeight = m_bitmapSize.cy;
        RECT content = rc;
        ::InflateRect(&content, -static_cast<int>(kTooltipPadding), -static_cast<int>(kTooltipPadding));
        int x = content.left + (content.right - content.left - destWidth) / 2;
        int y = content.top + (content.bottom - content.top - destHeight) / 2;
        ::SetStretchBltMode(hdc, HALFTONE);
        ::StretchBlt(hdc, x, y, destWidth, destHeight, memDC, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
        ::SelectObject(memDC, oldBmp);
        ::DeleteDC(memDC);
    } else if(m_mode == Mode::Text) {
        HFONT font = static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
        HGDIOBJ oldFont = ::SelectObject(hdc, font);
        ::SetBkMode(hdc, TRANSPARENT);
        ::SetTextColor(hdc, kTooltipText);
        RECT content = rc;
        ::InflateRect(&content, -static_cast<int>(kTooltipPadding), -static_cast<int>(kTooltipPadding));
        ::DrawTextW(hdc, m_textPreview.c_str(), static_cast<int>(m_textPreview.size()), &content,
                    DT_LEFT | DT_NOPREFIX | DT_WORDBREAK);
        ::SelectObject(hdc, oldFont);
    }

    ::SelectObject(hdc, oldPen);
    ::DeleteObject(pen);
    EndPaint(&ps);
    return 0;
}

bool ThumbnailTooltipWindow::EnsureWindow() {
    if(IsWindow()) {
        return true;
    }
    DWORD style = WS_POPUP | WS_BORDER;
    DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
    HWND hwnd = Create(nullptr, CWindow::rcDefault, L"", style, exStyle);
    return hwnd != nullptr;
}

bool ThumbnailTooltipWindow::LoadImage(const std::wstring& path, CacheEntry& entry) {
    CComPtr<IShellItem> item;
    if(FAILED(::SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
        return false;
    }

    CComPtr<IShellItemImageFactory> factory;
    if(FAILED(item->BindToHandler(nullptr, BHID_ThumbnailHandler, IID_PPV_ARGS(&factory)))) {
        if(FAILED(item->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&factory)))) {
            return false;
        }
    }

    SIZE desired{m_settings.previewMaxWidth, m_settings.previewMaxHeight};
    desired = ClampSize(desired, m_settings.previewMaxWidth, m_settings.previewMaxHeight);
    HBITMAP bitmap = nullptr;
    HRESULT hr = factory->GetImage(desired, SIIGBF_RESIZETOFIT | SIIGBF_BIGGERSIZEOK, &bitmap);
    if(FAILED(hr) || bitmap == nullptr) {
        return false;
    }

    entry.bitmap = bitmap;
    BITMAP bm{};
    if(::GetObject(bitmap, sizeof(bm), &bm)) {
        entry.size.cx = bm.bmWidth;
        entry.size.cy = bm.bmHeight;
        entry.size = ClampSize(entry.size, m_settings.previewMaxWidth, m_settings.previewMaxHeight);
    } else {
        entry.size = desired;
    }
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if(::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        entry.timestamp = data.ftLastWriteTime;
    }
    return true;
}

bool ThumbnailTooltipWindow::LoadTextPreview(const std::wstring& path, std::wstring& text) const {
    return ReadSmallTextFile(path, text);
}

void ThumbnailTooltipWindow::DestroyCacheEntry(CacheEntry& entry) {
    if(entry.bitmap) {
        ::DeleteObject(entry.bitmap);
        entry.bitmap = nullptr;
    }
}

void ThumbnailTooltipWindow::UpdateWindowPlacement(const RECT& reference) {
    RECT bounds = reference;
    int width = static_cast<int>(m_settings.previewMaxWidth + 2 * kTooltipPadding);
    int height = static_cast<int>(m_settings.previewMaxHeight + 2 * kTooltipPadding);

    if(m_mode == Mode::Text) {
        width = static_cast<int>(m_settings.previewMaxWidth);
        height = static_cast<int>(m_settings.previewMaxHeight);
    } else if(m_mode == Mode::Image && m_bitmap) {
        width = static_cast<int>(m_bitmapSize.cx + 2 * kTooltipPadding);
        height = static_cast<int>(m_bitmapSize.cy + 2 * kTooltipPadding);
    }

    RECT workArea{};
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);

    int x = bounds.right + 8;
    int y = bounds.top;
    if(x + width > workArea.right) {
        x = bounds.left - width - 8;
    }
    if(y + height > workArea.bottom) {
        y = workArea.bottom - height;
    }
    if(y < workArea.top) {
        y = workArea.top;
    }

    SetWindowPos(HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
}
