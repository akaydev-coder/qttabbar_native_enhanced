//    This file is part of QTTabBar, a shell extension for Microsoft
//    Windows Explorer.
//    Copyright (C) 2007-2021  Quizo, Paul Accisano
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
using System.IO;
using System.Linq;
using System.Management;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;
using Microsoft.Win32;
using QTTabBarLib.Interop;

namespace QTTabBarLib {
    public static class HookLibManager {
        private static bool fShellBrowserIsHooked;
        private static bool fHookLibraryLoaded;
        private static bool fBackgroundRendererLoaded;
        private static int[] hookStatus = Enumerable.Repeat(-1, Enum.GetNames(typeof(Hooks)).Length).ToArray();

        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate void HookLibCallback(int hookId, int retcode, IntPtr context);

        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate bool NewWindowCallback(IntPtr pIDL, IntPtr context);

        [StructLayout(LayoutKind.Sequential)]
        private struct HookCallbacks
        {
            public HookLibCallback HookResult;
            public NewWindowCallback NewWindow;
            public IntPtr Context;
        }

        private static HookCallbacks nativeCallbacks = new HookCallbacks();
        private static HookLibCallback hookResultDelegate = HookResult;
        private static NewWindowCallback newWindowDelegate = NewWindow;

        [DllImport("QTTabBarNative.dll", CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Unicode)]
        private static extern int QTTabBarNative_InitializeHookLibrary(ref HookCallbacks callbacks, string libraryPath);

        [DllImport("QTTabBarNative.dll", CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Unicode)]
        private static extern int QTTabBarNative_InitializeBackgroundLibrary(ref HookCallbacks callbacks, string libraryPath);

        [DllImport("QTTabBarNative.dll", CallingConvention = CallingConvention.StdCall)]
        private static extern int QTTabBarNative_InitializeBackgroundRenderer(ref HookCallbacks callbacks);

        [DllImport("QTTabBarNative.dll", CallingConvention = CallingConvention.StdCall)]
        private static extern int QTTabBarNative_InstallBackgroundHooks();

        [DllImport("QTTabBarNative.dll", CallingConvention = CallingConvention.StdCall)]
        private static extern void QTTabBarNative_ShutdownHookLibrary();

        [DllImport("QTTabBarNative.dll", CallingConvention = CallingConvention.StdCall)]
        private static extern int QTTabBarNative_InitShellBrowserHook(IntPtr shellBrowser);

        [DllImport("QTTabBarNative.dll", CallingConvention = CallingConvention.StdCall)]
        private static extern int QTTabBarNative_RegisterBackgroundWindow(IntPtr window);

        [DllImport("QTTabBarNative.dll", CallingConvention = CallingConvention.StdCall,
            CharSet = CharSet.Unicode)]
        private static extern int QTTabBarNative_UpdateBackgroundWindow(IntPtr window, string path);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
        private static extern uint GetPrivateProfileString(string section, string key,
                string defaultValue, StringBuilder value, uint size, string filePath);

        private static void EnsureCallbacks()
        {
            if(nativeCallbacks.HookResult == null)
            {
                nativeCallbacks.HookResult = hookResultDelegate;
            }

            if(nativeCallbacks.NewWindow == null)
            {
                nativeCallbacks.NewWindow = newWindowDelegate;
            }

            nativeCallbacks.Context = IntPtr.Zero;
        }

        public enum HookCheckPoint{
            Initial,
            ShellBrowser,
            NewWindow,
            Automation,
        }

