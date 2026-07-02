#pragma once

#include <atlbase.h>
#include <atlwin.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Config.h"

class ThumbnailTooltipWindow final : public CWindowImpl<ThumbnailTooltipWindow> {
public:
    DECLARE_WND_CLASS_EX(L"QTTabBarNative_ThumbnailTooltip", CS_DBLCLKS, COLOR_WINDOW);

    ThumbnailTooltipWindow() noexcept;
    ~ThumbnailTooltipWindow() override;

    BEGIN_MSG_MAP(ThumbnailTooltipWindow)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
    END_MSG_MAP()

    void ApplyConfiguration(const qttabbar::TipsSettings& settings);
    void ClearCache();
    void HideTooltip();
    bool ShowForPath(const std::wstring& path, const RECT& reference, HWND owner);

    static bool IsSupportedImageExtension(const std::wstring& extension,
                                          const qttabbar::TipsSettings& settings);
    static bool IsSupportedTextExtension(const std::wstring& extension,
                                         const qttabbar::TipsSettings& settings);

private:
    enum class Mode { None, Image, Text };

    struct CacheEntry {
        HBITMAP bitmap = nullptr;
        SIZE size{0, 0};
        FILETIME timestamp{};
    };

    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    bool EnsureWindow();
    bool LoadImage(const std::wstring& path, CacheEntry& entry);
    bool LoadTextPreview(const std::wstring& path, std::wstring& text) const;
    void DestroyCacheEntry(CacheEntry& entry);
    void UpdateWindowPlacement(const RECT& reference);

    qttabbar::TipsSettings m_settings{};
    std::unordered_map<std::wstring, CacheEntry> m_cache;
    Mode m_mode = Mode::None;
    HBITMAP m_bitmap = nullptr;
    SIZE m_bitmapSize{0, 0};
    std::wstring m_textPreview;
};
