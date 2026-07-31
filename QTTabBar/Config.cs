/* File Info 
 * Author:      indiff
 * CreateTime:  2021/1/5����1:58:08
 * LastEditor:  indiff
 * ModifyTime:  2021/8/28����7:47:22
 * Description: 
*/
//    This file is part of QTTabBar, a shell extension for Microsoft
//    Windows Explorer.
//    Copyright (C) 2007-2024  Quizo, Paul Accisano, indiff
//
//    QTTabBar is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    QTTabBar is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with QTTabBar.  If not, see <http://www.gnu.org/licenses/>.

using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Runtime.Serialization.Json;
using System.Text;
using System.Linq;
using System.Windows.Forms;
using Microsoft.Win32;
using Padding = System.Windows.Forms.Padding;
using Key = System.Windows.Forms.Keys;

namespace QTTabBarLib {

     // Wrapper class to get around  Font serialization stupidity
    [Serializable]
    public class XmlSerializableFont {
        public string FontName { get; set; }
        public float FontSize { get; set; }
        public FontStyle FontStyle { get; set; }

        public static XmlSerializableFont FromFont(Font font) {
            return font == null ? null : new XmlSerializableFont
                    {FontName = font.Name, FontSize = font.Size, FontStyle = font.Style};
        }

        public Font ToFont() {
            return ToFont(this);
        }

        public static Font ToFont(XmlSerializableFont xmlSerializableFont) {
            try {
                return new Font(
                        xmlSerializableFont.FontName,
                        xmlSerializableFont.FontSize,
                        xmlSerializableFont.FontStyle);
            }
            catch(ArgumentException) {
                return (Font)Control.DefaultFont.Clone();
            }
        }
    }

   /* 
    * @����: ��ǩλ��
    */     
    public enum TabPos {
        Rightmost,
        Right,
        Left,
        Leftmost,
        LastActive,
    }

   /* 
    * @����: ����ģʽ
    */   
    public enum StretchMode {
        Full,
        Real,
        Tile,
    }

    public enum TabDropDefaultEffect {
        SystemDefault,
        Copy,
        Move,
        Link,
    }

    public enum TabDropHoverAction {
        None,
        SelectTab,
        ShowSubfolderMenu,
    }

   /* 
    * @����: ����Ŀ��
    */   
    public enum MouseTarget {
        Anywhere,
        Tab,
        TabBarBackground,
        FolderLink,
        ExplorerItem,
        ExplorerBackground,
        Breadcrumbs
    }

   /* 
    * @����: �����
    */  
    [Flags]
    public enum MouseChord {
        None    =   0,
        Shift   =   1,
        Ctrl    =   2,
        Alt     =   4,
        Left    =   8,
        Right   =  16,
        Middle  =  32,
        Double  =  64,
        X1      = 128,
        X2      = 256,
    }

    /* 
    * @����: �󶨶���
    */  
    // WARNING
    // reordering these will break existing settings.
    public enum BindAction
    {
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
        /***** add by indiff end *****/
       // AddToGroup,  // ��������ǩ�� �� ����һ���²���������
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
        // Mouse-only from here on down

        Nothing = QTUtility.FIRST_MOUSE_ONLY_ACTION,
        UpOneLevel,
        Refresh,
        Paste,
        Maximize,
        Minimize,

        // Item Actions
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

        // Tab Actions
        CloseTab,
        CloseLeftTab,
        CloseRightTab,
        UpOneLevelTab, //hmm
        LockTab,
        ShowTabMenu,
        TearOffTab,
        CloneTab,
        CopyTabPath,
        TabProperties,
        ShowTabSubfolderMenu,
        CloseAllButThis,

        /******* add by indiff start *****/
        // add by indiff 2012 08 10
        OpenCmd
        ,
        ItemsOpenInNewTabNoSel //add bool indiff 2012 08 12
            /***** add by indiff end *****/

          /******* add by indiff start *****/
            // add by indiff 2019 12 16 19:27
        , SortTab 
        , TurnOffRepeat
        //add bool indiff 2019 12 16 19:27
        , KEYBOARD_ACTION_COUNT2
    }

    [Serializable]
    public class Config {
		// Shortcuts to the loaded config, for convenience.
        public static _Window Window    { get { return ConfigManager.LoadedConfig.window; } }	/*������Ϊ*/
        public static _Tabs Tabs        { get { return ConfigManager.LoadedConfig.tabs; } }		/*��ǩ��Ϊ*/
        public static _Tweaks Tweaks    { get { return ConfigManager.LoadedConfig.tweaks; } }	/*��������*/
        public static _Tips Tips        { get { return ConfigManager.LoadedConfig.tips; } }		/*Ԥ����ʾ*/
        public static _Misc Misc        { get { return ConfigManager.LoadedConfig.misc; } }		/*����ѡ��*/
        public static _Skin Skin        { get { return ConfigManager.LoadedConfig.skin; } }		/*��ǩ���*/
        public static _DragDrop DragDrop { get { return ConfigManager.LoadedConfig.dragdrop; } }
        public static _BBar BBar        { get { return ConfigManager.LoadedConfig.bbar; } }		/*��ťѡ��*/
        public static _Mouse Mouse      { get { return ConfigManager.LoadedConfig.mouse; } }	/*������*/
        public static _Keys Keys        { get { return ConfigManager.LoadedConfig.keys; } }		/*��ݲ���*/
        public static _Plugin Plugin    { get { return ConfigManager.LoadedConfig.plugin; } }	/*�������*/
        public static _Lang Lang        { get { return ConfigManager.LoadedConfig.lang; } }		/*��������*/
        public static _Desktop Desktop { get { return ConfigManager.LoadedConfig.desktop; } }   /*������Ϣ*/

        public _Window window   { get; set; }
        public _Tabs tabs       { get; set; }
        public _Tweaks tweaks   { get; set; }
        public _Tips tips       { get; set; }
        public _Misc misc       { get; set; }
        public _Skin skin       { get; set; }
        public _DragDrop dragdrop { get; set; }
        public _BBar bbar       { get; set; }
        public _Mouse mouse     { get; set; }
        public _Keys keys       { get; set; }
        public _Plugin plugin   { get; set; }
        public _Lang lang       { get; set; }
        public _Desktop desktop { get; set; }

        public Config() {
            window = new _Window();
            tabs = new _Tabs();
            tweaks = new _Tweaks();
            tips = new _Tips();
            misc = new _Misc();
            skin = new _Skin();
            dragdrop = new _DragDrop();
            bbar = new _BBar();
            mouse = new _Mouse();
            keys = new _Keys();
            plugin = new _Plugin();
            lang = new _Lang();
            desktop = new _Desktop();
        }

        [Serializable]
        public class _Window {
            public bool CaptureNewWindows        { get; set; }
            public bool CaptureWeChatSelection   { get; set; } // �Ƿ񲶻�΢�š�qq�������Ĵ��ļ�ѡ��״̬
            public bool RestoreSession           { get; set; }
            public bool RestoreOnlyLocked        { get; set; }
            public bool CloseBtnClosesUnlocked   { get; set; }
            public bool CloseBtnClosesSingleTab  { get; set; }
            public bool TrayOnClose              { get; set; }
            public bool TrayOnMinimize           { get; set; }
            public bool AutoHookWindow           { get; set; }
            public bool ShowFailNavMsg           { get; set; } // SHOW_FAIL_NAV_MSG
           
            public byte[] DefaultLocation        { get; set; }

