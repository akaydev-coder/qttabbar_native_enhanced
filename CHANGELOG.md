# Changelog

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