        // Unmarked hooks exist only to set other hooks.
        private enum Hooks {
            CoCreateInstance = 0,           // Treeview Middle-click
            RegisterDragDrop,               // DragDrop into SubDirTips
            SHCreateShellFolderView,
            BrowseObject,                   // Control Panel dialog OK/Cancel buttons
            CreateViewWindow3,              // Header in all Views
            MessageSFVCB,                   // Refresh = clear text
            UiaReturnRawElementProvider,
            QueryInterface,                 // Scrolling Lag workaround
            TravelToEntry,                  // Clear Search bar = back
            OnActivateSelection,            // Recently activated files
            SetNavigationState,             // Breadcrumb Bar Middle-click
            ShowWindow,                     // New Explorer window capturing
            UpdateWindowList,               // Compatibility with SHOpenFolderAndSelectItems
            CreateWindowExW ,
            DestroyWindow,
            BeginPaint,
            FillRect,
            CreateCompatibleDC
        }

        /** Do not initialize hook.*/
        public static void Initialize_donot()
        {
            QTUtility2.log("Do not initialize hook" );
        }

        public static void Initialize_bgtool()
        {
            if (fHookLibraryLoaded) return;

            string installPath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), "QTTabBar");
            string filename = "ExplorerBgTool.dll";
            string fullPath = Path.Combine(installPath, filename);

            EnsureCallbacks();

            int hr = -1;
            try
            {
                hr = QTTabBarNative_InitializeHookLibrary(ref nativeCallbacks, fullPath);
            }
            catch (DllNotFoundException ex)
            {
                QTUtility2.MakeErrorLog(ex, "QTTabBarNative.dll missing for Explorer background tool.");
            }
            catch (EntryPointNotFoundException ex)
            {
                QTUtility2.MakeErrorLog(ex, "Explorer background tool hook exports missing.");
            }

            if (hr == 0)
            {
                fHookLibraryLoaded = true;
                QTUtility2.log("HookLib Initialize success");
                return;
            }

            QTUtility2.MakeErrorLog(null, "HookLib Initialize failed: 0x" + hr.ToString("X8"));

