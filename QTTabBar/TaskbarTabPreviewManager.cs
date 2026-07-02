//    This file is part of QTTabBar, a shell extension for Microsoft
//    Windows Explorer.

using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Runtime.InteropServices;
using System.Windows.Forms;
using QTTabBarLib.Common;
using QTTabBarLib.Interop;

namespace QTTabBarLib {
    internal sealed class TaskbarTabPreviewManager : IDisposable {
        internal sealed class Entry {
            public string Title;
            public string Path;
            public Icon Icon;
            public Bitmap Preview;
            public bool Active;
        }

        private sealed class ProxyWindow : NativeWindow, IDisposable {
            private readonly TaskbarTabPreviewManager owner;
            private Icon icon;
            private Bitmap preview;
            private Bitmap previewSource;
            private bool livePreviewRequestLogged;
            private bool livePreviewResultLogged;

            [DllImport("user32.dll", CharSet = CharSet.Unicode)]
            private static extern bool SetWindowText(IntPtr hwnd, string text);

            public int Index { get; set; }
            public bool Active { get; set; }
            public string Title { get; private set; }
            public string Path { get; private set; }

            public ProxyWindow(TaskbarTabPreviewManager owner, int index, IntPtr ownerHandle) {
                this.owner = owner;
                Index = index;
                CreateParams cp = new CreateParams {
                    Caption = "QTTabBar",
                    Style = unchecked((int)0x80000000), // WS_POPUP
                    ExStyle = 0x08000000, // WS_EX_NOACTIVATE
                    // RegisterTab groups the thumbnails, but Windows 10 only sends
                    // the live-preview callback when the proxy is also owned by the
                    // Explorer top-level window.
                    Parent = ownerHandle,
                    X = -32000,
                    Y = -32000,
                    Width = 1,
                    Height = 1
                };
                CreateHandle(cp);
                try {
                    TabbedThumbnailNativeMethods.EnableCustomWindowPreview(Handle, true);
                    // The Explorer owner stays excluded from Peek, while this proxy
                    // supplies the captured tab bitmap as its live preview.
                    TabbedThumbnailNativeMethods.SetWindowPeekPolicy(Handle, false, false);
                }
                catch(Exception ex) {
                    QTUtility2.MakeErrorLog(ex, "TaskbarTabPreviewManager EnableCustomWindowPreview");
                }
            }

            public bool Update(Entry entry, int index, out bool previewChanged) {
                string nextTitle = string.IsNullOrEmpty(entry.Title) ? entry.Path : entry.Title;
                string nextPath = entry.Path ?? string.Empty;
                bool metadataChanged = !String.Equals(Title, nextTitle, StringComparison.Ordinal) ||
                                       !String.Equals(Path, nextPath, StringComparison.Ordinal);
                previewChanged = !ReferenceEquals(previewSource, entry.Preview);
                Index = index;
                Active = entry.Active;
                if(metadataChanged) {
                    Title = nextTitle;
                    Path = nextPath;
                    SetIcon(entry.Icon);
                    SetWindowText(Handle, Title ?? string.Empty);
                }
                if(previewChanged) {
                    SetPreview(entry.Preview);
                }
                return metadataChanged || previewChanged;
            }

            public void Dispose() {
                if(icon != null) {
                    icon.Dispose();
                    icon = null;
                }
                if(preview != null) {
                    preview.Dispose();
                    preview = null;
                }
                previewSource = null;
                if(Handle != IntPtr.Zero) {
                    DestroyHandle();
                }
            }