            public _Window() {
              /*  CaptureNewWindows = false;
                RestoreSession = false;
                RestoreOnlyLocked = false;
                CloseBtnClosesSingleTab = true;
                CloseBtnClosesUnlocked = false;
                TrayOnClose = false;
                TrayOnMinimize = false;*/

                /* qwop's default value. */
                CaptureNewWindows = true;
                if (QTUtility.IsThanWin11)
                {
                    CaptureWeChatSelection = false;
                }
                else
                {
                    CaptureWeChatSelection = true;
                }
                
                RestoreSession = true;
                RestoreOnlyLocked = false;
                CloseBtnClosesUnlocked = false;
                CloseBtnClosesSingleTab = true;
                TrayOnClose = false;
                TrayOnMinimize = false;
                // Ĭ�Ϲر��Զ�����hook
                AutoHookWindow = false;
  //              string idl = Environment.OSVersion.Version >= new Version(6, 1)
  //                       ? "::{031E4825-7B94-4DC3-B131-E946B44C8DD5}"  // Libraries
  //                     : "::{20D04FE0-3AEA-1069-A2D8-08002B30309D}"; // Computer
                string idl = "::{20D04FE0-3AEA-1069-A2D8-08002B30309D}"; // �ҵĵ��ԣ� Ĭ�ϴ�
                using(IDLWrapper w = new IDLWrapper(idl)) {
                    DefaultLocation = w.IDL;
                }
                // ����ʾ����ʧ�ܵ���ʾ��
                ShowFailNavMsg = false;
            }
        }

        [Serializable]
        public class _Tabs {
            public TabPos NewTabPosition         { get; set; }
            public TabPos NextAfterClosed        { get; set; }
            public bool ActivateNewTab           { get; set; }
            public bool NeverOpenSame            { get; set; }
            public bool RenameAmbTabs            { get; set; }
            public bool DragOverTabOpensSDT      { get; set; }
            public bool ShowFolderIcon           { get; set; }
            public bool ShowSubDirTipOnTab       { get; set; }
            public bool ShowDriveLetters         { get; set; }
            public bool ShowCloseButtons         { get; set; }
            public bool CloseBtnsWithAlt         { get; set; }
            public bool CloseBtnsOnHover         { get; set; }
            public bool ShowNavButtons           { get; set; }
            public bool NavButtonsOnRight        { get; set; }
            public bool MultipleTabRows          { get; set; }
            public bool ActiveTabOnBottomRow     { get; set; }
            public bool NeedPlusButton           { get; set; }
            public bool TabSwitchAnimation       { get; set; }

            public _Tabs() {
               /* NewTabPosition = TabPos.Rightmost;
                NextAfterClosed = TabPos.LastActive;
                ActivateNewTab = true;
                NeverOpenSame = true;
                RenameAmbTabs = false;
                DragOverTabOpensSDT = false;
                ShowFolderIcon = true;
                ShowSubDirTipOnTab = true;
                ShowDriveLetters = false;
                ShowCloseButtons = false;
                CloseBtnsWithAlt = false;
                CloseBtnsOnHover = false;
                ShowNavButtons = false;
                MultipleTabRows = true;
                ActiveTabOnBottomRow = true;*/

                /* qwop's default's value.*/
                NewTabPosition = TabPos.Rightmost;  // �±�ǩ�����Ҳ�
                NextAfterClosed = TabPos.LastActive; // �رձ�ǩʱ���л���
                ActivateNewTab = true;  // �Ƿ������л����±�ǩ
                NeverOpenSame = true;   //  �����±�ǩ
                RenameAmbTabs = true;  // ģ����������ǩ
                DragOverTabOpensSDT = false; // ��ק��ǩ��Ĳ���
                ShowFolderIcon = true;  // ��ʾ������ͼ��
                ShowSubDirTipOnTab = false;  // ���ļ�����ʾ�ӱ�ǩ
                ShowDriveLetters = false; // ��ʾ�������ַ�
                ShowCloseButtons = true;  // ��ʾ�رհ�ť
                CloseBtnsWithAlt = false; // ֻ��alt��ס���йر�
                CloseBtnsOnHover = false;  // ����������ر�
                ShowNavButtons = false;  // �ڱ�ǩ��ʾ������ť (	(Ĭ�����ùرհ�ť��ʾ)��ǩ���Ҳ��������ť�ܷ�����һ��ѡ���Կ����Ƿ�ȥ���ء�#28��
                NavButtonsOnRight = true; // �����ұ�
                MultipleTabRows = true; // �������б�ǩ
                ActiveTabOnBottomRow = false; // ʼ�ս����ǩ���ڵײ���
                NeedPlusButton = true; // �Ƿ���ʾ��ɫ������ǩ��ť
                TabSwitchAnimation = false;
            }
        }

        [Serializable]
        public class _DragDrop {
            public bool TabDragSourceEnabled { get; set; }
            public bool TabDragSourceCreatesWindow { get; set; }
            public bool TabDropTargetEnabled { get; set; }
            public TabDropDefaultEffect TabDropDefaultEffect { get; set; }
            public TabDropHoverAction TabDropHoverAction { get; set; }
            public int TabDropHoverTime { get; set; }
            public bool TabDropAcceptSameTabBar { get; set; }
            public bool TabBarDropTargetEnabled { get; set; }
            public bool TabBarDropAcceptSameTabBar { get; set; }
            public bool TabBarDropAllowDuplicate { get; set; }

            public _DragDrop() {
                TabDragSourceEnabled = true;
                TabDragSourceCreatesWindow = true;
                TabDropTargetEnabled = true;
                TabDropDefaultEffect = TabDropDefaultEffect.SystemDefault;
                TabDropHoverAction = TabDropHoverAction.ShowSubfolderMenu;
                TabDropHoverTime = 700;
                TabDropAcceptSameTabBar = true;
                TabBarDropTargetEnabled = true;
                TabBarDropAcceptSameTabBar = true;
                TabBarDropAllowDuplicate = true;
            }
        }

        [Serializable]
        public class _Tweaks {
            public bool AlwaysShowHeaders        { get; set; }
            public bool KillExtWhileRenaming     { get; set; }
            public bool RedirectLibraryFolders   { get; set; }
            public bool F2Selection              { get; set; }
            public bool WrapArrowKeySelection    { get; set; }
            public bool BackspaceUpLevel         { get; set; }
            public bool HorizontalScroll         { get; set; }
            public bool ForceSysListView         { get; set; }
            public bool ToggleFullRowSelect      { get; set; }
            public bool DetailsGridLines         { get; set; }
            public bool AlternateRowColors       { get; set; }
            public Color AltRowBackgroundColor   { get; set; }
            public Color AltRowForegroundColor   { get; set; }

            public _Tweaks() {
               /* AlwaysShowHeaders = !QTUtility.IsXP && !QTUtility.IsWin7;
                KillExtWhileRenaming = true;
                RedirectLibraryFolders = false;
                F2Selection = true;
                WrapArrowKeySelection = false;
                BackspaceUpLevel = QTUtility.IsXP;
                HorizontalScroll = true;
                ForceSysListView = false;
                ToggleFullRowSelect = false;
                DetailsGridLines = false;
                AlternateRowColors = false;
                AltRowForegroundColor = SystemColors.WindowText;
                AltRowBackgroundColor = QTUtility2.MakeColor(0xfaf5f1); */

                /* qwop's default value.*/
                if (QTUtility.IsWin7)
                {
                    AlwaysShowHeaders = true;  // ��ʾ�б���
                }
                else {
                    AlwaysShowHeaders = false;  // ��ʾ�б���
                }
                
                RedirectLibraryFolders = false; // ʹ�ÿ��ļ���
                KillExtWhileRenaming = true;  // ������ʱ�򣬲�ʹ����չ��
                F2Selection = false; // ����F2����������ѡ��
                WrapArrowKeySelection = true; // ʹ�ü�ͷ��ʱ����ѡ���ļ���
                BackspaceUpLevel = true;  // backupspace ���ص���һ��Ŀ¼
                HorizontalScroll = true;  // ͬʱ��סshift����ˮƽ����
                ForceSysListView = false; // ���þɰ��б���ͼ�ؼ�
                ToggleFullRowSelect = QTUtility.IsXP; // ��ϸ��ͼѡ������
                DetailsGridLines = false;  // ������
                AlternateRowColors = false;// ��������ɫ
                AltRowForegroundColor = SystemColors.WindowText; // ǰ��ɫ
                AltRowBackgroundColor = QTUtility2.MakeColor(0xfaf5f1); // ����ɫ
            }
        }