            MessageForm.Show(IntPtr.Zero,
                String.Format(
                    "{0}: {1} {2}",
                    QTUtility.TextResourcesDic["ErrorDialogs"][4],
                    QTUtility.TextResourcesDic["ErrorDialogs"][5],
                    QTUtility.TextResourcesDic["ErrorDialogs"][7]
                ),
                QTUtility.TextResourcesDic["ErrorDialogs"][1],
                MessageBoxIcon.Hand,
                30000, false, true
            );
        }

        private static Boolean LoadedHook = false;

        public static void Initialize()
        {
            string installPath = Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),
                    "QTTabBar");
            bool legacyHookEnabled = IsLegacyHookExplicitlyEnabled(installPath);
            bool backgroundEnabled = IsExplorerBackgroundEnabled(installPath);
            bool autoHookEnabled = Config.Window.AutoHookWindow || IsConfigRegistryBool("Window", "AutoHookWindow");
            bool fullHookRequested = legacyHookEnabled || autoHookEnabled;
            bool backgroundOnly = backgroundEnabled && !fullHookRequested;
            if(!fullHookRequested && !backgroundEnabled)
            {
                if(fHookLibraryLoaded)
                {
                    QTTabBarNative_ShutdownHookLibrary();
                }
                fHookLibraryLoaded = false;
                fBackgroundRendererLoaded = false;
                LoadedHook = false;
                QTUtility2.flog("Legacy QTHookLib disabled by safe mode; Explorer background is not enabled");
                return;
            }

            bool shouldLoadHookLibrary = fullHookRequested || backgroundEnabled;
            try
            {
                if (LoadedHook)
                {
                    return;
                }
                
                ManagementObjectSearcher searcher = backgroundOnly
                        ? null
                        : new ManagementObjectSearcher("SELECT * FROM Win32_OperatingSystem");
                string sCPUSerialNumber = "";
                if(searcher != null) foreach (ManagementObject mo in searcher.Get())
                {
                    object osName = mo["Name"];
                    if(osName != null) {
                        sCPUSerialNumber = osName.ToString().ToLower().Trim();//操作系统名字
                    }
                    //sCPUSerialNumber = mo["BootDevice"].ToString().Trim();//系统启动分区
                    //sCPUSerialNumber = mo["NumberOfProcesses"].ToString().Trim();//当前运行的进程数
                    //sCPUSerialNumber = mo["SerialNumber"].ToString().Trim();//操作系统序列号
                    //sCPUSerialNumber = mo["OSLanguage"].ToString().Trim();//操作系统的语言
                    //sCPUSerialNumber = mo["Manufacturer"].ToString().Trim();//
                }

                var isServer = sCPUSerialNumber.Contains("windows server");
                if (isServer)
                {
                    QTUtility2.log("can not hook in server by get server");
                    Config.Window.AutoHookWindow = false;
                    LoadedHook = false;
                    return;
                }
            }
            catch (Exception)
            {   
                QTUtility2.log("can not hook in server by get server exception");
                Config.Window.AutoHookWindow = false;
                LoadedHook = false;
                return;
            }

            string filename = IntPtr.Size == 8 ? "QTHookLib64.dll" : "QTHookLib32.dll";
            if (fHookLibraryLoaded)
            {
                if (!shouldLoadHookLibrary)
                {
                    QTTabBarNative_ShutdownHookLibrary();
                    fHookLibraryLoaded = false;
                    fBackgroundRendererLoaded = false;
                    LoadedHook = false;
                }
                return;
            }

            if (!shouldLoadHookLibrary)
            {
                fBackgroundRendererLoaded = false;
                LoadedHook = false;
                return;
            }

            if (!backgroundOnly && !File.Exists(Path.Combine(installPath, filename))) // 如果文件不存在则设置为不自动加载
            {
                QTUtility2.flog("not exists file , close auto hook " + Path.Combine(installPath, filename));
                Config.Window.AutoHookWindow = false;
                LoadedHook = false;
                return;
            }
            if(!FluentGlassManager.EnsureNativeModuleLoaded()) {
                QTUtility2.log("Hook library skipped: QTTabBarNative.dll could not be loaded from ProgramData.");
                fHookLibraryLoaded = false;
                LoadedHook = false;
                return;
            }
            EnsureCallbacks();
            int hr = -1;
            try
            {
                if(backgroundOnly) {
                    hr = QTTabBarNative_InitializeBackgroundRenderer(ref nativeCallbacks);
                    if(hr == 0) {
                        int hookHr = QTTabBarNative_InstallBackgroundHooks();
                        if(hookHr != 0) {
                            hr = hookHr;
                        }
                    }
                }
                else {
                    hr = QTTabBarNative_InitializeHookLibrary(ref nativeCallbacks,
                            Path.Combine(installPath, filename));
                }
            }
            catch (DllNotFoundException ex)
            {
                QTUtility2.MakeErrorLog(ex, "QTTabBarNative.dll missing for hook initialization.");
                fHookLibraryLoaded = false;
                LoadedHook = false;
                return;
            }
            catch (EntryPointNotFoundException ex)
            {
                QTUtility2.MakeErrorLog(ex, "Hook exports missing in QTTabBarNative.dll.");
                fHookLibraryLoaded = false;
                LoadedHook = false;
                return;
            }
            catch (Exception ex)
            {
                QTUtility2.MakeErrorLog(ex, "Hook initialization failed safely.");
                fHookLibraryLoaded = false;
                LoadedHook = false;
                return;
            }

            if (hr == 0)
            {
                fHookLibraryLoaded = true;
                fBackgroundRendererLoaded = backgroundOnly;
                LoadedHook = true;
                return;
            }
            QTUtility2.MakeErrorLog(null, "HookLib Initialize failed: 0x" + hr.ToString("X8"));
            LoadedHook = false;
            fHookLibraryLoaded = false;
            fBackgroundRendererLoaded = false;
        }

        private static bool IsLegacyHookExplicitlyEnabled(string installPath)
        {
            string configPath = Path.Combine(installPath, "config.ini");
            if(!File.Exists(configPath)) return false;

            StringBuilder value = new StringBuilder(32);
            GetPrivateProfileString("hook", "enabled", "false", value,
                    (uint)value.Capacity, configPath);
            string enabled = value.ToString().Trim();
            return enabled.Equals("true", StringComparison.OrdinalIgnoreCase) || enabled == "1";
        }

        private static bool IsExplorerBackgroundEnabled(string installPath)
        {
            string configPath = Path.Combine(installPath, "config.ini");
            if(!File.Exists(configPath)) return false;

            StringBuilder value = new StringBuilder(1024);
            GetPrivateProfileString("image", "enabled", "true", value,
                    (uint)value.Capacity, configPath);
            string enabled = value.ToString().Trim();
            if(enabled.Equals("false", StringComparison.OrdinalIgnoreCase) || enabled == "0") {
                return false;
            }

            value.Clear();
            GetPrivateProfileString("image", "imgPath", string.Empty, value,
                    (uint)value.Capacity, configPath);
            string imagePath = Environment.ExpandEnvironmentVariables(value.ToString().Trim().Trim('"'));
            if(!string.IsNullOrEmpty(imagePath)) {
                if(!Path.IsPathRooted(imagePath)) imagePath = Path.Combine(installPath, imagePath);
                if(File.Exists(imagePath)) return true;
            }

            value.Clear();
            GetPrivateProfileString("image", "folder", "Image", value,
                    (uint)value.Capacity, configPath);
            string imageDirectory = Environment.ExpandEnvironmentVariables(
                    value.ToString().Trim().Trim('"'));
            if(String.IsNullOrEmpty(imageDirectory)) imageDirectory = "Image";
            if(!Path.IsPathRooted(imageDirectory)) imageDirectory = Path.Combine(installPath, imageDirectory);
            if(!Directory.Exists(imageDirectory)) return false;
            return Directory.EnumerateFiles(imageDirectory).Any(path =>
                    path.EndsWith(".png", StringComparison.OrdinalIgnoreCase) ||
                    path.EndsWith(".bmp", StringComparison.OrdinalIgnoreCase) ||
                    path.EndsWith(".jpg", StringComparison.OrdinalIgnoreCase) ||
                    path.EndsWith(".jpeg", StringComparison.OrdinalIgnoreCase));
        }

        private static bool IsConfigRegistryBool(string category, string name)
        {
            try {
                using(RegistryKey key = Registry.CurrentUser.OpenSubKey(RegConst.Root + RegConst.Config + category, false)) {
                    object value = key == null ? null : key.GetValue(name);
                    return value is int && (int)value != 0;
                }
            }
            catch {
                return false;
            }
        }

        internal static void RegisterBackgroundWindow(IntPtr window, string path = null)
        {
            if(!fHookLibraryLoaded || window == IntPtr.Zero) return;
            try {
                int result = QTTabBarNative_RegisterBackgroundWindow(window);
                if(result != 0) {
                    QTUtility2.log("RegisterBackgroundWindow failed: 0x" + result.ToString("X8"));
                }
                else {
                    if(!String.IsNullOrEmpty(path)) {
                        UpdateBackgroundWindow(window, path);
                    }
                }
            }
            catch(Exception ex) {
                QTUtility2.MakeErrorLog(ex, "RegisterBackgroundWindow");
            }
        }

        internal static void UpdateBackgroundWindow(IntPtr window, string path)
        {
            if(!fHookLibraryLoaded || window == IntPtr.Zero) return;
            try {
                int result = QTTabBarNative_UpdateBackgroundWindow(window, path ?? String.Empty);
                if(result != 0 && result != unchecked((int)0x80004001)) {
                    QTUtility2.log("UpdateBackgroundWindow failed: 0x" + result.ToString("X8"));
                }
            }
            catch(Exception ex) {
                QTUtility2.MakeErrorLog(ex, "UpdateBackgroundWindow");
            }
        }

        private static void HookResult(int hookId, int retcode, IntPtr context) {
            try {
                if(hookId < 0 || hookId >= hookStatus.Length) {
                    QTUtility2.log("HookResult ignored unknown hook id " + hookId + ", result " + retcode);
                    return;
                }
                lock(hookStatus) {
                    hookStatus[hookId] = retcode;
                }
            }
            catch(Exception ex) {
                // Exceptions must never cross this unmanaged callback boundary.
                QTUtility2.MakeErrorLog(ex, "HookResult callback id=" + hookId + " result=" + retcode);
            }
        }

        // We need to use a callback rather than a message for window capturing,
        // since the main instance could be in another process.
        private static bool NewWindow(IntPtr pIDL, IntPtr context) {
            try {
                if(pIDL == IntPtr.Zero) return false;
                byte[] IDL;
                using(IDLWrapper wrapper = new IDLWrapper(PInvoke.ILClone(pIDL))) {
                    if(!Config.Window.AutoHookWindow
                            || !Config.Window.CaptureNewWindows
                            || InstanceManager.GetTotalInstanceCount() == 0
                            || QTUtility2.IsShellPathButNotFileSystem(wrapper.Path)
                            || wrapper.Path.PathEquals(QTUtility.PATH_SEARCHFOLDER)
                            || QTUtility.NoCapturePathsList.Any(path => wrapper.Path.PathEquals(path))
                            || (Control.ModifierKeys & Keys.Control) != Keys.None) {
                        return false;
                    }
                    IDL = wrapper.IDL;
                }
                InstanceManager.BeginInvokeMain(tabbar => {
                    QTUtility2.log("BeginInvokeMain OpenNewTabOrWindow");
                    using (IDLWrapper wrapper = new IDLWrapper(IDL)) {
                        tabbar.OpenNewTabOrWindow(wrapper, true);
                    }
                });
                return true;
            }
            catch(Exception ex) {
                // Native callers cannot safely receive a managed exception.
                QTUtility2.MakeErrorLog(ex, "NewWindow callback");
                return false;
            }
        }
        /** do not init shell brownser hook. */
        public static void InitShellBrowserHook_old(IShellBrowser shellBrowser) { }

        public static void InitShellBrowserHook(IShellBrowser shellBrowser)
        {
            lock (typeof(HookLibManager))
            {
                if(fShellBrowserIsHooked || !fHookLibraryLoaded || fBackgroundRendererLoaded)
                {
                    if(fBackgroundRendererLoaded)
                    {
                        QTUtility2.flog("InitShellBrowserHook skipped for isolated background renderer");
                    }
                    return;
                }
                IntPtr pShellBrowser = Marshal.GetComInterfaceForObject(shellBrowser, typeof(IShellBrowser));
                if(pShellBrowser == IntPtr.Zero) return;
                int retcode = -1;
                try {
                    retcode = QTTabBarNative_InitShellBrowserHook(pShellBrowser);
                }
                catch(Exception e) {
                    QTUtility2.MakeErrorLog(e, "");
                }
                finally {
                    Marshal.Release(pShellBrowser);
                }
                if(retcode != 0) {
                    QTUtility2.MakeErrorLog(null, "InitShellBrowserHook failed: " + retcode);

                    MessageForm.Show(IntPtr.Zero,
                        String.Format(
                            "{0}: {1} {2}",
                            QTUtility.TextResourcesDic["ErrorDialogs"][4],
                            QTUtility.TextResourcesDic["ErrorDialogs"][6],
                            QTUtility.TextResourcesDic["ErrorDialogs"][7]
                        ),
                        QTUtility.TextResourcesDic["ErrorDialogs"][1],
                        MessageBoxIcon.Hand, 30000, false, true
                    );
                }
                else {
                    fShellBrowserIsHooked = true;
                }
            }
        }

        public static void CheckHooks() {
            // TODO
        }
    }
}
