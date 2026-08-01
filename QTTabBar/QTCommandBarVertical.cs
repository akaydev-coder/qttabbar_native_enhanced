using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Windows.Forms;
using BandObjectLib;
using Microsoft.Win32;
using QTTabBarLib.Interop;
using SHDocVw;

namespace QTTabBarLib {
    [ComVisible(true)]
    [Guid("D2BF470E-ED1C-487F-A888-2BD8835EB6CE")]
    public sealed class QCommandBarVertical : BandObject {
        private const string CurrentItemsKey = @"Software\QTTabBar\Commands\Vertical";
        private const string LegacyItemsKey = @"Software\Quizo\QTTabBar\Commands\Vertical";
        private const string ExplorerBarsKey = @"Software\Microsoft\Internet Explorer\Explorer Bars\";
        private const int DefaultIconSize = 24;
        private const int BarPropping = 13;
        private const string InternalItemDragFormat = "QTTabBar.NativeEnhanced.VersatileItem";
        private static readonly byte[] DefaultBarSize = { 0x44, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00 };

        private IContainer components;
        private ToolStripClasses toolStrip;
        private ContextMenuStrip barContextMenu;
        private ToolStripMenuItem removeMenuItem;
        private ToolStripMenuItem iconSizesMenuItem;
        private ToolStripMenuItem autoShowMenuItem;
        private Timer initializationTimer;
        private Timer chromeTimer;
        private int initializationAttempts;
        private int chromeAttempts;
        private int iconSize = DefaultIconSize;
        private IntPtr explorerHandle;
        private List<VersatileItem> items = new List<VersatileItem>();
        private VersatileItem contextItem;
        private bool followsApplicationList = true;
        private bool subscribedToApplicationChanges;
        private bool fixingBarWidth;
        private bool widthFixPending;
        private Point dragStartPoint;
        private VersatileItem dragCandidate;
        private int dragInsertionIndex = -1;

        private enum VersatileItemKind {
            Separator,
            File,
            Group,
            BuiltIn,
            Folder
        }

        private sealed class VersatileItem {
            public VersatileItemKind Kind;
            public string DisplayText;
            public string TooltipText;
            public string Path;
            public string Arguments;
            public string WorkingDirectory;
            public string IconResource;
            public byte[] Idl;
            public int CommandId;
            public string[] GroupNames;
            public UserApp UserApp;
            public List<VersatileItem> Children = new List<VersatileItem>();
        }

        private sealed class VersatileBarRenderer : ToolStripSystemRenderer {
            private readonly Func<bool> suppressBackground;

            internal VersatileBarRenderer(Func<bool> suppressBackground) {
                this.suppressBackground = suppressBackground;
            }

            protected override void OnRenderToolStripBorder(ToolStripRenderEventArgs e) {
            }

            protected override void OnRenderToolStripBackground(ToolStripRenderEventArgs e) {
                if(!suppressBackground()) base.OnRenderToolStripBackground(e);
            }

            protected override void OnRenderSeparator(ToolStripSeparatorRenderEventArgs e) {
                if(e.Item.Owner != null &&
                        e.Item.Owner.LayoutStyle == ToolStripLayoutStyle.VerticalStackWithOverflow) {
                    Rectangle bounds = e.Item.ContentRectangle;
                    int y = bounds.Top + (bounds.Height / 2);
                    int inset = Math.Max(2, bounds.Width / 6);
                    using(Pen darkPen = new Pen(SystemColors.ControlDark)) {
                        e.Graphics.DrawLine(darkPen, bounds.Left + inset, y,
                                bounds.Right - inset - 1, y);
                    }
                    if(!suppressBackground() && y + 1 < bounds.Bottom) {
                        using(Pen lightPen = new Pen(SystemColors.ControlLightLight)) {
                            e.Graphics.DrawLine(lightPen, bounds.Left + inset, y + 1,
                                    bounds.Right - inset - 1, y + 1);
                        }
                    }
                    return;
                }
                base.OnRenderSeparator(e);
            }
        }

