//    This file is part of QTTabBar, a shell extension for Microsoft
//    Windows Explorer.

using System;
using System.Windows.Forms;
using QTTabBarLib.Interop;

namespace QTTabBarLib {
    public partial class QTTabBarClass {
        private const int FileListFocusRetryLimit = 20;
        private Timer timerFileListFocus;
        private int fileListFocusAttempts;
        private bool focusFileListAfterNavigation;

        private void RequestFileListFocus() {
            if(fClosedDW || ExplorerHandle == IntPtr.Zero) return;

            if(timerFileListFocus == null) {
                timerFileListFocus = new Timer { Interval = 50 };
                timerFileListFocus.Tick += timerFileListFocus_Tick;
            }

            fileListFocusAttempts = 0;
            timerFileListFocus.Stop();
            timerFileListFocus.Start();
        }

        private void timerFileListFocus_Tick(object sender, EventArgs e) {
            if(fClosedDW || !PInvoke.IsWindow(ExplorerHandle)) {
                StopFileListFocusTimer();
                return;
            }

            // Never steal focus back after the user has switched to another window.
            if(PInvoke.GetForegroundWindow() == ExplorerHandle) {
                AbstractListView currentListView = listViewManager == null
                        ? listView
                        : listViewManager.CurrentListView;
                if(currentListView != null &&
                        currentListView.Handle != IntPtr.Zero &&
                        PInvoke.IsWindow(currentListView.Handle)) {
                    listView = currentListView;
                    currentListView.SetFocus();
                    if(currentListView.HasFocus()) {
                        StopFileListFocusTimer();
                        QTUtility2.log("File list focused after tab activation");
                        return;
                    }
                }
            }

            if(++fileListFocusAttempts >= FileListFocusRetryLimit) {
                StopFileListFocusTimer();
            }
        }

        private void StopFileListFocusTimer() {
            if(timerFileListFocus != null) {
                timerFileListFocus.Stop();
            }
        }

        private void DisposeFileListFocusTimer() {
            if(timerFileListFocus == null) return;

            timerFileListFocus.Stop();
            timerFileListFocus.Tick -= timerFileListFocus_Tick;
            timerFileListFocus.Dispose();
            timerFileListFocus = null;
        }
    }
}
