#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "ConfigEnums.h"
#include "ConfigTypes.h"

namespace qttabbar {

ByteVector PidlFromDisplayName(const wchar_t* name);

struct WindowSettings {
    bool captureNewWindows = true;
    bool captureWeChatSelection = true;
    bool restoreSession = true;
    bool restoreOnlyLocked = false;
    bool closeBtnClosesUnlocked = false;
    bool closeBtnClosesSingleTab = true;
    bool trayOnClose = false;
    bool trayOnMinimize = false;
    bool autoHookWindow = false;
    bool showFailNavMsg = false;
    ByteVector defaultLocation;
};

struct TabsSettings {
    TabPos newTabPosition = TabPos::Rightmost;
    TabPos nextAfterClosed = TabPos::LastActive;
    bool activateNewTab = true;
    bool neverOpenSame = true;
    bool renameAmbTabs = true;
    bool dragOverTabOpensSDT = false;
    bool showFolderIcon = true;
    bool showSubDirTipOnTab = false;
    bool showDriveLetters = false;
    bool showCloseButtons = true;
    bool closeBtnsWithAlt = false;
    bool closeBtnsOnHover = false;
    bool showNavButtons = false;
    bool navButtonsOnRight = true;
    bool multipleTabRows = true;
    bool activeTabOnBottomRow = false;
    bool needPlusButton = true;
};

struct TweaksSettings {
    bool alwaysShowHeaders = true;
    bool killExtWhileRenaming = true;
    bool redirectLibraryFolders = false;
    bool f2Selection = false;
    bool wrapArrowKeySelection = true;
    bool backspaceUpLevel = true;
    bool horizontalScroll = true;
    bool forceSysListView = false;
    bool toggleFullRowSelect = false;
    bool detailsGridLines = false;
    bool alternateRowColors = false;
    ColorValue altRowBackgroundColor = ColorValue(0xFFFaf5f1);
    ColorValue altRowForegroundColor = ColorValue(0xFF000000);
};

struct TipsSettings {
    bool showSubDirTips = true;
    bool subDirTipsPreview = true;
    bool subDirTipsFiles = true;
    bool subDirTipsWithShift = false;
    bool showTooltipPreviews = true;
    bool showPreviewsWithShift = true;
    bool showPreviewInfo = true;
    int previewMaxWidth = 600;
    int previewMaxHeight = 400;
    FontConfig previewFont{L"微软雅黑", 9.0f, 0};
    StringList textExt;
    StringList imageExt;
};

struct MiscSettings {
    bool taskbarThumbnails = false;
    bool keepHistory = true;
    int tabHistoryCount = 15;
    bool keepRecentFiles = true;
    int fileHistoryCount = 15;
    int networkTimeout = 0;
    bool autoUpdate = true;
    bool soundBox = false;
    bool enableLog = false;
};

struct SkinSettings {
    bool useTabSkin = false;
    std::wstring tabImageFile;
    Padding tabSizeMargin{};
    Padding tabContentMargin{};
    int overlapPixels = 0;
    bool hitTestTransparent = false;
    int tabHeight = 30;
    int tabMinWidth = 100;
    int tabMaxWidth = 200;
    bool fixedWidthTabs = false;
    FontConfig tabTextFont{L"微软雅黑", 9.0f, 0};
    ColorValue toolBarTextColor{0xFF000000};
    ColorValue tabTextActiveColor{0xFF000000};
    ColorValue tabTextInactiveColor{0xFF000000};
    ColorValue tabTextHotColor{0xFF000000};
    ColorValue tabShadActiveColor{0xFFF5F6F7};
    ColorValue tabShadInactiveColor{0xFFF5F6F7};
    ColorValue tabShadHotColor{0xFFF5F6F7};
    bool tabTitleShadows = false;
    bool tabActiveTitleShadow = true;
    bool tabInactiveTitleShadow = true;
    bool tabHotTitleShadow = true;
    bool tabTextCentered = true;
    bool useRebarBGColor = true;
    ColorValue rebarColor{0xFFF5F6F7};
    bool useRebarImage = false;
    StretchMode rebarStretchMode = StretchMode::Tile;
    std::wstring rebarImageFile;
    bool rebarImageSeperateBars = false;
    Padding rebarSizeMargin{};
    bool activeTabInBold = true;
    bool skinAutoColorChangeClose = false;
    bool drawHorizontalExplorerBarBgColor = false;
    bool drawVerticalExplorerBarBgColor = false;
};

struct BBarSettings {
    IntVector buttonIndexes;
    StringVector activePluginIDs;
    bool largeButtons = true;
    bool lockSearchBarWidth = true;
    bool lockDropDownButtons = true;
    bool showButtonLabels = true;
    std::wstring imageStripPath;
};

struct MouseSettings {
    bool mouseScrollsHotWnd = false;
    MouseActionMap globalMouseActions;
    MouseActionMap tabActions;
    MouseActionMap barActions;
    MouseActionMap linkActions;
    MouseActionMap itemActions;
    MouseActionMap marginActions;
};

struct KeysSettings {
    IntVector shortcuts;
    std::map<std::wstring, IntVector> pluginShortcuts;
    bool useTabSwitcher = true;
};

struct PluginSettings {
    StringVector enabled;
};

struct LangSettings {
    StringVector pluginLangFiles;
    bool useLangFile = false;
    std::wstring langFile;
    std::wstring builtInLang = L"简体中文";
    int builtInLangSelectedIndex = 1;
};

struct DesktopSettings {
    int firstItem = 0;
    int secondItem = 1;
    int thirdItem = 2;
    int fourthItem = 3;
    bool groupExpanded = false;
    bool recentTabExpanded = false;
    bool applicationExpanded = false;
    bool recentFileExpanded = false;
    bool taskBarDblClickEnabled = true;
    bool desktopDblClickEnabled = true;
    bool lockMenu = false;
    bool titleBackground = false;
    bool includeGroup = false;
    bool includeRecentTab = true;
    bool includeApplication = true;
    bool includeRecentFile = true;
    bool oneClickMenu = false;
    bool enableAppShortcuts = true;
    int width = 12;
    int lstSelectedIndex = 0;
};

struct ConfigData {
    WindowSettings window;
    TabsSettings tabs;
    TweaksSettings tweaks;
    TipsSettings tips;
    MiscSettings misc;
    SkinSettings skin;
    BBarSettings bbar;
    MouseSettings mouse;
    KeysSettings keys;
    PluginSettings plugin;
    LangSettings lang;
    DesktopSettings desktop;

    ConfigData();
};

ConfigData LoadConfigFromRegistry();
void WriteConfigToRegistry(const ConfigData& config, bool desktopOnly = false);
void UpdateConfigSideEffects(ConfigData& config, bool broadcastChanges);

}  // namespace qttabbar