        [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
        private static extern uint ExtractIconEx(
                string szFileName,
                int nIconIndex,
                IntPtr[] phiconLarge,
                IntPtr[] phiconSmall,
                uint nIcons);

        public QCommandBarVertical() {
            InitializeComponent();
        }

        private int BarThickness {
            get { return iconSize + BarPropping; }
        }

        private QTTabBarClass TabBar {
            get { return InstanceManager.GetThreadTabBar(); }
        }

        private ShellBrowserEx ShellBrowser {
            get {
                QTTabBarClass tabBar = TabBar;
                return tabBar == null ? null : tabBar.GetShellBrowser();
            }
        }

        public override int SetSite(object pUnkSite) {
            int result = base.SetSite(pUnkSite);
            if(pUnkSite != null) StartChromeTimer();
            return result;
        }

        public override void CloseDW(uint dwReserved) {
            StopInitializationTimer();
            StopChromeTimer();
            UnsubscribeFromApplicationChanges();
            FluentGlassManager.UnregisterSideBar(
                    explorerHandle,
                    IsHandleCreated ? Handle : IntPtr.Zero);
            base.CloseDW(dwReserved);
        }

        public override void GetBandInfo(uint dwBandID, uint dwViewMode, ref DESKBANDINFO dbi) {
            base.GetBandInfo(dwBandID, dwViewMode, ref dbi);
            int height = Math.Max(1, Height);
            Size bandSize = new Size(BarThickness, height);
            MinSize = new Size(BarThickness, 0);
            MaxSize = new Size(BarThickness, -1);
            if((dbi.dwMask & DBIM.ACTUAL) != 0) {
                dbi.ptActual.X = bandSize.Width;
                dbi.ptActual.Y = bandSize.Height;
            }
            if((dbi.dwMask & DBIM.MINSIZE) != 0) {
                dbi.ptMinSize.X = BarThickness;
                dbi.ptMinSize.Y = 0;
            }
            if((dbi.dwMask & DBIM.MAXSIZE) != 0) {
                dbi.ptMaxSize.X = BarThickness;
                dbi.ptMaxSize.Y = -1;
            }
            if((dbi.dwMask & DBIM.INTEGRAL) != 0) {
                dbi.ptIntegral.X = 1;
                dbi.ptIntegral.Y = 1;
            }
            if((dbi.dwMask & DBIM.MODEFLAGS) != 0) {
                // DBIMF_FIXED is 0x0001. This legacy enum names that value MINSIZE.
                dbi.dwModeFlags = DBIMF.MINSIZE | DBIMF.NOGRIPPER | DBIMF.NOMARGINS;
            }
            if((dbi.dwMask & DBIM.BKCOLOR) != 0) {
                dbi.dwMask &= ~DBIM.BKCOLOR;
            }
            if((dbi.dwMask & DBIM.TITLE) != 0) {
                dbi.wszTitle = null;
            }
        }

        protected override void OnExplorerAttached() {
            try {
                explorerHandle = Explorer == null ? IntPtr.Zero : (IntPtr)Explorer.HWND;
                SetAutoShow(true);
                SubscribeToApplicationChanges();
                RefreshItems();
                StartInitializationTimer();
                StartChromeTimer();
            }
            catch(Exception ex) {
                QTUtility2.MakeErrorLog(ex, "QTCommandBarVertical OnExplorerAttached");
            }
        }

        protected override void OnPaintBackground(PaintEventArgs e) {
            if(FluentGlassManager.SuppressManagedBackgroundFor(explorerHandle)) return;
            Color color = QTUtility.InNightMode
                    ? ShellColors.Default
                    : ShellColors.ExplorerBarVertBGColor;
            using(SolidBrush brush = new SolidBrush(color)) {
                e.Graphics.FillRectangle(brush, e.ClipRectangle);
            }
        }

        protected override void WndProc(ref Message m) {
            if(m.Msg == WM.WINDOWPOSCHANGING && m.LParam != IntPtr.Zero) {
                WINDOWPOS pos = (WINDOWPOS)Marshal.PtrToStructure(m.LParam, typeof(WINDOWPOS));
                if((pos.flags & SWP.NOMOVE) == 0) {
                    pos.y = 0;
                }
                if((pos.flags & SWP.NOSIZE) == 0) pos.cx = BarThickness;
                Marshal.StructureToPtr(pos, m.LParam, false);
            }
            base.WndProc(ref m);
        }

        protected override void Dispose(bool disposing) {
            StopInitializationTimer();
            StopChromeTimer();
            UnsubscribeFromApplicationChanges();
            FluentGlassManager.UnregisterSideBar(
                    explorerHandle,
                    IsHandleCreated ? Handle : IntPtr.Zero);
            if(disposing && components != null) {
                components.Dispose();
                components = null;
            }
            base.Dispose(disposing);
        }

        protected override void OnHandleCreated(EventArgs e) {
            base.OnHandleCreated(e);
            StartChromeTimer();
        }

        protected override void OnVisibleChanged(EventArgs e) {
            base.OnVisibleChanged(e);
            if(Visible) {
                StartChromeTimer();
                ScheduleBarWidthFix();
            }
        }

        protected override void OnResize(EventArgs e) {
            base.OnResize(e);
            if(!fixingBarWidth && Width != BarThickness) {
                ScheduleBarWidthFix();
            }
        }

        private void InitializeComponent() {
            components = new Container();
            toolStrip = new ToolStripClasses {
                    AutoSize = false,
                    AllowDrop = true,
                    BackColor = Color.Transparent,
                    CanOverflow = false,
                    Dock = DockStyle.Fill,
                    GripStyle = ToolStripGripStyle.Hidden,
                    ImeMode = ImeMode.Disable,
                    LayoutStyle = ToolStripLayoutStyle.VerticalStackWithOverflow,
                    Padding = new Padding(2, 0, 2, 0),
                    Renderer = new VersatileBarRenderer(
                            () => FluentGlassManager.SuppressManagedBackgroundFor(explorerHandle)),
                    ShowItemToolTips = true
            };
            SuspendLayout();
            AllowDrop = true;
            BackColor = Color.Transparent;
            Margin = Padding.Empty;
            Padding = Padding.Empty;
            Width = BarThickness;
            MinSize = new Size(16, BarThickness);
            MaxSize = new Size(-1, BarThickness);
            toolStrip.DragEnter += toolStrip_DragEnter;
            toolStrip.DragOver += toolStrip_DragOver;
            toolStrip.DragLeave += toolStrip_DragLeave;
            toolStrip.DragDrop += toolStrip_DragDrop;
            toolStrip.MouseDown += toolStrip_MouseDown;
            toolStrip.MouseMove += toolStrip_MouseMove;
            toolStrip.MouseUp += toolStrip_MouseUp;
            toolStrip.Paint += toolStrip_Paint;
            toolStrip.MouseEnter += delegate {
                if(items.Count == 0) RefreshItems();
            };
            InitializeContextMenu();
            toolStrip.ContextMenuStrip = barContextMenu;
            Controls.Add(toolStrip);
            ResumeLayout(false);
        }

        private void InitializeContextMenu() {
            barContextMenu = new ContextMenuStrip(components);
            removeMenuItem = new ToolStripMenuItem("Entfernen");
            removeMenuItem.Click += removeMenuItem_Click;

            ToolStripMenuItem separator = new ToolStripMenuItem("Trennlinie hinzufuegen");
            separator.Click += addSeparatorMenuItem_Click;

            iconSizesMenuItem = new ToolStripMenuItem("Symbolgroesse");
            foreach(int size in new[] {16, 24, 32}) {
                ToolStripMenuItem sizeItem = new ToolStripMenuItem(size + " px") {Tag = size};
                sizeItem.Click += iconSize_Click;
                iconSizesMenuItem.DropDownItems.Add(sizeItem);
            }

            ToolStripMenuItem reload = new ToolStripMenuItem("Neu laden");
            reload.Click += delegate { RefreshItems(); };

            ToolStripMenuItem reset = new ToolStripMenuItem("Anwendungsliste verwenden");
            reset.Click += resetMenuItem_Click;

            autoShowMenuItem = new ToolStripMenuItem("In neuen Explorer-Fenstern anzeigen") {
                    CheckOnClick = true
            };
            autoShowMenuItem.Click += autoShowMenuItem_Click;

            ToolStripMenuItem options = new ToolStripMenuItem("QTTabBar-Optionen");
            options.Click += optionsMenuItem_Click;

            barContextMenu.Items.AddRange(new ToolStripItem[] {
                    removeMenuItem,
                    separator,
                    iconSizesMenuItem,
                    reload,
                    reset,
                    autoShowMenuItem,
                    new ToolStripSeparator(),
                    options
            });
            barContextMenu.Opening += barContextMenu_Opening;
        }

        private void SubscribeToApplicationChanges() {
            if(subscribedToApplicationChanges) return;
            AppsManager.UserAppsChanged += AppsManager_UserAppsChanged;
            subscribedToApplicationChanges = true;
        }

        private void UnsubscribeFromApplicationChanges() {
            if(!subscribedToApplicationChanges) return;
            AppsManager.UserAppsChanged -= AppsManager_UserAppsChanged;
            subscribedToApplicationChanges = false;
        }

        private void AppsManager_UserAppsChanged(object sender, EventArgs e) {
            if(!followsApplicationList || IsDisposed) return;
            try {
                if(InvokeRequired) {
                    BeginInvoke(new MethodInvoker(RefreshItems));
                }
                else {
                    RefreshItems();
                }
            }
            catch(InvalidOperationException) {
            }
            catch(Exception ex) {
                QTUtility2.MakeErrorLog(ex, "QTCommandBarVertical Applications Changed");
            }
        }

        private void StartInitializationTimer() {
            if(initializationTimer != null || TabBar != null) return;
            initializationAttempts = 0;
            initializationTimer = new Timer(components) {Interval = 250};
            initializationTimer.Tick += initializationTimer_Tick;
            initializationTimer.Start();
        }

        private void StopInitializationTimer() {
            if(initializationTimer == null) return;
            initializationTimer.Stop();
            initializationTimer.Tick -= initializationTimer_Tick;
            initializationTimer.Dispose();
            initializationTimer = null;
        }

        private void StartChromeTimer() {
            if(IsDisposed || chromeTimer != null) return;
            chromeAttempts = 0;
            chromeTimer = new Timer(components) {Interval = 250};
            chromeTimer.Tick += chromeTimer_Tick;
            chromeTimer.Start();
            ApplyExplorerBarChrome();
        }

        private void StopChromeTimer() {
            if(chromeTimer == null) return;
            chromeTimer.Stop();
            chromeTimer.Tick -= chromeTimer_Tick;
            chromeTimer.Dispose();
            chromeTimer = null;
        }

        private void chromeTimer_Tick(object sender, EventArgs e) {
            if(IsDisposed || ++chromeAttempts >= 40) {
                StopChromeTimer();
                return;
            }
            ApplyExplorerBarChrome();
        }

        private void ApplyExplorerBarChrome() {
            if(!IsHandleCreated || ReBarHandle == IntPtr.Zero || !PInvoke.IsWindow(ReBarHandle)) return;
            try {
                HideExplorerBarHeader();

                const int RBS_BANDBORDERS = 0x0400;
                int style = unchecked((int)PInvoke.GetWindowLongPtr(ReBarHandle, GWL.STYLE).ToInt64());
                int desiredStyle = style & ~RBS_BANDBORDERS;
                if(desiredStyle != style) {
                    PInvoke.SetWindowLongPtr(ReBarHandle, (int)GWL.STYLE, (IntPtr)desiredStyle);
                    PInvoke.SetWindowPos(ReBarHandle, IntPtr.Zero, 0, 0, 0, 0,
                            SWP.NOMOVE | SWP.NOSIZE | SWP.NOZORDER | SWP.NOACTIVATE | SWP.FRAMECHANGED);
                }

                int bandIndex = (int)PInvoke.SendMessage(ReBarHandle, RB.IDTOINDEX, (IntPtr)BandID, IntPtr.Zero);
                if(bandIndex >= 0) {
                    REBARBANDINFO info = new REBARBANDINFO {
                            cbSize = Marshal.SizeOf(typeof(REBARBANDINFO)),
                            fMask = RBBIM.STYLE | RBBIM.CHILDSIZE | RBBIM.SIZE |
                                    RBBIM.IDEALSIZE | RBBIM.HEADERSIZE
                    };
                    if(PInvoke.SendMessage(ReBarHandle, RB.GETBANDINFO, (IntPtr)bandIndex, ref info) != IntPtr.Zero) {
                        info.fStyle &= ~(RBBS.CHILDEDGE | RBBS.GRIPPERALWAYS |
                                RBBS.VARIABLEHEIGHT);
                        info.fStyle |= RBBS.FIXEDSIZE | RBBS.NOGRIPPER | RBBS.HIDETITLE;
                        info.cxMinChild = 16;
                        info.cyMinChild = BarThickness;
                        info.cyChild = BarThickness;
                        info.cyMaxChild = BarThickness;
                        info.cyIntegral = 1;
                        info.cx = 16;
                        info.cxIdeal = 16;
                        info.cxHeader = 0;
                        PInvoke.SendMessage(ReBarHandle, RB.SETBANDINFO, (IntPtr)bandIndex, ref info);
                    }
                }

                FixExplorerBarWidth();
                FluentGlassManager.RegisterSideBar(explorerHandle, Handle, ReBarHandle);
            }
            catch(Exception ex) {
                QTUtility2.MakeErrorLog(ex, "QTCommandBarVertical ApplyExplorerBarChrome");
            }
        }

        private void HideExplorerBarHeader() {
            IntPtr toolbar = IntPtr.Zero;
            while((toolbar = PInvoke.FindWindowEx(ReBarHandle, toolbar, "ToolbarWindow32", null)) != IntPtr.Zero) {
                if(toolbar == Handle || PInvoke.IsChild(toolbar, Handle)) continue;
                PInvoke.ShowWindow(toolbar, 0);
            }
        }

        private void ScheduleBarWidthFix() {
            if(widthFixPending || fixingBarWidth || IsDisposed || !IsHandleCreated) return;
            widthFixPending = true;
            try {
                BeginInvoke((MethodInvoker)delegate {
                    widthFixPending = false;
                    FixExplorerBarWidth();
                });
            }
            catch(InvalidOperationException) {
                widthFixPending = false;
            }
        }

        private void FixExplorerBarWidth() {
            if(fixingBarWidth || IsDisposed || !IsHandleCreated) return;
            fixingBarWidth = true;
            try {
                int width = BarThickness;
                if(Width != width) Width = width;

                if(ReBarHandle == IntPtr.Zero || !PInvoke.IsWindow(ReBarHandle)) return;
                Rectangle rebarRect = PInvoke.GetWindowRect(ReBarHandle);
                if(PInvoke.GetClassName(ReBarHandle) == "ReBarWindow32" &&
                        rebarRect.Width != width && rebarRect.Height > 0) {
                    PInvoke.SetWindowPos(ReBarHandle, IntPtr.Zero, 0, 0, width, rebarRect.Height,
                            SWP.NOMOVE | SWP.NOZORDER | SWP.NOACTIVATE | SWP.FRAMECHANGED);
                }

                IntPtr baseBar = PInvoke.GetParent(ReBarHandle);
                if(baseBar != IntPtr.Zero && PInvoke.IsWindow(baseBar) &&
                        PInvoke.GetClassName(baseBar) == "BaseBar") {
                    Rectangle baseRect = PInvoke.GetWindowRect(baseBar);
                    if(baseRect.Width != width && baseRect.Height > 0) {
                        PInvoke.SetWindowPos(baseBar, IntPtr.Zero, 0, 0, width, baseRect.Height,
                                SWP.NOMOVE | SWP.NOZORDER | SWP.NOACTIVATE | SWP.FRAMECHANGED);
                    }
                }
            }
            finally {
                fixingBarWidth = false;
            }
        }

        private void initializationTimer_Tick(object sender, EventArgs e) {
            if(IsDisposed || ++initializationAttempts >= 40) {
                StopInitializationTimer();
                return;
            }
            if(TabBar == null) return;
            StopInitializationTimer();
            RefreshItems();
        }

        private void RefreshItems() {
            try {
                bool currentKeyExists;
                using(RegistryKey currentKey = Registry.CurrentUser.OpenSubKey(CurrentItemsKey, false)) {
                    currentKeyExists = currentKey != null;
                    if(currentKeyExists) {
                        iconSize = EnsureIconSize(ReadInt(currentKey, "IconSize", DefaultIconSize));
                        // Builds before 1.5.24 stored a stale application-list snapshot here.
                        followsApplicationList = ReadInt(currentKey, "FollowApplications", 1) != 0;
                    }
                    else {
                        followsApplicationList = true;
                    }
                }

                List<VersatileItem> loaded = followsApplicationList
                        ? CreateItemsFromUserApps()
                        : LoadRegistryItems(CurrentItemsKey, false);
                if(loaded.Count == 0 && followsApplicationList && currentKeyExists) {
                    loaded = LoadRegistryItems(CurrentItemsKey, false);
                    followsApplicationList = loaded.Count == 0;
                }
                if(loaded.Count == 0 && !currentKeyExists) {
                    loaded = LoadRegistryItems(LegacyItemsKey, false)
                            .Where(item => item.Kind != VersatileItemKind.BuiltIn || item.CommandId != 5)
                            .ToList();
                    if(loaded.Count > 0) followsApplicationList = false;
                }
                if(loaded.Count == 0) {
                    loaded = CreateDefaultItems();
                }
                items = loaded;
                RebuildToolStrip();
            }
            catch(Exception ex) {
                QTUtility2.MakeErrorLog(ex, "QTCommandBarVertical RefreshItems");
            }
        }

        private List<VersatileItem> LoadRegistryItems(string keyPath, bool readIconSize) {
            List<VersatileItem> loaded = new List<VersatileItem>();
            using(RegistryKey key = Registry.CurrentUser.OpenSubKey(keyPath, false)) {
                if(key == null) return loaded;
                if(readIconSize) {
                    iconSize = EnsureIconSize(ReadInt(key, "IconSize", DefaultIconSize));
                }
                foreach(string name in OrderedSubKeyNames(key)) {
                    using(RegistryKey itemKey = key.OpenSubKey(name, false)) {
                        if(itemKey != null) loaded.Add(ReadRegistryItem(itemKey));
                    }
                }
            }
            return loaded;
        }

        private static IEnumerable<string> OrderedSubKeyNames(RegistryKey key) {
            return key.GetSubKeyNames().OrderBy(name => {
                int value;
                return int.TryParse(name, out value) ? value : int.MaxValue;
            }).ThenBy(name => name, StringComparer.OrdinalIgnoreCase);
        }

        private VersatileItem ReadRegistryItem(RegistryKey key) {
            int customKind = ReadInt(key, "QTVKind", -1);
            int originalType = ReadInt(key, "Type", 3);
            int commandId = ReadInt(key, "CommandID", 0);
            VersatileItem item = new VersatileItem {
                    Kind = customKind >= 0 && customKind <= (int)VersatileItemKind.Folder
                            ? (VersatileItemKind)customKind
                            : MapOriginalKind(originalType, commandId),
                    DisplayText = key.GetValue("DisplayText") as string,
                    TooltipText = key.GetValue("TooltipText") as string,
                    Path = key.GetValue("Path") as string,
                    IconResource = key.GetValue("IconResource") as string,
                    Idl = key.GetValue("IDL") as byte[],
                    CommandId = commandId,
                    GroupNames = (key.GetValue("GroupNames") as string[]) ?? MakeArray(key.GetValue("GroupName") as string),
                    WorkingDirectory = key.GetValue("WorkingDirectory") as string
            };
            string[] arguments = key.GetValue("Arguments") as string[];
            if(arguments != null && arguments.Length > 0) {
                item.Arguments = arguments[0];
                if(string.IsNullOrEmpty(item.WorkingDirectory) && arguments.Length > 1) {
                    item.WorkingDirectory = arguments[1];
                }
            }
            else {
                item.Arguments = key.GetValue("Arguments") as string;
            }
            foreach(string name in OrderedSubKeyNames(key)) {
                using(RegistryKey childKey = key.OpenSubKey(name, false)) {
                    if(childKey != null) item.Children.Add(ReadRegistryItem(childKey));
                }
            }
            if(item.Children.Count > 0 && item.Kind == VersatileItemKind.File && string.IsNullOrEmpty(item.Path)) {
                item.Kind = VersatileItemKind.Folder;
            }
            EnsureDisplayText(item);
            return item;
        }

        private static VersatileItemKind MapOriginalKind(int type, int commandId) {
            switch(type) {
                case 0:
                    return commandId == 0 ? VersatileItemKind.Separator : VersatileItemKind.BuiltIn;
                case 4:
                    return VersatileItemKind.Group;
                default:
                    return VersatileItemKind.File;
            }
        }

        private List<VersatileItem> CreateItemsFromUserApps() {
            return AppsManager.BuildNestedStructure(
                    app => new VersatileItem {
                            Kind = VersatileItemKind.File,
                            DisplayText = app.Name,
                            TooltipText = app.Name,
                            Path = app.Path,
                            Arguments = app.Args,
                            WorkingDirectory = app.WorkingDir,
                            UserApp = app
                    },
                    (name, children) => {
                        VersatileItem folder = new VersatileItem {
                                Kind = VersatileItemKind.Folder,
                                DisplayText = name,
                                TooltipText = name
                        };
                        folder.Children.AddRange(children);
                        return folder;
                    }).ToList();
        }

        private static List<VersatileItem> CreateDefaultItems() {
            return new List<VersatileItem> {
                    CreatePathItem("Dieser PC", "::{20D04FE0-3AEA-1069-A2D8-08002B30309D}"),
                    CreatePathItem("Systemsteuerung", "::{26EE0668-A00A-44D7-9371-BEB064C98683}"),
                    new VersatileItem {Kind = VersatileItemKind.Separator},
                    CreatePathItem("Eingabeaufforderung", @"%windir%\system32\cmd.exe")
            };
        }

        private static VersatileItem CreatePathItem(string name, string path) {
            return new VersatileItem {
                    Kind = VersatileItemKind.File,
                    DisplayText = name,
                    TooltipText = name,
                    Path = path
            };
        }

        private void RebuildToolStrip() {
            toolStrip.SuspendLayout();
            while(toolStrip.Items.Count > 0) {
                toolStrip.Items[0].Dispose();
            }
            toolStrip.ImageScalingSize = new Size(iconSize, iconSize);
            foreach(VersatileItem item in items) {
                ToolStripItem control = CreateToolStripItem(item, false);
                if(control != null) {
                    control.Overflow = ToolStripItemOverflow.Never;
                    toolStrip.Items.Add(control);
                }
            }
            Width = BarThickness;
            MinSize = new Size(16, BarThickness);
            MaxSize = new Size(-1, BarThickness);
            toolStrip.ResumeLayout(true);
            toolStrip.RaiseOnResize();
            ScheduleBarWidthFix();
            Invalidate(true);
        }

        private ToolStripItem CreateToolStripItem(VersatileItem item, bool menuItem) {
            if(item.Kind == VersatileItemKind.Separator) {
                ToolStripItem separator = new ToolStripSeparator {
                        AutoSize = menuItem,
                        Margin = menuItem ? Padding.Empty : new Padding(0, 2, 0, 2),
                        Size = menuItem ? Size.Empty : new Size(BarThickness - 4, 5),
                        Tag = item
                };
                if(!menuItem) AttachItemDragHandlers(separator);
                return separator;
            }
            if(item.Kind == VersatileItemKind.Folder || item.Children.Count > 0 || IsDropDownBuiltIn(item)) {
                ToolStripDropDownItem button = menuItem
                        ? (ToolStripDropDownItem)new ToolStripMenuItem()
                        : new ToolStripDropDownButton();
                button.AutoSize = menuItem;
                button.DisplayStyle = menuItem ? ToolStripItemDisplayStyle.ImageAndText : ToolStripItemDisplayStyle.Image;
                button.Image = LoadItemImage(item);
                button.ImageScaling = ToolStripItemImageScaling.None;
                button.Tag = item;
                button.Text = item.DisplayText;
                button.ToolTipText = MakeTooltip(item);
                if(!menuItem) {
                    ((ToolStripDropDownButton)button).ShowDropDownArrow = false;
                    button.Margin = Padding.Empty;
                    button.Padding = new Padding(2);
                    button.Size = new Size(BarThickness - 4, BarThickness - 2);
                    AttachItemDragHandlers(button);
                }
                PopulateDropDown(button, item);
                return button;
            }
            ToolStripItem result = menuItem
                    ? (ToolStripItem)new ToolStripMenuItem()
                    : new ToolStripButton {AutoSize = false, Size = new Size(BarThickness - 4, BarThickness - 2)};
            result.DisplayStyle = menuItem ? ToolStripItemDisplayStyle.ImageAndText : ToolStripItemDisplayStyle.Image;
            result.Image = LoadItemImage(item);
            result.ImageScaling = ToolStripItemImageScaling.None;
            result.Tag = item;
            result.Text = item.DisplayText;
            result.ToolTipText = MakeTooltip(item);
            if(!menuItem) {
                result.Margin = Padding.Empty;
                result.Padding = new Padding(2);
                AttachItemDragHandlers(result);
            }
            result.Click += item_Click;
            return result;
        }

        private void PopulateDropDown(ToolStripDropDownItem button, VersatileItem item) {
            IEnumerable<VersatileItem> children = item.Children;
            if(item.Kind == VersatileItemKind.BuiltIn && item.CommandId == 3) {
                children = GroupsManager.Groups.Select(group => new VersatileItem {
                        Kind = VersatileItemKind.Group,
                        DisplayText = group.Name,
                        TooltipText = group.Name,
                        GroupNames = new[] {group.Name},
                        Path = group.Paths.FirstOrDefault()
                });
            }
            else if(item.Kind == VersatileItemKind.BuiltIn && item.CommandId == 5) {
                children = CreateItemsFromUserApps();
            }
            foreach(VersatileItem child in children) {
                ToolStripItem menuItem = CreateToolStripItem(child, true);
                if(menuItem != null) button.DropDownItems.Add(menuItem);
            }
        }

        private static bool IsDropDownBuiltIn(VersatileItem item) {
            return item.Kind == VersatileItemKind.BuiltIn && (item.CommandId == 3 || item.CommandId == 5);
        }

        private void item_Click(object sender, EventArgs e) {
            ToolStripItem control = sender as ToolStripItem;
            VersatileItem item = control == null ? null : control.Tag as VersatileItem;
            if(item == null) return;
            ExecuteItem(item);
        }

        private void ExecuteItem(VersatileItem item) {
            try {
                switch(item.Kind) {
                    case VersatileItemKind.File:
                        ExecuteFileItem(item);
                        break;
                    case VersatileItemKind.Group:
                        QTTabBarClass tabBar = TabBar;
                        string groupName = item.GroupNames == null ? null : item.GroupNames.FirstOrDefault();
                        if(tabBar != null && !string.IsNullOrEmpty(groupName)) tabBar.ReplaceByGroup(groupName);
                        break;
                    case VersatileItemKind.BuiltIn:
                        ExecuteBuiltIn(item.CommandId);
                        break;
                }
            }
            catch(Exception ex) {
                QTUtility2.MakeErrorLog(ex, "QTCommandBarVertical ExecuteItem");
            }
        }

        private void ExecuteFileItem(VersatileItem item) {
            ShellBrowserEx shellBrowser = ShellBrowser;
            if(item.UserApp != null && shellBrowser != null) {
                AppsManager.Execute(item.UserApp, shellBrowser);
                return;
            }
            using(IDLWrapper wrapper = item.Idl != null
                    ? new IDLWrapper(item.Idl)
                    : new IDLWrapper(Environment.ExpandEnvironmentVariables(item.Path ?? string.Empty))) {
                if(wrapper.Available && wrapper.IsFolder && shellBrowser != null) {
                    if((ModifierKeys & Keys.Control) != 0 && TabBar != null) {
                        TabBar.OpenNewTabOrWindow(wrapper);
                    }
                    else {
                        shellBrowser.Navigate(wrapper);
                    }
                    return;
                }
            }
            if(string.IsNullOrEmpty(item.Path)) return;
            UserApp app = new UserApp(
                    item.DisplayText,
                    item.Path,
                    item.Arguments ?? string.Empty,
                    item.WorkingDirectory ?? string.Empty,
                    Keys.None);
            if(shellBrowser != null) {
                AppsManager.Execute(app, shellBrowser);
            }
            else {
                Process.Start(new ProcessStartInfo(Environment.ExpandEnvironmentVariables(item.Path)) {UseShellExecute = true});
            }
        }

        private void ExecuteBuiltIn(int commandId) {
            QTTabBarClass tabBar = TabBar;
            ShellBrowserEx shellBrowser = ShellBrowser;
            switch(commandId) {
                case 1:
                    if(Explorer != null) Explorer.GoBack();
                    return;
                case 2:
                    if(Explorer != null) Explorer.GoForward();
                    return;
                case 6:
                    if(tabBar != null && shellBrowser != null) {
                        using(IDLWrapper current = shellBrowser.GetShellPath()) tabBar.OpenNewWindow(current);
                    }
                    return;
                case 7:
                    if(tabBar != null && shellBrowser != null) {
                        using(IDLWrapper current = shellBrowser.GetShellPath()) tabBar.OpenNewTabOrWindow(current);
                    }
                    return;
                case 13:
                    if(explorerHandle != IntPtr.Zero) PInvoke.PostMessage(explorerHandle, WM.CLOSE, IntPtr.Zero, IntPtr.Zero);
                    return;
                case 16:
                    if(shellBrowser != null) {
                        using(IDLWrapper current = shellBrowser.GetShellPath())
                        using(IDLWrapper parent = current.GetParent()) {
                            if(parent != null && parent.Available) shellBrowser.Navigate(parent);
                        }
                    }
                    return;
                case 17:
                    if(Explorer != null) Explorer.Refresh();
                    return;
                case 19:
                    OpenOptionsDeferred();
                    return;
            }
            QTTabBarClass.TryCallButtonBar(buttonBar => buttonBar.ClickItem(commandId));
        }

        private Image LoadItemImage(VersatileItem item) {
            Image image = null;
            if(!string.IsNullOrEmpty(item.IconResource)) {
                image = LoadIconResource(item.IconResource);
            }
            if(image == null && item.Kind == VersatileItemKind.BuiltIn) {
                image = LoadBuiltInImage(item.CommandId);
            }
            if(image == null && item.Kind == VersatileItemKind.Group) {
                image = ResizeImage(Resources_Image.imgGroupRoot);
            }
            if(image == null && item.Kind == VersatileItemKind.Folder) {
                image = ResizeImage(Resources_Image.imgUserAppsRoot);
            }
            if(image == null) {
                image = LoadShellImage(item);
            }
            return image ?? ResizeImage(Resources_Image.icoEmpty.ToBitmap());
        }

        private Image LoadShellImage(VersatileItem item) {
            IntPtr iconHandle = IntPtr.Zero;
            try {
                SHFILEINFO info = new SHFILEINFO();
                if(item.Idl != null) {
                    using(IDLWrapper wrapper = new IDLWrapper(item.Idl)) {
                        if(wrapper.Available && PInvoke.SHGetFileInfo(wrapper.PIDL, 0, ref info, Marshal.SizeOf(info), 0x108) != IntPtr.Zero) {
                            iconHandle = info.hIcon;
                        }
                    }
                }
                else if(!string.IsNullOrEmpty(item.Path)) {
                    string path = Environment.ExpandEnvironmentVariables(item.Path);
                    if(PInvoke.SHGetFileInfo(path, 0, ref info, Marshal.SizeOf(info), 0x100) != IntPtr.Zero) {
                        iconHandle = info.hIcon;
                    }
                }
                return BitmapFromIconHandle(iconHandle);
            }
            finally {
                if(iconHandle != IntPtr.Zero) PInvoke.DestroyIcon(iconHandle);
            }
        }

        private Image LoadIconResource(string resource) {
            string expanded = Environment.ExpandEnvironmentVariables(resource.Trim());
            int index = 0;
            int comma = expanded.LastIndexOf(',');
            if(comma > 0) {
                int parsed;
                if(int.TryParse(expanded.Substring(comma + 1).Trim(), out parsed)) {
                    index = parsed;
                    expanded = expanded.Substring(0, comma);
                }
            }
            expanded = expanded.Trim().Trim('"');
            if(!File.Exists(expanded)) return null;
            IntPtr[] large = new IntPtr[1];
            try {
                if(ExtractIconEx(expanded, index, large, null, 1) == 0 || large[0] == IntPtr.Zero) return null;
                return BitmapFromIconHandle(large[0]);
            }
            finally {
                if(large[0] != IntPtr.Zero) PInvoke.DestroyIcon(large[0]);
            }
        }

        private Image LoadBuiltInImage(int commandId) {
            switch(commandId) {
                case 3:
                    return ResizeImage(Resources_Image.imgGroupRoot);
                case 5:
                    return ResizeImage(Resources_Image.imgUserAppsRoot);
                case 6:
                    return ResizeImage(Resources_Image.imgNewWindow);
                case 19:
                    return ResizeImage(Resources_Image.imgOptions_NoFocus);
            }
            Bitmap strip = Resources_Image.ButtonStrip24;
            const int sourceSize = 24;
            int x = (commandId - 1) * sourceSize;
            if(commandId > 0 && x + sourceSize <= strip.Width && sourceSize <= strip.Height) {
                using(Bitmap source = strip.Clone(new Rectangle(x, 0, sourceSize, sourceSize), strip.PixelFormat)) {
                    return ResizeImage(source);
                }
            }
            return null;
        }

        private Image BitmapFromIconHandle(IntPtr handle) {
            if(handle == IntPtr.Zero) return null;
            using(Icon source = Icon.FromHandle(handle))
            using(Icon resized = new Icon(source, iconSize, iconSize)) {
                return resized.ToBitmap();
            }
        }

        private Image ResizeImage(Image source) {
            Bitmap target = new Bitmap(iconSize, iconSize);
            using(Graphics graphics = Graphics.FromImage(target)) {
                graphics.Clear(Color.Transparent);
                graphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
                graphics.PixelOffsetMode = PixelOffsetMode.HighQuality;
                graphics.DrawImage(source, new Rectangle(0, 0, iconSize, iconSize));
            }
            return target;
        }

        private void toolStrip_DragEnter(object sender, DragEventArgs e) {
            e.Effect = GetDropEffect(e.Data);
            UpdateDragInsertion(e);
        }

        private void toolStrip_DragOver(object sender, DragEventArgs e) {
            e.Effect = GetDropEffect(e.Data);
            UpdateDragInsertion(e);
        }

        private void toolStrip_DragLeave(object sender, EventArgs e) {
            ClearDragInsertion();
        }

        private void toolStrip_DragDrop(object sender, DragEventArgs e) {
            try {
                int insertionIndex = GetInsertionIndex(toolStrip.PointToClient(new Point(e.X, e.Y)));
                VersatileItem movedItem = GetDraggedItem(e.Data);
                if(movedItem != null) {
                    MoveItem(movedItem, insertionIndex);
                    return;
                }

                string[] paths = e.Data == null ? null : e.Data.GetData(DataFormats.FileDrop) as string[];
                if(paths == null || paths.Length == 0) return;
                bool changed = false;
                int targetIndex = Math.Max(0, Math.Min(insertionIndex, items.Count));
                foreach(string path in paths.Where(path => !string.IsNullOrEmpty(path))) {
                    if(items.Any(item => string.Equals(item.Path, path, StringComparison.OrdinalIgnoreCase))) continue;
                    items.Insert(targetIndex++, CreatePathItem(Path.GetFileName(path.TrimEnd('\\')), path));
                    changed = true;
                }
                if(!changed) return;
                followsApplicationList = false;
                SaveItems();
                RebuildToolStrip();
            }
            catch(Exception ex) {
                QTUtility2.MakeErrorLog(ex, "QTCommandBarVertical DragDrop");
            }
            finally {
                ClearDragInsertion();
            }
        }

        private void toolStrip_MouseDown(object sender, MouseEventArgs e) {
            if(e.Button == MouseButtons.Right) {
                SetContextItem(e.Location);
            }
            else if(e.Button == MouseButtons.Left) {
                ToolStripItem control = toolStrip.GetItemAt(e.Location);
                BeginItemDrag(control == null ? null : control.Tag as VersatileItem, e.Location);
            }
        }

        private void toolStrip_MouseMove(object sender, MouseEventArgs e) {
            TryStartItemDrag(e.Location);
        }

        private void toolStrip_MouseUp(object sender, MouseEventArgs e) {
            dragCandidate = null;
        }

        private void AttachItemDragHandlers(ToolStripItem control) {
            control.MouseDown += versatileItem_MouseDown;
            control.MouseMove += versatileItem_MouseMove;
            control.MouseUp += versatileItem_MouseUp;
        }

        private void versatileItem_MouseDown(object sender, MouseEventArgs e) {
            if(e.Button != MouseButtons.Left) return;
            ToolStripItem control = sender as ToolStripItem;
            BeginItemDrag(control == null ? null : control.Tag as VersatileItem,
                    toolStrip.PointToClient(Cursor.Position));
        }

        private void versatileItem_MouseMove(object sender, MouseEventArgs e) {
            TryStartItemDrag(toolStrip.PointToClient(Cursor.Position));
        }

        private void versatileItem_MouseUp(object sender, MouseEventArgs e) {
            dragCandidate = null;
        }

        private void BeginItemDrag(VersatileItem item, Point location) {
            dragCandidate = item;
            dragStartPoint = location;
        }

        private void TryStartItemDrag(Point location) {
            if((Control.MouseButtons & MouseButtons.Left) == 0 || dragCandidate == null) return;
            Size dragSize = SystemInformation.DragSize;
            Rectangle dragBounds = new Rectangle(
                    dragStartPoint.X - dragSize.Width / 2,
                    dragStartPoint.Y - dragSize.Height / 2,
                    dragSize.Width,
                    dragSize.Height);
            if(dragBounds.Contains(location)) return;

            VersatileItem item = dragCandidate;
            dragCandidate = null;
            try {
                DataObject data = new DataObject();
                data.SetData(InternalItemDragFormat, false, item);
                toolStrip.DoDragDrop(data, DragDropEffects.Move);
            }
            catch(Exception ex) {
                QTUtility2.MakeErrorLog(ex, "QTCommandBarVertical Begin Drag");
            }
            finally {
                ClearDragInsertion();
            }
        }

        private void toolStrip_Paint(object sender, PaintEventArgs e) {
            if(dragInsertionIndex < 0) return;
            int y = GetInsertionLineY(dragInsertionIndex);
            using(Pen pen = new Pen(SystemColors.Highlight, 2f)) {
                e.Graphics.DrawLine(pen, 2, y, Math.Max(2, toolStrip.ClientSize.Width - 3), y);
            }
        }

        private DragDropEffects GetDropEffect(IDataObject data) {
            try {
                if(GetDraggedItem(data) != null) return DragDropEffects.Move;
                return data != null && data.GetDataPresent(DataFormats.FileDrop)
                        ? DragDropEffects.Copy
                        : DragDropEffects.None;
            }
            catch(Exception ex) {
                QTUtility2.MakeErrorLog(ex, "QTCommandBarVertical Get Drop Effect");
                return DragDropEffects.None;
            }
        }

        private VersatileItem GetDraggedItem(IDataObject data) {
            if(data == null || !data.GetDataPresent(InternalItemDragFormat)) return null;
            VersatileItem item = data.GetData(InternalItemDragFormat) as VersatileItem;
            return item != null && items.Contains(item) ? item : null;
        }

        private void UpdateDragInsertion(DragEventArgs e) {
            int newIndex = e.Effect == DragDropEffects.None
                    ? -1
                    : GetInsertionIndex(toolStrip.PointToClient(new Point(e.X, e.Y)));
            if(newIndex == dragInsertionIndex) return;
            dragInsertionIndex = newIndex;
            toolStrip.Invalidate();
        }

        private void ClearDragInsertion() {
            if(dragInsertionIndex < 0) return;
            dragInsertionIndex = -1;
            toolStrip.Invalidate();
        }

        private int GetInsertionIndex(Point location) {
            for(int i = 0; i < toolStrip.Items.Count; i++) {
                Rectangle bounds = toolStrip.Items[i].Bounds;
                if(location.Y < bounds.Top + bounds.Height / 2) return i;
            }
            return items.Count;
        }

        private int GetInsertionLineY(int insertionIndex) {
            if(toolStrip.Items.Count == 0) return Math.Max(1, toolStrip.Padding.Top);
            if(insertionIndex <= 0) return Math.Max(1, toolStrip.Items[0].Bounds.Top);
            if(insertionIndex >= toolStrip.Items.Count) {
                return Math.Min(toolStrip.ClientSize.Height - 2,
                        toolStrip.Items[toolStrip.Items.Count - 1].Bounds.Bottom);
            }
            return toolStrip.Items[insertionIndex].Bounds.Top;
        }

        private void MoveItem(VersatileItem item, int insertionIndex) {
            int sourceIndex = items.IndexOf(item);
            if(sourceIndex < 0) return;

            int targetIndex = Math.Max(0, Math.Min(insertionIndex, items.Count));
            items.RemoveAt(sourceIndex);
            if(sourceIndex < targetIndex) targetIndex--;
            targetIndex = Math.Max(0, Math.Min(targetIndex, items.Count));
            items.Insert(targetIndex, item);
            if(targetIndex == sourceIndex) return;

            followsApplicationList = false;
            SaveItems();
            RebuildToolStrip();
        }

        private void barContextMenu_Opening(object sender, CancelEventArgs e) {
            try {
                Point location = toolStrip.PointToClient(Cursor.Position);
                if(toolStrip.ClientRectangle.Contains(location)) {
                    SetContextItem(location);
                }
                removeMenuItem.Enabled = contextItem != null;
                foreach(ToolStripMenuItem sizeItem in iconSizesMenuItem.DropDownItems) {
                    sizeItem.Checked = (int)sizeItem.Tag == iconSize;
                }
                autoShowMenuItem.Checked = ShouldAutoShow();
            }
            catch(Exception ex) {
                e.Cancel = true;
                QTUtility2.MakeErrorLog(ex, "QTCommandBarVertical ContextMenu Opening");
            }
        }

        private void SetContextItem(Point location) {
            ToolStripItem control = toolStrip.GetItemAt(location);
            contextItem = control == null ? null : control.Tag as VersatileItem;
        }

        private void removeMenuItem_Click(object sender, EventArgs e) {
            try {
                if(contextItem != null && items.Remove(contextItem)) {
                    followsApplicationList = false;
                    SaveItems();
                    RebuildToolStrip();
                }
            }
            catch(Exception ex) {
                QTUtility2.MakeErrorLog(ex, "QTCommandBarVertical ContextMenu Remove");
            }
        }

        private void addSeparatorMenuItem_Click(object sender, EventArgs e) {
            try {
                items.Add(new VersatileItem {Kind = VersatileItemKind.Separator});
                followsApplicationList = false;
                SaveItems();
                RebuildToolStrip();
            }
            catch(Exception ex) {
                QTUtility2.MakeErrorLog(ex, "QTCommandBarVertical ContextMenu Separator");
            }
        }

        private void resetMenuItem_Click(object sender, EventArgs e) {
            try {
                Registry.CurrentUser.DeleteSubKeyTree(CurrentItemsKey, false);
                SetAutoShow(true);
                iconSize = DefaultIconSize;
                followsApplicationList = true;
                RefreshItems();
            }
            catch(Exception ex) {
                QTUtility2.MakeErrorLog(ex, "QTCommandBarVertical ContextMenu Reset");
            }
        }

        private void autoShowMenuItem_Click(object sender, EventArgs e) {
            SetAutoShow(autoShowMenuItem.Checked);
        }

        private static bool ShouldAutoShow() {
            try {
                using(RegistryKey key = Registry.CurrentUser.OpenSubKey(CurrentItemsKey, false)) {
                    // An existing key means the bar was already used by a pre-1.5.26 build.
                    return key != null && Convert.ToInt32(key.GetValue("AutoShow", 1)) != 0;
                }
            }
            catch(Exception ex) {
                QTUtility2.MakeErrorLog(ex, "QTCommandBarVertical Read AutoShow");
                return false;
            }
        }

        internal static object ShowBrowserBarSize {
            get { return (byte[])DefaultBarSize.Clone(); }
        }

        private static void SetAutoShow(bool enabled) {
            try {
                using(RegistryKey key = Registry.CurrentUser.CreateSubKey(CurrentItemsKey)) {
                    key.SetValue("AutoShow", enabled ? 1 : 0, RegistryValueKind.DWord);
                }
            }
            catch(Exception ex) {
                QTUtility2.MakeErrorLog(ex, "QTCommandBarVertical Write AutoShow");
            }
        }

        internal static void RestoreForExplorer(WebBrowserClass explorer) {
            if(explorer == null || !ShouldAutoShow()) return;
            try {
                EnsureUserExplorerBarRegistration();
                object clsid = typeof(QCommandBarVertical).GUID.ToString("B");
                object show = true;
                object size = ShowBrowserBarSize;
                explorer.ShowBrowserBar(ref clsid, ref show, ref size);
            }
            catch(Exception ex) {
                QTUtility2.MakeErrorLog(ex, "QTCommandBarVertical Restore");
            }
        }

        private void optionsMenuItem_Click(object sender, EventArgs e) {
            OpenOptionsDeferred();
        }

        private void OpenOptionsDeferred() {
            try {
                if(barContextMenu != null && barContextMenu.Visible) barContextMenu.Close();
                if(IsHandleCreated) {
                    BeginInvoke(new MethodInvoker(QTTabBarClass.OpenOptionDialog));
                }
                else {
                    QTTabBarClass.OpenOptionDialog();
                }
            }
            catch(InvalidOperationException) {
            }
            catch(Exception ex) {
                QTUtility2.MakeErrorLog(ex, "QTCommandBarVertical Open Options");
            }
        }

        private void iconSize_Click(object sender, EventArgs e) {
            ToolStripMenuItem menuItem = sender as ToolStripMenuItem;
            if(menuItem == null) return;
            iconSize = EnsureIconSize((int)menuItem.Tag);
            SaveItems();
            RebuildToolStrip();
        }

        private void SaveItems() {
            try {
                using(RegistryKey key = Registry.CurrentUser.CreateSubKey(CurrentItemsKey)) {
                    foreach(string name in key.GetSubKeyNames()) key.DeleteSubKeyTree(name, false);
                    key.SetValue("IconSize", iconSize, RegistryValueKind.DWord);
                    key.SetValue("FollowApplications", followsApplicationList ? 1 : 0, RegistryValueKind.DWord);
                    for(int i = 0; i < items.Count; i++) {
                        WriteRegistryItem(key, i + 1, items[i]);
                    }
                }
            }
            catch(Exception ex) {
                QTUtility2.MakeErrorLog(ex, "QTCommandBarVertical SaveItems");
            }
        }

        private static void WriteRegistryItem(RegistryKey parent, int index, VersatileItem item) {
            using(RegistryKey key = parent.CreateSubKey(index.ToString("D4"))) {
                key.SetValue("QTVKind", (int)item.Kind, RegistryValueKind.DWord);
                key.SetValue("Type", OriginalType(item.Kind), RegistryValueKind.DWord);
                if(item.CommandId != 0) key.SetValue("CommandID", item.CommandId, RegistryValueKind.DWord);
                SetString(key, "DisplayText", item.DisplayText);
                SetString(key, "TooltipText", item.TooltipText);
                SetString(key, "Path", item.Path);
                SetString(key, "IconResource", item.IconResource);
                SetString(key, "WorkingDirectory", item.WorkingDirectory);
                if(!string.IsNullOrEmpty(item.Arguments)) key.SetValue("Arguments", new[] {item.Arguments, item.WorkingDirectory ?? string.Empty});
                if(item.Idl != null && item.Idl.Length > 0) key.SetValue("IDL", item.Idl, RegistryValueKind.Binary);
                if(item.GroupNames != null && item.GroupNames.Length > 0) key.SetValue("GroupNames", item.GroupNames);
                for(int i = 0; i < item.Children.Count; i++) WriteRegistryItem(key, i + 1, item.Children[i]);
            }
        }

        private static int OriginalType(VersatileItemKind kind) {
            switch(kind) {
                case VersatileItemKind.BuiltIn:
                case VersatileItemKind.Separator:
                    return 0;
                case VersatileItemKind.Group:
                    return 4;
                default:
                    return 3;
            }
        }

        private static void SetString(RegistryKey key, string name, string value) {
            if(!string.IsNullOrEmpty(value)) key.SetValue(name, value, RegistryValueKind.String);
        }

        private static int ReadInt(RegistryKey key, string name, int fallback) {
            object value = key.GetValue(name, fallback);
            return value is int ? (int)value : fallback;
        }

        private static string[] MakeArray(string value) {
            return string.IsNullOrEmpty(value) ? null : new[] {value};
        }

        private static int EnsureIconSize(int value) {
            return value == 16 || value == 24 || value == 32 ? value : DefaultIconSize;
        }

        private static void EnsureDisplayText(VersatileItem item) {
            if(!string.IsNullOrEmpty(item.DisplayText)) return;
            if(item.GroupNames != null && item.GroupNames.Length > 0) {
                item.DisplayText = item.GroupNames[0];
            }
            else if(!string.IsNullOrEmpty(item.Path)) {
                string path = Environment.ExpandEnvironmentVariables(item.Path).TrimEnd('\\');
                item.DisplayText = Path.GetFileNameWithoutExtension(path);
                if(string.IsNullOrEmpty(item.DisplayText)) item.DisplayText = path;
            }
            else {
                item.DisplayText = "QT Command";
            }
        }

        private static string MakeTooltip(VersatileItem item) {
            if(!string.IsNullOrEmpty(item.TooltipText)) return item.TooltipText;
            if(!string.IsNullOrEmpty(item.DisplayText)) return item.DisplayText;
            return item.Path ?? "QT Command";
        }

        internal static void EnsureRegistered() {
            Register(typeof(QCommandBarVertical));
        }

        internal static void EnsureUserExplorerBarRegistration() {
            try {
                string clsid = typeof(QCommandBarVertical).GUID.ToString("B");
                using(RegistryKey key = Registry.CurrentUser.CreateSubKey(ExplorerBarsKey + clsid)) {
                    if(key != null && !(key.GetValue("BarSize") is byte[])) {
                        key.SetValue("BarSize", DefaultBarSize, RegistryValueKind.Binary);
                    }
                }
            }
            catch(Exception ex) {
                QTUtility2.MakeErrorLog(ex, "QTCommandBarVertical Register User Explorer Bar");
            }
        }

        [ComRegisterFunction]
        private static void Register(Type type) {
            RegisterInView(type, RegistryView.Default);
            if(Environment.Is64BitOperatingSystem) {
                RegisterInView(type, RegistryView.Registry64);
                RegisterInView(type, RegistryView.Registry32);
            }
            QTUtility2.log("QTCommandBarVertical registry registered");
        }

        private static void RegisterInView(Type type, RegistryView view) {
            string clsid = type.GUID.ToString("B");
            const string title = "QT Command Bar (vertical)";
            using(RegistryKey classesRoot = RegistryKey.OpenBaseKey(RegistryHive.ClassesRoot, view))
            using(RegistryKey key = classesRoot.CreateSubKey("CLSID\\" + clsid)) {
                key.SetValue(null, title);
                key.SetValue("MenuText", title);
                key.SetValue("HelpText", title);
                key.CreateSubKey("Implemented Categories\\{00021493-0000-0000-C000-000000000046}").Dispose();
                key.CreateSubKey("Implemented Categories\\{62C8FE65-4EBB-45e7-B440-6E39B2CDBF29}").Dispose();
            }
            using(RegistryKey localMachine = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view))
            using(RegistryKey key = localMachine.CreateSubKey("Software\\Microsoft\\Internet Explorer\\Explorer Bars\\" + clsid)) {
                key.SetValue(null, title);
            }
            using(RegistryKey localMachine = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view))
            using(RegistryKey key = localMachine.CreateSubKey("Software\\Microsoft\\Windows\\CurrentVersion\\Ext\\PreApproved\\" + clsid)) {
                key.SetValue(null, title);
            }
            using(RegistryKey localMachine = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view))
            using(RegistryKey key = localMachine.CreateSubKey("Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved")) {
                key.SetValue(clsid, title);
            }
            EnsureUserExplorerBarRegistration();
        }

        [ComUnregisterFunction]
        private static void Unregister(Type type) {
            try {
                using(RegistryKey key = Registry.ClassesRoot.OpenSubKey("CLSID", true)) {
                    if(key != null) key.DeleteSubKeyTree(type.GUID.ToString("B"), false);
                }
            }
            catch(Exception ex) {
                QTUtility2.MakeErrorLog(ex, "QTCommandBarVertical Unregister");
            }
        }
    }
}