            protected override void WndProc(ref Message m) {
                switch(m.Msg) {
                    case 0x0006: // WM_ACTIVATE
                        if(((int)m.WParam & 0xFFFF) != 0) {
                            owner.Activate(Index);
                            m.Result = IntPtr.Zero;
                            return;
                        }
                        break;
                    case 0x0010: // WM_CLOSE
                        QTUtility2.flog("Taskbar thumbnail close requested via WM_CLOSE for proxy index " + Index);
                        owner.Close(Index);
                        m.Result = IntPtr.Zero;
                        return;
                    case 0x0112: // WM_SYSCOMMAND
                        if(((int)m.WParam & 0xFFF0) == 0xF060) {
                            QTUtility2.flog("Taskbar thumbnail close requested via SC_CLOSE for proxy index " + Index);
                            owner.Close(Index);
                            m.Result = IntPtr.Zero;
                            return;
                        }
                        break;
                    case 0x007F: // WM_GETICON
                        if(icon != null) {
                            m.Result = icon.Handle;
                            return;
                        }
                        break;
                    case 0x0323: // WM_DWMSENDICONICTHUMBNAIL
                        int thumbnailSize = unchecked((int)m.LParam.ToInt64());
                        SendThumbnail(PInvoke.HiWord(thumbnailSize), PInvoke.LoWord(thumbnailSize));
                        m.Result = IntPtr.Zero;
                        return;
                    case 0x0326: // WM_DWMSENDICONICLIVEPREVIEWBITMAP
                        if(!livePreviewRequestLogged) {
                            livePreviewRequestLogged = true;
                            QTUtility2.flog("Taskbar live preview requested for proxy index " + Index);
                        }
                        SendLivePreview();
                        m.Result = IntPtr.Zero;
                        return;
                }
                base.WndProc(ref m);
            }

            private void SetIcon(Icon source) {
                if(icon != null) {
                    icon.Dispose();
                    icon = null;
                }
                if(source != null) {
                    icon = (Icon)source.Clone();
                    PInvoke.SendMessage(Handle, 0x0080, IntPtr.Zero, icon.Handle); // WM_SETICON, ICON_SMALL
                    PInvoke.SendMessage(Handle, 0x0080, (IntPtr)1, icon.Handle);   // WM_SETICON, ICON_BIG
                }
            }

            private void SetPreview(Bitmap source) {
                if(preview != null) {
                    preview.Dispose();
                    preview = null;
                }
                previewSource = source;
                if(source != null) {
                    preview = (Bitmap)source.Clone();
                }
            }

            private void SendThumbnail(int width, int height) {
                using(Bitmap bitmap = CreateThumbnailBitmap(width > 0 ? width : 420, height > 0 ? height : 236)) {
                    IntPtr hBitmap = bitmap.GetHbitmap();
                    try {
                        TabbedThumbnailNativeMethods.SetIconicThumbnail(Handle, hBitmap);
                    }
                    catch(Exception ex) {
                        QTUtility2.MakeErrorLog(ex, "TaskbarTabPreviewManager SetIconicThumbnail");
                    }
                    finally {
                        PInvoke.DeleteObject(hBitmap);
                    }
                }
            }

            private Bitmap CreateThumbnailBitmap(int maxWidth, int maxHeight) {
                if(preview != null) {
                    return CreateScaledSnapshotBitmap(maxWidth, maxHeight, false);
                }
                return CreatePreviewBitmap(maxWidth, maxHeight);
            }

            private void SendLivePreview() {
                Size size;
                Point offset;
                owner.GetLivePreviewGeometry(out size, out offset);
                using(Bitmap bitmap = CreateLivePreviewBitmap(size.Width, size.Height)) {
                    IntPtr hBitmap = bitmap.GetHbitmap();
                    try {
                        TabbedThumbnailNativeMethods.SetPeekBitmap(Handle, hBitmap, offset, false);
                        if(!livePreviewResultLogged) {
                            livePreviewResultLogged = true;
                            QTUtility2.flog(String.Format(
                                    "Taskbar live preview supplied index={0} bitmap={1}x{2} offset={3},{4} snapshot={5}",
                                    Index, bitmap.Width, bitmap.Height, offset.X, offset.Y,
                                    preview == null ? "none" : preview.Width + "x" + preview.Height));
                        }
                    }
                    catch(Exception ex) {
                        QTUtility2.MakeErrorLog(ex, "TaskbarTabPreviewManager SetPeekBitmap");
                    }
                    finally {
                        PInvoke.DeleteObject(hBitmap);
                    }
                }
            }

