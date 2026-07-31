//    This file is part of QTTabBar, a shell extension for Microsoft
//    Windows Explorer.
//    Copyright (C) 2007-2022  Quizo, Paul Accisano, indiff
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
using System.ComponentModel;
using System.Drawing;
using System.IO;
using System.Windows.Forms;
using System.Windows.Forms.VisualStyles;
using QTTabBarLib.Interop;

namespace QTTabBarLib {
    public sealed class QTabControl : Control 
    {
        private Bitmap bmpCloseBtn_Cold;
        private Bitmap bmpCloseBtn_ColdAlt;
        private Bitmap bmpCloseBtn_Hot;
        private Bitmap bmpCloseBtn_Pressed;
        private Bitmap bmpFolIconBG;
        private Bitmap bmpLocked;
        private Bitmap[] customCloseButtonImages;
        private Bitmap customLockIcon;
        private Size customCloseButtonSize = Size.Empty;
        private Size customLockIconSize = Size.Empty;
        private Bitmap plusButtonImage;
        private SolidBrush brshActive;
        private SolidBrush brshInactv;
        private Color[] colorSet;
        private IContainer components;
        private QTabItem draggingTab;
        private bool fActiveTxtBold;
        private bool fAutoSubText;
        private bool fCloseBtnOnHover;
        private bool fDrawCloseButton;
        private bool fDrawFolderImg;
        private bool fDrawShadow;
        private bool fDrawActiveShadow;
        private bool fDrawInactiveShadow;
        private bool fDrawHotShadow;
        private bool fForceClassic;
        private bool fLimitSize;
        private bool fNeedToDrawUpDown;
        // �Ƿ�����������ť
        private bool fNeedPlusButton;
        private bool fNowMouseIsOnCloseBtn;
        private bool fNowMouseIsOnIcon;
        private bool fNowShowCloseBtnAlt;
        private bool fNowTabContextMenuStripShowing;
        private Font fnt_Underline;
        private Font fntBold;
        private Font fntBold_Underline;
        private Font fntDriveLetter;
        private Font fntSubText;
        private bool fOncePainted;
        internal const float FONTSIZE_DIFF = 0.75f;
        private bool fRedrawSuspended;
        private bool fShowSubDirTip;
        private bool fSubDirShown;
        private bool fSuppressDoubleClick;
        private bool fSuppressMouseUp;
        private QTabItem hotTab;
        private int iCurrentRow;
        private int iFocusedTabIndex = -1;
        private int iMultipleType;
        private int iPointedChanged_LastRaisedIndex = -2;
        private int iPseudoHotIndex = -1;
        private int iScrollClickedCount;
        private int iScrollWidth;
        private int iSelectedIndex;
        private int iTabIndexOfSubDirShown = -1;
        private int iTabMouseOnButtonsIndex = -1;
        private Size itemSize = new Size(100, 0x18);
        private int iToolTipIndex = -1;
        private int maxAllowedTabWidth = 10;
        private int minAllowedTabWidth = 10;
        private QTabItem selectedTabPage;
        private StringFormat sfTypoGraphic;
        private TabSizeMode sizeMode;
        private Padding contentMargin;
        private int overlapPixels;
        private bool hitTestTransparent;
        private Padding sizingMargin;
        private Bitmap[] tabImages;
        private QTabCollection tabPages;
        private StringAlignment tabTextAlignment;
        private Timer timerSuppressDoubleClick;
        private Timer timerTabSwitchAnimation;
        private ToolTip toolTip;
        private UpDown upDown;
        private int tabAnimationFromIndex = -1;
        private int tabAnimationToIndex = -1;
        private int tabAnimationStartTick;
        private const int TAB_ANIMATION_DURATION = 170;
        private const int UPDOWN_WIDTH = 0x24;

        [ThreadStatic()]
        private static VisualStyleRenderer vsr_LHot;
        [ThreadStatic()]
        private static VisualStyleRenderer vsr_LNormal;
        [ThreadStatic()]
        private static VisualStyleRenderer vsr_LPressed;
        [ThreadStatic()]
        private static VisualStyleRenderer vsr_MHot;
        [ThreadStatic()]
        private static VisualStyleRenderer vsr_MNormal;
        [ThreadStatic()]
        private static VisualStyleRenderer vsr_MPressed;
        private static VisualStyleRenderer vsr_RHot;
        [ThreadStatic()]
        private static VisualStyleRenderer vsr_RNormal;
        [ThreadStatic()]
        private static VisualStyleRenderer vsr_RPressed;

        public event QTabCancelEventHandler CloseButtonClicked; // �ر��¼�
        public event QTabCancelEventHandler Deselecting; 
        public event ItemDragEventHandler ItemDrag;
        public event QTabCancelEventHandler PointedTabChanged;
        public event QEventHandler RowCountChanged;
        public event EventHandler SelectedIndexChanged;
        public event QTabCancelEventHandler Selecting;
        public event QTabCancelEventHandler TabCountChanged;
        public event QTabCancelEventHandler TabIconMouseDown;
        // ��ɫ��ť�¼�
        public event QTabCancelEventHandler PlusButtonClicked;

        public QTabControl() {
            fNeedPlusButton = Config.Tabs.NeedPlusButton;
            /*SetStyle(ControlStyles.OptimizedDoubleBuffer |
                     ControlStyles.AllPaintingInWmPaint | 
                     ControlStyles.SupportsTransparentBackColor | 
                     ControlStyles.ResizeRedraw | 
                     ControlStyles.UserPaint, true);*/
            
            // ControlStyles.UserPaint//ʹ���Զ���Ļ��Ʒ�ʽ
            // |ControlStyles.ResizeRedraw//���ؼ���С�����仯ʱ�����»���
            // |ControlStyles.SupportsTransparentBackColor//��ؼ����� alpha �����С�� 255 ���� BackColor ��ģ��͸����
            // | ControlStyles.AllPaintingInWmPaint//��ؼ����Դ�����Ϣ WM_ERASEBKGND �Լ�����˸
            // | ControlStyles.OptimizedDoubleBuffer//��ؼ������Ȼ��Ƶ�������������ֱ�ӻ��Ƶ���Ļ������Լ�����˸
       
            // ��ʼ��֮ǰ���л�ȡһ�ΰ���ģʽ
            QTUtility.InNightMode = QTUtility.getNightMode();

            SetStyle(ControlStyles.UserPaint
                     | ControlStyles.OptimizedDoubleBuffer 
                     | ControlStyles.ResizeRedraw//���ؼ���С�����仯ʱ�����»���
                     | ControlStyles.AllPaintingInWmPaint //��ؼ����Դ�����Ϣ WM_ERASEBKGND �Լ�����˸
                     | ControlStyles.SupportsTransparentBackColor//��ؼ����� alpha �����С�� 255 ���� BackColor ��ģ��͸����
                     | ControlStyles.OptimizedDoubleBuffer //��ؼ������Ȼ��Ƶ�������������ֱ�ӻ��Ƶ���Ļ������Լ�����˸
            , value : true);

            /*this.SetStyle(ControlStyles.UserPaint |
                          ControlStyles.SupportsTransparentBackColor |
                          ControlStyles.AllPaintingInWmPaint |
                          ControlStyles.OptimizedDoubleBuffer, true);*/
            
            components = new Container();
            tabPages = new QTabCollection(this);
            
            sfTypoGraphic = StringFormat.GenericTypographic;
            // MeasureTrailingSpaces ����ÿһ�н�β����β��ո� ��Ĭ������£�MeasureString �������صı߽���ζ����ų�ÿһ�н�β���Ŀո� ���ô˱���Ա��ڲⶨʱ���ո������ȥ��
            // NoWrap �ھ��������ø�ʽʱ�������Զ����й��ܡ� �����ݵ��ǵ�����Ǿ���ʱ������ָ�����ε��г���Ϊ��ʱ���������˱�ǡ�
            sfTypoGraphic.FormatFlags |= StringFormatFlags.MeasureTrailingSpaces | StringFormatFlags.NoWrap;
            sfTypoGraphic.LineAlignment = StringAlignment.Far;  // StringAlignment.Center StringAlignment.Near StringAlignment.Far
            sfTypoGraphic.Trimming = StringTrimming.EllipsisCharacter;
            if (QTUtility.IsRTL)
            {
                this.sfTypoGraphic.FormatFlags |= StringFormatFlags.DirectionRightToLeft;
            }

            /*if (QTUtility.InNightMode)
            {
                this.colorSet = new Color[]
                {
                    ShellColors.NightModeTextColor,
                    ShellColors.NightModeDisabledColor,
                    Config.Skin.TabTextHotColor,
                    ShellColors.NightModeTextShadow,
                     Config.Skin.TabShadInactiveColor,
                    ShellColors.NightModeColor
                };
            }
            else {
                colorSet = new Color[] 
                {
                    Config.Skin.TabTextActiveColor,
                    Config.Skin.TabTextInactiveColor,
                    Config.Skin.TabTextHotColor,
                    Config.Skin.TabShadActiveColor,
                    Config.Skin.TabShadInactiveColor,
                    Config.Skin.TabShadHotColor
                };
            }*/
            // brshActive = new SolidBrush(colorSet[0]);
            // brshInactv = new SolidBrush(colorSet[1]);
            // ���䰵�� by indiff dark mode
            /*brshActive = new SolidBrush(Config.Skin.TabTextActiveColor);  // ��ǩ���ˢ
            brshInactv = new SolidBrush(Config.Skin.TabTextInactiveColor); // ��ǩ�Ǽ��ˢ
            if (QTUtility.InNightMode)
            {
                BackColor = Config.Skin.TabShadActiveColor;
            }
            else
            {
                BackColor = Color.Transparent;
            }*/

            InitializeColors();
            this.BackColor = Color.Transparent;
            /*
            if (QTUtility.InNightMode)
            {
                // this.BackColor = SystemColors.ControlDarkDark;;
                this.BackColor = Color.Black;
            }
            else
            {
                this.BackColor = SystemColors.Window;
            }*/
            // ��ʱ����֧��˫���¼�
            timerSuppressDoubleClick = new Timer(components);
            timerSuppressDoubleClick.Interval = SystemInformation.DoubleClickTime + 100;
            timerSuppressDoubleClick.Tick += timerSuppressDoubleClick_Tick;
            timerTabSwitchAnimation = new Timer(components);
            timerTabSwitchAnimation.Interval = 15;
            timerTabSwitchAnimation.Tick += timerTabSwitchAnimation_Tick;
            if(VisualStyleRenderer.IsSupported) {
                InitializeRenderer();
            }
        }


        public  void InitializeColors()
        {
            this.colorSet = new Color[]
            {
                Config.Skin.TabTextActiveColor,
                Config.Skin.TabTextInactiveColor,
                Config.Skin.TabTextHotColor,
                QTUtility.InNightMode ? ShellColors.TextShadow : Config.Skin.TabShadActiveColor,
                QTUtility.InNightMode ? ShellColors.Default : Config.Skin.TabShadInactiveColor,
                Config.Skin.TabShadHotColor
            };
            if (brshActive == null)
            {
                brshActive = new SolidBrush(this.colorSet[0]);
                brshInactv = new SolidBrush(this.colorSet[1]);
            }
            else
            {
                brshActive.Color = this.colorSet[0];
                brshInactv.Color = this.colorSet[1];
            }
        }

        public static Color selectedColor(bool fSelected)
        {
            Color[] colorSet;
            if (QTUtility.InNightMode)
                colorSet = new Color[]
                {
                    ShellColors.Text,
                    ShellColors.Disabled,
                    Config.Skin.TabTextActiveColor, // Config.TabHiliteColor,
                    ShellColors.TextShadow,
                    ShellColors.Default,
                    Config.Skin.TabShadHotColor
                };
            else
                colorSet = new Color[]
                {
                    Config.Skin.TabTextActiveColor,
                    Config.Skin.TabTextInactiveColor,
                    Config.Skin.TabTextActiveColor, // Config.TabHiliteColor,
                    Config.Skin.TabShadActiveColor,
                    Config.Skin.TabShadInactiveColor,
                    Config.Skin.TabShadHotColor
                };
            if (fSelected)
            {
                return colorSet[0];
            }
            else
            {
                return colorSet[1];
            }
        }

        private Color GetTabTextColor(bool selected, bool hot) {
            if(selected) return colorSet[0];
            return hot ? colorSet[2] : colorSet[1];
        }

        private Color GetTabShadowColor(bool selected, bool hot) {
            if(selected) return colorSet[3];
            return hot ? colorSet[5] : colorSet[4];
        }