        [Serializable]
        public class _Tips {
            public bool ShowSubDirTips           { get; set; }
            public bool SubDirTipsPreview        { get; set; }
            public bool SubDirTipsFiles          { get; set; }
            public bool SubDirTipsWithShift      { get; set; }
            public bool ShowTooltipPreviews      { get; set; }
            public bool ShowPreviewsWithShift    { get; set; }
            public bool ShowPreviewInfo          { get; set; }
            public bool ShowDetailedTooltip      { get; set; }
            public int PreviewMaxWidth           { get; set; }
            public int PreviewMaxHeight          { get; set; }
            public int PreviewDelay              { get; set; }
            public int PreviewOpacity            { get; set; }
            public int PreviewCacheCapacity      { get; set; }
            public Font PreviewFont              { get; set; }
            public List<string> TextExt          { get; set; }
            public List<string> ImageExt         { get; set; }
            
            public _Tips() {
                /*ShowSubDirTips = true;
                 SubDirTipsPreview = true;
                 SubDirTipsFiles = true;
                 SubDirTipsWithShift = false;
                 ShowTooltipPreviews = true;
                 ShowPreviewsWithShift = false;
                 ShowPreviewInfo = true;
                 PreviewMaxWidth = 512;
                 PreviewMaxHeight = 256;
                 PreviewFont = Control.DefaultFont;
                 TextExt = new List<string> {".txt", ".ini", ".inf" ,".cs", ".log", ".js", ".vbs"};
                 ImageExt = ThumbnailTooltipForm.MakeDefaultImgExts();*/

                ShowSubDirTips = true;  // �Ƿ���ʾ��Ŀ¼��ʾ
                SubDirTipsPreview = true;  // ��Ŀ¼��ʾԤ��
                SubDirTipsFiles = true;  // ��Ŀ¼��ʾ�ļ�
                SubDirTipsWithShift = false ; // ����shift��������ʾ��Ŀ¼
                ShowTooltipPreviews = true;  
                ShowPreviewInfo = true; // �����ļ�Ԥ��
                ShowDetailedTooltip = false;
                ShowPreviewsWithShift = true; // ����shift������, �����ļ�Ԥ��
                
                // Ԥ���Ŀ���
                PreviewMaxWidth = 600;
                PreviewMaxHeight = 400;
                PreviewDelay = 300;
                PreviewOpacity = 100;
                PreviewCacheCapacity = 128;
                //  PreviewMaxWidth = 512;
                // PreviewMaxHeight = 256;
                //  ��������
                PreviewFont = Control.DefaultFont;
                PreviewFont = new Font(Control.DefaultFont.FontFamily, 9f);
                // �ı�������չ�� ���� yml yaml �ļ�֧��
                TextExt = new List<string> { 
                    ".txt",".rtf"
                    ,".ini", ".inf",".properties", ".ruleset", ".settings"
                    ,".cs", ".log"
                    ,".js", ".vbs", ".bat", ".cmd", ".sh"
                    ,".c",".cpp",".cc",".h", ".rc"
                    ,".xml"
                    ,".yml",".yaml"
                    ,".htm",".html",".mht",".mhtml", ".shtml", ".hta"
                    ,".HxT",".HxC",".hhc",".hhk", ".hhp"
                    ,".java"
                    ,".sql"
                    ,".csv"
                    ,".md" 

                    ,".m" 
                    ,".reg" 

                    ,".wxl" 
                    ,".wxs" 
                    
                    ,".py", ".rb"
                    ,".jsp", ".asp", ".php",".aspx"
                    ,".resx",".xaml",  ".config", ".manifest", ".csproj", ".vbproj"
                };
                // ����Ĭ�ϵ�ͼ����չ��
                ImageExt = ThumbnailTooltipForm.MakeDefaultImgExts();
            }
        }

        [Serializable]
        public class _Misc {
            public bool TaskbarThumbnails        { get; set; }
            public bool KeepHistory              { get; set; }
            public int TabHistoryCount           { get; set; }
            public bool KeepRecentFiles          { get; set; }
            public int FileHistoryCount          { get; set; }
            public int NetworkTimeout            { get; set; }
            public bool AutoUpdate               { get; set; }
            public bool SoundBox { get; set; }
            public bool EnableLog { get; set; }

            public _Misc() {
                TaskbarThumbnails = false;
                KeepHistory = true;
                TabHistoryCount = 15;
                KeepRecentFiles = true;
                FileHistoryCount = 15;
                NetworkTimeout = 0;
                AutoUpdate = true;
                // Ĭ�Ϲر���������
                SoundBox = false;
                // Ĭ�ϲ�������־����
                EnableLog = false;
            }
        }

        [Serializable]
        public class _Skin {
            public bool UseTabSkin               { get; set; }
            public string TabImageFile           { get; set; }
            public bool UsePlusButtonImage        { get; set; }
            public string PlusButtonImageFile     { get; set; }
            public bool UseCloseButtonImage       { get; set; }
            public string CloseButtonImageFile    { get; set; }
            public int CloseButtonImageOffsetX    { get; set; }
            public int CloseButtonImageOffsetY    { get; set; }
            public bool UseLockIconImage          { get; set; }
            public string LockIconImageFile       { get; set; }
            public int LockIconImageOffsetX       { get; set; }
            public int LockIconImageOffsetY       { get; set; }
            public Padding TabSizeMargin         { get; set; }
            public Padding TabContentMargin      { get; set; }
            public int OverlapPixels             { get; set; }
            public bool HitTestTransparent       { get; set; }
            public int TabHeight                 { get; set; }
            public int TabMinWidth               { get; set; }
            public int TabMaxWidth               { get; set; }
            public bool FixedWidthTabs           { get; set; }
            public Font TabTextFont              { get; set; }
            public Color ToolBarTextColor        { get; set; }
            public Color TabTextActiveColor      { get; set; }
            public Color TabTextInactiveColor    { get; set; }
            public Color TabTextHotColor         { get; set; }
            public Color TabShadActiveColor      { get; set; }
            public Color TabShadInactiveColor    { get; set; }
            public Color TabShadHotColor         { get; set; }
            public bool TabTitleShadows          { get; set; }
            public bool TabActiveTitleShadow     { get; set; }
            public bool TabInactiveTitleShadow   { get; set; }
            public bool TabHotTitleShadow        { get; set; }
            public bool TabTextCentered          { get; set; }
            public bool UseRebarBGColor          { get; set; }
            public Color RebarColor              { get; set; }
            public bool UseRebarImage            { get; set; }
            public StretchMode RebarStretchMode  { get; set; }
            public string RebarImageFile         { get; set; }
            public bool RebarImageSeperateBars   { get; set; }
            public Padding RebarSizeMargin       { get; set; }
            public bool UseFluentExplorerGlass   { get; set; }
            public int FluentGlassAddressMode    { get; set; }
            public int FluentGlassAddressExtraPixels { get; set; }
            public Color FluentGlassInactiveColor { get; set; }
            public bool FluentGlassApplyCaptionColor { get; set; }
            public bool FluentGlassApplyBorderColor { get; set; }
            public bool FluentGlassSuppressQtChildErase { get; set; }
            public bool ActiveTabInBold          { get; set; }
            public bool SkinAutoColorChangeClose              { get; set; }
            public bool DrawHorizontalExplorerBarBgColor { get; set; }
            public bool DrawVerticalExplorerBarBgColor { get; set; }

