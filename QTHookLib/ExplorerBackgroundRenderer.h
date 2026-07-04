#pragma once

#include <windows.h>

namespace qttabbar {
namespace background {

HRESULT Initialize(HMODULE module);
void Shutdown() noexcept;
bool IsActive() noexcept;

HRESULT RegisterWindow(HWND window, const wchar_t* path);
HRESULT UpdateWindow(HWND window, const wchar_t* path);

void OnWindowDcAcquired(HWND window, HDC dc) noexcept;
void OnCompatibleDcCreated(HDC sourceDc, HDC compatibleDc) noexcept;
void OnDcDeleted(HDC dc) noexcept;
void OnFillRect(HDC targetDc, const RECT* paintedRect) noexcept;

}  // namespace background
}  // namespace qttabbar