        private bool ShouldDrawTextShadow(bool selected, bool hot) {
            if(!fDrawShadow) return false;
            if(selected) return fDrawActiveShadow;
            return hot ? fDrawHotShadow : fDrawInactiveShadow;
        }
        private bool CalculateItemRectangle() {
            int x = 0;
            int right = 0;
            int count = tabPages.Count;
            if(sizeMode == TabSizeMode.Fixed) {
                for(int i = 0; i < count; i++) {
                    tabPages[i].TabBounds = new Rectangle(x, 0, itemSize.Width, itemSize.Height);
                    tabPages[i].Edge = 0;
                    right = x + itemSize.Width;
                    x += GetTabAdvance(itemSize.Width);
                }
            }
            else {
                int width;
                if(fLimitSize) {
                    for(int j = 0; j < count; j++) {
                        width = tabPages[j].TabBounds.Width;
                        if(width > maxAllowedTabWidth) {
                            width = maxAllowedTabWidth;
                        }
                        if(width < minAllowedTabWidth) {
                            width = minAllowedTabWidth;
                        }
                        tabPages[j].TabBounds = new Rectangle(x, 0, width, itemSize.Height);
                        tabPages[j].Edge = 0;
                        right = x + width;
                        x += GetTabAdvance(width);
                    }
                }
                else {
                    for(int k = 0; k < count; k++) {
                        width = tabPages[k].TabBounds.Width;
                        tabPages[k].TabBounds = new Rectangle(x, 0, width, itemSize.Height);
                        tabPages[k].Edge = 0;
                        right = x + width;
                        x += GetTabAdvance(width);
                    }
                }
            }
            if(tabPages.Count > 1) {
                tabPages[0].Edge = Edges.Left;
                tabPages[tabPages.Count - 1].Edge = Edges.Right;
            }
            return (right > (Width - 0x24));
        }

        private int GetTabAdvance(int tabWidth) {
            return TabSkinGeometry.GetTabAdvance(tabWidth, overlapPixels);
        }

        private void CalculateItemRectangle_MultiRows() {
            int x = 0;
            int count = tabPages.Count;
            int width = Width;
            int num4 = itemSize.Width;
            int height = itemSize.Height;
            int num6 = height - 3;
            int num7 = 0;
            int num8 = 0;
            if(sizeMode == TabSizeMode.Fixed) {  // �̶�����
                for(int i = 0; i < count; i++) {
                    if((x + num4) > width) {
                        num7++;
                        x = 0;
                    }
                    tabPages[i].TabBounds = new Rectangle(x, num6 * num7, num4, height);
                    tabPages[i].Row = num7;
                    if(x == 0) {
                        tabPages[i].Edge = Edges.Left;
                    }
                    else if((i == (count - 1)) || (((x + num4) + num4) > width)) {
                        tabPages[i].Edge = Edges.Right;
                    }
                    else {
                        tabPages[i].Edge = 0;
                    }
                    x += GetTabAdvance(num4);
                    if(i == iSelectedIndex) {
                        num8 = num7;
                    }
                }
            }
            else {
                int maxTabWidth;
                if(fLimitSize) {
                    for(int j = 0; j < count; j++) {
                        maxTabWidth = tabPages[j].TabBounds.Width;
                        if(maxTabWidth > maxAllowedTabWidth) {
                            maxTabWidth = maxAllowedTabWidth;
                        }
                        if(maxTabWidth < minAllowedTabWidth) {
                            maxTabWidth = minAllowedTabWidth;
                        }
                        if((x + maxTabWidth) > width) {
                            num7++;
                            x = 0;
                        }
                        tabPages[j].TabBounds = new Rectangle(x, num6 * num7, maxTabWidth, height);
                        tabPages[j].Row = num7;
                        if(x == 0) {
                            tabPages[j].Edge = Edges.Left;
                        }
                        else if(j == (count - 1)) {
                            tabPages[j].Edge = Edges.Right;
                        }
                        else {
                            int minTabWidth = tabPages[j + 1].TabBounds.Width;
                            if(minTabWidth > maxAllowedTabWidth) {
                                minTabWidth = maxAllowedTabWidth;
                            }
                            if(minTabWidth < minAllowedTabWidth) {
                                minTabWidth = minAllowedTabWidth;
                            }
                            if(((x + GetTabAdvance(maxTabWidth)) + minTabWidth) > width) {
                                tabPages[j].Edge = Edges.Right;
                            }
                            else {
                                tabPages[j].Edge = 0;
                            }
                        }
                        x += GetTabAdvance(maxTabWidth);
                        if(j == iSelectedIndex) {
                            num8 = num7;
                        }
                    }
                }
                else {
                    for(int k = 0; k < count; k++) {
                        maxTabWidth = tabPages[k].TabBounds.Width;
                        if((x + maxTabWidth) > width) {
                            num7++;
                            x = 0;
                        }
                        tabPages[k].TabBounds = new Rectangle(x, num6 * num7, maxTabWidth, height);
                        tabPages[k].Row = num7;
                        if(x == 0) {
                            tabPages[k].Edge = Edges.Left;
                        }
                        else if(k == (count - 1)) {
                            tabPages[k].Edge = Edges.Right;
                        }
                        else {
                            int num14 = tabPages[k + 1].TabBounds.Width;
                            if(((x + GetTabAdvance(maxTabWidth)) + num14) > width) {
                                tabPages[k].Edge = Edges.Right;
                            }
                            else {
                                tabPages[k].Edge = 0;
                            }
                        }
                        x += GetTabAdvance(maxTabWidth);
                        if(k == iSelectedIndex) {
                            num8 = num7;
                        }
                    }
                }
            }
            if((num7 != 0) && (iMultipleType == 1)) {
                int num15 = num7 - num8;
                if(num15 > 0) {
                    for(int m = 0; m < count; m++) {
                        QTabItem base2 = tabPages[m];
                        Rectangle tabBounds = base2.TabBounds;
                        if(base2.Row > num8) {
                            base2.Row -= num8 + 1;
                            tabBounds.Y = base2.Row * num6;
                            base2.TabBounds = tabBounds;
                        }
                        else {
                            tabBounds.Y += num15 * num6;
                            base2.TabBounds = tabBounds;
                            base2.Row += num15;
                        }
                    }
                }
            }
            if(num7 != iCurrentRow) {
                iCurrentRow = num7;
                if(RowCountChanged != null) {
                    RowCountChanged(this, new QEventArgs(iCurrentRow + 1));
                }
            }
        }

        /**
         * ��ǩ�л�
         */
        private bool ChangeSelection(QTabItem tabToSelect, int index) {
            if(((Deselecting != null) && (this.iSelectedIndex > -1)) && (this.iSelectedIndex < tabPages.Count)) {
                QTabCancelEventArgs e = new QTabCancelEventArgs(tabPages[this.iSelectedIndex], this.iSelectedIndex, false, TabControlAction.Deselecting);
                Deselecting(this, e);
            }
            int curSelectedIndex = this.iSelectedIndex;
            QTabItem curSelectedTabPage = this.selectedTabPage;
            this.iSelectedIndex = index;
            this.selectedTabPage = tabToSelect;
            if(Selecting != null) {
                QTabCancelEventArgs args2 = new QTabCancelEventArgs(tabToSelect, index, false, TabControlAction.Selecting);
                Selecting(this, args2);
                if(args2.Cancel) {
                    this.iSelectedIndex = curSelectedIndex;
                    this.selectedTabPage = curSelectedTabPage;
                    return false;
                }
            }
            if(fNeedToDrawUpDown) {
                if((tabToSelect.TabBounds.X + iScrollWidth) < 0) {
                    iScrollWidth = -tabToSelect.TabBounds.X;
                    iScrollClickedCount = index;
                }
                else if((tabToSelect.TabBounds.X + iScrollWidth) > (Width - 0x24)) {
                    while((tabToSelect.TabBounds.Right + iScrollWidth) > Width) {
                        OnUpDownClicked(true, true);
                    }
                }
            }
            StartTabSwitchAnimation(curSelectedIndex, index);
            Refresh();
            if(SelectedIndexChanged != null) { // ѡ��ı�ǩ���������仯�� ����ö�Ӧ���¼�
                SelectedIndexChanged(this, new EventArgs());
            }
            iFocusedTabIndex = -1;
            return true;
        }

        private void StartTabSwitchAnimation(int fromIndex, int toIndex) {
            if(!Config.Tabs.TabSwitchAnimation || fromIndex == toIndex ||
                    fromIndex < 0 || toIndex < 0 ||
                    fromIndex >= tabPages.Count || toIndex >= tabPages.Count) {
                StopTabSwitchAnimation();
                return;
            }

            tabAnimationFromIndex = fromIndex;
            tabAnimationToIndex = toIndex;
            tabAnimationStartTick = Environment.TickCount;
            if(timerTabSwitchAnimation != null) {
                timerTabSwitchAnimation.Stop();
                timerTabSwitchAnimation.Start();
            }
        }

        private void StopTabSwitchAnimation() {
            if(timerTabSwitchAnimation != null) {
                timerTabSwitchAnimation.Stop();
            }
            tabAnimationFromIndex = -1;
            tabAnimationToIndex = -1;
        }

        private void timerTabSwitchAnimation_Tick(object sender, EventArgs e) {
            if(tabAnimationFromIndex < 0 || tabAnimationToIndex < 0) {
                StopTabSwitchAnimation();
                return;
            }

            int elapsed = unchecked(Environment.TickCount - tabAnimationStartTick);
            if(elapsed >= TAB_ANIMATION_DURATION) {
                StopTabSwitchAnimation();
            }
            Invalidate();
        }

        protected override void Dispose(bool disposing) {
            if(disposing && (components != null)) {
                components.Dispose();
            }
            if(brshActive != null) {
                brshActive.Dispose();
                brshActive = null;
            }
            if(brshInactv != null) {
                brshInactv.Dispose();
                brshInactv = null;
            }
            if(sfTypoGraphic != null) {
                sfTypoGraphic.Dispose();
                sfTypoGraphic = null;
            }
            DisposeCustomTabButtonImages();
            if(bmpLocked != null) {
                bmpLocked.Dispose();
                bmpLocked = null;
            }            if(plusButtonImage != null) {
                plusButtonImage.Dispose();
                plusButtonImage = null;
            }
            if(bmpCloseBtn_Cold != null) {
                bmpCloseBtn_Cold.Dispose();
                bmpCloseBtn_Cold = null;
            }
            if(bmpCloseBtn_Hot != null) {
                bmpCloseBtn_Hot.Dispose();
                bmpCloseBtn_Hot = null;
            }
            if(bmpCloseBtn_Pressed != null) {
                bmpCloseBtn_Pressed.Dispose();
                bmpCloseBtn_Pressed = null;
            }
            if(bmpCloseBtn_ColdAlt != null) {
                bmpCloseBtn_ColdAlt.Dispose();
            }
            if(bmpFolIconBG != null) {
                bmpFolIconBG.Dispose();
                bmpFolIconBG = null;
            }
            if(fnt_Underline != null) {
                fnt_Underline.Dispose();
                fnt_Underline = null;
            }
            if(fntBold != null) {
                fntBold.Dispose();
                fntBold = null;
            }
            if(fntBold_Underline != null) {
                fntBold_Underline.Dispose();
                fntBold_Underline = null;
            }
            if(fntSubText != null) {
                fntSubText.Dispose();
                fntSubText = null;
            }
            if(fntDriveLetter != null) {
                fntDriveLetter.Dispose();
                fntDriveLetter = null;
            }
            foreach(QTabItem base2 in tabPages) {
                if(base2 != null) {
                    base2.OnClose();
                }
            }
            base.Dispose(disposing);
        }

        private static void DrawFluentGlassTabOutline(Graphics g, bool selected, bool hot, Rectangle item) {
            if(item.Width < 4 || item.Height < 4) {
                return;
            }

            Rectangle outline = new Rectangle(item.X, item.Y, item.Width - 1, item.Height - 1);
            int highlightAlpha = selected ? 210 : (hot ? 155 : 95);
            int shadowAlpha = selected ? 115 : (hot ? 85 : 55);
            using(Pen highlight = new Pen(Color.FromArgb(highlightAlpha, Color.White)))
            using(Pen shadow = new Pen(Color.FromArgb(shadowAlpha, Color.Black))) {
                g.DrawLine(highlight, outline.Left + 2, outline.Top, outline.Right - 2, outline.Top);
                g.DrawLine(highlight, outline.Left, outline.Top + 2, outline.Left, outline.Bottom - 1);
                g.DrawLine(shadow, outline.Right, outline.Top + 2, outline.Right, outline.Bottom);
                g.DrawLine(shadow, outline.Left + 1, outline.Bottom, outline.Right - 1, outline.Bottom);
            }
        }

        private void DrawTabSwitchAnimation(Graphics g) {
            if(tabAnimationFromIndex < 0 || tabAnimationToIndex < 0 ||
                    tabAnimationFromIndex >= tabPages.Count || tabAnimationToIndex >= tabPages.Count) {
                return;
            }

            Rectangle from = GetItemRectangle(tabAnimationFromIndex);
            Rectangle to = GetItemRectangle(tabAnimationToIndex);
            if(from.Width <= 0 || to.Width <= 0) {
                return;
            }

            int elapsed = unchecked(Environment.TickCount - tabAnimationStartTick);
            float progress = Math.Max(0f, Math.Min(1f, elapsed / (float)TAB_ANIMATION_DURATION));
            progress = 1f - ((1f - progress) * (1f - progress));
            int left = (int)(from.Left + ((to.Left - from.Left) * progress));
            int right = (int)(from.Right + ((to.Right - from.Right) * progress));
            int y = (int)(from.Bottom + ((to.Bottom - from.Bottom) * progress)) - 2;
            if(right <= left) {
                return;
            }

            Color baseColor = GetTabAnimationAccentColor();
            using(Pen glow = new Pen(Color.FromArgb(80, baseColor), 5f))
            using(Pen line = new Pen(Color.FromArgb(210, baseColor), 2f)) {
                glow.StartCap = System.Drawing.Drawing2D.LineCap.Round;
                glow.EndCap = System.Drawing.Drawing2D.LineCap.Round;
                line.StartCap = System.Drawing.Drawing2D.LineCap.Round;
                line.EndCap = System.Drawing.Drawing2D.LineCap.Round;
                g.DrawLine(glow, left + 4, y, right - 4, y);
                g.DrawLine(line, left + 5, y, right - 5, y);
            }
        }

