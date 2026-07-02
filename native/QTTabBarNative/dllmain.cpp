#include "pch.h"
#include "QTTabBarNativeGuids.h"
#include "resource.h"

class CQTTabBarNativeModule : public ATL::CAtlDllModuleT<CQTTabBarNativeModule>
{
public:
    DECLARE_LIBID(LIBID_QTTabBarNativeLib)
    DECLARE_REGISTRY_APPID_RESOURCEID(IDR_QTTABBARNATIVE, "{53A08D7D-6A5C-466E-B706-280BF1C4D775}")
};

CQTTabBarNativeModule _AtlModule;

extern "C" BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
    return _AtlModule.DllMain(dwReason, lpReserved);
}

STDAPI DllCanUnloadNow()
{
    return _AtlModule.DllCanUnloadNow();
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
    return _AtlModule.DllGetClassObject(rclsid, riid, ppv);
}

STDAPI DllRegisterServer()
{
    return _AtlModule.DllRegisterServer();
}

STDAPI DllUnregisterServer()
{
    return _AtlModule.DllUnregisterServer();
}

STDAPI DllInstall(BOOL bInstall, LPCWSTR /*pszCmdLine*/)
{
    return bInstall ? DllRegisterServer() : DllUnregisterServer();
}