            public _Skin() {
                /* UseTabSkin = false;
                 TabImageFile = "";
                 TabSizeMargin = Padding.Empty;
                 TabContentMargin = Padding.Empty;
                 OverlapPixels = 0;
                 HitTestTransparent = false;
                 TabHeight = 24;
                 TabMinWidth = 50;
                 TabMaxWidth = 200;
                 FixedWidthTabs = false;
                 TabTextFont = Control.DefaultFont;
                 TabTextActiveColor = Color.Black;
                 TabTextInactiveColor = Color.Black;
                 TabTextHotColor = Color.Black;
                 TabShadActiveColor = Color.Gray;
                 TabShadInactiveColor = Color.White;
                 TabShadHotColor = Color.White;
                 TabTitleShadows = false;
                 TabActiveTitleShadow = true;
                 TabInactiveTitleShadow = true;
                 TabHotTitleShadow = true;
                 TabTextCentered = false;
                 UseRebarBGColor = false;
                 RebarColor = Color.Gray;
                 UseRebarImage = false;
                 RebarStretchMode = StretchMode.Full;
                 RebarImageFile = "";
                 RebarImageSeperateBars = false;
                 RebarSizeMargin = Padding.Empty;
                 ActiveTabInBold = false;*/

                /* qwop's default value. */
                UseTabSkin = false;  // ��ǩ����
                TabImageFile = "";  // ��ǩ�����ļ�
                UsePlusButtonImage = false;
                PlusButtonImageFile = "";
                UseCloseButtonImage = false;
                CloseButtonImageFile = "";
                CloseButtonImageOffsetX = 0;
                CloseButtonImageOffsetY = 0;
                UseLockIconImage = false;
                LockIconImageFile = "";
                LockIconImageOffsetX = 0;
                LockIconImageOffsetY = 0;
                TabSizeMargin = Padding.Empty;  // ���ñ�Ե
                TabContentMargin = Padding.Empty; // ���ݱ�Ե
                OverlapPixels = 0;  // 
                HitTestTransparent = false;
                TabHeight = 30;  // �߶�
                TabMinWidth = 100;  // ��С����
                TabMaxWidth = 200;  // ������
                FixedWidthTabs = false;
                TabTextFont = new Font(Control.DefaultFont.FontFamily, 9f);
                ToolBarTextColor = Color.Black;  // �������ı���ɫ
                TabTextActiveColor = Color.Black;
                TabTextInactiveColor = Color.Black;
                TabTextHotColor = Color.Black;
                // TabShadActiveColor = Color.Gray;
                TabShadActiveColor = Color.WhiteSmoke;
                TabShadInactiveColor = Color.WhiteSmoke;
                TabShadHotColor = Color.WhiteSmoke;
                RebarColor = Color.WhiteSmoke;
                // RebarColor = Color.FromArgb(230,230,230);
                // ���ñ�ǩ����ɫ
                // RebarColor = Color.FromArgb(245, 246, 247);
                TabTitleShadows = false;  // ��ǩ�ı���Ӱ�Ƿ�����
                TabActiveTitleShadow = true;
                TabInactiveTitleShadow = true;
                TabHotTitleShadow = true;
                TabTextCentered = true; // ��ǩ�ı��Ƿ����
                UseRebarBGColor = true;  // �Ƿ��������ñ�����ɫ
                UseRebarImage = false;  // �Ƿ񹤾����Զ���ͼƬ,�����Զ���ͼƬ
                RebarStretchMode = StretchMode.Tile;  // ���ַ�ʽ
                RebarImageFile = "";  // �������Զ���ͼƬ
                RebarImageSeperateBars = false;
                RebarSizeMargin = Padding.Empty;
                UseFluentExplorerGlass = true;
                FluentGlassAddressMode = 1;
                FluentGlassAddressExtraPixels = 40;
                FluentGlassInactiveColor = Color.FromArgb(255, 32, 32, 32);
                FluentGlassApplyCaptionColor = true;
                FluentGlassApplyBorderColor = true;
                FluentGlassSuppressQtChildErase = true;
                ActiveTabInBold = true;
                SkinAutoColorChangeClose = false;  // �Ƿ�ر��Զ���ɫ��
                DrawHorizontalExplorerBarBgColor = false; // 
                DrawVerticalExplorerBarBgColor = false; // 
            }
			
			// �л���ɫ����ģʽʱ���л���ɫ
            internal void SwitchNighMode(bool isNighMode)
            {
                if (this.SkinAutoColorChangeClose)
                {
                    return;
                }

                if (isNighMode)
                {
                    QTUtility2.log("change nightMode white skinChanged " + this.SkinAutoColorChangeClose);
                    // UseTabSkin = false;  // ��ǩ����
                    // TabImageFile = "";  // ��ǩ�����ļ�
                    // TabSizeMargin = Padding.Empty;  // ���ñ�Ե
                    // TabContentMargin = Padding.Empty; // ���ݱ�Ե
                    // OverlapPixels = 0;  // 
                    // HitTestTransparent = false;
                    // TabHeight = 24;
                    // TabMinWidth = 50;
                    // TabMaxWidth = 200;
                    // FixedWidthTabs = false;
                    // TabTextFont = new Font(Control.DefaultFont.FontFamily, 9f);
                    ToolBarTextColor = Color.White;
                    TabTextActiveColor = Color.White;
                    TabTextInactiveColor = Color.White;
                    TabTextHotColor = Color.White;
                    TabShadActiveColor = Color.Black;
                    TabShadInactiveColor = Color.Black;
                    TabShadHotColor = Color.Black;
                    RebarColor = Color.Black;
                    UseRebarBGColor = true;
                    // TabTitleShadows = false;
                    // TabTextCentered = false;
                    // UseRebarBGColor = false;
                    // RebarColor = Color.FromArgb(230,230,230);
                    // ���ñ�ǩ����ɫ
                    
                    // UseRebarImage = false;  // �Ƿ񹤾����Զ���ͼƬ,�����Զ���ͼƬ
                    // RebarStretchMode = StretchMode.Tile;
                    // RebarImageFile = "";  // �������Զ���ͼƬ
                    // RebarImageSeperateBars = false;
                    // RebarSizeMargin = Padding.Empty;
                    // ActiveTabInBold = true;
                }
                else
                {
                    // UseTabSkin = false;  // ��ǩ����
                    // TabImageFile = "";  // ��ǩ�����ļ�
                    // TabSizeMargin = Padding.Empty;  // ���ñ�Ե
                    // TabContentMargin = Padding.Empty; // ���ݱ�Ե
                    // OverlapPixels = 0;  // 
                    // HitTestTransparent = false;
                    // TabHeight = 24;
                    // TabMinWidth = 50;
                    // TabMaxWidth = 200;
                    // FixedWidthTabs = false;
                    // TabTextFont = new Font(Control.DefaultFont.FontFamily, 9f);
                    QTUtility2.log("change nightMode black skinChanged " + this.SkinAutoColorChangeClose);
                    ToolBarTextColor = Color.Black;
                    TabTextActiveColor = Color.Black;
                    TabTextInactiveColor = Color.Black;
                    TabTextHotColor = Color.Black;
                    // TabShadActiveColor = Color.FromArgb(245, 246, 247);
                    TabShadActiveColor = Color.White;
                    TabShadInactiveColor = Color.White;
                    TabShadHotColor = Color.White;
                    // RebarColor = Color.FromArgb(245, 246, 247);
                    RebarColor = Color.White;
                    // TabTitleShadows = false;
                    // TabTextCentered = false;
                    UseRebarBGColor = true;
                    // // RebarColor = Color.FromArgb(230,230,230);
                    // // ���ñ�ǩ����ɫ
                    // UseRebarImage = false;  // �Ƿ񹤾����Զ���ͼƬ,�����Զ���ͼƬ
                    // RebarStretchMode = StretchMode.Tile;
                    // RebarImageFile = "";  // �������Զ���ͼƬ
                    // RebarImageSeperateBars = false;
                    // RebarSizeMargin = Padding.Empty;
                    // ActiveTabInBold = true;
                }
                SkinAutoColorChangeClose = false;
            }
        }

