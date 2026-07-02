#include <windows.h>

extern "C" __declspec(dllexport) int __cdecl Initialize(void*) {
    RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    return 0;
}

extern "C" __declspec(dllexport) int __cdecl Dispose() {
    return 0;
}
