#include "pch.h"
#include "AutoLoaderNative.h"

#include "InstanceManager.h"

#include <OleAuto.h>

#include <algorithm>
#include <array>
#include <vector>

namespace {
constexpr wchar_t kRegistryRoot[] = L"Software\\QTTabBar\\";
constexpr wchar_t kInstallDateValue[] = L"InstallDate";
constexpr wchar_t kActivationDateValue[] = L"ActivationDate";
constexpr wchar_t kAutoLoaderDescription[] = L"QTTabBar AutoLoader";
constexpr BYTE kVerticalCommandBarSize[] = {0x44, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};

// The tab host attaches to the Explorer instance when the band window is
// created in QTTabBarClass::OnCreate. Triggering ShowBrowserBar here is the
// native equivalent of the managed AutoLoader; Explorer instantiates the band
// synchronously, so TabBarHost::Initialize runs before we persist the
// activation stamp.
} // namespace

HRESULT AutoLoaderNative::ActivateForBrowser(IWebBrowser2* browser) {
    if(browser == nullptr) {
        return E_POINTER;
    }
    if(IsInternetExplorerProcess()) {
        return S_FALSE;
    }
    return ActivateExplorer(browser);
}

IFACEMETHODIMP AutoLoaderNative::SetSite(IUnknown* pUnkSite) {
    m_spExplorer.Release();
    m_spSite.Release();

    if(pUnkSite == nullptr) {
        InstanceManager::Instance().UnregisterAutoLoader(this);
        return S_OK;
    }

    m_spSite = pUnkSite;
    InstanceManager::Instance().RegisterAutoLoader(this);

    CComPtr<IServiceProvider> spServiceProvider;
    HRESULT hr = pUnkSite->QueryInterface(IID_PPV_ARGS(&spServiceProvider));
    if(SUCCEEDED(hr) && spServiceProvider) {
        hr = spServiceProvider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&m_spExplorer));
    }

    if(FAILED(hr) || !m_spExplorer) {
        hr = pUnkSite->QueryInterface(IID_PPV_ARGS(&m_spExplorer));
    }

    if(!m_spExplorer) {
        m_spSite.Release();
        return S_OK;
    }

    if(IsInternetExplorerProcess()) {
        return S_OK;
    }

    ActivateIt();
    return S_OK;
}

IFACEMETHODIMP AutoLoaderNative::GetSite(REFIID riid, void** ppvSite) {
    if(ppvSite == nullptr) {
        return E_POINTER;
    }
    if(!m_spSite) {
        *ppvSite = nullptr;
        return E_FAIL;
    }
    return m_spSite->QueryInterface(riid, ppvSite);
}

HRESULT AutoLoaderNative::ActivateIt() {
    return ActivateExplorer(m_spExplorer);
}

bool AutoLoaderNative::IsInternetExplorerProcess() {
    std::array<wchar_t, MAX_PATH> modulePath{};
    DWORD count = ::GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
    if(count == 0 || count >= modulePath.size()) {
        return false;
    }
    const wchar_t* fileName = ::PathFindFileNameW(modulePath.data());
    return fileName != nullptr && _wcsicmp(fileName, L"iexplore.exe") == 0;
}

HRESULT AutoLoaderNative::ActivateExplorer(IWebBrowser2* browser) {
    if(browser == nullptr) {
        return E_POINTER;
    }

    std::wstring installDate = ReadRegistryString(HKEY_LOCAL_MACHINE, kRegistryRoot, kInstallDateValue);
    if(installDate.empty()) {
        return S_FALSE;
    }

    CRegKey userKey;
    LONG status = userKey.Create(HKEY_CURRENT_USER, kRegistryRoot);
    if(status != ERROR_SUCCESS) {
        return HRESULT_FROM_WIN32(status);
    }

    std::wstring activationDate = ReadRegistryString(userKey, kActivationDateValue);
    auto installToken = ParseDate(installDate);
    auto activationToken = ParseDate(activationDate);
    if(installToken && activationToken && *installToken <= *activationToken) {
        return S_FALSE;
    }

    std::vector<HRESULT> results;
    results.reserve(4);

    results.push_back(ShowBrowserBar(browser, CLSID_QTTabBarClass));
    results.push_back(ShowBrowserBar(browser, CLSID_QTButtonBar));
    results.push_back(ShowBrowserBar(browser, CLSID_QCommandBarVertical));
    results.push_back(ShowBrowserBar(browser, CLSID_QTSecondViewBar));

    bool anyFailed = std::any_of(results.begin(), results.end(), [](HRESULT value) { return FAILED(value); });

    if(userKey.SetStringValue(kActivationDateValue, installDate.c_str()) != ERROR_SUCCESS) {
        ATLTRACE(L"AutoLoaderNative failed to persist activation timestamp\n");
    }

    if(anyFailed) {
        ATLTRACE(L"AutoLoaderNative::ActivateExplorer encountered ShowBrowserBar failures\n");
        constexpr wchar_t kMessage[] =
            L"QTTabBar could not automatically enable all toolbars.\n"
            L"Use Explorer's View > Toolbars menu to enable them manually.";
        ::MessageBoxW(nullptr, kMessage, kAutoLoaderDescription, MB_OK | MB_ICONWARNING);
    }

    return anyFailed ? S_FALSE : S_OK;
}