            private Bitmap CreateLivePreviewBitmap(int width, int height) {
                width = Math.Max(320, width);
                height = Math.Max(220, height);
                if(preview != null) {
                    return CreateScaledSnapshotBitmap(width, height, false);
                }
                Bitmap bitmap = new Bitmap(width, height);
                using(Graphics g = Graphics.FromImage(bitmap))
                using(Font titleFont = new Font("Segoe UI", 28f, FontStyle.Bold))
                using(Font pathFont = new Font("Segoe UI", 12f, FontStyle.Regular))
                using(Brush titleBrush = new SolidBrush(Color.FromArgb(24, 28, 34)))
                using(Brush pathBrush = new SolidBrush(Color.FromArgb(78, 86, 96)))
                using(Brush panelBrush = new SolidBrush(Color.FromArgb(244, 248, 251)))
                using(Pen border = new Pen(Color.FromArgb(160, 190, 210))) {
                    g.SmoothingMode = SmoothingMode.HighQuality;
                    g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.ClearTypeGridFit;
                    g.Clear(Color.FromArgb(250, 252, 254));

                    using(LinearGradientBrush captionBrush = new LinearGradientBrush(
                        new Rectangle(0, 0, width, Math.Max(64, height / 8)),
                        Color.FromArgb(205, 238, 248),
                        Color.FromArgb(155, 214, 232),
                        LinearGradientMode.Vertical)) {
                        g.FillRectangle(captionBrush, 0, 0, width, Math.Max(64, height / 8));
                    }

                    int panelWidth = Math.Min(Math.Max(440, width / 3), width - 80);
                    int panelHeight = 190;
                    Rectangle panel = new Rectangle((width - panelWidth) / 2, Math.Max(90, height / 3), panelWidth, panelHeight);
                    g.FillRectangle(panelBrush, panel);
                    g.DrawRectangle(border, panel.Left, panel.Top, panel.Width - 1, panel.Height - 1);

                    int iconSize = 48;
                    int iconX = panel.Left + 28;
                    int iconY = panel.Top + 34;
                    if(icon != null) {
                        g.DrawIcon(icon, new Rectangle(iconX, iconY, iconSize, iconSize));
                    }

                    using(StringFormat titleFormat = new StringFormat {
                        Trimming = StringTrimming.EllipsisCharacter,
                        FormatFlags = StringFormatFlags.NoWrap,
                        LineAlignment = StringAlignment.Center
                    }) {
                        RectangleF titleRect = new RectangleF(iconX + iconSize + 18, iconY - 4, panel.Right - iconX - iconSize - 46, iconSize + 8);
                        g.DrawString(Title ?? string.Empty, titleFont, titleBrush, titleRect, titleFormat);
                    }

                    using(StringFormat pathFormat = new StringFormat {
                        Trimming = StringTrimming.EllipsisPath,
                        FormatFlags = StringFormatFlags.NoWrap,
                        Alignment = StringAlignment.Center
                    }) {
                        RectangleF pathRect = new RectangleF(panel.Left + 28, panel.Top + 118, panel.Width - 56, 34);
                        g.DrawString(Path ?? string.Empty, pathFont, pathBrush, pathRect, pathFormat);
                    }
                }
                return bitmap;
            }

            private Bitmap CreatePreviewBitmap(int width, int height) {
                if(preview != null) {
                    return CreateScaledSnapshotBitmap(width, height, false);
                }
                Bitmap bitmap = new Bitmap(width, height);
                using(Graphics g = Graphics.FromImage(bitmap)) {
                    DrawFrostedExplorerBackground(g, width, height);
                    using(Icon explorerIcon = LoadExplorerIcon()) {
                        if(explorerIcon != null) {
                            const int iconSize = 48;
                            int x = (width - iconSize) / 2;
                            int y = (height - iconSize) / 2;
                            g.DrawIcon(explorerIcon, new Rectangle(x, y, iconSize, iconSize));
                        }
                    }
                }
                return bitmap;
            }

            private static void DrawFrostedExplorerBackground(Graphics target, int width, int height) {
                using(Bitmap scene = new Bitmap(32, 20))
                using(Graphics g = Graphics.FromImage(scene))
                using(Brush chrome = new SolidBrush(Color.FromArgb(74, 150, 174)))
                using(Brush navigation = new SolidBrush(Color.FromArgb(170, 213, 224)))
                using(Brush content = new SolidBrush(Color.FromArgb(226, 241, 245)))
                using(Brush row = new SolidBrush(Color.FromArgb(145, 185, 198))) {
                    g.Clear(Color.FromArgb(202, 230, 237));
                    g.FillRectangle(chrome, 0, 0, 32, 4);
                    g.FillRectangle(navigation, 0, 4, 8, 16);
                    g.FillRectangle(content, 8, 4, 24, 16);
                    for(int y = 6; y < 19; y += 3) {
                        g.FillRectangle(row, 10, y, 18, 1);
                    }

                    target.InterpolationMode = InterpolationMode.HighQualityBicubic;
                    target.PixelOffsetMode = PixelOffsetMode.HighQuality;
                    target.DrawImage(scene, new Rectangle(0, 0, width, height));
                }
                using(Brush frost = new SolidBrush(Color.FromArgb(145, 240, 248, 250))) {
                    target.FillRectangle(frost, 0, 0, width, height);
                }
            }