        [Serializable]
        public class _BBar {
            public int[] ButtonIndexes           { get; set; }
            public string[] ActivePluginIDs      { get; set; }
            public bool LargeButtons             { get; set; }
            public bool LockSearchBarWidth       { get; set; }
            public bool LockDropDownButtons      { get; set; }
            public bool ShowButtonLabels         { get; set; }
            public string ImageStripPath         { get; set; }
            
            public _BBar() {
                /* // the old 
                ButtonIndexes = QTUtility.IsXP 
                        ? new int[] {1, 2, 0, 3, 4, 5, 0, 6, 7, 0, 11, 13, 12, 14, 15, 0, 9, 20} 
                        : new int[] {3, 4, 5, 0, 6, 7, 0, 11, 13, 12, 14, 15, 0, 9, 20};
                ActivePluginIDs = new string[0];
                LockDropDownButtons = false;
                LargeButtons = true;
                LockSearchBarWidth = false;
                ShowButtonLabels = false;
                ImageStripPath = ""; */

                /* indiff 's default. */
                ButtonIndexes	=	QTUtility.IsXP
                        // ? new int[] { 1, 2, 0, 3, 4, 5, 0, 6, 7, 0, 11, 13, 12, 14, 15, 0, 21, 9, 20  }
                        // : new System.Int32[] { 3, 4, 5, 0, 6, 7, 0, 17, 11, 12, 14, 15, 13, 0, 21, 9, 19, 10 };
                // ȥ���ָ���
                        ? new int[] { 1, 2,  3, 4, 5,  6, 7,  11, 13, 12, 14, 15,  21, 9, 20  }
                        : new System.Int32[] { 3, 4, 5, 6, 7,  17, 11, 12, 14, 15, 13,  21, 9, 19, 10 };
                ActivePluginIDs = new string[0];
                LargeButtons	= true;  // �Ƿ���ʾ��ť
                LockSearchBarWidth	=	true;  // �����������С
                LockDropDownButtons	=	true;  // ����������ť�˵�˳��
                ShowButtonLabels	=	true; // �Ƿ���ʾ��ť��ǩ
                ImageStripPath	=	"";  // �Զ���ͼƬ·��
            }
        }

        [Serializable]
        public class _Mouse {
            public bool MouseScrollsHotWnd       { get; set; }
            public Dictionary<MouseChord, BindAction> GlobalMouseActions { get; set; }
            public Dictionary<MouseChord, BindAction> TabActions { get; set; }
            public Dictionary<MouseChord, BindAction> BarActions { get; set; }
            public Dictionary<MouseChord, BindAction> LinkActions { get; set; }
            public Dictionary<MouseChord, BindAction> BreadcrumbActions { get; set; }
            public Dictionary<MouseChord, BindAction> ItemActions { get; set; }
            public Dictionary<MouseChord, BindAction> MarginActions { get; set; }

            public _Mouse() {
                /*MouseScrollsHotWnd = false;
                GlobalMouseActions = new Dictionary<MouseChord, BindAction> {
                    {MouseChord.X1, BindAction.GoBack},
                    {MouseChord.X2, BindAction.GoForward},
                    {MouseChord.X1 | MouseChord.Ctrl, BindAction.GoFirst},
                    {MouseChord.X2 | MouseChord.Ctrl, BindAction.GoLast}
                };
                TabActions = new Dictionary<MouseChord, BindAction> { 
                    {MouseChord.Middle, BindAction.CloseTab},
                    {MouseChord.Ctrl | MouseChord.Left, BindAction.LockTab},
                    {MouseChord.Double, BindAction.UpOneLevelTab},
                };
                BarActions = new Dictionary<MouseChord, BindAction> {
                    {MouseChord.Double, BindAction.NewTab},
                    {MouseChord.Middle, BindAction.RestoreLastClosed}
                };
                LinkActions = new Dictionary<MouseChord, BindAction> {
                    {MouseChord.Middle, BindAction.ItemOpenInNewTab},
                    {MouseChord.Ctrl | MouseChord.Middle, BindAction.ItemOpenInNewWindow}
                };
                ItemActions = new Dictionary<MouseChord, BindAction> {
                    {MouseChord.Middle, BindAction.ItemOpenInNewTab},
                    {MouseChord.Ctrl | MouseChord.Middle, BindAction.ItemOpenInNewWindow}                        
                };
                MarginActions = new Dictionary<MouseChord, BindAction> {
                    {MouseChord.Double, BindAction.UpOneLevel}
                };*/

                /* qwop's default value. */
                MouseScrollsHotWnd = false;
                // ȫ����궯��
                GlobalMouseActions = new Dictionary<MouseChord, BindAction> {
                    {MouseChord.X1, BindAction.GoBack},
                    {MouseChord.X2, BindAction.GoForward},
                    {MouseChord.X1 | MouseChord.Ctrl, BindAction.GoFirst},
                    {MouseChord.X2 | MouseChord.Ctrl, BindAction.GoLast}
                };
               // ��ǩ����
                TabActions = new Dictionary<MouseChord, BindAction> { 
                    {MouseChord.Middle, BindAction.CloseTab},
                    {MouseChord.Ctrl | MouseChord.Left, BindAction.LockTab},
                    {MouseChord.Double, BindAction.UpOneLevelTab},
                };
                // ��ǩBar������
               BarActions = new Dictionary<MouseChord, BindAction> {
                    {MouseChord.Double, BindAction.NewTab},
                    {MouseChord.Middle, BindAction.RestoreLastClosed},
                    {MouseChord.Ctrl | MouseChord.Middle, BindAction.TearOffCurrent}
                };
                // �ļ������Ӷ���
                LinkActions = new Dictionary<MouseChord, BindAction> {
                    {MouseChord.None, BindAction.ItemsOpenInNewTabNoSel},
                    {MouseChord.Middle, BindAction.ItemOpenInNewTab},
                    {MouseChord.Ctrl | MouseChord.Middle, BindAction.ItemOpenInNewWindow}
                };
                BreadcrumbActions = new Dictionary<MouseChord, BindAction> {
                    {MouseChord.Middle, BindAction.ItemOpenInNewTab}
                };
                // ��Դ��������Ŀ�հ״�
               ItemActions = new Dictionary<MouseChord, BindAction> {
                    {MouseChord.Middle, BindAction.ItemOpenInNewTab},
                    {MouseChord.Ctrl | MouseChord.Middle, BindAction.ItemOpenInNewTabNoSel}                        
                };

               // ��Դ�������հ״�
               MarginActions = new Dictionary<MouseChord, BindAction> {
                    { MouseChord.Double, BindAction.UpOneLevel}
                    // add by qwop //
                    ,{ MouseChord.Middle, BindAction.BrowseFolder}
                    // ctrl + ˫�� ��������ʾ��
                    ,{ ( MouseChord) 66, BindAction.OpenCmd } // ===  {MouseChord.Ctrl | MouseChord.Double, BindAction.OpenCmd}
                    ,{ MouseChord.Ctrl | MouseChord.Middle, BindAction.ItemsOpenInNewTabNoSel}
                    // add by qwop //
                };
            }
        }