        private static Color GetTabAnimationAccentColor() {
            try {
                uint colorizationColor;
                bool opaqueBlend;
                if(PInvoke.DwmGetColorizationColor(out colorizationColor, out opaqueBlend) == 0) {
                    Color accent = Color.FromArgb(255,
                        (int)((colorizationColor >> 16) & 0xff),
                        (int)((colorizationColor >> 8) & 0xff),
                        (int)(colorizationColor & 0xff));
                    int brightness = (accent.R * 299 + accent.G * 587 + accent.B * 114) / 1000;
                    if(brightness > 28 && brightness < 235) {
                        return accent;
                    }
                }
            }
            catch {
            }
            return SystemColors.Highlight;
        }

        private void DrawBackground(Graphics g, bool bSelected, bool fHot, Rectangle rctItem, Edges edges, bool fVisualStyle, int index) {
            if(FluentGlassManager.SuppressManagedBackground && !Config.Skin.UseTabSkin) {
                DrawFluentGlassTabOutline(g, bSelected, fHot || iPseudoHotIndex == index, rctItem);
                return;
            }
            // add by indiff for dark mode
            Brush rectBrush = null;
            if(!FluentGlassManager.SuppressManagedBackground) {
            if (QTUtility.InNightMode)
            {
                // QTUtility2.log("QTabControl DrawBackground InNightMode ");
                rectBrush = new SolidBrush(Config.Skin.TabShadActiveColor);
                // Color light = Color.FromArgb(242, 242, 242);
                Color light = Color.FromArgb(122, 122, 122);
                // Color defaultColor = ShellColors.Default;
                // Color defaultColor2 = Color.FromArgb(240, 240, 240);
                // defaultColor = Color.Black;
                /*Graphic.FillRectangleRTL(g, 
                    QTUtility.InNightMode ?
                        (bSelected ? ShellColors.Light : ShellColors.Default) : 
                        (QTUtility.LaterThan10Beta17666 ? 
                            (bSelected ? ShellColors.Light : ShellColors.Default) :
                            Color.Black), 
                    rctItem, 
                    QTUtility.IsRTL);*/
                Graphic.FillRectangleRTL(g,
                    (bSelected ? light : Color.Black),
                    rctItem,
                    true);
            }
            else
            {
                QTUtility2.log("QTabControl DrawBackground NormanMode ");
                rectBrush = SystemBrushes.Control;
                g.FillRectangle(rectBrush, rctItem);
            }
            }

            if(!fVisualStyle) {
               // g.FillRectangle(rectBrush, rctItem);
               /* 
                  g.FillRectangle(rectBrush, rctItem);
                  g.DrawRectangle(Pens.Black, new Rectangle(0, 0, rctItem.Width - 1, rctItem.Height - 1));
                  */
                int num = bSelected ? 0 : 1;
                if(tabImages == null) { // ���ͼƬΪ��
                    // g.FillRectangle(rectBrush, rctItem);
                    g.DrawLine(SystemPens.ControlLightLight, 
                        new Point(rctItem.X + 2, rctItem.Y), 
                        new Point(((rctItem.X + rctItem.Width) - 2) - num, rctItem.Y));
                    g.DrawLine(SystemPens.ControlLightLight, 
                        new Point(rctItem.X + 2, rctItem.Y), 
                        new Point(rctItem.X, rctItem.Y + 2));
                    g.DrawLine(SystemPens.ControlLightLight, 
                        new Point(rctItem.X, rctItem.Y + 2), 
                        new Point(rctItem.X, (rctItem.Y + rctItem.Height) - 1));
                    g.DrawLine(SystemPens.ControlDarkDark, 
                        new Point((rctItem.X + rctItem.Width) - num, rctItem.Y + 2), 
                        new Point((rctItem.X + rctItem.Width) - num, (rctItem.Y + rctItem.Height) - 1));
                    g.DrawLine(SystemPens.ControlDark, 
                        new Point(((rctItem.X + rctItem.Width) - num) - 1, rctItem.Y + 1), 
                        new Point(((rctItem.X + rctItem.Width) - num) - 1, (rctItem.Y + rctItem.Height) - 1));
                    g.DrawLine(SystemPens.ControlDarkDark, 
                        new Point(((rctItem.X + rctItem.Width) - num) - 1, rctItem.Y + 1), 
                        new Point((rctItem.X + rctItem.Width) - num, rctItem.Y + 2));
                    if(bSelected) {
                        // QTUtility2.log("DrawBackground g.DrawLine bSelected");
                        Pen pen = new Pen(colorSet[2], 2f);
                        g.DrawLine(pen, 
                            new Point(rctItem.X, (rctItem.Y + rctItem.Height) - 1), 
                            new Point((rctItem.X + rctItem.Width) + 1,  (rctItem.Y + rctItem.Height) - 1));
                        pen.Dispose();
                    }
                }  else {  // ���ͼƬ��Ϊ��
                    Bitmap bitmap;
                    if(bSelected) {
                        // QTUtility2.log("tabImages[0] ");
                        bitmap = tabImages[0];
                    }
                    else if(fHot || (iPseudoHotIndex == index)) {
                        // QTUtility2.log("tabImages[2] ");
                        bitmap = tabImages[2];
                    }
                    else {
                        // QTUtility2.log("tabImages[1] ");
                        bitmap = tabImages[1];
                    }
                    if(bitmap != null) { // ���ͼƬ��Ϊ��
                                int left = sizingMargin.Left;
                                int top = sizingMargin.Top;
                                int right = sizingMargin.Right;
                                int bottom = sizingMargin.Bottom;
                                int vertical = sizingMargin.Vertical;
                                int horizontal = sizingMargin.Horizontal;
                                Rectangle[] rectangleArray = new Rectangle[]
                                {
                                    new Rectangle(rctItem.X, rctItem.Y, left, top), 
                                    new Rectangle(rctItem.X + left, rctItem.Y, rctItem.Width - horizontal, top), 
                                    new Rectangle(rctItem.Right - right, rctItem.Y, right, top), 
                                    new Rectangle(rctItem.X, rctItem.Y + top, left, rctItem.Height - vertical), 
                                    new Rectangle(rctItem.X + left, rctItem.Y + top, rctItem.Width - horizontal, rctItem.Height - vertical), 
                                    new Rectangle(rctItem.Right - right, rctItem.Y + top, right, rctItem.Height - vertical), 
                                    new Rectangle(rctItem.X, rctItem.Bottom - bottom, left, bottom), 
                                    new Rectangle(rctItem.X + left, rctItem.Bottom - bottom, rctItem.Width - horizontal, bottom), 
                                    new Rectangle(rctItem.Right - right, rctItem.Bottom - bottom, right, bottom)
                                };
                                Rectangle[] rectangleArray2 = new Rectangle[9];
                                // QTUtility2.log("ͼƬ�������� 9 ");
                                int width = bitmap.Width;
                                int height = bitmap.Height;

                                // QTUtility2.log("ͼƬ����  " + width + " ͼƬ�߶�  " + height);
                                rectangleArray2[0] = new Rectangle(0, 0, left, top);
                                rectangleArray2[1] = new Rectangle(left, 0, width - horizontal, top);
                                rectangleArray2[2] = new Rectangle(width - right, 0, right, top);
                                rectangleArray2[3] = new Rectangle(0, top, left, height - vertical);
                                rectangleArray2[4] = new Rectangle(left, top, width - horizontal, height - vertical);
                                rectangleArray2[5] = new Rectangle(width - right, top, right, height - vertical);
                                rectangleArray2[6] = new Rectangle(0, height - bottom, left, bottom);
                                rectangleArray2[7] = new Rectangle(left, height - bottom, width - horizontal, bottom);
                                rectangleArray2[8] = new Rectangle(width - right, height - bottom, right, bottom);
                                for (int i = 0; i < 9; i++)
                                {
                                    g.DrawImage(bitmap, rectangleArray[i], rectangleArray2[i], GraphicsUnit.Pixel);
                                }
                                // QTUtility2.log("drawbackground by image end");
                                // bitmap.Dispose(); // ���ﵼ��ͼƬ����
                    }
                }
            } // !fVisualStyle
            else {
                VisualStyleRenderer renderer;
                if(!bSelected) {
                    // ��ѡ������ renderer
                    if(!fHot && (iPseudoHotIndex != index)) {
                        Edges edges4 = edges;
                        if(edges4 == Edges.Left) {
                            renderer = vsr_LNormal;
                        }
                        else if(edges4 == Edges.Right) {
                            renderer = vsr_RNormal;
                        }
                        else {
                            renderer = vsr_MNormal;
                        }
                    }
                    else {
                        Edges edges3 = edges;
                        if(edges3 == Edges.Left) {
                            renderer = vsr_LHot;
                        }
                        else if(edges3 == Edges.Right) {
                            renderer = vsr_RHot;
                        }
                        else {
                            renderer = vsr_MHot;
                        }
                    }
                } //  !bSelected
                else {
                    Edges edges2 = edges;
                    if(edges2 == Edges.Left) {
                        renderer = vsr_LPressed;
                    }
                    else if(edges2 == Edges.Right) {
                        renderer = vsr_RPressed;
                    }
                    else {
                        renderer = vsr_MPressed;
                    }
                    // QTUtility2.log("DrawBackground renderer.DrawBackground1");
                    if (!QTUtility.InNightMode)
                    {
                        renderer.DrawBackground(g, rctItem);
                    }
                    return;
                }
                // QTUtility2.log("DrawBackground renderer.DrawBackground2");
                if (!QTUtility.InNightMode)
                {
                    renderer.DrawBackground(g, rctItem);
                }
            }
        }

        private static void DrawDriveLetter(Graphics g, string str, Font fnt, Rectangle rctFldImg, bool fSelected) {
            Rectangle layoutRectangle = new Rectangle(rctFldImg.X + 7, rctFldImg.Y + 6, 0x10, 0x10);
            using(SolidBrush brush = 
                        new SolidBrush( 
                                /*QTUtility2.MakeModColor(fSelected ? 
                                    Config.Skin.TabShadActiveColor : 
                                    Config.Skin.TabShadInactiveColor
                                    )*/
                                QTUtility2.MakeModColor(selectedColor(fSelected))
                        )
                   ) {
                Rectangle rectangle2 = layoutRectangle;
                rectangle2.Offset(1, 0);
                g.DrawString(str, fnt, brush, rectangle2);
                rectangle2.Offset(-2, 0);
                g.DrawString(str, fnt, brush, rectangle2);
                rectangle2.Offset(1, -1);
                g.DrawString(str, fnt, brush, rectangle2);
                rectangle2.Offset(0, 2);
                g.DrawString(str, fnt, brush, rectangle2);
                rectangle2.Offset(1, 0);
                g.DrawString(str, fnt, brush, rectangle2);
                rectangle2.Offset(0, -2);
                g.DrawString(str, fnt, brush, rectangle2);
                rectangle2.Offset(-2, 0);
                g.DrawString(str, fnt, brush, rectangle2);
                rectangle2.Offset(0, 2);
                g.DrawString(str, fnt, brush, rectangle2);
                // dark mode brshActive.Color
                // brush.Color = fSelected ? Config.Skin.TabTextActiveColor : Config.Skin.TabTextInactiveColor;
                brush.Color = selectedColor(fSelected);
                g.DrawString(str, fnt, brush, layoutRectangle);
            }
        }