            private static Icon LoadExplorerIcon() {
                try {
                    string path = System.IO.Path.Combine(
                            Environment.GetFolderPath(Environment.SpecialFolder.Windows),
                            "explorer.exe");
                    return Icon.ExtractAssociatedIcon(path);
                }
                catch {
                    return null;
                }
            }

            private Bitmap CreateScaledSnapshotBitmap(int width, int height, bool fillTarget) {
                if(preview.Width == width && preview.Height == height) {
                    return (Bitmap)preview.Clone();
                }
                Bitmap bitmap = new Bitmap(width, height);
                using(Graphics g = Graphics.FromImage(bitmap)) {
                    g.SmoothingMode = SmoothingMode.HighQuality;
                    g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                    g.PixelOffsetMode = PixelOffsetMode.HighQuality;
                    g.Clear(Color.FromArgb(245, 248, 250));

                    if(fillTarget) {
                        Rectangle source = GetAspectFillSourceRectangle(preview.Size, new Size(width, height));
                        g.DrawImage(preview, new Rectangle(0, 0, width, height), source, GraphicsUnit.Pixel);
                    }
                    else {
                        Rectangle dest = GetAspectFitRectangle(preview.Size, new Size(width, height));
                        g.DrawImage(preview, dest);
                    }
                }
                return bitmap;
            }

            private static Size GetAspectFitSize(Size source, Size target) {
                if(source.Width <= 0 || source.Height <= 0 || target.Width <= 0 || target.Height <= 0) {
                    return new Size(Math.Max(1, target.Width), Math.Max(1, target.Height));
                }
                double scale = Math.Min((double)target.Width / source.Width, (double)target.Height / source.Height);
                return new Size(
                    Math.Max(1, (int)Math.Round(source.Width * scale)),
                    Math.Max(1, (int)Math.Round(source.Height * scale)));
            }

            private static Rectangle GetAspectFitRectangle(Size source, Size target) {
                if(source.Width <= 0 || source.Height <= 0 || target.Width <= 0 || target.Height <= 0) {
                    return new Rectangle(Point.Empty, target);
                }
                Size size = GetAspectFitSize(source, target);
                return new Rectangle((target.Width - size.Width) / 2, (target.Height - size.Height) / 2, size.Width, size.Height);
            }

            private static Rectangle GetAspectFillSourceRectangle(Size source, Size target) {
                if(source.Width <= 0 || source.Height <= 0 || target.Width <= 0 || target.Height <= 0) {
                    return new Rectangle(Point.Empty, source);
                }
                double sourceRatio = (double)source.Width / source.Height;
                double targetRatio = (double)target.Width / target.Height;
                if(sourceRatio > targetRatio) {
                    int width = Math.Max(1, (int)Math.Round(source.Height * targetRatio));
                    return new Rectangle((source.Width - width) / 2, 0, width, source.Height);
                }
                int height = Math.Max(1, (int)Math.Round(source.Width / targetRatio));
                return new Rectangle(0, (source.Height - height) / 2, source.Width, height);
            }
        }

        [ComImport]
        [Guid("56FDF344-FD6D-11d0-958A-006097C9A090")]
        private class CTaskbarList {
        }

