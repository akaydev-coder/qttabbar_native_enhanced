# Changelog

## 1.6.5.0 Stable - 2026-08-23

- Serialize popup-capture handoffs as a small request containing only paths, mode, and selection behavior.
- Acknowledge capture only after the target Explorer deserializes the request and queues it on a live QTTabBar UI thread.
- Close `/factory -Embedding` windows only after that acknowledgement; otherwise keep and initialize the original window.
- Prune destroyed or stale Explorer list-view wrappers before reusing handles and recapture newly created shell views cleanly.
- Decode XButton input from the native mouse-hook payload and pass unhandled browser commands back to Explorer.
- Provide a German name, description, options dialog, search view, and context menu for the bundled Folder Memo plugin.
- Promoted to Stable after nearly three weeks without observed regressions on the production system.

## 1.6.4.0 Resume Handoff Hotfix - 2026-08-01

- Require an acknowledgement before closing a newly captured Explorer window.
- Keep and initialize the requested Explorer window when a stale named-pipe callback rejects the handoff after suspend or resume.

## 1.6.3.0 Infrastructure Baseline - 2026-08-01

- Bundle the pinned Microsoft Visual C++ 2015-2022 x86 and x64 redistributables.
- Pin MSVC 14.44.35207, Windows SDK 10.0.19041.0, MSBuild, WiX, and redistributable hashes.
- Add strict build-environment validation and a complete release-build entry point.

## 1.6.2.0 Stable - 2026-08-01

- Hardens MinHook thread enumeration, suspension, instruction-pointer migration, and resume handling for safer Explorer hook transitions.
- Flushes the instruction cache after enabling or disabling hooks and supports hooks that do not request an original-function pointer.
- Adds Win32 and x64 concurrent MinHook smoke tests covering repeated enable/disable cycles and null-original hooks.
- Keeps routine production diagnostics behind the existing opt-in logger while preserving forced warnings and exception reports.

## 1.6.0.0 Stable - 2026-07-14

- Updates the integrated German language resources from the corrected language file so the Options dialog labels line up with their intended controls again.
- Ships the restored native/managed stable baseline with the initial Explorer middle-click fix as the next stable release.

## 1.5.99.0 Stable - 2026-07-14

- Initializes the active Explorer folder view on first attach instead of only after a previous view existed, allowing File/Folder middle-click to resolve items immediately on the initial "This PC" page.

## 1.5.98.0 Stable - 2026-07-14

- Restores synchronous AutoLoader activation for the first Explorer instance and only records ActivationDate after ShowBrowserBar succeeds, preventing a missed timer from leaving the initial window without a real QTTabBar host.

## 1.5.97.0 Stable - 2026-07-14

- Falls back to the full Explorer window when the active ShellTabWindowClass has not exposed its initial SHELLDLL_DefView yet, matching the working upstream ListView discovery path more closely.
- Makes main ListView event registration idempotent during recapture so middle-click handlers cannot stack up or remain stale after view changes.

## 1.5.96.0 Stable - 2026-07-13

- Reads AutoHookWindow directly from the registry during hook startup so the first Explorer instance cannot fall back to background-renderer-only mode before config binding finishes.
- Captures the initial item view from the active ShellTabWindowClass container first, improving File/Folder middle-click handling on the initial "This PC" view.

## 1.5.95.0 Stable - 2026-07-13

- Keeps the full Explorer hook active when AutoHookWindow is enabled, even if the Explorer background renderer is also enabled.
- Prevents the background-renderer-only mode from suppressing ShellBrowser hooks needed by file and folder middle-click handling.

## 1.5.94.0 Stable - 2026-07-13

- Treats access-denied Shell Link target resolution as a recoverable condition, so protected or unreadable links no longer get misclassified as dead folder links.
- Falls back to the original shell item when a link target cannot be resolved instead of creating an empty PIDL wrapper.

## 1.5.93.0 Stable - 2026-07-13

- Reverts the 1.5.92 full-hook/background-mode experiment because it did not improve File/Folder middle-click capture and could add startup side effects.
- Restores the upstream niklas2233/v1.5.7 item-view middle-click dispatch semantics while keeping the folder-tree middle-click guard.

## 1.5.92.0 Stable - 2026-07-13

- Restores the upstream native-tab ListView lookup path so the active Explorer file view is captured immediately after startup.
- Loads the full hook library when AutoHookWindow is enabled instead of staying in background-renderer-only mode.

## 1.5.91.0 Stable - 2026-07-13

- Ports the robust native-tab ListView monitor from niklas2233/qttabbar v1.5.7 so Explorer item-view middle-clicks are handled by the direct view subclass immediately after startup.
- Removes the 1.5.90 startup recapture timer and global FolderView middle-click fallback that could block Explorer.

## 1.5.90.0 Stable - 2026-07-13

- Recaptures Explorer's item view shortly after startup and lazily before FolderView middle-click handling, so File/Folder middle-click no longer depends on first using the folder tree.

## 1.5.89.0 Stable - 2026-07-13