        // 43 ����bug
        /*
         * 
            Message ---
            δ�������������õ������ʵ����
            HelpLink ---

            Source ---
            QTTabBar

            StackTrace ---
               �� QTTabBarLib.QTabControl.DrawTab(Graphics g, Rectangle itemRct, Int32 index, QTabItem tabHot, Boolean fVisualStyle)
               �� QTTabBarLib.QTabControl.OnPaint_MultipleRow(PaintEventArgs e)
            TargetSite ---
            Void DrawTab(System.Drawing.Graphics, System.Drawing.Rectangle, Int32, QTTabBarLib.QTabItem, Boolean)
         
             Message ---
            ����������Χ������Ϊ�Ǹ�ֵ��С�ڼ��ϴ�С��
                       ������: index
            HelpLink ---

            Source ---
            mscorlib
            StackTrace ---
                       �� System.Collections.ArrayList.get_Item(Int32 index)
                       �� System.Windows.Forms.ImageList.ImageCollection.IndexOfKey(String key)
                       �� System.Windows.Forms.ImageList.ImageCollection.ContainsKey(String key)
                       �� QTTabBarLib.QTabControl.DrawTab(Graphics g, Rectangle itemRct, Int32 index, QTabItem tabHot, Boolean fVisualStyle)
*/
        // ��ָ���߿��ڻ��Ƶ�ǰ�Ӿ���ʽԪ�صı���ͼ��
        private void DrawTab(Graphics g, Rectangle itemRct, int index, QTabItem tabHot, bool fVisualStyle) {
            try
            {
                Rectangle textRect; // �����ı�����
                Rectangle rctItem = textRect = itemRct; // ��ǩ����
                // ����������Χ������Ϊ�Ǹ�ֵ��С�ڼ��ϴ�С��
                QTabItem baseTabItem = tabPages[index]; // ��ǰ�ı�ǩ��
                bool bSelected = iSelectedIndex == index; // �Ƿ�ѡ��
                bool fHot = baseTabItem == tabHot; // �Ƿ�δ�ȵ��ǩ
                textRect.X += 2; // x��ƫ�� 2 ����
                if(bSelected) {
                    rctItem.Width += 4; // ���ѡ������ȼӿ� 4 ����
                }
                else {
                    rctItem.X += 2;  // ��ѡ�� ��ǩ����x��ƫ�� 2 ����
                    rctItem.Y += 2;  // ��ѡ�� ��ǩ����y��ƫ�� 2 ����
                    rctItem.Height -= 2;  // ��ѡ�� ��ǩ����߶Ȼ��� 2 ����
                    // textRect.Y += 2; // ��ѡ�� �ı�����y��ƫ�� 2 ����
                }
                Rectangle contentRect = ApplyContentMargins(rctItem);
                textRect = ApplyContentMargins(textRect);
                DrawBackground(g, bSelected, fHot, rctItem, baseTabItem.Edge, fVisualStyle, index);
                int tabPosYHalfTabHeight = (rctItem.Height - 0x10) / 2; // ��ǩY����� 10 ���ص�һ��
                tabPosYHalfTabHeight = (contentRect.Height - 0x10) / 2;
                // QTUtility2.log("draw folder image " + fDrawFolderImg +  " baseTabItem.ImageKey " + baseTabItem.ImageKey );
                // �ж��Ƿ�ʹ��ͼƬ
                if(fDrawFolderImg && QTUtility.ImageListGlobal.Images.ContainsKey(baseTabItem.ImageKey)) {
                    // ͼƬ���� 0x10 -> 16
                    Rectangle imgRect = new Rectangle(
                        contentRect.X + (bSelected ? 7 : 5),
                        contentRect.Y + tabPosYHalfTabHeight,
                        0x10, 
                        0x10); // 16 �߶�  * 16 ����
                    textRect.X += 0x18;
                    textRect.Width -= 0x18; // 24
                    if((fNowMouseIsOnIcon && (iTabMouseOnButtonsIndex == index)) || (iTabIndexOfSubDirShown == index)) {
                        if(fSubDirShown && (iTabIndexOfSubDirShown == index)) {
                            imgRect.X++;
                            imgRect.Y++;
                        }
                        if(bmpFolIconBG == null) {
                            bmpFolIconBG = Resources_Image.imgFolIconBG;
                        }
                        g.DrawImage(bmpFolIconBG, new Rectangle(imgRect.X - 2, imgRect.Y - 2, imgRect.Width + 4, imgRect.Height + 4));
                    }
					// ���Ʊ���ͼƬ
                    g.DrawImage(QTUtility.ImageListGlobal.Images[baseTabItem.ImageKey], imgRect);
					// �ж��Ƿ��������ͼ��
                    if(Config.Tabs.ShowDriveLetters) {
                        string pathInitial = baseTabItem.PathInitial;
                        if(pathInitial.Length > 0) {
                            DrawDriveLetter(g, pathInitial, fntDriveLetter, imgRect, bSelected);
                        }
                    }
                }
                else {
                    textRect.X += 4;
                    textRect.Width -= 4;
                }
                if(baseTabItem.TabLocked && customLockIcon != null) {
                    DrawCustomLockIcon(g, ref textRect, rctItem, contentRect, tabPosYHalfTabHeight, bSelected);
                }
                if(baseTabItem.TabLocked && customLockIcon == null) { // ����������������ͼƬ
                    Rectangle lockRect = new Rectangle(
                        rctItem.X + (bSelected ? 6 : 4),  // ѡ��ƫ�� 6 ���ء���ѡ��ƫ�� 4 ����
                        rctItem.Y + tabPosYHalfTabHeight,  // Y��Ϊ��ǩһ��߶�
                        9, 
                        11); // 9 * 11
                    lockRect.Offset(contentRect.X - rctItem.X, contentRect.Y - rctItem.Y);
                    if(fDrawFolderImg) { // �����ļ���ͼƬ
                        lockRect.X += 9;   //  X ƫ�� 9 ����
                        lockRect.Y += 5;   //  Y ƫ�� 9 ����
                    }
                    else {
                        lockRect.Y += 2; //  X ƫ�� 2 ����
                        textRect.X += 10;//  Y ƫ�� 10 ����
                        textRect.Width -= 10;  // ���ȼ�10����
                    }
                    if(bmpLocked == null) {
                        bmpLocked = Resources_Image.imgLocked;
                    }
                    g.DrawImage(bmpLocked, lockRect);
                }
                bool isComment = baseTabItem.Comment.Length > 0;
                if((fDrawCloseButton && !fCloseBtnOnHover) && !fNowShowCloseBtnAlt) {
                    textRect.Width -= CloseButtonReservedWidth - 2;
                }
                float textWidth = isComment ? 
                    ((baseTabItem.TitleTextSize.Width + baseTabItem.SubTitleTextSize.Width) + 4f) : 
                    (baseTabItem.TitleTextSize.Width + 2f);

                // ��ǩY��ƫ��Ϊ �ı�����߶�- �ı��߶�  һ��
                // [log] C:QTabControl M:DrawTab P:12464 T:1 cost:0.993���� 2022/10/1 16:57:52  Config.Skin.TabHeight 35
                // [log] C:QTabControl M:DrawTab P:12464 T:1 cost:0���� 2022/10/1 16:57:52  textRect.Height 35
                // [log] C:QTabControl M:DrawTab P:12464 T:1 cost:0���� 2022/10/1 16:57:52  baseTabItem.TitleTextSize.Height 20
                // [log] C:QTabControl M:DrawTab P:12464 T:1 cost:0���� 2022/10/1 16:57:52  textRect.X 26
                // [log] C:QTabControl M:DrawTab P:12464 T:1 cost:0���� 2022/10/1 16:57:52  textRect.Y 0
                // [log] C:QTabControl M:DrawTab P:12464 T:1 cost:0���� 2022/10/1 16:57:52  textPosX 53.5
                // [log] C:QTabControl M:DrawTab P:12464 T:1 cost:0.994���� 2022/10/1 16:57:52  textPosY 2.5
                // QTUtility2.log(" Config.Skin.TabHeight " + Config.Skin.TabHeight);
                // QTUtility2.log(" textRect.Height " + textRect.Height);
                // QTUtility2.log(" baseTabItem.TitleTextSize.Height " + baseTabItem.TitleTextSize.Height);
                // QTUtility2.log(" textRect.X " + textRect.X);
                // QTUtility2.log(" textRect.Y " + textRect.Y);
                // QTUtility2.log(" textPosX " + ((tabTextAlignment == StringAlignment.Center)
                //     ? Math.Max(((textRect.Width - textWidth) / 2f), 0f) :
                //     0f));
                // QTUtility2.log(" textPosY " + Math.Max(((textRect.Height - baseTabItem.TitleTextSize.Height) / 2f) - 5, 0f));
                // float textPosY = Math.Max(((textRect.Height - baseTabItem.TitleTextSize.Height) / 2f) - 5 , 0f);
                // float textPosY = 0;
                // ����Ϊ������ʾ
                float textPosY = -(textRect.Height - baseTabItem.TitleTextSize.Height) / 2;
                // float textPosY = 5f;
                // �����ǩ�ı�����������ƫ��ֵ
                float textPosX = (tabTextAlignment == StringAlignment.Center)
                              ? Math.Max(((textRect.Width - textWidth) / 2f), 0f) :
                              0f; 
                RectangleF textRct = new RectangleF(
                                            textRect.X + textPosX, 
                                            textRect.Y + textPosY,
                                            Math.Min((baseTabItem.TitleTextSize.Width + 2f), (textRect.Width - textPosX)), 
                                            textRect.Height);
                Font titleFont = (bSelected && fActiveTxtBold)
                        ? (baseTabItem.Underline ? fntBold_Underline : fntBold)
                        : (baseTabItem.Underline ? fnt_Underline : Font);
                Color titleColor = GetTabTextColor(bSelected, fHot);
                if(ShouldDrawTextShadow(bSelected, fHot)) {
                    DrawTextWithShadow(g,
                        baseTabItem.Text,
                        titleColor,
                        GetTabShadowColor(bSelected, fHot),
                        titleFont,
                        textRct,
                        sfTypoGraphic);
                }
                else {
                    using(SolidBrush textBrush = new SolidBrush(titleColor)) {
                        g.DrawString(baseTabItem.Text, titleFont, textBrush, textRct, sfTypoGraphic);
                    }
                }
                if(iFocusedTabIndex == index) {
                    Rectangle rectangle = rctItem;
                    rectangle.Inflate(-2, -1);
                    rectangle.Y++;
                    rectangle.Width--;
                    ControlPaint.DrawFocusRectangle(g, rectangle);
                }
				// �Ƿ����ñ�ע����
                if(isComment && (textRect.Width > baseTabItem.TitleTextSize.Width)) {
                    // ����Ϊ���е�����, �ı��߶� - ��ע�ı��߶ȵ�һ��
                    // float posY = Math.Max(((textRect.Height - baseTabItem.SubTitleTextSize.Height) / 2f), 0f);
                    float posY = Math.Max(((textRect.Height - baseTabItem.SubTitleTextSize.Height) / 2f), 0f);
					// PointF	����ʾ������������Ͻ�
					// SizeF	����ʾ��������Ŀ��Ⱥ͸߶ȡ�
					// posY = textRect.Y + posY;
					posY = textRect.Y  - posY; // �޸�������ǩ����������
                    // float posY = textRect.Y + Math.Max( baseTabItem.SubTitleTextSize.Height, 0f );
					RectangleF drawStrRectF = new RectangleF(
                        textRct.Right, 
                        posY, 
                        Math.Min(
                            (baseTabItem.SubTitleTextSize.Width + 2f),
                            (textRect.Width - ((baseTabItem.TitleTextSize.Width + textPosX) + 4f))
                        ), 
                        textRect.Height);  // �ı�����
                    string commentText = (fAutoSubText ? "@" : ":") + baseTabItem.Comment;
                    Color commentColor = GetTabTextColor(bSelected, fHot);
                    if(ShouldDrawTextShadow(bSelected, fHot)) {
                        DrawTextWithShadow(g,
                            commentText,
                            commentColor,
                            GetTabShadowColor(bSelected, fHot),
                            fntSubText,
                            drawStrRectF,
                            sfTypoGraphic);
                    }
                    else {
                        using(SolidBrush commentBrush = new SolidBrush(commentColor)) {
                            g.DrawString(commentText, fntSubText, commentBrush, drawStrRectF, sfTypoGraphic);
                        }
                    }
                }
                if(fDrawCloseButton && (!fCloseBtnOnHover || fHot)) {
                    Rectangle closeButtonRectangle = GetCloseButtonRectangle(baseTabItem.TabBounds, bSelected);
                    if(customCloseButtonImages != null) {
                        DrawCustomCloseButton(g, closeButtonRectangle, index);
                    }
                    else if(fNowMouseIsOnCloseBtn && (iTabMouseOnButtonsIndex == index)) {
                        if(MouseButtons == MouseButtons.Left) {
                            if(bmpCloseBtn_Pressed == null) {
                                bmpCloseBtn_Pressed = Resources_Image.imgCloseButton_Press;
                            }
                            g.DrawImage(bmpCloseBtn_Pressed, closeButtonRectangle);
                        }
                        else {
                            if(bmpCloseBtn_Hot == null) {
                                bmpCloseBtn_Hot = Resources_Image.imgCloseButton_Hot;
                            }
                            g.DrawImage(bmpCloseBtn_Hot, closeButtonRectangle);
                        }
                    }
                    else if(fNowShowCloseBtnAlt || fCloseBtnOnHover) {
                        if(bmpCloseBtn_ColdAlt == null) {
                            bmpCloseBtn_ColdAlt = Resources_Image.imgCloseButton_ColdAlt;
                        }
                        g.DrawImage(bmpCloseBtn_ColdAlt, closeButtonRectangle);
                    }
                    else {
                        if(bmpCloseBtn_Cold == null) {
                            bmpCloseBtn_Cold = Resources_Image.imgCloseButton_Cold;
                        }
                        g.DrawImage(bmpCloseBtn_Cold, closeButtonRectangle);
                    }
                }
            }
            catch (Exception e)
            {
                QTUtility2.MakeErrorLog(e, "DrawTab");
            }
        }

        private static void DrawTextWithShadow(Graphics g, string txt, Color clrTxt, Color clrShdw, Font fnt, RectangleF rct, StringFormat sf) {
            RectangleF layoutRectangle = rct;
            RectangleF ef2 = rct;
            RectangleF ef3 = rct;
            layoutRectangle.Offset(1f, 1f);
            ef2.Offset(2f, 0f);
            ef3.Offset(1f, 2f);
            Color color = Color.FromArgb(0xc0, clrShdw);
            Color color2 = Color.FromArgb(0x80, clrShdw);
            using(SolidBrush brush = new SolidBrush(Color.FromArgb(0x40, clrShdw))) {
                g.DrawString(txt, fnt, brush, ef3, sf);
                brush.Color = color2;
                g.DrawString(txt, fnt, brush, ef2, sf);
                brush.Color = color;
                g.DrawString(txt, fnt, brush, layoutRectangle, sf);
                brush.Color = clrTxt;
                g.DrawString(txt, fnt, brush, rct, sf);
            }
        }