        [Serializable]
        public class _Keys {
            public int[] Shortcuts               { get; set; }
            public Dictionary<string, int[]> PluginShortcuts { get; set; } 
            public bool UseTabSwitcher           { get; set; }

            public _Keys() {
                // ��ʼ��Ĭ�ϵĿ�ݼ��ֵ���
                var dict = new Dictionary<BindAction, Keys> {
                    // ���˲���
                    {BindAction.GoBack,             Key.Left  | Key.Alt},
                    // ǰ������
                    {BindAction.GoForward,          Key.Right | Key.Alt},
                    // ��ת��һ��
                    {BindAction.GoFirst,            Key.Left  | Key.Control | Key.Alt},
                    // ��ת���һ��
                    {BindAction.GoLast,             Key.Right | Key.Control | Key.Alt},
                    // ��һ����ǩ
                    {BindAction.NextTab,            Key.Tab   | Key.Control},
                    // ��һ����ǩ
                    {BindAction.PreviousTab,        Key.Tab   | Key.Control | Key.Shift},
                    //  �½���ǩ
                    {BindAction.NewTab,             Key.T     | Key.Control},
                    // �´���
                    {BindAction.NewWindow,          Key.T     | Key.Control | Key.Shift},
                    // �رձ�ǩ
                    {BindAction.CloseCurrent,       Key.W     | Key.Control},
                    // �ر�������ǩ
                    {BindAction.CloseAllButCurrent, Key.W     | Key.Control | Key.Shift},
                    // �ָ��رյı�ǩ
                    {BindAction.RestoreLastClosed,  Key.Z     | Key.Control | Key.Shift},
                    // ȡ���������̿�ݼ�
                   // {BindAction.LockCurrent,        Key.L     | Key.Control},
                   // {BindAction.LockAll,            Key.L     | Key.Control | Key.Shift},
                    {BindAction.BrowseFolder,       Key.O     | Key.Control},
                    // ��ѡ��
                    {BindAction.ShowOptions,        Key.O     | Key.Alt},
                    // ��ʾ�������˵�
                    {BindAction.ShowToolbarMenu,    Key.Oemcomma  | Key.Alt},
                    // ��ʾ��ǩ�˵�
                    {BindAction.ShowTabMenuCurrent, Key.OemPeriod | Key.Alt},
                    // ��ʾ��ǩ��˵�
                    {BindAction.ShowGroupMenu,      Key.G     | Key.Alt},
                    // ��ʾ�û�Ӧ�ó���˵�
                    {BindAction.ShowUserAppsMenu,   Key.H     | Key.Alt},
                    // ��ʾ�����ǩ�˵�
                    {BindAction.ShowRecentTabsMenu, Key.U     | Key.Alt},
                    // ��ʾ����ļ��˵�
                    {BindAction.ShowRecentFilesMenu,Key.F     | Key.Alt},
                    // Bug fix �ȼ���ͻ�� ���� by indiff
                    // {BindAction.NewFile,            Key.N     | Key.Control},
                    {BindAction.NewFile,            Key.N     | Key.Control | Key.Alt},
                    // {BindAction.NewFolder,          Key.N     | Key.Control | Key.Shift},
                   //  {BindAction.NewFolder,          Key.N     | Key.Shift }, // ϵͳĬ���Դ�
                   // ������ǩ��
                   {BindAction.CreateNewGroup,     Key.D    | Key.Control},
                   // ���ӵ���ǩ��
                 //  {BindAction.AddToGroup,         Key.D    | Key.Control  },
                   // {BindAction.AddToGroup,         Key.G    | Key.Control | Key.Alt },
                };
                // �޸�����Խ������ by indiff
                var keyboardActionCount = (int)BindAction.KEYBOARD_ACTION_COUNT;
                Shortcuts = new int[keyboardActionCount];
                // �����ݼ�
                PluginShortcuts = new Dictionary<string, int[]>();
                foreach(var pair in dict)
                {
                    var pairKey = (int)pair.Key;
                    // �޸�����Խ������ by indiff
                    if (pairKey > keyboardActionCount - 1)
                    {
                        continue;
                    }
                    Shortcuts[pairKey] = (int)pair.Value | QTUtility.FLAG_KEYENABLED;
                }
                // ���ñ�ǩ�л���
                UseTabSwitcher = true;
            }
        }

        [Serializable]
        public class _Plugin {
            public string[] Enabled              { get; set; }

            public _Plugin() {
                Enabled = new string[0];
            }
        }

        [Serializable]
        public class _Lang {
            public string[] PluginLangFiles      { get; set; }
            public bool UseLangFile              { get; set; }
            public string LangFile { get; set; }
            public string BuiltInLang { get; set; }
            public int BuiltInLangSelectedIndex { get; set; }
            public _Lang() {
                UseLangFile = false;
                LangFile = "";
                PluginLangFiles = new string[0];
                // WorkingConfig.lang.BuiltInLangSelectedIndex;
                // modify by qwop  at http://q.cnblogs.com/q/14857/  // en-US
                var uiCulture = System.Globalization.CultureInfo.InstalledUICulture.Name;
                var lUiCulture = uiCulture.ToLower();
                if (uiCulture.Equals("zh-CN") || lUiCulture.Equals("zh") || lUiCulture.Equals("cn"))
                {
                    BuiltInLangSelectedIndex = 1;
                    BuiltInLang = "��������";
                }
                else if (uiCulture.Equals("de_DE") || lUiCulture.Equals("de"))
                {
                    BuiltInLangSelectedIndex = 2;
                    BuiltInLang = "German";
                }
                else if (uiCulture.Equals("pt_BR") || lUiCulture.Equals("br") ||  lUiCulture.Equals("pt"))
                {
                    BuiltInLangSelectedIndex = 3;
                    BuiltInLang = "Brazil";
                }
                else if (uiCulture.Equals("es_ES") || lUiCulture.Equals("es") )
                {
                    BuiltInLangSelectedIndex = 4;
                    BuiltInLang = "Spanish";
                }
                else if (uiCulture.Equals("fr_FR") || lUiCulture.Equals("fr"))
                {
                    BuiltInLangSelectedIndex = 5;
                    BuiltInLang = "French";
                }
                else if (uiCulture.Equals("tr_TR") || lUiCulture.Equals("tr"))
                {
                    BuiltInLangSelectedIndex = 6;
                    BuiltInLang = "Turkish";
                }
                else if (uiCulture.Equals("ru_RU") || lUiCulture.Equals("ru"))
                {  // Сд�ж϶���˹
                    BuiltInLangSelectedIndex = 7;
                    BuiltInLang = "Russian";
                }
                else {
                    BuiltInLangSelectedIndex = 0;
                    BuiltInLang = "English";
                }
              //  BuiltInLangSelectedIndex = 0;// English version
            }
        }