- Prevents stale serialized cross-process actions from blocking Explorer startup or flooding the exception log.
- Updates the internal QTTabBar version constant used by diagnostic logs.

## 1.5.88.0 Stable - 2026-07-13

- Fixes initial File/Folder middle-click handling by routing Explorer FolderView middle clicks before the TreeView fallback and limiting TreeView hit tests to real tree targets.

## 1.5.87.0 Stable - 2026-07-13

- Fixes File/Folder middle-click on DirectUI item views by honoring the configured mouse item action, accepting child-window hit targets, and falling back to Explorer's focused item when the hit test cannot resolve the clicked item.

## 1.5.86.0 Stable - 2026-07-13

- Fixes File/Folder middle-click actions in modern Explorer item views by consuming handled middle clicks and accepting DirectUI child-window hits.
- Reduces the first-start Versatile Bar white-panel flash by passing the fixed vertical bar size to Explorer when the bar is auto-shown.

## 1.5.85.0 Stable - 2026-07-10

- Fixes F2 rename-selection cycling so the tweak is enabled when checked and disabled when unchecked.
- Applies the same F2 rename-selection cycling behavior to the Desktop list view.

## 1.5.84.0 Stable - 2026-07-10

- Restores cursor-loop arrow-key selection when Explorer does not report a focused item yet.
- Splits tab text shadows into active, inactive, and mouseover state toggles.
- Clarifies the automatic tab text color override option in Appearance.
- Fixes the inverted "Cycle selection with F2 while renaming" tweak in the Explorer tab view.

## 1.5.83.0 Preview - 2026-07-06

- Starts Versatile Bar reordering directly from each icon or separator while the left mouse button is held.
- Uses the live Windows mouse-button state so ToolStrip item event routing cannot suppress the drag gesture.

## 1.5.82.0 Preview - 2026-07-05

- Adds drag-and-drop reordering for icons and separators on the vertical Versatile Bar.
- Shows a precise insertion marker while dragging and persists the customized item order.
- Lets externally dropped files and folders be inserted at a chosen position instead of always appending them.

## 1.5.81.0 Preview - 2026-07-05

- Adds configurable custom images and X/Y offsets for the tab close button and locked-tab icon.
- Supports a single close-button image or horizontal/vertical four-state image strips (normal, hover, pressed, alternate).
- Scales oversized images to the available tab height, supports PNG alpha and BMP magenta transparency, and safely falls back to built-in resources.

## 1.5.80.0 Preview - 2026-07-05

- Implements tab-skin content margins for text, folder icons, lock glyphs, and close buttons.
- Implements configurable tab overlap in single-row and multi-row layouts with matching draw-order hit testing.
- Implements alpha-aware hit testing against the rendered nine-slice tab skin and fixes overlap-value validation.

## 1.5.79.0 Stable - 2026-07-04

- Promotes the exact-window `BeginPaint`/`FillRect` Explorer background renderer to production.
- Removes periodic managed diagnostics, native paint counters, obsolete draw exports, and diagnostic build markers.
- Retains strict `DirectUIHWND` validation, safe hook rollback, crash reporting, and operational error logs.

## 1.5.78.0 Diagnostic - 2026-07-04

- Replaces the overwritten `WM_ERASEBKGND` draw with process API detours matching the proven ExplorerBgTool paint order.
- Draws immediately after DirectUI's own `FillRect`, before folders, files, and labels are painted.
- Restricts rendering to HDCs resolved to exact registered `DirectUIHWND` instances and fully rolls back partial hook installation.

## 1.5.77.0 Diagnostic - 2026-07-04

- Moves exact-window background rendering from the post-paint overlay to `WM_ERASEBKGND`.
- Draws after the default background erase and before Explorer paints folders, files, and labels.
- Uses the message-provided HDC, removes the post-paint redraw loop, and keeps all IAT hooks disabled.

## 1.5.76.0 Diagnostic - 2026-07-04

- Fixes exact-window registration for the hook-free after-paint renderer by checking the initialized renderer state rather than the obsolete installed-hook state.
- Preserves the 1.5.75 exact `DirectUIHWND` rendering path with all background IAT hooks disabled.

## 1.5.75.0 Diagnostic - 2026-07-03

- Replaces background-mode IAT interception with exact-window rendering dispatched by QTTabBar's existing `DirectUIHWND` after-paint message.
- Uses the WinSpy-confirmed registered file-list window and disables all scoped background IAT hooks.
- Adds after-paint render telemetry while retaining the WIC image loader and strict window validation.

## 1.5.74.0 Diagnostic - 2026-07-03

- Extends the isolated background renderer to scoped `FillRect` and `CreateCompatibleDC` imports in `shell32` and `ExplorerFrame`.
- Adds per-module paint telemetry plus image/paint overlap and clip-region diagnostics.
- Retains exact registered-window filtering and avoids process-wide API hooks.

## 1.5.73.0 Diagnostic - 2026-07-03

