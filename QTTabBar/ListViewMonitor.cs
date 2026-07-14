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
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Windows.Forms;
using QTTabBarLib.Interop;

namespace QTTabBarLib {
    public class ListViewMonitor : IDisposable {
        public event EventHandler ListViewChanged;
        // Windows native Explorer tabs create one ShellTabWindowClass container per tab.
        // Every container can own its own SHELLDLL_DefView, so monitor all containers
        // and recapture when the active one changes.
        private List<NativeWindowController> containerControllers = new List<NativeWindowController>();
        private NativeWindowController explorerController;
        private List<AbstractListView> liveViews = new List<AbstractListView>();
        private ShellBrowserEx ShellBrowser;
        private IntPtr hwndExplorer;
        private IntPtr hwndSubDirTipMessageReflect;
        private bool fDisposed;

        internal ListViewMonitor(ShellBrowserEx shellBrowser, IntPtr hwndExplorer, IntPtr hwndSubDirTipMessageReflect) {
            ShellBrowser = shellBrowser;
            this.hwndExplorer = hwndExplorer;
            this.hwndSubDirTipMessageReflect = hwndSubDirTipMessageReflect;
            if(QTUtility.IsXP) {
                AddContainer(hwndExplorer);
            }
            else {
                IntPtr hwndContainer = IntPtr.Zero;
                while((hwndContainer = PInvoke.FindWindowEx(hwndExplorer, hwndContainer, "ShellTabWindowClass", null)) != IntPtr.Zero) {
                    AddContainer(hwndContainer);
                }
                explorerController = new NativeWindowController(hwndExplorer);
                explorerController.MessageCaptured += ExplorerController_MessageCaptured;
            }
        }

        public AbstractListView CurrentListView { get; private set; }
        public AbstractListView PreviousListView { get; private set; }

        private void AddContainer(IntPtr hwnd) {
            if(hwnd == IntPtr.Zero || containerControllers.Exists(existing => existing.Handle == hwnd)) return;
            NativeWindowController controller = new NativeWindowController(hwnd);
            controller.MessageCaptured += ContainerController_MessageCaptured;
            containerControllers.Add(controller);
        }

        private IntPtr ActiveContainer() {
            return QTUtility.IsXP ? hwndExplorer : WindowUtils.GetShellTabWindowClass(hwndExplorer);
        }

        private bool ExplorerController_MessageCaptured(ref Message msg) {
            if(msg.Msg == WM.PARENTNOTIFY &&
               PInvoke.LoWord((int)msg.WParam) == WM.CREATE &&
               PInvoke.GetClassName(msg.LParam) == "ShellTabWindowClass") {
                AddContainer(msg.LParam);
            }
            return false;
        }

        private bool ContainerController_MessageCaptured(ref Message msg) {
            if(msg.Msg == WM.PARENTNOTIFY &&
               PInvoke.LoWord((int)msg.WParam) == WM.CREATE) {
                string name = PInvoke.GetClassName(msg.LParam);
                if(name == "SHELLDLL_DefView" && msg.HWnd == ActiveContainer()) {
                    RecaptureHandles(msg.LParam);
                }
            }
            else if(msg.Msg == WM.WINDOWPOSCHANGED && containerControllers.Count > 1) {
                WINDOWPOS wp = (WINDOWPOS)Marshal.PtrToStructure(msg.LParam, typeof(WINDOWPOS));
                if((wp.flags & SWP.NOZORDER) == 0 && msg.HWnd == ActiveContainer()) {
                    IntPtr hwndShellView = WindowUtils.FindChildWindow(msg.HWnd,
                            hwnd => PInvoke.GetClassName(hwnd) == "SHELLDLL_DefView");
                    if(hwndShellView != IntPtr.Zero) {
                        RecaptureHandles(hwndShellView);
                    }
                }
            }
            return false;
        }

