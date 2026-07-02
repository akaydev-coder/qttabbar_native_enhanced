//    This file is part of QTTabBar, a shell extension for Microsoft
//    Windows Explorer.

using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace QTTabBarLib {
    internal static class FluentGlassManager {
        private const int AddressModeAuto = 1;
        private static readonly object appliedWindowsLock = new object();
        private static readonly HashSet<IntPtr> appliedWindows = new HashSet<IntPtr>();
        private static readonly Dictionary<IntPtr, IntPtr> tabBarWindows = new Dictionary<IntPtr, IntPtr>();
        private static readonly Dictionary<IntPtr, SideBarInfo> sideBarWindows = new Dictionary<IntPtr, SideBarInfo>();
        private static readonly object nativeLoadLock = new object();
        private static IntPtr nativeModule;
        private static DateTime nextNativeLoadAttempt;

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr LoadLibrary(string fileName);

        [DllImport("QTTabBarNative.dll", CallingConvention = CallingConvention.StdCall)]
        private static extern int QTTabBarNative_ApplyFluentGlass(
            IntPtr explorerWindow,
            IntPtr tabBarWindow,
            IntPtr sideBarWindow,
            IntPtr sideBarRebarWindow,
            int addressMode,
            int addressExtraPixels,
            uint inactiveColorArgb,
            bool applyCaptionColor,
            bool applyBorderColor,
            bool suppressQtChildErase);

        private sealed class SideBarInfo {
            internal IntPtr Window;
            internal IntPtr RebarWindow;
        }

        [DllImport("QTTabBarNative.dll", CallingConvention = CallingConvention.StdCall)]
        private static extern int QTTabBarNative_DisableFluentGlass(IntPtr explorerWindow);

        [DllImport("user32.dll", SetLastError = false)]
        private static extern IntPtr GetAncestor(IntPtr hwnd, uint flags);

        internal static bool Enabled {
            get {
                Config loaded = ConfigManager.LoadedConfig;
                return loaded != null && loaded.skin != null && loaded.skin.UseFluentExplorerGlass;
            }
        }

        internal static bool SuppressManagedBackground {
            get {
                lock(appliedWindowsLock) {
                    Config loaded = ConfigManager.LoadedConfig;
                    return loaded != null && loaded.skin != null &&
                            loaded.skin.UseFluentExplorerGlass &&
                            loaded.skin.FluentGlassSuppressQtChildErase &&
                            appliedWindows.Count != 0;
                }
            }
        }

        internal static bool SuppressManagedBackgroundFor(IntPtr explorerWindow) {
            Config loaded = ConfigManager.LoadedConfig;
            return loaded != null && loaded.skin != null &&
                    loaded.skin.UseFluentExplorerGlass &&
                    loaded.skin.FluentGlassSuppressQtChildErase &&
                    IsAppliedTo(explorerWindow);
        }

        internal static bool IsAppliedTo(IntPtr explorerWindow) {
            lock(appliedWindowsLock) {
                return appliedWindows.Contains(explorerWindow);
            }
        }

        internal static void Apply(IntPtr explorerWindow, IntPtr tabBarWindow) {
            if(explorerWindow == IntPtr.Zero) {
                return;
            }

            try {
                SideBarInfo sideBar;
                lock(appliedWindowsLock) {
                    if(tabBarWindow != IntPtr.Zero) {
                        tabBarWindows[explorerWindow] = tabBarWindow;
                    }
                    else {
                        tabBarWindows.TryGetValue(explorerWindow, out tabBarWindow);
                    }
                    sideBarWindows.TryGetValue(explorerWindow, out sideBar);
                }

                if(!Enabled) {
                    SetApplied(explorerWindow, false);
                    QTTabBarNative_DisableFluentGlass(explorerWindow);
                    return;
                }

                if(!EnsureNativeModuleLoaded()) {
                    SetApplied(explorerWindow, false);
                    return;
                }

                int mode = Config.Skin.FluentGlassAddressMode;
                if(mode < 0 || mode > 2) {
                    mode = AddressModeAuto;
                }

                int extraPixels = Math.Max(0, Math.Min(400, Config.Skin.FluentGlassAddressExtraPixels));
                int result = QTTabBarNative_ApplyFluentGlass(
                    explorerWindow,
                    tabBarWindow,
                    sideBar == null ? IntPtr.Zero : sideBar.Window,
                    sideBar == null ? IntPtr.Zero : sideBar.RebarWindow,
                    mode,
                    extraPixels,
                    ToArgb(Config.Skin.FluentGlassInactiveColor),
                    Config.Skin.FluentGlassApplyCaptionColor,
                    Config.Skin.FluentGlassApplyBorderColor,
                    Config.Skin.FluentGlassSuppressQtChildErase);
                // S_FALSE only means that Explorer's child geometry is temporarily
                // unavailable while it is laying out a newly selected tab.  The
                // previously extended DWM frame remains valid during that interval.
                SetApplied(explorerWindow, result >= 0);
                QTUtility2.log(String.Format(
                    "FluentGlass Apply explorer=0x{0:X} tab=0x{1:X} result=0x{2:X8}",
                    explorerWindow.ToInt64(), tabBarWindow.ToInt64(), result));
            }
            catch(DllNotFoundException) {
                SetApplied(explorerWindow, false);
            }
            catch(EntryPointNotFoundException) {
                SetApplied(explorerWindow, false);
            }
            catch(Exception ex) {
                SetApplied(explorerWindow, false);
                QTUtility2.MakeErrorLog(ex, "FluentGlassManager.Apply");
            }
        }

        internal static void RegisterSideBar(IntPtr explorerWindow, IntPtr sideBarWindow, IntPtr rebarWindow) {
            if(explorerWindow == IntPtr.Zero || sideBarWindow == IntPtr.Zero) return;
            bool changed;
            lock(appliedWindowsLock) {
                SideBarInfo current;
                changed = !sideBarWindows.TryGetValue(explorerWindow, out current) ||
                        current.Window != sideBarWindow || current.RebarWindow != rebarWindow;
                if(changed) {
                    sideBarWindows[explorerWindow] = new SideBarInfo {
                            Window = sideBarWindow,
                            RebarWindow = rebarWindow
                    };
                }
            }
            if(changed && Enabled) Apply(explorerWindow, IntPtr.Zero);
        }

        internal static void UnregisterSideBar(IntPtr explorerWindow, IntPtr sideBarWindow) {
            if(explorerWindow == IntPtr.Zero) return;
            bool removed = false;
            lock(appliedWindowsLock) {
                SideBarInfo current;
                if(sideBarWindows.TryGetValue(explorerWindow, out current) &&
                        (sideBarWindow == IntPtr.Zero || current.Window == sideBarWindow)) {
                    sideBarWindows.Remove(explorerWindow);
                    removed = true;
                }
            }
            if(removed && Enabled) Apply(explorerWindow, IntPtr.Zero);
        }

        internal static void ApplyFor(Control control) {
            if(control == null || !Enabled || !control.IsHandleCreated) {
                return;
            }

            IntPtr explorerWindow = GetAncestor(control.Handle, 2);
            if(explorerWindow != IntPtr.Zero) {
                Apply(explorerWindow, control.Handle);
            }
        }

        internal static void Disable(IntPtr explorerWindow) {
            if(explorerWindow == IntPtr.Zero) {
                return;
            }

            try {
                lock(appliedWindowsLock) {
                    appliedWindows.Remove(explorerWindow);
                    tabBarWindows.Remove(explorerWindow);
                    sideBarWindows.Remove(explorerWindow);
                }
                QTTabBarNative_DisableFluentGlass(explorerWindow);
            }
            catch(DllNotFoundException) {
            }
            catch(EntryPointNotFoundException) {
            }
            catch(Exception ex) {
                QTUtility2.MakeErrorLog(ex, "FluentGlassManager.Disable");
            }
        }

        private static void SetApplied(IntPtr explorerWindow, bool applied) {
            lock(appliedWindowsLock) {
                if(applied) {
                    appliedWindows.Add(explorerWindow);
                }
                else {
                    appliedWindows.Remove(explorerWindow);
                }
            }
        }

        private static uint ToArgb(Color color) {
            return ((uint)color.A << 24) |
                   ((uint)color.R << 16) |
                   ((uint)color.G << 8) |
                   color.B;
        }

        internal static bool EnsureNativeModuleLoaded() {
            if(nativeModule != IntPtr.Zero) {
                return true;
            }

            lock(nativeLoadLock) {
                if(nativeModule != IntPtr.Zero) {
                    return true;
                }
                if(DateTime.UtcNow < nextNativeLoadAttempt) {
                    return false;
                }

                string installPath = Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),
                    "QTTabBar",
                    "QTTabBarNative.dll");
                nativeModule = LoadLibrary(installPath);
                if(nativeModule == IntPtr.Zero) {
                    nextNativeLoadAttempt = DateTime.UtcNow.AddSeconds(5);
                }
                return nativeModule != IntPtr.Zero;
            }
        }
    }
}