- Replaced the non-windowed `dui70!GetDC(NULL)` diagnostic path with scoped `shell32!BeginPaint` and `shell32!EndPaint` import hooks.
- Mirrors the reference renderer's DirectUI paint-DC association while retaining exact registered-window filtering.

## 1.5.72.0 Diagnostic - 2026-07-03

- Replaced the unrelated `ExplorerFrame!BeginPaint` import hook with scoped `dui70!GetDC` and `dui70!ReleaseDC` import hooks.
- Associates only exact registered `DirectUIHWND` DCs with the background renderer.
- Retains delayed telemetry to verify DC resolution and alpha blending in the target Windows 10 VM.

## 1.5.71.0 Diagnostic - 2026-07-03

- Added read-only native telemetry for the isolated Explorer background renderer.
- Logs delayed snapshots for `BeginPaint`, compatible DC propagation, `FillRect`, and `AlphaBlend` without changing hook targets or rendering behavior.
- Keeps 1.5.70.0 Stable as the rollback baseline.

## 1.5.70.0 Stable - 2026-07-03

- Adds a scoped `ExplorerFrame.dll` `BeginPaint` import patch to capture the real
  DirectUI paint DC without installing a process-wide hook
- Maps the captured paint DC to the already registered `DirectUIHWND`
- Keeps `FillRect` and `CreateCompatibleDC` patches limited to `dui70.dll`
- Retains the stable background-only state separation from 1.5.69

## 1.5.69.0 Stable - 2026-07-03

- Separates isolated background-renderer state from the legacy QTHookLib state
- Skips the invalid ShellBrowser-hook call in background-only mode
- Removes the additional `dui70.dll` `DeleteDC` patch not used by the reference implementation
- Replaces DeleteDC tracking with bounded, overwrite-on-create compatible-DC mapping

## 1.5.68.0 Stable - 2026-07-03

- Removed `std::random_device` from Explorer background renderer construction
- Uses a deterministic renderer seed so startup does not enter platform entropy code
- Added phase diagnostics for singleton creation, configuration, image enumeration,
  WIC decode, DIB creation, pixel copy, and state commit
- Logs the exact native renderer phase after both success and guarded failure

## 1.5.67.0 Stable - 2026-07-03

- Replaced the background renderer's GDI+ decoder with Windows Imaging Component
- Removed `GdiplusStartup`, raw `LockBits` access and manual source-stride traversal
- Decodes PNG/BMP/JPEG directly into validated premultiplied `32bppPBGRA` DIB surfaces
- Addresses the renderer-stage access violation observed on Windows 10 22H2 build 19045.5679
- Retains phased renderer/hook diagnostics and guarded `dui70.dll` activation

## 1.5.66.0 Stable - 2026-07-03

- Hardened `dui70.dll` PE/import parsing for later Windows 10 22H2 servicing builds
- Bounded every descriptor, thunk and import-name read to the loaded module image
- Guarded native IAT reads and writes against structured exceptions
- Split renderer initialization from IAT activation for precise managed diagnostics
- Added native export guards so unsupported layouts return an error instead of terminating Explorer

## 1.5.65.0 Stable - 2026-07-03

- Moved Explorer background ownership into the already loaded `QTTabBarNative.dll`
- Stopped loading the legacy monolithic `QTHookLib` in background-only mode
- Removed the background feature's file dependency on `QTHookLib32/64.dll`
- Preserved the three module-scoped `dui70.dll` import hooks and safe image renderer
- Added an architecture smoke test proving initialization succeeds with no legacy hook DLL

## 1.5.64.0 Stable - 2026-07-03

- Replaced the legacy Explorer background path with an encapsulated renderer
- Removed MinHook entirely from background-only initialization
- Limited interception to three `dui70.dll` import slots and registered `DirectUIHWND` views
- Added validated immutable image snapshots and safe live configuration reloads
- Added correct premultiplied PNG alpha rendering and HDC lifecycle tracking
- Added window-property tokens to prevent reused HWND values from matching stale state
- Added configurable image directories, random selection and path-specific images
- Added ESC startup recovery while keeping legacy Shell hooks disabled by default
- Preserved native HRESULT diagnostics across the managed bridge

## 1.5.62.0 Stable - 2026-07-02

- Consolidated the native and .NET Framework 4.8 QTTabBar code lines
- Stabilized toolbar and Explorer-bar COM registration on Windows 10
- Restored the vertical QT Command Bar / Versatile Bar
- Added fixed-width vertical-bar layout, shortcuts and separators
- Added Fluent Glass rendering with scoped Explorer integration
- Added custom new-tab button image configuration
- Fixed custom tab-image transparency and tab-bar layout gaps
- Restored and stabilized the options dialog
- Raised the configurable minimum tab width limit
- Added per-tab taskbar thumbnails, DWM live previews and thumbnail close handling
- Added shell icons and fallback previews for inactive tabs
- Added isolated Explorer background rendering from `config.ini`
- Improved Win+E capture, tab focus and toolbar persistence
- Removed release debug paths and reduced success-log noise