        public bool FocusNextTab(bool fBack, bool fEntered, bool fEnd) {
            if(tabPages.Count <= 0) {
                return false;
            }
            if(fEntered) {
                iFocusedTabIndex = fBack ? (tabPages.Count - 1) : 0;
                SetPseudoHotIndex(iFocusedTabIndex);
                return true;
            }
            if((fBack && (iFocusedTabIndex == 0)) || (!fBack && (iFocusedTabIndex == (tabPages.Count - 1)))) {
                iFocusedTabIndex = -1;
                return false;
            }
            if(fEnd) {
                iFocusedTabIndex = fBack ? 0 : (tabPages.Count - 1);
            }
            else {
                iFocusedTabIndex += fBack ? -1 : 1;
                if(iFocusedTabIndex < 0) {
                    iFocusedTabIndex = tabPages.Count - 1;
                }
            }
            SetPseudoHotIndex(iFocusedTabIndex);
            return true;
        }

        private void DrawCustomLockIcon(Graphics graphics, ref Rectangle textRect, Rectangle itemRect,
                Rectangle contentRect, int halfTabHeight, bool selected) {
            Rectangle lockRect = new Rectangle(
                    itemRect.X + (selected ? 6 : 4),
                    itemRect.Y + halfTabHeight,
                    customLockIconSize.Width,
                    customLockIconSize.Height);
            lockRect.Offset(contentRect.X - itemRect.X, contentRect.Y - itemRect.Y);
            if(fDrawFolderImg) {
                lockRect.X += 9;
                lockRect.Y += 5;
            }
            else {
                lockRect.Y += 2;
                int inset = customLockIconSize.Width + 1;
                textRect.X += inset;
                textRect.Width -= inset;
            }
            lockRect.Offset(Config.Skin.LockIconImageOffsetX, Config.Skin.LockIconImageOffsetY);
            DrawCustomImage(graphics, customLockIcon, lockRect);
        }

        private void DrawCustomCloseButton(Graphics graphics, Rectangle buttonBounds, int tabIndex) {
            int state = 0;
            if(fNowMouseIsOnCloseBtn && iTabMouseOnButtonsIndex == tabIndex) {
                state = MouseButtons == MouseButtons.Left ? 2 : 1;
            }
            else if(fNowShowCloseBtnAlt || fCloseBtnOnHover) {
                state = 3;
            }
            int imageIndex = customCloseButtonImages.Length == 1
                    ? 0
                    : Math.Min(state, customCloseButtonImages.Length - 1);
            Rectangle imageRect = TabButtonImageGeometry.CenterWithin(buttonBounds, customCloseButtonSize);
            DrawCustomImage(graphics, customCloseButtonImages[imageIndex], imageRect);
        }

        private static void DrawCustomImage(Graphics graphics, Bitmap image, Rectangle destination) {
            System.Drawing.Drawing2D.InterpolationMode interpolation = graphics.InterpolationMode;
            System.Drawing.Drawing2D.PixelOffsetMode pixelOffset = graphics.PixelOffsetMode;
            try {
                graphics.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.HighQualityBicubic;
                graphics.PixelOffsetMode = System.Drawing.Drawing2D.PixelOffsetMode.HighQuality;
                graphics.DrawImage(image, destination);
            }
            finally {
                graphics.InterpolationMode = interpolation;
                graphics.PixelOffsetMode = pixelOffset;
            }
        }
        private Rectangle ApplyContentMargins(Rectangle rectangle) {
            return TabSkinGeometry.ApplyContentMargins(rectangle, contentMargin);
        }

        private Rectangle GetCloseButtonRectangle(Rectangle rctTab, bool fSelected) {
            int buttonWidth = customCloseButtonImages == null ? 15 : Math.Max(15, customCloseButtonSize.Width);
            int buttonHeight = customCloseButtonImages == null ? 15 : Math.Max(15, customCloseButtonSize.Height);
            int availableHeight = Math.Max(buttonHeight, itemSize.Height - contentMargin.Vertical);
            int num = contentMargin.Top + ((availableHeight - buttonHeight) / 2) + 1;
            if(!fSelected) {
                num += 2;
            }
            if((iMultipleType == 0) && fNeedToDrawUpDown) {
                rctTab.X += iScrollWidth;
            }
            int x = rctTab.Right - contentMargin.Right - buttonWidth - 2;
            int y = rctTab.Top + num;
            if(customCloseButtonImages != null) {
                x += Config.Skin.CloseButtonImageOffsetX;
                y += Config.Skin.CloseButtonImageOffsetY;
            }
            return new Rectangle(x, y, buttonWidth, buttonHeight);
        }

        public int GetFocusedTabIndex() {
            return iFocusedTabIndex;
        }

        private Rectangle GetFolderIconRectangle(Rectangle rctTab, bool fSelected) {
            int availableHeight = Math.Max(0x10, rctTab.Height - contentMargin.Vertical);
            int num = contentMargin.Top + (availableHeight - 0x10) / 2;
            if(!fSelected) {
                num += 2;
            }
            if((iMultipleType == 0) && fNeedToDrawUpDown) {
                rctTab.X += iScrollWidth;
            }
            return new Rectangle(rctTab.X + contentMargin.Left + (fSelected ? 5 : 3), (rctTab.Y + num) - 2, 20, 20);
        }

        private Rectangle GetItemRectangle(int index) {
            Rectangle tabBounds = tabPages[index].TabBounds;
            if(fNeedToDrawUpDown) {
                tabBounds.X += iScrollWidth;
            }
            return tabBounds;
        }

        private bool TryGetItemRectangle(int index, out Rectangle rectangle) {
            rectangle = Rectangle.Empty;
            if(index < 0 || index >= tabPages.Count) {
                return false;
            }
            try {
                rectangle = GetItemRectangle(index);
                return true;
            }
            catch(ArgumentOutOfRangeException) {
                return false;
            }
        }

        private Rectangle GetItemRectWithInflation(int index) {
            Rectangle tabBounds = tabPages[index].TabBounds;
            if(index == iSelectedIndex) {
                tabBounds.Inflate(4, 0);
            }
            if(fNeedToDrawUpDown) {
                tabBounds.X += iScrollWidth;
            }
            return tabBounds;
        }

        /**
         * ��ȡ�������ı�ǩ
         * bug ��ֻ��һ����ǩ��ʱ�򣬵����ǩ�հ״�ʶ��Ϊ��ǩ
         */
        private bool UsesAdvancedSkinHitTesting() {
            return tabImages != null && (overlapPixels > 0 || hitTestTransparent);
        }

        private QTabItem GetAdvancedTabAtPoint(Point point, out int index) {
            index = -1;
            int bestRow = int.MinValue;
            int bestSelected = -1;
            int bestIndex = -1;
            for(int i = 0; i < tabPages.Count; i++) {
                if(!IsPointOverTab(i, point)) {
                    continue;
                }
                int row = iMultipleType == 0 ? 0 : tabPages[i].Row;
                int selected = i == iSelectedIndex ? 1 : 0;
                if(row > bestRow ||
                        (row == bestRow && selected > bestSelected) ||
                        (row == bestRow && selected == bestSelected && i > bestIndex)) {
                    bestRow = row;
                    bestSelected = selected;
                    bestIndex = i;
                }
            }
            index = bestIndex;
            return bestIndex < 0 ? null : tabPages[bestIndex];
        }

        private bool IsPointOverTab(int index, Point point) {
            if(!hitTestTransparent) {
                return GetItemRectWithInflation(index).Contains(point);
            }

            Rectangle destination = GetTabBackgroundRectangle(index);
            if(!destination.Contains(point)) {
                return false;
            }
            Bitmap bitmap = tabImages[index == iSelectedIndex ? 0 : 1];
            if(bitmap == null || bitmap.Width == 0 || bitmap.Height == 0) {
                return true;
            }

            int sourceX = TabSkinGeometry.MapNineSliceCoordinate(
                    point.X - destination.X,
                    destination.Width,
                    bitmap.Width,
                    sizingMargin.Left,
                    sizingMargin.Right);
            int sourceY = TabSkinGeometry.MapNineSliceCoordinate(
                    point.Y - destination.Y,
                    destination.Height,
                    bitmap.Height,
                    sizingMargin.Top,
                    sizingMargin.Bottom);
            try {
                return bitmap.GetPixel(sourceX, sourceY).A != 0;
            }
            catch(ArgumentException) {
                return true;
            }
        }

        private Rectangle GetTabBackgroundRectangle(int index) {
            Rectangle rectangle = GetItemRectangle(index);
            if(index == iSelectedIndex) {
                rectangle.Width += 4;
            }
            else {
                rectangle.X += 2;
                rectangle.Y += 2;
                rectangle.Height = Math.Max(1, rectangle.Height - 2);
            }
            return rectangle;
        }

        public QTabItem GetTabMouseOn() {
            if (this == null || this.IsDisposed)
            {
                 if (tabPages.Count == 1)
                 {
                     Point pp = PointToClient(MousePosition);
                     if (((upDown != null) && upDown.Visible) && upDown.Bounds.Contains(pp))
                     {
                         return null;
                     }
                     QTUtility2.log(" return tabPage[0] 1");
                     return tabPages[0];
                 }
                 return null;
            }
            Point pt = PointToClient(MousePosition);
            if (((upDown != null) && upDown.Visible) && upDown.Bounds.Contains(pt))
            {
                return null;
            }

            // �����ǩֻ��һ���Ļ�
            if(UsesAdvancedSkinHitTesting()) {
                int advancedIndex;
                return GetAdvancedTabAtPoint(pt, out advancedIndex);
            }

            if (tabPages.Count == 1) {
                 if (tabPages[0].TabBounds.Contains(pt))
                 {
                     QTUtility2.log("contains pt return tabPage[0] 2");
                     return tabPages[0];
                 }
                 return null;
            }

            QTabItem base2 = null;
            QTabItem base3 = null;
            for(int i = 0; i < tabPages.Count; i++) {
                if(GetItemRectWithInflation(i).Contains(pt)) {
                    if(base2 == null) {
                        base2 = tabPages[i];
                        if(iMultipleType == 0) {
                            return base2;
                        }
                    }
                    else {
                        base3 = tabPages[i];
                        break;
                    }
                }
            }
            if((base3 != null) && (base2.Row <= base3.Row)) {
                return base3;
            }
            return base2;
        }

        public QTabItem GetTabMouseOn(out int index) {
            Point pt = PointToClient(MousePosition);
            if(UsesAdvancedSkinHitTesting()) {
                if(((upDown != null) && upDown.Visible) && upDown.Bounds.Contains(pt)) {
                    index = -1;
                    return null;
                }
                return GetAdvancedTabAtPoint(pt, out index);
            }

            QTabItem base2 = null;
            QTabItem base3 = null;
            int num = -1;
            int num2 = -1;
            for(int i = 0; i < tabPages.Count; i++) {
                if(GetItemRectWithInflation(i).Contains(pt)) {
                    if(base2 == null) {
                        base2 = tabPages[i];
                        num = i;
                        if(iMultipleType == 0) {
                            index = i;
                            return base2;
                        }
                    }
                    else {
                        base3 = tabPages[i];
                        num2 = i;
                        break;
                    }
                }
            }
            if(base3 != null) {
                if(base2.Row > base3.Row) {
                    index = num;
                    return base2;
                }
                index = num2;
                return base3;
            }
            index = num;
            return base2;
        }

        public Rectangle GetTabRect(QTabItem tab) {
            Rectangle tabBounds = tab.TabBounds;
            if(fNeedToDrawUpDown) {
                tabBounds.X += iScrollWidth;
            }
            return tabBounds;
        }

        public Rectangle GetTabRect(int index, bool fInflation) {
            if((index <= -1) || (index >= tabPages.Count)) {
                throw new ArgumentOutOfRangeException("index," + index, "index is out of range.");
            }
            if(fInflation) {
                return GetItemRectWithInflation(index);
            }
            return GetItemRectangle(index);
        }

        private bool HitTestOnButtons(Rectangle rctTab, Point pntClient, bool fCloseButton, bool fSelected) {
            if(fCloseButton) {
                return GetCloseButtonRectangle(rctTab, fSelected).Contains(pntClient);
            }
            return GetFolderIconRectangle(rctTab, fSelected).Contains(pntClient);
        }

        private void InitializeRenderer() {
            vsr_LPressed = new VisualStyleRenderer(VisualStyleElement.Tab.TopTabItemLeftEdge.Pressed);
            vsr_RPressed = new VisualStyleRenderer(VisualStyleElement.Tab.TopTabItemRightEdge.Pressed);
            vsr_MPressed = new VisualStyleRenderer(VisualStyleElement.Tab.TopTabItem.Pressed);
            vsr_LNormal = new VisualStyleRenderer(VisualStyleElement.Tab.TopTabItemLeftEdge.Normal);
            vsr_RNormal = new VisualStyleRenderer(VisualStyleElement.Tab.TopTabItemRightEdge.Normal);
            vsr_MNormal = new VisualStyleRenderer(VisualStyleElement.Tab.TopTabItem.Normal);
            vsr_LHot = new VisualStyleRenderer(VisualStyleElement.Tab.TopTabItem.Hot);
            vsr_RHot = new VisualStyleRenderer(VisualStyleElement.Tab.TopTabItemRightEdge.Hot);
            vsr_MHot = new VisualStyleRenderer(VisualStyleElement.Tab.TopTabItem.Hot);
        }