        [Serializable]
        public class _Desktop {
            public int FirstItem                 { get; set; }
            public int SecondItem                { get; set; }
            public int ThirdItem                 { get; set; }
            public int FourthItem                { get; set; }
            public bool GroupExpanded            { get; set; }
            public bool RecentTabExpanded        { get; set; }
            public bool ApplicationExpanded      { get; set; }
            public bool RecentFileExpanded       { get; set; }
            public bool TaskBarDblClickEnabled   { get; set; }
            public bool DesktopDblClickEnabled   { get; set; }
            public bool LockMenu                 { get; set; }
            public bool TitleBackground          { get; set; }
            public bool IncludeGroup             { get; set; }
            public bool IncludeRecentTab         { get; set; }
            public bool IncludeApplication       { get; set; }
            public bool IncludeRecentFile        { get; set; }
            public bool OneClickMenu             { get; set; }
            public bool EnableAppShortcuts       { get; set; }
            public int Width                     { get; set; }
            public int lstSelectedIndex          { get; set; } /*���ѡ�еĲ˵���.*/
            public _Desktop() {
                FirstItem = 0;
                SecondItem = 1;
                ThirdItem = 2;
                FourthItem = 3;
                GroupExpanded = false;
                RecentTabExpanded = false;
                ApplicationExpanded = false;
                RecentFileExpanded = false;
                TaskBarDblClickEnabled = true;
                DesktopDblClickEnabled = true;
                LockMenu = false;
                TitleBackground = false;
                IncludeApplication = true;
                IncludeRecentTab = true;
                IncludeApplication = true;
                IncludeRecentFile = true;
                OneClickMenu = false;
                EnableAppShortcuts = true;
                Width = 80;

                // qwop's default value.
                Width = 12;

                // ���ѡ�еĲ˵�������Ĭ��Ϊ0. �´δ򿪵�ʱ���Զ���λ��������
                lstSelectedIndex = 0;
            }
        }
    }

    public static class ConfigManager {
        public static Config LoadedConfig;

        public static void Initialize() {
            LoadedConfig = new Config();
            QTUtility2.log("��ʼ��������Ϣ�ɹ�");
            ReadConfig();
            QTUtility2.log("ע�����ȡ������Ϣ�ɹ�");
        }

        public static void UpdateConfig(bool fBroadcast = true) {
            QTUtility.TextResourcesDic = Config.Lang.UseLangFile && File.Exists(Config.Lang.LangFile)
                    ? QTUtility.ReadLanguageFile(Config.Lang.LangFile)
                    : null;
            QTUtility.ValidateTextResources();
            StaticReg.ClosedTabHistoryList.MaxCapacity = Config.Misc.TabHistoryCount;
            StaticReg.ExecutedPathsList.MaxCapacity = Config.Misc.FileHistoryCount;
            DropDownMenuBase.InitializeMenuRenderer();
            ContextMenuStripEx.InitializeMenuRenderer();
            // Options run on their own STA thread. Reloading managed plugins here can
            // dispose Explorer-owned objects while their UI thread is still using
            // them. Persist the selection now and load it on the next Explorer start.
            QTUtility2.flog("Plugin refresh deferred until Explorer restart");
            InstanceManager.LocalTabBroadcast(tabbar => tabbar.RefreshOptions());
            if(fBroadcast) {
                // SyncTaskBarMenu(); todo
                InstanceManager.StaticBroadcast(() => {
                    ReadConfig();
                    UpdateConfig(false);
                });
            }
        }