        public void Initialize() {
            IntPtr searchRoot = ActiveContainer();
            if(searchRoot == IntPtr.Zero) {
                searchRoot = hwndExplorer;
            }
            IntPtr hwndShellView = WindowUtils.FindChildWindow(searchRoot,
                    hwnd => PInvoke.GetClassName(hwnd) == "SHELLDLL_DefView");
            if(hwndShellView == IntPtr.Zero && searchRoot != hwndExplorer) {
                hwndShellView = WindowUtils.FindChildWindow(hwndExplorer,
                        hwnd => PInvoke.GetClassName(hwnd) == "SHELLDLL_DefView");
            }
            if(hwndShellView == IntPtr.Zero) {
                if(CurrentListView != null) {
                    CurrentListView.Dispose();
                }
                CurrentListView = new AbstractListView();
                ListViewChanged(this, null);
            }
            else {
                RecaptureHandles(hwndShellView);
            }
        }

        private void RecaptureHandles(IntPtr hwndShellView) {
            bool fIsSysListView = false;
            IntPtr hwndListView = WindowUtils.FindChildWindow(hwndShellView, hwnd => {
                string name = PInvoke.GetClassName(hwnd);
                if(name == "SysListView32") {
                    fIsSysListView = true;
                    return true;
                }
                else if(!QTUtility.IsXP && name == "DirectUIHWND") {
                    fIsSysListView = false;
                    return true;
                }
                return false;
            });

            if(CurrentListView != null) {
                if(CurrentListView.Handle == hwndListView) {
                    return;
                }
                PreviousListView = CurrentListView;
            }

            AbstractListView live = hwndListView == IntPtr.Zero ? null
                    : liveViews.Find(view => view.Handle == hwndListView);
            if(live != null) {
                CurrentListView = live;
                UpdateBackgroundWindow(hwndListView, fIsSysListView);
                ListViewChanged(this, null);
                return;
            }

            if(hwndListView == IntPtr.Zero) {
                QTUtility2.log("new AbstractListView");
                CurrentListView = new AbstractListView();
            }
            else if(fIsSysListView) {
                QTUtility2.log("new ExtendedSysListView32");
                CurrentListView = new ExtendedSysListView32(ShellBrowser, hwndShellView, hwndListView, hwndSubDirTipMessageReflect);
            }
            else {
                QTUtility2.log("new ExtendedItemsView");
                CurrentListView = new ExtendedItemsView(ShellBrowser, hwndShellView, hwndListView, hwndSubDirTipMessageReflect);
            }
            UpdateBackgroundWindow(hwndListView, fIsSysListView);
            CurrentListView.ListViewDestroyed += ListView_Destroyed;
            liveViews.Add(CurrentListView);
            ListViewChanged(this, null);
        }

        private void UpdateBackgroundWindow(IntPtr hwndListView, bool fIsSysListView) {
            if(fIsSysListView || hwndListView == IntPtr.Zero) return;
            try {
                using(IDLWrapper path = ShellBrowser.GetShellPath()) {
                    HookLibManager.UpdateBackgroundWindow(hwndListView,
                        path != null && path.Available ? path.Path : String.Empty);
                }
            }
            catch(Exception ex) {
                QTUtility2.MakeErrorLog(ex, "Explorer background initial path");
            }
        }

        private void ListView_Destroyed(object sender, EventArgs args) {
            liveViews.Remove((AbstractListView)sender);
            if(sender == CurrentListView) {
                if(PreviousListView != null) {
                    CurrentListView = PreviousListView;
                    PreviousListView = null;
                }
                else {
                    CurrentListView = new AbstractListView();
                }
                ListViewChanged(this, null);
            }
            else if(sender == PreviousListView) {
                PreviousListView = null;
            }
            ((AbstractListView)sender).Dispose();
        }

        #region IDisposable Members

        public void Dispose() {
            if(fDisposed) return;
            if(explorerController != null) {
                explorerController.MessageCaptured -= ExplorerController_MessageCaptured;
                explorerController = null;
            }
            foreach(NativeWindowController controller in containerControllers) {
                controller.MessageCaptured -= ContainerController_MessageCaptured;
            }
            containerControllers.Clear();
            foreach(AbstractListView view in liveViews) {
                if(view != CurrentListView) view.Dispose();
            }
            liveViews.Clear();
            if(CurrentListView != null) {
                CurrentListView.Dispose();
                CurrentListView = null;
            }
            fDisposed = true;
        }

        #endregion
    }
}
