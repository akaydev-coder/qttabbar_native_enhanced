#pragma once

#include <cstdint>

namespace qttabbar {

// Wrapper around managed TabPos enum.
enum class TabPos : uint32_t {
    Rightmost = 0,
    Right,
    Left,
    Leftmost,
    LastActive,
};

// Wrapper around managed StretchMode enum.
enum class StretchMode : uint32_t {
    Full = 0,
    Real,
    Tile,
};

// Wrapper around managed MouseTarget enum.
enum class MouseTarget : uint32_t {
    Anywhere = 0,
    Tab,
    TabBarBackground,
    FolderLink,
    ExplorerItem,
    ExplorerBackground,
};

// Flag style enum describing a mouse chord.
enum class MouseChord : uint32_t {
    None   = 0,
    Shift  = 1 << 0,
    Ctrl   = 1 << 1,
    Alt    = 1 << 2,
    Left   = 1 << 3,
    Right  = 1 << 4,
    Middle = 1 << 5,
    Double = 1 << 6,
    X1     = 1 << 7,
    X2     = 1 << 8,
};

inline MouseChord operator|(MouseChord lhs, MouseChord rhs) {
    return static_cast<MouseChord>(static_cast<uint32_t>(lhs) |
                                   static_cast<uint32_t>(rhs));
}

inline MouseChord& operator|=(MouseChord& lhs, MouseChord rhs) {
    lhs = lhs | rhs;
    return lhs;
}

inline MouseChord operator&(MouseChord lhs, MouseChord rhs) {
    return static_cast<MouseChord>(static_cast<uint32_t>(lhs) &
                                   static_cast<uint32_t>(rhs));
}

inline bool Any(MouseChord chord) {
    return static_cast<uint32_t>(chord) != 0u;
}

// NOTE: the numeric values must remain stable. Changing them breaks existing
// registry data because the managed implementation serialized these values.
enum class BindAction : uint32_t {
    GoBack = 0,
    GoForward,
    GoFirst,
    GoLast,
    NextTab,
    PreviousTab,
    FirstTab,
    LastTab,
    SwitchToLastActivated,
    NewTab,
    NewWindow,
    MergeWindows,
    CloseCurrent,
    CloseAllButCurrent,
    CloseLeft,
    CloseRight,
    CloseWindow,
    RestoreLastClosed,
    CloneCurrent,
    TearOffCurrent,
    LockCurrent,
    LockAll,
    BrowseFolder,
    CreateNewGroup,
    ShowOptions,
    ShowToolbarMenu,
    ShowTabMenuCurrent,
    ShowGroupMenu,
    ShowUserAppsMenu,
    ShowRecentTabsMenu,
    ShowRecentFilesMenu,
    NewFile,
    NewFolder,
    CopySelectedPaths,
    CopySelectedNames,
    CopyCurrentFolderPath,
    CopyCurrentFolderName,
    ChecksumSelected,
    ToggleTopMost,
    TransparencyPlus,
    TransparencyMinus,
    FocusFileList,
    FocusSearchBarReal,
    FocusSearchBarBBar,
    ShowSDTSelected,
    SendToTray,
    FocusTabBar,
    SortTabsByName,
    SortTabsByPath,
    SortTabsByActive,
    KEYBOARD_ACTION_COUNT,
    Nothing = 1000,
    UpOneLevel,
    Refresh,
    Paste,
    Maximize,
    Minimize,
    ItemOpenInNewTab,
    ItemOpenInNewTabNoSel,
    ItemOpenInNewWindow,
    ItemCut,
    ItemCopy,
    ItemDelete,
    ItemProperties,
    CopyItemPath,
    CopyItemName,
    ChecksumItem,
    CloseTab,
    CloseLeftTab,
    CloseRightTab,
    UpOneLevelTab,
    LockTab,
    ShowTabMenu,
    TearOffTab,
    CloneTab,
    CopyTabPath,
    TabProperties,
    ShowTabSubfolderMenu,
    CloseAllButThis,
    OpenCmd,
    ItemsOpenInNewTabNoSel,
    SortTab,
    TurnOffRepeat,
    KEYBOARD_ACTION_COUNT2,
};

}  // namespace qttabbar