        public static void ReadConfig() {
            try
            {
                const string RegPath = RegConst.Root + RegConst.Config;

                var categories =
                    from categoryProperty in typeof(Config).GetProperties()
                    where categoryProperty.CanWrite
                    let categoryType = categoryProperty.PropertyType
                    let categoryObject = categoryProperty.GetValue(LoadedConfig, null)
                    select new {
                        keyPath = RegPath + categoryType.Name.Substring(1),
                        categoryObject, 
                        settings = (
                            from settingProperty in categoryType.GetProperties()
                            select new {
                                name = settingProperty.Name,
                                type = settingProperty.PropertyType,
                                value = settingProperty.GetValue(categoryObject, null),
                                property = settingProperty
                            }
                        )
                    };

                foreach(var category in categories) {
                    using (var key=Registry.CurrentUser.CreateSubKey(category.keyPath)) {
                        foreach(var setting in category.settings) {
                                object value = key.GetValue(setting.name);
                                if (value == null) { continue;}

                                Type t = setting.type;

                                if (t == typeof(bool))
                                {
                                    value = (int)value != 0;
                                }
                                else if (t.IsEnum)
                                {
                                    value = Enum.Parse(t, value.ToString());
                                }
                                else if (t != typeof(int) && t != typeof(string))
                                {
                                    using (var stream = new MemoryStream(Encoding.UTF8.GetBytes(value.ToString())))
                                    {
                                        if (t == typeof(Font))
                                        {
                                            var ser = new DataContractJsonSerializer(typeof(XmlSerializableFont));
                                            var xsf = ser.ReadObject(stream) as XmlSerializableFont;
                                            value = xsf == null ? null : xsf.ToFont();
                                        }
                                        else
                                        {
                                            var ser = new DataContractJsonSerializer(t);
                                            value = ser.ReadObject(stream);
                                        }

                                        QTUtility2.Close(stream);
                                    }
                                }

                                setting.property.SetValue(category.categoryObject, value, null);
                            
                           
                        }
                    }
                }

                using(IDLWrapper wrapper = new IDLWrapper(Config.Window.DefaultLocation)) {
                    if(!wrapper.Available) {
                        Config.Window.DefaultLocation = new Config._Window().DefaultLocation;
                    }
                }
                Config.Tips.PreviewFont = Config.Tips.PreviewFont ?? Control.DefaultFont;
                Config.Tips.PreviewMaxWidth = QTUtility.ValidateMinMax(Config.Tips.PreviewMaxWidth, 128, 1920);
                Config.Tips.PreviewMaxHeight = QTUtility.ValidateMinMax(Config.Tips.PreviewMaxHeight, 96, 1200);
                if(Config.Tips.PreviewDelay <= 0) {
                    Config.Tips.PreviewDelay = new Config._Tips().PreviewDelay;
                }
                if(Config.Tips.PreviewOpacity <= 0) {
                    Config.Tips.PreviewOpacity = new Config._Tips().PreviewOpacity;
                }
                if(Config.Tips.PreviewCacheCapacity <= 0) {
                    Config.Tips.PreviewCacheCapacity = new Config._Tips().PreviewCacheCapacity;
                }
                Config.Tips.PreviewDelay = QTUtility.ValidateMinMax(Config.Tips.PreviewDelay, 50, 5000);
                Config.Tips.PreviewOpacity = QTUtility.ValidateMinMax(Config.Tips.PreviewOpacity, 20, 100);
                Config.Tips.PreviewCacheCapacity = QTUtility.ValidateMinMax(Config.Tips.PreviewCacheCapacity, 8, 1024);
                Config.Misc.TabHistoryCount = QTUtility.ValidateMinMax(Config.Misc.TabHistoryCount, 1, 30);
                Config.Misc.FileHistoryCount = QTUtility.ValidateMinMax(Config.Misc.FileHistoryCount, 1, 30);
                Config.Misc.NetworkTimeout = QTUtility.ValidateMinMax(Config.Misc.NetworkTimeout, 0, 120);
                Config.Skin.TabHeight = QTUtility.ValidateMinMax(Config.Skin.TabHeight, 10, 50);
                // ������ǩ��С����
				Config.Skin.TabMinWidth = QTUtility.ValidateMinMax(Config.Skin.TabMinWidth, 10, 999);
                Config.Skin.TabMaxWidth = QTUtility.ValidateMinMax(Config.Skin.TabMaxWidth, 50, 999);
                Config.Skin.OverlapPixels = QTUtility.ValidateMinMax(Config.Skin.OverlapPixels, 0, 20);
                Config.Skin.TabTextFont = Config.Skin.TabTextFont ?? Control.DefaultFont;
                Config.Skin.FluentGlassAddressMode = QTUtility.ValidateMinMax(Config.Skin.FluentGlassAddressMode, 0, 2);
                Config.Skin.FluentGlassAddressExtraPixels = QTUtility.ValidateMinMax(Config.Skin.FluentGlassAddressExtraPixels, 0, 400);
                if(Config.Skin.FluentGlassInactiveColor == Color.Empty) {
                    Config.Skin.FluentGlassInactiveColor = Color.FromArgb(255, 32, 32, 32);
                }
                Func<Padding, Padding> validatePadding = p => {
                    p.Left   = QTUtility.ValidateMinMax(p.Left,   0, 99);
                    p.Top    = QTUtility.ValidateMinMax(p.Top,    0, 99);
                    p.Right  = QTUtility.ValidateMinMax(p.Right,  0, 99);
                    p.Bottom = QTUtility.ValidateMinMax(p.Bottom, 0, 99);
                    return p;
                };
                Config.Skin.RebarSizeMargin = validatePadding(Config.Skin.RebarSizeMargin);
                Config.Skin.TabContentMargin = validatePadding(Config.Skin.TabContentMargin);
                Config.Skin.TabSizeMargin = validatePadding(Config.Skin.TabSizeMargin);
                using(IDLWrapper wrapper = new IDLWrapper(Config.Skin.TabImageFile)) {
                    if(!wrapper.Available) Config.Skin.TabImageFile = "";
                }
                if(string.IsNullOrEmpty(Config.Skin.PlusButtonImageFile) || !File.Exists(Config.Skin.PlusButtonImageFile)) {
                    Config.Skin.PlusButtonImageFile = "";
                }
                if(string.IsNullOrEmpty(Config.Skin.CloseButtonImageFile) || !File.Exists(Config.Skin.CloseButtonImageFile)) {
                    Config.Skin.CloseButtonImageFile = "";
                }
                if(string.IsNullOrEmpty(Config.Skin.LockIconImageFile) || !File.Exists(Config.Skin.LockIconImageFile)) {
                    Config.Skin.LockIconImageFile = "";
                }
                Config.Skin.CloseButtonImageOffsetX = QTUtility.ValidateMinMax(Config.Skin.CloseButtonImageOffsetX, -50, 50);
                Config.Skin.CloseButtonImageOffsetY = QTUtility.ValidateMinMax(Config.Skin.CloseButtonImageOffsetY, -50, 50);
                Config.Skin.LockIconImageOffsetX = QTUtility.ValidateMinMax(Config.Skin.LockIconImageOffsetX, -50, 50);
                Config.Skin.LockIconImageOffsetY = QTUtility.ValidateMinMax(Config.Skin.LockIconImageOffsetY, -50, 50);
                Config.DragDrop.TabDropHoverTime = QTUtility.ValidateMinMax(Config.DragDrop.TabDropHoverTime, 100, 5000);
                using(IDLWrapper wrapper = new IDLWrapper(Config.Skin.RebarImageFile)) {
                    if(!wrapper.Available) Config.Skin.RebarImageFile = "";
                }
                if(Config.BBar.ButtonIndexes == null) {
                    Config.BBar.ButtonIndexes = new int[0];
                }
                if(Config.BBar.ActivePluginIDs == null) {
                    Config.BBar.ActivePluginIDs = new string[0];
                }
                if(Config.BBar.ImageStripPath == null) {
                    Config.BBar.ImageStripPath = "";
                }
                using(IDLWrapper wrapper = new IDLWrapper(Config.BBar.ImageStripPath)) {
                    // todo: check dimensions
                    if(!wrapper.Available) Config.BBar.ImageStripPath = "";
                }
                List<int> blist = Config.BBar.ButtonIndexes.ToList();
                blist.RemoveAll(i => (i.HiWord() - 1) >= Config.BBar.ActivePluginIDs.Length);
                Config.BBar.ButtonIndexes = blist.ToArray();
                var keys = Config.Keys.Shortcuts;
                Array.Resize(ref keys, (int)BindAction.KEYBOARD_ACTION_COUNT);
                Config.Keys.Shortcuts = keys;
                foreach(var pair in Config.Keys.PluginShortcuts.Where(p => p.Value == null).ToList()) {
                    Config.Keys.PluginShortcuts.Remove(pair.Key);
                }
                if(QTUtility.IsXP) Config.Tweaks.AlwaysShowHeaders = false;
                if(!QTUtility.IsWin7) Config.Tweaks.RedirectLibraryFolders = false;
                if(!QTUtility.IsXP) Config.Tweaks.KillExtWhileRenaming = true;
                if(QTUtility.IsXP) Config.Tweaks.BackspaceUpLevel = true;
                if(!QTUtility.IsWin7) Config.Tweaks.ForceSysListView = true;
            } catch (Exception e)
            {
                QTUtility2.MakeErrorLog(e, "ReadConfig foreach category");
            }
        }

        public static void WriteConfig(bool DesktopOnly = false) {
            const string RegPath = RegConst.Root + RegConst.Config;
            QTUtility2.log("WriteConfig " + RegPath);
            //Returns details of setting properties from all categories, or only Desktop category
            var settings =
                from categoryProperty in typeof(Config).GetProperties()
                where DesktopOnly ? categoryProperty.Name == "desktop" : categoryProperty.CanWrite
                let categoryType = categoryProperty.PropertyType
                let categoryObject = categoryProperty.GetValue(LoadedConfig,null)
                from settingProperty in categoryType .GetProperties()
                select new {
                    keyPath = RegPath + categoryType.Name.Substring(1),
                    name = settingProperty.Name,
                    type = settingProperty.PropertyType,
                    value = settingProperty.GetValue(categoryObject, null)
                };

            foreach(var setting in settings) {
                using (var key=Registry.CurrentUser.CreateSubKey(setting.keyPath)) {
                    Type t = setting.type;
                    object value = setting.value;

                    if (t==typeof(bool)) {
                        value=(bool)value ? 1 : 0;
                    } else if (t != typeof(int) && t != typeof(string) && !t.IsEnum) {
                        if (t==typeof(Font)) {
                            value = XmlSerializableFont.FromFont((Font)value);
                            t = typeof(XmlSerializableFont);
                        }
                        var ser = new DataContractJsonSerializer(t);
                        using (var stream=new MemoryStream()) {
                            try {
                                ser.WriteObject(stream,value);
                            } catch (Exception e) {
                                QTUtility2.MakeErrorLog(e);
                            }
                            stream.Position = 0;
                            StreamReader streamReader = new StreamReader(stream);
                            value = streamReader.ReadToEnd();

                            QTUtility2.Close(streamReader);
                            QTUtility2.Close(stream);
                           // if (streamReader != null) { streamReader.Close(); }
                           // if (stream != null) { stream.Close(); }
                        }
                    }
                    key.SetValue(setting.name,value);
                }
            }
			
        }
    }
}
