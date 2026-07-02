#pragma once

#include <windows.h>

#include <optional>
#include <string>

class TextInputDialog {
public:
    static std::optional<std::wstring> Show(HWND owner, const std::wstring& title, const std::wstring& prompt,
                                            const std::wstring& initialValue = L"");
};

