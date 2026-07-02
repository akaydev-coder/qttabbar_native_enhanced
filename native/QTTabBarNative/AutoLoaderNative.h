#pragma once

#include "QTTabBarNativeGuids.h"
#include "resource.h"

#include <atlbase.h>
#include <atlcom.h>
#include <exdisp.h>

#include <optional>
#include <string>

class ATL_NO_VTABLE AutoLoaderNative
    : public CComObjectRootEx<CComSingleThreadModel>
    , public CComCoClass<AutoLoaderNative, &CLSID_AutoLoaderNative>
    , public IObjectWithSite {
public:
    AutoLoaderNative() = default;
    ~AutoLoaderNative() = default;

    DECLARE_REGISTRY_RESOURCEID(IDR_AUTOLOADERNATIVE)
    DECLARE_NOT_AGGREGATABLE(AutoLoaderNative)

    BEGIN_COM_MAP(AutoLoaderNative)
        COM_INTERFACE_ENTRY(IObjectWithSite)
    END_COM_MAP()

    // IObjectWithSite
    IFACEMETHODIMP SetSite(IUnknown* pUnkSite) override;
    IFACEMETHODIMP GetSite(REFIID riid, void** ppvSite) override;

    static HRESULT ActivateForBrowser(IWebBrowser2* browser);

private:
    HRESULT ActivateIt();

    static bool IsInternetExplorerProcess();
    static HRESULT ActivateExplorer(IWebBrowser2* browser);
    static HRESULT ShowBrowserBar(IWebBrowser2* browser, REFGUID clsid);
    static std::wstring ReadRegistryString(HKEY root, const wchar_t* subKey, const wchar_t* valueName);
    static std::wstring ReadRegistryString(CRegKey& key, const wchar_t* valueName);
    static std::optional<double> ParseDate(const std::wstring& value);

    CComPtr<IUnknown> m_spSite;
    CComPtr<IWebBrowser2> m_spExplorer;
};