        private void InvalidateTabsOnMouseMove(QTabItem tabPage, int index, Point pnt) {
            iTabMouseOnButtonsIndex = index;
            if(tabPage != hotTab) {
                hotTab = tabPage;
                if((tabPage != null) && !tabPage.TabLocked) {
                    bool fSelected = index == iSelectedIndex;
                    if(fDrawCloseButton) {
                        fNowMouseIsOnCloseBtn = HitTestOnButtons(tabPage.TabBounds, pnt, true, fSelected);
                    }
                    if(fDrawFolderImg && fShowSubDirTip) {
                        fNowMouseIsOnIcon = HitTestOnButtons(tabPage.TabBounds, pnt, false, fSelected);
                    }
                }
                else {
                    fNowMouseIsOnCloseBtn = false;
                    fNowMouseIsOnIcon = false;
                }
                PInvoke.InvalidateRect(Handle, IntPtr.Zero, true);
            }
            else if(tabPage != null) {
                bool flag2 = index == iSelectedIndex;
                bool flag3 = false;
                if(fDrawCloseButton) {
                    bool flag4 = HitTestOnButtons(tabPage.TabBounds, pnt, true, flag2);
                    if(fNowMouseIsOnCloseBtn ^ flag4) {
                        fNowMouseIsOnCloseBtn = flag4 && !tabPage.TabLocked;
                        flag3 = true;
                    }
                }
                if(fDrawFolderImg && fShowSubDirTip) {
                    bool flag5 = HitTestOnButtons(tabPage.TabBounds, pnt, false, flag2);
                    if(fNowMouseIsOnIcon ^ flag5) {
                        fNowMouseIsOnIcon = flag5;
                        flag3 = true;
                    }
                }
                if(flag3) {
                    PInvoke.InvalidateRect(Handle, IntPtr.Zero, true);
                }
            }
        }

        protected override void OnLostFocus(EventArgs e) {
            iFocusedTabIndex = -1;
            if(iPseudoHotIndex != -1) {
                SetPseudoHotIndex(-1);
            }
            base.OnLostFocus(e);
        }

        protected override void OnMouseDoubleClick(MouseEventArgs e) {
            if(!fSuppressDoubleClick) {
                int num;
                QTabItem tabMouseOn = GetTabMouseOn(out num);
                if(((!fDrawCloseButton || (tabMouseOn == null)) || !HitTestOnButtons(tabMouseOn.TabBounds, e.Location, true, num == iSelectedIndex)) && ((!fDrawFolderImg || !fShowSubDirTip) || ((tabMouseOn == null) || !HitTestOnButtons(tabMouseOn.TabBounds, e.Location, false, num == iSelectedIndex)))) {
                    base.OnMouseDoubleClick(e);
                    fSuppressMouseUp = true;
                }
            }
        }

        protected override void OnMouseDown(MouseEventArgs e) {
            int num;
            QTabItem tabMouseOn = GetTabMouseOn(out num);
            if(tabMouseOn != null) {
                bool cancel = e.Button == MouseButtons.Right;
                if((!cancel && fDrawCloseButton) && HitTestOnButtons(tabMouseOn.TabBounds, e.Location, true, num == iSelectedIndex)) {
                    PInvoke.InvalidateRect(Handle, IntPtr.Zero, true);
                    return;
                }
                if((fNowMouseIsOnIcon && HitTestOnButtons(tabMouseOn.TabBounds, e.Location, false, num == iSelectedIndex)) && (TabIconMouseDown != null)) {
                    if((e.Button == MouseButtons.Left) || cancel) {
                        iTabIndexOfSubDirShown = num;
                        int tabPageIndex = 0;
                        if((iMultipleType == 0) && fNeedToDrawUpDown) {
                            tabPageIndex = iScrollWidth;
                        }
                        TabIconMouseDown(this, new QTabCancelEventArgs(tabMouseOn, tabPageIndex, cancel, TabControlAction.Selecting));
                        PInvoke.InvalidateRect(Handle, IntPtr.Zero, true);
                    }
                    return;
                }
                if(e.Button == MouseButtons.Left) {
                    MouseChord chord = QTUtility.MakeMouseChord(MouseChord.Left, ModifierKeys);
                    if(!Config.Mouse.TabActions.ContainsKey(chord) && SelectTab(tabMouseOn)) {
                        fSuppressDoubleClick = true;
                        timerSuppressDoubleClick.Enabled = true;
                    }
                }
            }
            draggingTab = tabMouseOn;
            base.OnMouseDown(e);
        }

        protected override void OnMouseLeave(EventArgs e) {
            iToolTipIndex = -1;
            if(toolTip != null) {
                toolTip.Active = false;
            }
            iPointedChanged_LastRaisedIndex = -2;
            if((PointedTabChanged != null) && (hotTab != null)) {
                PointedTabChanged(null, new QTabCancelEventArgs(null, -1, false, TabControlAction.Deselecting));
            }
            hotTab = null;
            fNowMouseIsOnCloseBtn = fNowMouseIsOnIcon = false;
            PInvoke.InvalidateRect(Handle, IntPtr.Zero, true);
            base.OnMouseLeave(e);
        }

        protected override void OnMouseMove(MouseEventArgs e) {
            int num;
            if(((e.Button == MouseButtons.Right || e.Button == MouseButtons.Left) &&
                    !RectangleToScreen(ClientRectangle).Contains(MousePosition)) &&
                    ((ItemDrag != null) && (draggingTab != null))) {
                QTabItem dragItem = draggingTab;
                draggingTab = null;
                ItemDrag(this, new ItemDragEventArgs(e.Button, dragItem));
            }
            QTabItem tabMouseOn = GetTabMouseOn(out num);
            InvalidateTabsOnMouseMove(tabMouseOn, num, e.Location);
            if((PointedTabChanged != null) && (num != iPointedChanged_LastRaisedIndex)) {
                if(tabMouseOn != null) {
                    iPointedChanged_LastRaisedIndex = num;
                    PointedTabChanged(this, new QTabCancelEventArgs(tabMouseOn, num, false, TabControlAction.Selecting));
                }
                else if(iPointedChanged_LastRaisedIndex != -2) {
                    iPointedChanged_LastRaisedIndex = -1;
                    PointedTabChanged(this, new QTabCancelEventArgs(null, -1, false, TabControlAction.Deselecting));
                }
            }
            if(tabMouseOn != null) {
                if(((iToolTipIndex != num) && IsHandleCreated) && !string.IsNullOrEmpty(tabMouseOn.ToolTipText)) {
                    if(toolTip == null) {
                        toolTip = new ToolTip(components) { ShowAlways = true };
                    }
                    else {
                        toolTip.Active = false;
                    }
                    string toolTipText = tabMouseOn.ToolTipText;
                    string str2 = tabMouseOn.ShellToolTip;
                    if(!string.IsNullOrEmpty(str2)) {
                        toolTipText = toolTipText + "\r\n" + str2;
                    }
                    iToolTipIndex = num;
                    toolTip.SetToolTip(this, toolTipText);
                    toolTip.Active = true;
                }
            }
            else {
                iToolTipIndex = -1;
                if(toolTip != null) {
                    toolTip.Active = false;
                }
            }
            base.OnMouseMove(e);
        }

        protected override void OnMouseUp(MouseEventArgs e) {
            draggingTab = null;
            if(fSuppressMouseUp) {
                fSuppressMouseUp = false;
                base.OnMouseUp(e);
            }
            else {
                int num;
                QTabItem tabMouseOn = GetTabMouseOn(out num);
                if(((fDrawCloseButton && (e.Button != MouseButtons.Right)) && ((CloseButtonClicked != null) && (tabMouseOn != null))) && (!tabMouseOn.TabLocked && HitTestOnButtons(tabMouseOn.TabBounds, e.Location, true, num == iSelectedIndex))) {
                    if(e.Button == MouseButtons.Left) {
                        iTabMouseOnButtonsIndex = -1;
                        QTabCancelEventArgs args = new QTabCancelEventArgs(tabMouseOn, num, false, TabControlAction.Deselected);
                        CloseButtonClicked(this, args);
                        if(args.Cancel) {
                            PInvoke.InvalidateRect(Handle, IntPtr.Zero, true);
                        }
                    }
                } else if ( (fNeedPlusButton && (e.Button != MouseButtons.Right)) && ((PlusButtonClicked != null) && tabMouseOn == null && IsPlusButton(e) ) ) {
                    PlusButtonClicked(this, null);
                }
                else {
                    base.OnMouseUp(e);
                }
            }
        }

        private bool IsPlusButton(MouseEventArgs e)
        {
            if (newRect != null && newRect.Contains( e.Location ))
            {
                return true;
            }
            return false;
        }

        protected override void OnPaintBackground(PaintEventArgs e) {
            if(FluentGlassManager.SuppressManagedBackground) {
                return;
            }

            base.OnPaintBackground(e);
        }

        protected override void OnPaint(PaintEventArgs e) {
            fOncePainted = true;
            if(iMultipleType != 0) {
                OnPaint_MultipleRow(e);
            }
            else {
                fNeedToDrawUpDown = CalculateItemRectangle();
                try {
                    QTabItem tabMouseOn = GetTabMouseOn();
                    bool fVisualStyle = !fForceClassic && VisualStyleRenderer.IsSupported;
                    if(fVisualStyle && (vsr_LPressed == null)) {
                        InitializeRenderer();
                    }
                    int paintCount = tabPages.Count;
                    int selectedIndex = iSelectedIndex;
                    for(int i = 0; i < paintCount; i++) {
                        Rectangle itemRectangle;
                        if(i != selectedIndex && TryGetItemRectangle(i, out itemRectangle)) {
                            DrawTab(e.Graphics, itemRectangle, i, tabMouseOn, fVisualStyle);
                        }
                    }
                    Rectangle selectedRectangle = Rectangle.Empty;
                    bool selectedRectangleAvailable =
                            selectedIndex >= 0 &&
                            selectedIndex < paintCount &&
                            TryGetItemRectangle(selectedIndex, out selectedRectangle);
                    if(selectedRectangleAvailable) {
                        DrawTab(e.Graphics, selectedRectangle, selectedIndex, tabMouseOn, fVisualStyle);
                    }
                    DrawTabSwitchAnimation(e.Graphics);
                    if(!FluentGlassManager.SuppressManagedBackground &&
                            fNeedToDrawUpDown &&
                            selectedRectangleAvailable &&
                            selectedRectangle.X != 0) {
                        e.Graphics.FillRectangle(SystemBrushes.Control, new Rectangle(0, 0, 2, e.ClipRectangle.Height));
                    }

                    Rectangle lastRectangle;
                    if(fNeedPlusButton && paintCount > 0 &&
                            TryGetItemRectangle(paintCount - 1, out lastRectangle)) {
                        DrawPlusButton(e.Graphics, lastRectangle);
                    }

                    ShowUpDown(fNeedToDrawUpDown);
                }
                catch(Exception exception) {
                    QTUtility2.MakeErrorLog(exception);
                }
            }
        }

        private RectangleF newRect;
        /**
         * Draws the new-tab button.
         */
        private void DisposeCustomTabButtonImages() {
            if(customCloseButtonImages != null) {
                foreach(Bitmap image in customCloseButtonImages) {
                    if(image != null) image.Dispose();
                }
                customCloseButtonImages = null;
            }
            if(customLockIcon != null) {
                customLockIcon.Dispose();
                customLockIcon = null;
            }
            customCloseButtonSize = Size.Empty;
            customLockIconSize = Size.Empty;
        }

        private void RefreshTabButtonImages() {
            DisposeCustomTabButtonImages();
            int maximumHeight = Math.Max(1, itemSize.Height - contentMargin.Vertical - 4);
            Size maximumSize = new Size(Math.Min(32, maximumHeight), maximumHeight);

            if(Config.Skin.UseCloseButtonImage && File.Exists(Config.Skin.CloseButtonImageFile)) {
                try {
                    customCloseButtonImages = LoadCloseButtonImages(Config.Skin.CloseButtonImageFile);
                    if(customCloseButtonImages.Length > 0) {
                        customCloseButtonSize = TabButtonImageGeometry.FitWithin(
                                customCloseButtonImages[0].Size, maximumSize);
                    }
                }
                catch(Exception ex) {
                    DisposeCustomTabButtonImages();
                    QTUtility2.MakeErrorLog(ex, "QTabControl.RefreshTabButtonImages.CloseButton");
                }
            }

            if(Config.Skin.UseLockIconImage && File.Exists(Config.Skin.LockIconImageFile)) {
                try {
                    customLockIcon = LoadBitmapWithoutLock(Config.Skin.LockIconImageFile);
                    customLockIconSize = TabButtonImageGeometry.FitWithin(customLockIcon.Size, maximumSize);
                }
                catch(Exception ex) {
                    if(customLockIcon != null) customLockIcon.Dispose();
                    customLockIcon = null;
                    customLockIconSize = Size.Empty;
                    QTUtility2.MakeErrorLog(ex, "QTabControl.RefreshTabButtonImages.LockIcon");
                }
            }
        }