        [ComImport]
        [Guid("EA1AFB91-9E28-4B86-90E9-9E9F8A5EEFAF")]
        [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        private interface ITaskbarList3 {
            [PreserveSig] int HrInit();
            [PreserveSig] int AddTab(IntPtr hwnd);
            [PreserveSig] int DeleteTab(IntPtr hwnd);
            [PreserveSig] int ActivateTab(IntPtr hwnd);
            [PreserveSig] int SetActiveAlt(IntPtr hwnd);
            [PreserveSig] int MarkFullscreenWindow(IntPtr hwnd, [MarshalAs(UnmanagedType.Bool)] bool fFullscreen);
            [PreserveSig] int SetProgressValue(IntPtr hwnd, ulong ullCompleted, ulong ullTotal);
            [PreserveSig] int SetProgressState(IntPtr hwnd, int tbpFlags);
            [PreserveSig] int RegisterTab(IntPtr hwndTab, IntPtr hwndMDI);
            [PreserveSig] int UnregisterTab(IntPtr hwndTab);
            [PreserveSig] int SetTabOrder(IntPtr hwndTab, IntPtr hwndInsertBefore);
            [PreserveSig] int SetTabActive(IntPtr hwndTab, IntPtr hwndMDI, uint dwReserved);
            [PreserveSig] int ThumbBarAddButtons(IntPtr hwnd, uint cButtons, IntPtr pButton);
            [PreserveSig] int ThumbBarUpdateButtons(IntPtr hwnd, uint cButtons, IntPtr pButton);
            [PreserveSig] int ThumbBarSetImageList(IntPtr hwnd, IntPtr himl);
            [PreserveSig] int SetOverlayIcon(IntPtr hwnd, IntPtr hIcon, [MarshalAs(UnmanagedType.LPWStr)] string pszDescription);
            [PreserveSig] int SetThumbnailTooltip(IntPtr hwnd, [MarshalAs(UnmanagedType.LPWStr)] string pszTip);
            [PreserveSig] int SetThumbnailClip(IntPtr hwnd, IntPtr prcClip);
        }

        private readonly Action<int> activateCallback;
        private readonly Action<int> closeCallback;
        private readonly List<ProxyWindow> proxies = new List<ProxyWindow>();
        private ITaskbarList3 taskbar;
        private IntPtr explorerHandle;
        private bool enabled;
        private int activeIndex = -1;
        private bool taskbarActiveInitialized;
        private IntPtr peekProtectedExplorerHandle;

        public TaskbarTabPreviewManager(Action<int> activateCallback, Action<int> closeCallback) {
            this.activateCallback = activateCallback;
            this.closeCallback = closeCallback;
            QTUtility2.log("TaskbarTabPreviewManager build=1.5.62.0 Stable mode=isolated-explorer-background deferred-thumbnail-close sharp-offset-live-preview owned-proxies fixed-versatile-width");
        }

        public void Update(IntPtr ownerExplorerHandle, bool enable, IList<Entry> entries) {
            explorerHandle = ownerExplorerHandle;
            enabled = enable;
            if(!enabled || explorerHandle == IntPtr.Zero || entries == null || entries.Count == 0) {
                Clear();
                return;
            }
            EnsureExplorerPeekProtection(explorerHandle);
            if(!EnsureTaskbar()) {
                Clear();
                return;
            }
            bool proxyCountChanged = EnsureProxyCount(entries.Count);
            for(int i = 0; i < entries.Count; i++) {
                bool previewChanged;
                if(proxies[i].Update(entries[i], i, out previewChanged)) {
                    int hr = taskbar.SetThumbnailTooltip(proxies[i].Handle, proxies[i].Title);
                    LogFailure("SetThumbnailTooltip", hr);
                }
                if(previewChanged) {
                    LogFailure("DwmInvalidateIconicBitmaps",
                            TabbedThumbnailNativeMethods.DwmInvalidateIconicBitmaps(proxies[i].Handle));
                }
            }
            if(proxyCountChanged) {
                UpdateOrder();
            }
            UpdateActive();
        }

        public void Clear() {
            if(taskbar != null) {
                foreach(ProxyWindow proxy in proxies) {
                    int hr = taskbar.UnregisterTab(proxy.Handle);
                    LogFailure("UnregisterTab", hr);
                }
            }
            foreach(ProxyWindow proxy in proxies) {
                proxy.Dispose();
            }
            proxies.Clear();
            activeIndex = -1;
            taskbarActiveInitialized = false;
        }

        public void Dispose() {
            Clear();
            RestoreExplorerPeekPolicy();
            if(taskbar != null) {
                Marshal.FinalReleaseComObject(taskbar);
                taskbar = null;
            }
        }

        private bool EnsureTaskbar() {
            if(taskbar != null) {
                return true;
            }
            try {
                taskbar = (ITaskbarList3)new CTaskbarList();
                int hr = taskbar.HrInit();
                LogFailure("HrInit", hr);
                return hr >= 0;
            }
            catch(Exception ex) {
                QTUtility2.MakeErrorLog(ex, "TaskbarTabPreviewManager EnsureTaskbar");
                taskbar = null;
                return false;
            }
        }

        private void EnsureExplorerPeekProtection(IntPtr hwnd) {
            if(peekProtectedExplorerHandle == hwnd) {
                return;
            }
            RestoreExplorerPeekPolicy();
            try {
                // A registered tab inherits the owner's Peek eligibility. Any owner
                // exclusion prevents DWM from requesting the proxy's live bitmap.
                TabbedThumbnailNativeMethods.SetWindowPeekPolicy(hwnd, false, false);
                peekProtectedExplorerHandle = hwnd;
                QTUtility2.flog("Taskbar tab Peek enabled without owner restrictions hwnd=0x" + hwnd.ToInt64().ToString("X"));
            }
            catch(Exception ex) {
                QTUtility2.MakeErrorLog(ex, "TaskbarTabPreviewManager owner peek shield");
            }
        }

        private void RestoreExplorerPeekPolicy() {
            if(peekProtectedExplorerHandle == IntPtr.Zero) {
                return;
            }
            try {
                TabbedThumbnailNativeMethods.SetWindowPeekPolicy(peekProtectedExplorerHandle, false, false);
            }
            catch(Exception ex) {
                QTUtility2.MakeErrorLog(ex, "TaskbarTabPreviewManager restore owner peek policy");
            }
            peekProtectedExplorerHandle = IntPtr.Zero;
        }

        private bool EnsureProxyCount(int count) {
            bool changed = proxies.Count != count;
            while(proxies.Count > count) {
                ProxyWindow proxy = proxies[proxies.Count - 1];
                if(taskbar != null) {
                    LogFailure("UnregisterTab", taskbar.UnregisterTab(proxy.Handle));
                }
                proxy.Dispose();
                proxies.RemoveAt(proxies.Count - 1);
            }
            while(proxies.Count < count) {
                ProxyWindow proxy = new ProxyWindow(this, proxies.Count, explorerHandle);
                LogFailure("RegisterTab", taskbar.RegisterTab(proxy.Handle, explorerHandle));
                proxies.Add(proxy);
            }
            if(activeIndex >= proxies.Count) {
                activeIndex = -1;
            }
            return changed;
        }

        private void UpdateOrder() {
            IntPtr previous = IntPtr.Zero;
            foreach(ProxyWindow proxy in proxies) {
                LogFailure("SetTabOrder", taskbar.SetTabOrder(proxy.Handle, previous));
                previous = proxy.Handle;
            }
        }

        private void UpdateActive() {
            int nextActiveIndex = proxies.FindIndex(proxy => proxy.Active);
            if(nextActiveIndex >= 0 && !taskbarActiveInitialized) {
                LogFailure("SetTabActive", taskbar.SetTabActive(proxies[nextActiveIndex].Handle, explorerHandle, 0));
                taskbarActiveInitialized = true;
            }
            activeIndex = nextActiveIndex;
        }

        private void Activate(int index) {
            if(index >= 0 && index < proxies.Count) {
                activateCallback(index);
                WindowUtils.BringExplorerToFront(explorerHandle);
                activeIndex = index;
            }
        }

        private void Close(int index) {
            if(index >= 0 && index < proxies.Count) {
                closeCallback(index);
            }
        }

        private void GetLivePreviewGeometry(out Size size, out Point offset) {
            Rectangle bounds = PInvoke.GetWindowRect(explorerHandle);
            if(bounds.Width > 0 && bounds.Height > 0) {
                size = bounds.Size;
                Point clientOrigin = Point.Empty;
                if(PInvoke.ClientToScreen(explorerHandle, ref clientOrigin)) {
                    offset = new Point(bounds.Left - clientOrigin.X, bounds.Top - clientOrigin.Y);
                }
                else {
                    offset = Point.Empty;
                }
                return;
            }
            size = new Size(960, 540);
            offset = Point.Empty;
        }

        private static void LogFailure(string operation, int hr) {
            if(hr < 0) {
                QTUtility2.log("TaskbarTabPreviewManager " + operation + " failed hr=0x" + hr.ToString("X8"));
            }
        }
    }
}
