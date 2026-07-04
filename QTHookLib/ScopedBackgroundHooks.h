#pragma once

#include <windows.h>

namespace qttabbar {
namespace background {
namespace hooks {

HRESULT Install();
void Remove() noexcept;
bool IsInstalled() noexcept;

}  // namespace hooks
}  // namespace background
}  // namespace qttabbar