        private static Bitmap LoadBitmapWithoutLock(string path) {
            using(Bitmap source = new Bitmap(path)) {
                Bitmap result = new Bitmap(source);
                if(Path.GetExtension(path).PathEquals(".bmp")) {
                    result.MakeTransparent(Color.Magenta);
                }
                return result;
            }
        }

        private static Bitmap[] LoadCloseButtonImages(string path) {
            using(Bitmap source = LoadBitmapWithoutLock(path)) {
                Rectangle[] frames = TabButtonImageGeometry.GetCloseButtonFrames(source.Size);
                Bitmap[] images = new Bitmap[frames.Length];
                try {
                    for(int i = 0; i < frames.Length; i++) {
                        Rectangle frame = frames[i];
                        Bitmap image = new Bitmap(frame.Width, frame.Height,
                                System.Drawing.Imaging.PixelFormat.Format32bppArgb);
                        using(Graphics graphics = Graphics.FromImage(image)) {
                            graphics.DrawImage(source,
                                    new Rectangle(0, 0, frame.Width, frame.Height),
                                    frame, GraphicsUnit.Pixel);
                        }
                        images[i] = image;
                    }
                    return images;
                }
                catch {
                    foreach(Bitmap image in images) {
                        if(image != null) image.Dispose();
                    }
                    throw;
                }
            }
        }
        private void RefreshPlusButtonImage() {
            if(plusButtonImage != null) {
                plusButtonImage.Dispose();
                plusButtonImage = null;
            }

            string path = Config.Skin.PlusButtonImageFile;
            if(!Config.Skin.UsePlusButtonImage || string.IsNullOrEmpty(path) || !File.Exists(path)) {
                return;
            }

            try {
                using(Bitmap source = new Bitmap(path)) {
                    plusButtonImage = new Bitmap(source);
                }
                if(Path.GetExtension(path).PathEquals(".bmp")) {
                    plusButtonImage.MakeTransparent(Color.Magenta);
                }
            }
            catch(Exception ex) {
                if(plusButtonImage != null) {
                    plusButtonImage.Dispose();
                    plusButtonImage = null;
                }
                QTUtility2.MakeErrorLog(ex, "QTabControl.RefreshPlusButtonImage");
            }
        }

        private void DrawPlusButton(Graphics g, Rectangle drawRect) {
            int buttonSize = Math.Max(8, Math.Min(24, drawRect.Height - 4));
            int x = drawRect.Right + 4;
            int y = drawRect.Y + Math.Max(0, (drawRect.Height - buttonSize) / 2);
            Rectangle buttonRect = new Rectangle(x, y, buttonSize, buttonSize);
            newRect = buttonRect;

            if(plusButtonImage != null && plusButtonImage.Width > 0 && plusButtonImage.Height > 0) {
                float scale = Math.Min(1f, Math.Min(
                        (float)buttonSize / plusButtonImage.Width,
                        (float)buttonSize / plusButtonImage.Height));
                int width = Math.Max(1, (int)Math.Round(plusButtonImage.Width * scale));
                int height = Math.Max(1, (int)Math.Round(plusButtonImage.Height * scale));
                Rectangle imageRect = new Rectangle(
                        buttonRect.X + (buttonRect.Width - width) / 2,
                        buttonRect.Y + (buttonRect.Height - height) / 2,
                        width,
                        height);
                System.Drawing.Drawing2D.InterpolationMode oldInterpolation = g.InterpolationMode;
                System.Drawing.Drawing2D.PixelOffsetMode oldPixelOffset = g.PixelOffsetMode;
                try {
                    g.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.HighQualityBicubic;
                    g.PixelOffsetMode = System.Drawing.Drawing2D.PixelOffsetMode.Half;
                    g.DrawImage(plusButtonImage, imageRect);
                }
                finally {
                    g.InterpolationMode = oldInterpolation;
                    g.PixelOffsetMode = oldPixelOffset;
                }
                return;
            }

            Color color = QTUtility.InNightMode ? Color.White : Color.FromArgb(220, Color.Black);
            float centerX = buttonRect.Left + buttonRect.Width / 2f;
            float centerY = buttonRect.Top + buttonRect.Height / 2f;
            float arm = Math.Max(3f, buttonRect.Width / 5f);
            System.Drawing.Drawing2D.SmoothingMode oldSmoothing = g.SmoothingMode;
            try {
                g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
                using(Pen pen = new Pen(color, 2f)) {
                    pen.StartCap = System.Drawing.Drawing2D.LineCap.Round;
                    pen.EndCap = System.Drawing.Drawing2D.LineCap.Round;
                    g.DrawLine(pen, centerX - arm, centerY, centerX + arm, centerY);
                    g.DrawLine(pen, centerX, centerY - arm, centerX, centerY + arm);
                }
            }
            finally {
                g.SmoothingMode = oldSmoothing;
            }
        }
        private void OnPaint_MultipleRow(PaintEventArgs e) {
            CalculateItemRectangle_MultiRows();
            try {
                QTabItem tabMouseOn = GetTabMouseOn();
                bool fVisualStyle = !fForceClassic && VisualStyleRenderer.IsSupported;
                if(fVisualStyle && (vsr_LPressed == null)) {
                    InitializeRenderer();
                }
                bool flag2 = false;
                for(int i = 0; i < (iCurrentRow + 1); i++) {
                    for(int j = 0; j < tabPages.Count; j++) {
                        QTabItem base3 = tabPages[j];
                        if(base3.Row == i) {
                            if(j != iSelectedIndex) {
                                DrawTab(e.Graphics, base3.TabBounds, j, tabMouseOn, fVisualStyle);
                            }
                            else {
                                flag2 = true;
                            }
                        }
                    }
                    if(flag2) {
                        DrawTab(e.Graphics, tabPages[iSelectedIndex].TabBounds, iSelectedIndex, tabMouseOn, fVisualStyle);
                        flag2 = false;
                    }

                    if (fNeedPlusButton)
                    {
                        if (tabPages.Count > 0)
                        {
                            Rectangle plusButtonRect = tabPages[tabPages.Count - 1].TabBounds;
                            DrawPlusButton(e.Graphics,plusButtonRect);
                        }
                    }
                }
                DrawTabSwitchAnimation(e.Graphics);
                ShowUpDown(false);
            }
            catch(Exception exception) {
                QTUtility2.MakeErrorLog(exception);
            }
        }

        private void OnTabPageAdded(QTabItem tabPage, int index) {
            if(index == 0) {
                selectedTabPage = tabPage;
            }
            if(TabCountChanged != null) {
                TabCountChanged(this, new QTabCancelEventArgs(tabPage, index, false, TabControlAction.Selected));
            }
        }

        private void OnTabPageInserted(QTabItem tabPage, int index) {
            if(index <= iSelectedIndex) {
                iSelectedIndex++;
            }
            if(TabCountChanged != null) {
                TabCountChanged(this, new QTabCancelEventArgs(tabPage, index, false, TabControlAction.Selected));
            }
        }

        private void OnTabPageRemoved(QTabItem tabPage, int index) {
            if(!Disposing && (index != -1)) {
                if(index == iSelectedIndex) {
                    iSelectedIndex = -1;
                }
                else if(index < iSelectedIndex) {
                    iSelectedIndex--;
                }
                if(TabCountChanged != null) {
                    TabCountChanged(this, new QTabCancelEventArgs(tabPage, index, false, TabControlAction.Deselected));
                }
            }
        }

        private void OnUpDownClicked(bool dir, bool lockPaint) {
            int num = Width - 0x24;
            if((!dir || ((tabPages[tabPages.Count - 1].TabBounds.Right + iScrollWidth) >= num)) && (dir || ((tabPages[0].TabBounds.Left + iScrollWidth) != 0))) {
                iScrollClickedCount += dir ? 1 : -1;
                if(iScrollClickedCount > (tabPages.Count - 1)) {
                    iScrollClickedCount = tabPages.Count - 1;
                }
                else if(iScrollClickedCount < 0) {
                    iScrollClickedCount = 0;
                }
                else {
                    iScrollWidth = -tabPages[iScrollClickedCount].TabBounds.X;
                    if(!lockPaint) {
                        Invalidate();
                    }
                }
            }
        }

        public bool PerformFocusedFolderIconClick(bool fParent) {
            if(((TabIconMouseDown == null) || !Focused) || ((-1 >= iFocusedTabIndex) || (iFocusedTabIndex >= tabPages.Count))) {
                return false;
            }
            iTabIndexOfSubDirShown = iFocusedTabIndex;
            QTabItem tabPage = tabPages[iFocusedTabIndex];
            int tabPageIndex = 0;
            if((iMultipleType == 0) && fNeedToDrawUpDown) {
                tabPageIndex = iScrollWidth;
            }
            TabIconMouseDown(this, new QTabCancelEventArgs(tabPage, tabPageIndex, fParent, TabControlAction.Selecting));
            PInvoke.InvalidateRect(Handle, IntPtr.Zero, true);
            return true;
        }

        public override void Refresh() {
            if(!fRedrawSuspended) {
                base.Refresh();
            }
        }

        public void RefreshFolderImage() {
            iTabMouseOnButtonsIndex = -1;
            fNowMouseIsOnIcon = false;
            PInvoke.InvalidateRect(Handle, IntPtr.Zero, true);
        }

        public void RefreshOptions(bool fInit) {
            if(fInit) {
                if(Config.Tabs.MultipleTabRows) {
                    iMultipleType = Config.Tabs.ActiveTabOnBottomRow ? 1 : 2;
                }
                fDrawFolderImg = Config.Tabs.ShowFolderIcon;
            }
            else {
                colorSet = new Color[] {
                    Config.Skin.TabTextActiveColor,
                    Config.Skin.TabTextInactiveColor,
                    Config.Skin.TabTextHotColor,
                    Config.Skin.TabShadActiveColor,
                    Config.Skin.TabShadInactiveColor,
                    Config.Skin.TabShadHotColor
                };
                brshActive.Color = colorSet[0];
                brshInactv.Color = colorSet[1];
            }
            if(Config.Skin.FixedWidthTabs) {
                sizeMode = TabSizeMode.Fixed;
                fLimitSize = false;
            }
            else {
                sizeMode = TabSizeMode.Normal;
                fLimitSize = true; // Config.LimitedWidthTabs;
            }
            if((Config.Skin.TabMaxWidth >= Config.Skin.TabMinWidth) && (Config.Skin.TabMinWidth > 9)) {
                maxAllowedTabWidth = Config.Skin.TabMaxWidth;
                minAllowedTabWidth = Config.Skin.TabMinWidth;
            }
            itemSize = new Size(maxAllowedTabWidth, Config.Skin.TabHeight);
            fNeedPlusButton = Config.Tabs.NeedPlusButton;
            RefreshPlusButtonImage();
            fActiveTxtBold = Config.Skin.ActiveTabInBold;
            fForceClassic = Config.Skin.UseTabSkin;
            SetFont(Config.Skin.TabTextFont);
            sizingMargin = Config.Skin.TabSizeMargin + new Padding(0, 0, 1, 1);
            Padding previousContentMargin = contentMargin;
            if(Config.Skin.UseTabSkin && Config.Skin.TabImageFile.Length > 0) {
                SetTabImages(QTTabBarClass.CreateTabImage());
            }
            else {
                SetTabImages(null);
            }
            bool hasCustomSkin = tabImages != null;
            contentMargin = hasCustomSkin ? Config.Skin.TabContentMargin : Padding.Empty;
            overlapPixels = hasCustomSkin ? Config.Skin.OverlapPixels : 0;
            hitTestTransparent = hasCustomSkin && Config.Skin.HitTestTransparent;
            RefreshTabButtonImages();
            if(!contentMargin.Equals(previousContentMargin)) {
                foreach(QTabItem tab in tabPages) {
                    tab.RefreshRectangle();
                }
            }
            // �жϱ�ǩ�ı��Ƿ���� ���� ����
            tabTextAlignment = Config.Skin.TabTextCentered ? StringAlignment.Center : StringAlignment.Near;
            fDrawShadow = Config.Skin.TabTitleShadows;
            fDrawActiveShadow = Config.Skin.TabActiveTitleShadow;
            fDrawInactiveShadow = Config.Skin.TabInactiveTitleShadow;
            fDrawHotShadow = Config.Skin.TabHotTitleShadow;
            fDrawCloseButton = Config.Tabs.ShowCloseButtons && !Config.Tabs.CloseBtnsWithAlt;
            fCloseBtnOnHover = Config.Tabs.CloseBtnsOnHover;
            fShowSubDirTip = Config.Tabs.ShowSubDirTipOnTab;
            if(!fInit) {
                if(fDrawFolderImg != Config.Tabs.ShowFolderIcon) {
                    fDrawFolderImg = Config.Tabs.ShowFolderIcon;
                    if(fDrawFolderImg) {
                        foreach(QTabItem base2 in TabPages) {
                            base2.ImageKey = base2.ImageKey;
                        }
                    }
                    else {
                        fNowMouseIsOnIcon = false;
                    }
                }
                if(fAutoSubText && !Config.Tabs.RenameAmbTabs) {
                    foreach(QTabItem item in TabPages) {
                        item.Comment = string.Empty;
                        item.RefreshRectangle();
                    }
                    Refresh();
                }
                else if(!fAutoSubText && Config.Tabs.RenameAmbTabs) {
                    QTabItem.CheckSubTexts(this);
                }   
            }
            fAutoSubText = Config.Tabs.RenameAmbTabs;
        }

