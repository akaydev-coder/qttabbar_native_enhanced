#include "pch.h"
#include "TextInputDialog.h"

#include "Resource.h"

#include <string>

namespace {

struct DialogState {
    std::wstring title;
    std::wstring prompt;
    std::wstring value;
    bool accepted = false;
};

INT_PTR CALLBACK TextInputProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<DialogState*>(::GetWindowLongPtr(hwnd, GWLP_USERDATA));
    switch(msg) {
    case WM_INITDIALOG: {
        state = reinterpret_cast<DialogState*>(lParam);
        ::SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        if(state != nullptr) {
            ::SetWindowTextW(hwnd, state->title.c_str());
            ::SetDlgItemTextW(hwnd, IDC_TEXT_PROMPT, state->prompt.c_str());
            ::SetDlgItemTextW(hwnd, IDC_TEXT_INPUT, state->value.c_str());
            HWND edit = ::GetDlgItem(hwnd, IDC_TEXT_INPUT);
            if(edit != nullptr) {
                ::SendMessageW(edit, EM_SETSEL, 0, -1);
                ::SetFocus(edit);
            }
        }
        return FALSE;
    }
    case WM_COMMAND:
        switch(LOWORD(wParam)) {
        case IDOK: {
            if(state != nullptr) {
                wchar_t buffer[512] = {};
                ::GetDlgItemTextW(hwnd, IDC_TEXT_INPUT, buffer, static_cast<int>(std::size(buffer)));
                state->value.assign(buffer);
                state->accepted = true;
            }
            ::EndDialog(hwnd, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            ::EndDialog(hwnd, IDCANCEL);
            return TRUE;
        default:
            break;
        }
        break;
    default:
        break;
    }
    return FALSE;
}

} // namespace

std::optional<std::wstring> TextInputDialog::Show(HWND owner, const std::wstring& title, const std::wstring& prompt,
                                                  const std::wstring& initialValue) {
    DialogState state{title, prompt, initialValue, false};
    INT_PTR result = ::DialogBoxParamW(_AtlBaseModule.GetResourceInstance(), MAKEINTRESOURCEW(IDD_TEXT_INPUT), owner,
                                       TextInputProc, reinterpret_cast<LPARAM>(&state));
    if(result == IDOK && state.accepted) {
        return state.value;
    }
    return std::nullopt;
}