HRESULT AutoLoaderNative::ShowBrowserBar(IWebBrowser2* browser, REFGUID clsid) {
    if(browser == nullptr) {
        return E_POINTER;
    }

    CComBSTR clsidString;
    LPOLESTR rawString = nullptr;
    HRESULT hr = ::StringFromCLSID(clsid, &rawString);
    if(FAILED(hr) || rawString == nullptr) {
        return FAILED(hr) ? hr : E_FAIL;
    }
    clsidString.Attach(rawString);

    CComVariant barId(clsidString);
    CComVariant show(VARIANT_TRUE);
    CComVariant size;
    if(::InlineIsEqualGUID(clsid, CLSID_QCommandBarVertical)) {
        SAFEARRAYBOUND bound = {};
        bound.lLbound = 0;
        bound.cElements = static_cast<ULONG>(sizeof(kVerticalCommandBarSize));
        SAFEARRAY* array = ::SafeArrayCreate(VT_UI1, 1, &bound);
        if(array != nullptr) {
            void* data = nullptr;
            HRESULT accessHr = ::SafeArrayAccessData(array, &data);
            if(SUCCEEDED(accessHr)) {
                bool copied = false;
                if(data != nullptr) {
                    auto bytes = static_cast<BYTE*>(data);
                    for(size_t i = 0; i < sizeof(kVerticalCommandBarSize); ++i) {
                        bytes[i] = kVerticalCommandBarSize[i];
                    }
                    copied = true;
                }
                ::SafeArrayUnaccessData(array);
                if(copied) {
                    size.vt = VT_ARRAY | VT_UI1;
                    size.parray = array;
                }
                else {
                    ::SafeArrayDestroy(array);
                    size.vt = VT_EMPTY;
                }
            }
            else {
                ::SafeArrayDestroy(array);
                size.vt = VT_EMPTY;
            }
        }
    }
    else {
        size.vt = VT_EMPTY;
    }

    hr = browser->ShowBrowserBar(&barId, &show, &size);
    if(FAILED(hr)) {
        ATLTRACE(L"AutoLoaderNative::ShowBrowserBar failed for %s hr=0x%08X\n", static_cast<LPCWSTR>(clsidString), hr);
    }
    return hr;
}

std::wstring AutoLoaderNative::ReadRegistryString(HKEY root, const wchar_t* subKey, const wchar_t* valueName) {
    CRegKey key;
    if(key.Open(root, subKey, KEY_READ) != ERROR_SUCCESS) {
        return {};
    }
    return ReadRegistryString(key, valueName);
}

std::wstring AutoLoaderNative::ReadRegistryString(CRegKey& key, const wchar_t* valueName) {
    ULONG chars = 0;
    if(key.QueryStringValue(valueName, nullptr, &chars) != ERROR_SUCCESS || chars == 0) {
        return {};
    }
    std::wstring buffer(chars, L'\0');
    if(key.QueryStringValue(valueName, buffer.data(), &chars) != ERROR_SUCCESS) {
        return {};
    }
    if(!buffer.empty() && buffer.back() == L'\0') {
        buffer.pop_back();
    }
    return buffer;
}

std::optional<double> AutoLoaderNative::ParseDate(const std::wstring& value) {
    if(value.empty()) {
        return std::nullopt;
    }
    double token = 0.0;
    if(SUCCEEDED(::VarDateFromStr(value.c_str(), LOCALE_USER_DEFAULT, 0, &token))) {
        return token;
    }
    return std::nullopt;
}
