#include "ScopedBackgroundHooks.h"

#include "ExplorerBackgroundRenderer.h"
#include "../MinHook/MinHook.h"

#include <array>
#include <atomic>
#include <mutex>

namespace qttabbar {
namespace background {
namespace hooks {
namespace {

using FillRectFn = int (WINAPI*)(HDC, const RECT*, HBRUSH);
using CreateCompatibleDcFn = HDC (WINAPI*)(HDC);
using DeleteDcFn = BOOL (WINAPI*)(HDC);
using BeginPaintFn = HDC (WINAPI*)(HWND, LPPAINTSTRUCT);
using EndPaintFn = BOOL (WINAPI*)(HWND, const PAINTSTRUCT*);

std::mutex hookMutex;
std::atomic<bool> installed{false};
bool ownsMinHook = false;

FillRectFn originalFillRect = nullptr;
CreateCompatibleDcFn originalCreateCompatibleDc = nullptr;
DeleteDcFn originalDeleteDc = nullptr;
BeginPaintFn originalBeginPaint = nullptr;
EndPaintFn originalEndPaint = nullptr;

const std::array<LPVOID, 5> targets{{
    reinterpret_cast<LPVOID>(&::BeginPaint),
    reinterpret_cast<LPVOID>(&::CreateCompatibleDC),
    reinterpret_cast<LPVOID>(&::FillRect),
    reinterpret_cast<LPVOID>(&::EndPaint),
    reinterpret_cast<LPVOID>(&::DeleteDC),
}};

HDC WINAPI ScopedBeginPaint(HWND window, LPPAINTSTRUCT paint) {
    HDC const dc = originalBeginPaint != nullptr
        ? originalBeginPaint(window, paint)
        : nullptr;
    OnWindowDcAcquired(window, dc);
    return dc;
}

HDC WINAPI ScopedCreateCompatibleDc(HDC sourceDc) {
    HDC const compatibleDc = originalCreateCompatibleDc != nullptr
        ? originalCreateCompatibleDc(sourceDc)
        : nullptr;
    OnCompatibleDcCreated(sourceDc, compatibleDc);
    return compatibleDc;
}

int WINAPI ScopedFillRect(HDC dc, const RECT* rect, HBRUSH brush) {
    const int result = originalFillRect != nullptr
        ? originalFillRect(dc, rect, brush)
        : 0;
    if(result != 0) {
        static thread_local bool dispatching = false;
        if(!dispatching) {
            dispatching = true;
            OnFillRect(dc, rect);
            dispatching = false;
        }
    }
    return result;
}

BOOL WINAPI ScopedEndPaint(HWND window, const PAINTSTRUCT* paint) {
    const BOOL result = originalEndPaint != nullptr
        ? originalEndPaint(window, paint)
        : FALSE;
    if(paint != nullptr) {
        OnDcDeleted(paint->hdc);
    }
    return result;
}

BOOL WINAPI ScopedDeleteDc(HDC dc) {
    OnDcDeleted(dc);
    return originalDeleteDc != nullptr ? originalDeleteDc(dc) : FALSE;
}

HRESULT HookFailure(MH_STATUS status) noexcept {
    return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF,
        static_cast<USHORT>(0x300 + static_cast<unsigned int>(status)));
}

void ClearOriginals() noexcept {
    originalFillRect = nullptr;
    originalCreateCompatibleDc = nullptr;
    originalDeleteDc = nullptr;
    originalBeginPaint = nullptr;
    originalEndPaint = nullptr;
}

}  // namespace

HRESULT Install() {
    std::lock_guard<std::mutex> guard(hookMutex);
    if(installed.load(std::memory_order_acquire)) {
        return S_OK;
    }

    MH_STATUS status = MH_Initialize();
    if(status != MH_OK) {
        return HookFailure(status);
    }
    ownsMinHook = true;

    struct HookDefinition {
        LPVOID target;
        LPVOID detour;
        LPVOID* original;
    };
    const std::array<HookDefinition, 5> definitions{{
        {targets[0], reinterpret_cast<LPVOID>(&ScopedBeginPaint),
            reinterpret_cast<LPVOID*>(&originalBeginPaint)},
        {targets[1], reinterpret_cast<LPVOID>(&ScopedCreateCompatibleDc),
            reinterpret_cast<LPVOID*>(&originalCreateCompatibleDc)},
        {targets[2], reinterpret_cast<LPVOID>(&ScopedFillRect),
            reinterpret_cast<LPVOID*>(&originalFillRect)},
        {targets[3], reinterpret_cast<LPVOID>(&ScopedEndPaint),
            reinterpret_cast<LPVOID*>(&originalEndPaint)},
        {targets[4], reinterpret_cast<LPVOID>(&ScopedDeleteDc),
            reinterpret_cast<LPVOID*>(&originalDeleteDc)},
    }};

    std::size_t created = 0;
    for(; created < definitions.size(); ++created) {
        status = MH_CreateHook(definitions[created].target,
            definitions[created].detour, definitions[created].original);
        if(status != MH_OK) {
            break;
        }
    }
    if(created != definitions.size()) {
        MH_Uninitialize();
        ownsMinHook = false;
        ClearOriginals();
        return HookFailure(status);
    }

    std::size_t enabled = 0;
    for(; enabled < definitions.size(); ++enabled) {
        status = MH_EnableHook(definitions[enabled].target);
        if(status != MH_OK) {
            break;
        }
    }
    if(enabled != definitions.size()) {
        while(enabled > 0) {
            --enabled;
            MH_DisableHook(definitions[enabled].target);
        }
        MH_Uninitialize();
        ownsMinHook = false;
        ClearOriginals();
        return HookFailure(status);
    }

    installed.store(true, std::memory_order_release);
    return S_OK;
}

void Remove() noexcept {
    std::lock_guard<std::mutex> guard(hookMutex);
    if(!installed.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    for(std::size_t index = targets.size(); index > 0; --index) {
        MH_DisableHook(targets[index - 1]);
    }
    if(ownsMinHook) {
        MH_Uninitialize();
    }
    ownsMinHook = false;
    ClearOriginals();
}

bool IsInstalled() noexcept {
    return installed.load(std::memory_order_acquire);
}

}  // namespace hooks
}  // namespace background
}  // namespace qttabbar