        public bool SelectFocusedTab() {
            if((Focused && (-1 < iFocusedTabIndex)) && (iFocusedTabIndex < tabPages.Count)) {
                SelectedIndex = iFocusedTabIndex;
                return true;
            }
            return false;
        }

        public bool SelectTab(QTabItem tabPage) {
            int index = tabPages.IndexOf(tabPage);
            if(index == -1) {
                throw new ArgumentException("arg was not found.");
            }
            return (((index != -1) && (selectedTabPage != tabPage)) && ChangeSelection(tabPage, index));
        }

        public void SelectTab(int index) {
            if((index <= -1) || (index >= tabPages.Count)) {
                throw new ArgumentOutOfRangeException("index," + index, "index is out of range.");
            }
            QTabItem tabToSelect = tabPages[index];
            if(selectedTabPage != tabToSelect) {
                ChangeSelection(tabToSelect, index);
            }
            else {
                iSelectedIndex = index;
            }
        }

        public void SelectTabDirectly(QTabItem tabPage) {
            int index = tabPages.IndexOf(tabPage);
            selectedTabPage = tabPage;
            SelectedIndex = index;
        }

        public void SetContextMenuState(bool fShow) {
            fNowTabContextMenuStripShowing = fShow;
        }

        private void SetFont(Font fnt) {
            Font = fnt;
            if(fntBold != null) {
                fntBold.Dispose();
            }
            fntBold = Font;
            try
            {
                fntBold = new Font(Font, FontStyle.Bold);
            }
            catch (Exception e)
            {
                QTUtility2.MakeErrorLog(e, "SetFont fntBold");

            }
            if(fnt_Underline != null) {
                fnt_Underline.Dispose();
            }
            fnt_Underline = Font;
            try
            {
                fnt_Underline = new Font(Font, FontStyle.Underline);
            }
            catch (Exception e)
            {
                QTUtility2.MakeErrorLog(e, "SetFont fnt_Underline");

            }
            if(fntBold_Underline != null) {
                fntBold_Underline.Dispose();
            }
            fntBold_Underline = fntBold;
            try
            {
                fntBold_Underline = new Font(fntBold, FontStyle.Underline);
            }
            catch  (Exception e)
            {
                QTUtility2.MakeErrorLog(e, "SetFont fntBold_Underline");

            }
            if(fntSubText != null) {
                fntSubText.Dispose();
            }
            float sizeInPoints = Font.SizeInPoints;
            fntSubText = Font;
            try
            {
                fntSubText = new Font(Font.FontFamily, (sizeInPoints > 8.25f) ? (sizeInPoints - 0.75f) : sizeInPoints);
            }
            catch (Exception e)
            {
                QTUtility2.MakeErrorLog(e, "SetFont sizeInPoints");

            }
            if(fntDriveLetter != null) {
                fntDriveLetter.Dispose();
            }
            fntDriveLetter = Font;
            try
            {
                fntDriveLetter = new Font(Font.FontFamily, 8.25f);
            }
            catch (Exception e)
            {
                QTUtility2.MakeErrorLog(e, "SetFont 8.25f");

            }
            QTabItem.TabFont = Font;
        }

        public void SetPseudoHotIndex(int index) {
            int iPseudoHotIndex = this.iPseudoHotIndex;
            this.iPseudoHotIndex = index;
            if((iPseudoHotIndex > -1) && (iPseudoHotIndex < TabCount)) {
                Invalidate(GetTabRect(iPseudoHotIndex, true));
            }
            if((this.iPseudoHotIndex > -1) && (this.iPseudoHotIndex < TabCount)) {
                Invalidate(GetTabRect(this.iPseudoHotIndex, true));
            }
            Update();
        }

        public void SetRedraw(bool bRedraw) {
            if(bRedraw && fRedrawSuspended) {
                base.Refresh();
            }
            fRedrawSuspended = !bRedraw;
        }

        public void SetSubDirTipShown(bool fShown) {
            if(!fShown) {
                iTabIndexOfSubDirShown = -1;
            }
            fSubDirShown = fShown;
        }

        private void SetTabImages(Bitmap[] bmps) {
            if((bmps != null) && (bmps.Length == 3)) {
                if(tabImages == null) {
                    tabImages = bmps;
                }
                else if((tabImages[0] != null) && (tabImages[1] != null)) {
                    Bitmap bitmap = tabImages[0];
                    Bitmap bitmap2 = tabImages[1];
                    Bitmap bitmap3 = tabImages[2];
                    tabImages[0] = bmps[0];
                    tabImages[1] = bmps[1];
                    tabImages[2] = bmps[2];
                    bitmap.Dispose();
                    bitmap2.Dispose();
                    bitmap3.Dispose();
                }
                else {
                    tabImages = bmps;
                }
            }
            else if(((tabImages != null) && (tabImages[0] != null)) && ((tabImages[1] != null) && (tabImages[2] != null))) {
                Bitmap bitmap4 = tabImages[0];
                Bitmap bitmap5 = tabImages[1];
                Bitmap bitmap6 = tabImages[2];
                tabImages = null;
                bitmap4.Dispose();
                bitmap5.Dispose();
                bitmap6.Dispose();
            }
        }

        public int SetTabRowType(int iType) {
            iMultipleType = iType;
            if(iType != 0) {
                fNeedToDrawUpDown = false;
                return (iCurrentRow + 1);
            }
            return 1;
        }

        public void ShowCloseButton(bool fShow) {
            fDrawCloseButton = fNowShowCloseBtnAlt = fShow;
            Invalidate();
        }

        private void ShowUpDown(bool fShow) {
            if(fShow) {
                if(upDown == null) {
                    upDown = new UpDown();
                    upDown.Anchor = AnchorStyles.Right;
                    upDown.ValueChanged += upDown_ValueChanged;
                    Controls.Add(upDown);
                }
                upDown.Location = new Point(Width - 0x24, 0);
                upDown.Visible = true;
                upDown.BringToFront();
            }
            else if((upDown != null) && upDown.Visible) {
                upDown.Visible = false;
            }
        }

        private void timerSuppressDoubleClick_Tick(object sender, EventArgs e) {
            timerSuppressDoubleClick.Enabled = false;
            fSuppressDoubleClick = false;
        }

        private void upDown_ValueChanged(object sender, QEventArgs e) {
            OnUpDownClicked(e.Direction == ArrowDirection.Right, false);
        }

        protected override void WndProc(ref Message m) {
            QTabItem tabMouseOn;
            int num;
            int msg = m.Msg;
            switch(msg) {
                case WM.SETCURSOR:
                    if(fSubDirShown || fNowTabContextMenuStripShowing) {
                        uint num4 = ((uint)((long)m.LParam)) & 0xffff;
                        uint num5 = (((uint)((long)m.LParam)) >> 0x10) & 0xffff;
                        if((num4 == 1) && (num5 == 0x200)) {
                            tabMouseOn = GetTabMouseOn(out num);
                            InvalidateTabsOnMouseMove(tabMouseOn, num, PointToClient(MousePosition));
                            m.Result = (IntPtr)1;
                            return;
                        }
                    }
                    break;

                case WM.MOUSEACTIVATE: {
                        if(!fSubDirShown || (TabIconMouseDown == null)) {
                            break;
                        }
                        int num2 = (((int)((long)m.LParam)) >> 0x10) & 0xffff;
                        if(num2 == 0x207) {
                            break;
                        }
                        bool cancel = num2 == 0x204;
                        m.Result = (IntPtr)4;
                        tabMouseOn = GetTabMouseOn(out num);
                        if(((tabMouseOn == null) || (num == iTabIndexOfSubDirShown)) || !HitTestOnButtons(tabMouseOn.TabBounds, PointToClient(MousePosition), false, num == iSelectedIndex)) {
                            TabIconMouseDown(this, new QTabCancelEventArgs(null, -1, false, TabControlAction.Deselected));
                            return;
                        }
                        int tabPageIndex = 0;
                        if((iMultipleType == 0) && fNeedToDrawUpDown) {
                            tabPageIndex = iScrollWidth;
                        }
                        TabIconMouseDown(this, new QTabCancelEventArgs(tabMouseOn, tabPageIndex, cancel, TabControlAction.Selecting));
                        if(fSubDirShown) {
                            iTabIndexOfSubDirShown = num;
                        }
                        else {
                            iTabIndexOfSubDirShown = -1;
                        }
                        fNowMouseIsOnIcon = true;
                        iTabMouseOnButtonsIndex = num;
                        PInvoke.InvalidateRect(Handle, IntPtr.Zero, true);
                        return;
                    }

                case WM.ERASEBKGND:
                    if(FluentGlassManager.SuppressManagedBackground) {
                        m.Result = (IntPtr)1;
                        return;
                    }
                    if(!fRedrawSuspended) {
                        break;
                    }
                    m.Result = (IntPtr)1;
                    return;

                default:
                    if(msg != WM.CONTEXTMENU) {
                        break;
                    }
                    if((QTUtility2.GET_X_LPARAM(m.LParam) != -1) || (QTUtility2.GET_Y_LPARAM(m.LParam) != -1)) {
                        tabMouseOn = GetTabMouseOn(out num);
                        if(tabMouseOn == null) {
                            PInvoke.SendMessage(Parent.Handle, 0x7b, m.WParam, m.LParam);
                            return;
                        }
                        if(!fShowSubDirTip || !HitTestOnButtons(tabMouseOn.TabBounds, PointToClient(MousePosition), false, num == iSelectedIndex)) {
                            break;
                        }
                    }
                    return;
            }
            base.WndProc(ref m);
        }

        public bool AutoSubText {
            get {
                return fAutoSubText;
            }
        }

        protected override bool CanEnableIme {
            get {
                return false;
            }
        }

        public Padding TabContentMargin {
            get {
                return contentMargin;
            }
        }

        public int CloseButtonReservedWidth {
            get { return customCloseButtonImages == null ? 17 : Math.Max(17, customCloseButtonSize.Width + 4); }
        }

        public int LockIconReservedWidth {
            get { return customLockIcon == null ? 13 : customLockIconSize.Width + 4; }
        }

        public bool DrawFolderImage {
            get {
                return fDrawFolderImg;
            }
        }

        public bool EnableCloseButton {
            get {
                return fDrawCloseButton;
            }
            set {
                fDrawCloseButton = value;
            }
        }

        public bool OncePainted {
            get {
                return fOncePainted;
            }
        }

        public int SelectedIndex {
            get {
                return iSelectedIndex;
            }
            set {
                SelectTab(value);
            }
        }

        public QTabItem SelectedTab {
            get {
                return tabPages[iSelectedIndex];
            }
        }

        public bool TabCloseButtonOnAlt {
            get {
                return fNowShowCloseBtnAlt;
            }
        }

        public bool TabCloseButtonOnHover {
            get {
                return fCloseBtnOnHover;
            }
        }

        public int TabCount {
            get {
                return tabPages.Count;
            }
        }

        public int TabOffset {
            get {
                if((iMultipleType == 0) && fNeedToDrawUpDown) {
                    return iScrollWidth;
                }
                return 0;
            }
        }

        public QTabCollection TabPages {
            get {
                return tabPages;
            }
        }

        public sealed class QTabCollection : List<QTabItem> {
            private QTabControl Owner;

            public QTabCollection(QTabControl owner) {
                Owner = owner;
            }

            new public void Add(QTabItem tabPage) {
                base.Add(tabPage);
                Owner.OnTabPageAdded(tabPage, Count - 1);
                Owner.Refresh();
            }

            new public void Insert(int index, QTabItem tabPage) {
                base.Insert(index, tabPage);
                Owner.OnTabPageInserted(tabPage, index);
                Owner.Refresh();
            }

            new public bool Remove(QTabItem tabPage) {
                int index = IndexOf(tabPage);
                Owner.OnTabPageRemoved(tabPage, index);
                bool flag = base.Remove(tabPage);
                Owner.Refresh();
                return flag;
            }

            public void Relocate(int indexSource, int indexDestination) {
                int selectedIndex = Owner.SelectedIndex;
                int num2 = (indexSource > indexDestination) ? indexSource : indexDestination;
                int num3 = (indexSource > indexDestination) ? indexDestination : indexSource;
                QTabItem item = base[indexSource];
                base.Remove(item);
                base.Insert(indexDestination, item);
                if((num2 >= selectedIndex) && (selectedIndex >= num3)) {
                    if(num2 == selectedIndex) {
                        if(num2 == indexSource) {
                            Owner.SelectedIndex = indexDestination;
                        }
                        else {
                            Owner.SelectedIndex--;
                        }
                    }
                    else if((num3 < selectedIndex) && (selectedIndex < num2)) {
                        if(num2 == indexSource) {
                            Owner.SelectedIndex++;
                        }
                        else {
                            Owner.SelectedIndex--;
                        }
                    }
                    else if(num3 == selectedIndex) {
                        if(num2 == indexSource) {
                            Owner.SelectedIndex++;
                        }
                        else {
                            Owner.SelectedIndex = indexDestination;
                        }
                    }
                }
                Owner.Refresh();
            }
        }
    }

  
}
