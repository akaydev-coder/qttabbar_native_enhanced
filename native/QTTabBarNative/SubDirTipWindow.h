#pragma once

#include <atlbase.h>
#include <atlwin.h>

#include <optional>
#include <string>
#include <vector>

#include "Config.h"

class TabBarHost;
class ThumbnailTooltipWindow;

class SubDirTipWindow final : public CWindowImpl<SubDirTipWindow> {
public:
    enum class Command {
        Open,
        OpenNewTab,
        OpenNewWindow,
    };

    struct Item {
        std::wstring name;
        std::wstring path;
        bool isDirectory = false;
        FILETIME modified{};
    };

    DECLARE_WND_CLASS_EX(L"QTTabBarNative_SubDirTip", CS_DBLCLKS, COLOR_WINDOW);

    explicit SubDirTipWindow(TabBarHost& owner) noexcept;
    ~SubDirTipWindow() override;

    BEGIN_MSG_MAP(SubDirTipWindow)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_SHOWWINDOW, OnShowWindow)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_KILLFOCUS, OnKillFocus)
        MESSAGE_HANDLER(WM_NOTIFY, OnNotify)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnBeginDrag)
    END_MSG_MAP()

    void ApplyConfiguration(const qttabbar::ConfigData& config);
    bool ShowForPath(const std::wstring& path, const POINT& anchor, bool byKeyboard);
    bool ShowAndExecute(const std::wstring& path, const POINT& anchor, Command command, bool byKeyboard);
    void HideTip();
    void ClearThumbnailCache();

private:
    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnShowWindow(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnKillFocus(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnBeginDrag(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    void EnsureListView();
    void PopulateItems(const std::wstring& path);
    void AddItemToList(std::size_t index, const Item& item);
    void UpdateColumns();
    void ExecuteCommand(const std::vector<int>& indices, Command command);
    void ShowContextMenu(const POINT& screenPoint);
    void BeginDragOperation(const std::vector<int>& indices);
    void UpdateThumbnailPreview(int hotItem);
    void UpdateAnchorRect();

    TabBarHost& m_owner;
    qttabbar::ConfigData m_config{};
    CWindow m_listView;
    HIMAGELIST m_imageList = nullptr;
    std::vector<Item> m_items;
    std::wstring m_currentPath;
    std::unique_ptr<ThumbnailTooltipWindow> m_thumbnailTooltip;
    RECT m_anchorRect{};
    bool m_showing = false;
};

