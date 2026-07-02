#pragma once

#include <memory>

#include <Windows.h>

namespace qttabbar {

struct ConfigData;

// OptionsDialog provides a modeless, tabbed configuration dialog hosted inside
// the native DLL. The dialog mirrors the managed OptionsDialog.xaml layout and
// runs on a dedicated STA thread so opening it does not block Explorer.
class OptionsDialog {
public:
    // Opens the options dialog. If an instance already exists it is activated.
    static void Open(HWND explorerWindow);

    // Forces any running options dialog to close immediately.
    static void ForceClose();

    // Returns true when an options dialog instance is already active.
    static bool IsOpen();

private:
    OptionsDialog() = default;
    ~OptionsDialog() = default;
    OptionsDialog(const OptionsDialog&) = delete;
    OptionsDialog& operator=(const OptionsDialog&) = delete;
};

}  // namespace qttabbar

