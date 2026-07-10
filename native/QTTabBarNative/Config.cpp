#include "pch.h"

#include "Config.h"
#include "HookManagerNative.h"

#include <Windows.h>
#include <ShlObj.h>
#include <ShObjIdl.h>
#include <VersionHelpers.h>
#include <algorithm>
#include <combaseapi.h>
#include <cwctype>
#include <cstring>
#include <gdiplus.h>
#include <sstream>

#include "third_party/nlohmann/json.hpp"

#pragma comment(lib, "Gdiplus.lib")

namespace qttabbar {
namespace {

using nlohmann::json;

constexpr const wchar_t kRegRoot[] = L"Software\\QTTabBar\\Config";

struct GdiplusToken {
    ULONG_PTR token = 0;
    GdiplusToken() {
        Gdiplus::GdiplusStartupInput input;
        if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) {
            token = 0;
        }
    }
    ~GdiplusToken() {
        if (token != 0) {
            Gdiplus::GdiplusShutdown(token);
        }
    }
};

uint32_t MakeColor(uint32_t rgb) {
    return 0xFF000000 | (rgb & 0x00FFFFFF);
}

StringList DefaultTextExtensions() {
    return {
        L".txt", L".rtf", L".ini", L".inf", L".properties", L".ruleset", L".settings",
        L".cs", L".log", L".js", L".vbs", L".bat", L".cmd", L".sh", L".c", L".cpp",
        L".cc", L".h", L".rc", L".xml", L".yml", L".yaml", L".htm", L".html", L".mht",
        L".mhtml", L".shtml", L".hta", L".hxt", L".hxc", L".hhc", L".hhk", L".hhp",
        L".java", L".sql", L".csv", L".md", L".m", L".reg", L".wxl", L".wxs", L".py",
        L".rb", L".jsp", L".asp", L".php", L".aspx", L".resx", L".xaml", L".config",
        L".manifest", L".csproj", L".vbproj"
    };
}

StringList SplitSemicolonList(const std::wstring& value) {
    StringList result;
    size_t start = 0;
    while (start < value.size()) {
        size_t end = value.find(L';', start);
        if (end == std::wstring::npos) {
            end = value.size();
        }
        if (end > start) {
            std::wstring token = value.substr(start, end - start);
            if (!token.empty()) {
                std::transform(token.begin(), token.end(), token.begin(), ::towlower);
                result.push_back(token);
            }
        }
        start = end + 1;
    }
    return result;
}

StringList DefaultImageExtensions() {
    GdiplusToken token;
    UINT count = 0;
    UINT bytes = 0;
    StringList result;
    if (Gdiplus::GetImageDecodersSize(&count, &bytes) == Gdiplus::Ok && bytes != 0) {
        std::vector<uint8_t> buffer(bytes);
        auto* decoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
        if (Gdiplus::GetImageDecoders(count, bytes, decoders) == Gdiplus::Ok) {
            for (UINT i = 0; i < count; ++i) {
                const WCHAR* ext = decoders[i].FilenameExtension;
                if (!ext) continue;
                StringList parts = SplitSemicolonList(ext);
                result.insert(result.end(), parts.begin(), parts.end());
            }
        }
    }
    static const wchar_t kSupportedMovies[] =
        L".asx;.dvr-ms;.mp2;.flv;.mkv;.ts;.3g2;.3gp;.3gp2;.3gpp;.amr;.amv;.asf;.avi;.bdmv;.bik;"
        L".d2v;.divx;.drc;.dsa;.dsm;.dss;.dsv;.evo;.f4v;.flc;.fli;.flic;.flv;.hdmov;.ifo;.ivf;.m1v;.m2p;.m2t;"
        L".m2ts;.m2v;.m4b;.m4p;.m4v;.mkv;.mp2v;.mp4;.mp4v;.mpe;.mpeg;.mpg;.mpls;.mpv2;.mpv4;.mov;.mts;.ogm;.ogv;"
        L".pss;.pva;.qt;.ram;.ratdvd;.rm;.rmm;.rmvb;.roq;.rpm;.smil;.smk;.swf;.tp;.tpr;.ts;.vob;.vp6;.webm;.wm;.wmp;.wmv";
    StringList movieExt = SplitSemicolonList(kSupportedMovies);
    result.insert(result.end(), movieExt.begin(), movieExt.end());
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

}  // namespace

ByteVector PidlFromDisplayName(const wchar_t* name) {
    ByteVector result;
    PIDLIST_RELATIVE pidl = nullptr;
    ULONG eaten = 0;
    HRESULT hr = SHParseDisplayName(name, nullptr, &pidl, 0, &eaten);
    if (SUCCEEDED(hr) && pidl) {
        size_t size = ILGetSize(pidl);
        result.resize(size);
        std::copy_n(reinterpret_cast<const uint8_t*>(pidl), size, result.begin());
    }
    if (pidl) {
        CoTaskMemFree(pidl);
    }
    return result;
}

namespace {

bool ReadDwordValue(HKEY key, const wchar_t* name, DWORD* out) {
    DWORD data = 0;
    DWORD size = sizeof(DWORD);
    DWORD type = 0;
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, &type, &data, &size) == ERROR_SUCCESS) {
        *out = data;
        return true;
    }
    return false;
}

bool ReadStringValue(HKEY key, const wchar_t* name, std::wstring* out) {
    DWORD type = 0;
    DWORD size = 0;
    LONG status = RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, &type, nullptr, &size);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    std::wstring buffer;
    buffer.resize(size / sizeof(wchar_t));
    status = RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, &type, buffer.data(), &size);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    if (!buffer.empty() && buffer.back() == L'\0') {
        buffer.pop_back();
    }
    *out = std::move(buffer);
    return true;
}

void WriteDwordValue(HKEY key, const wchar_t* name, DWORD value) {
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

void WriteStringValue(HKEY key, const wchar_t* name, const std::wstring& value) {
    const BYTE* data = reinterpret_cast<const BYTE*>(value.c_str());
    DWORD size = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    RegSetValueExW(key, name, 0, REG_SZ, data, size);
}

void WriteJsonValue(HKEY key, const wchar_t* name, const json& j) {
    std::string utf8 = j.dump();
    int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), wide.data(), length);
    WriteStringValue(key, name, wide);
}

std::optional<json> ReadJsonValue(HKEY key, const wchar_t* name) {
    std::wstring str;
    if (!ReadStringValue(key, name, &str)) {
        return std::nullopt;
    }
    int length = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), utf8.data(), length, nullptr, nullptr);
    try {
        return json::parse(utf8);
    } catch (...) {
        return std::nullopt;
    }
}

ColorValue ParseColor(const json& j) {
    if (j.is_object()) {
        auto tryGet = [&](const char* key) -> std::optional<uint32_t> {
            auto it = j.find(key);
            if (it != j.end() && it->is_number()) {
                return static_cast<uint32_t>(it->get<uint64_t>());
            }
            return std::nullopt;
        };
        if (auto value = tryGet("Argb")) {
            return ColorValue(*value);
        }
        if (auto value = tryGet("Value")) {
            return ColorValue(*value);
        }
        if (auto value = tryGet("value")) {
            return ColorValue(*value);
        }
        if (j.contains("A") && j.contains("R") && j.contains("G") && j.contains("B")) {
            uint32_t value = (j["A"].get<uint32_t>() << 24) |
                              (j["R"].get<uint32_t>() << 16) |
                              (j["G"].get<uint32_t>() << 8) |
                              (j["B"].get<uint32_t>());
            return ColorValue(value);
        }
    } else if (j.is_number_unsigned()) {
        return ColorValue(static_cast<uint32_t>(j.get<uint64_t>()));
    }
    return ColorValue();
}

json SerializeColor(ColorValue color) {
    json j;
    j["Value"] = color.argb;
    j["value"] = color.argb;
    j["knownColor"] = 0;
    j["name"] = nullptr;
    j["state"] = color.argb == 0 ? 1 : 2;
    return j;
}

Padding ParsePadding(const json& j) {
    Padding padding;
    if (j.is_object()) {
        padding.left = j.value("Left", j.value("_left", 0));
        padding.top = j.value("Top", j.value("_top", 0));
        padding.right = j.value("Right", j.value("_right", 0));
        padding.bottom = j.value("Bottom", j.value("_bottom", 0));
    } else if (j.is_array() && j.size() == 4) {
        padding.left = j[0].get<int>();
        padding.top = j[1].get<int>();
        padding.right = j[2].get<int>();
        padding.bottom = j[3].get<int>();
    }
    return padding;
}

json SerializePadding(const Padding& padding) {
    json j;
    j["Left"] = padding.left;
    j["Top"] = padding.top;
    j["Right"] = padding.right;
    j["Bottom"] = padding.bottom;
    j["_all"] = false;
    j["_left"] = padding.left;
    j["_top"] = padding.top;
    j["_right"] = padding.right;
    j["_bottom"] = padding.bottom;
    return j;
}

FontConfig ParseFont(const json& j) {
    FontConfig font;
    if (j.is_object()) {
        auto extractString = [&](const char* primary, const char* alt) -> std::optional<std::wstring> {
            const char* keys[] = { primary, alt };
            for(const char* key : keys) {
                if(!key) {
                    continue;
                }
                auto it = j.find(key);
                if (it == j.end() || !it->is_string()) {
                    continue;
                }
                std::string utf8 = it->get<std::string>();
                int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
                std::wstring wide(len, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), wide.data(), len);
                return wide;
            }
            return std::nullopt;
        };
        if (auto family = extractString("FontName", "<FontName>k__BackingField")) {
            font.family = std::move(*family);
        } else if (auto name = extractString("Name", nullptr)) {
            font.family = std::move(*name);
        }
        auto extractFloat = [&](const char* primary, const char* alt, float fallback) {
            auto it = j.find(primary);
            if (it != j.end() && it->is_number()) {
                return it->get<float>();
            }
            if (alt) {
                auto altIt = j.find(alt);
                if (altIt != j.end() && altIt->is_number()) {
                    return altIt->get<float>();
                }
            }
            return fallback;
        };
        auto extractUInt = [&](const char* primary, const char* alt, uint32_t fallback) {
            auto it = j.find(primary);
            if (it != j.end() && it->is_number()) {
                return static_cast<uint32_t>(it->get<uint64_t>());
            }
            if (alt) {
                auto altIt = j.find(alt);
                if (altIt != j.end() && altIt->is_number()) {
                    return static_cast<uint32_t>(altIt->get<uint64_t>());
                }
            }
            return fallback;
        };
        font.size = extractFloat("FontSize", "<FontSize>k__BackingField", 9.0f);
        font.style = extractUInt("FontStyle", "<FontStyle>k__BackingField", 0);
    }
    return font;
}

json SerializeFont(const FontConfig& font) {
    json j;
    std::wstring name = font.family.empty() ? L"微软雅黑" : font.family;
    std::string utf8Name;
    int length = WideCharToMultiByte(CP_UTF8, 0, name.c_str(), static_cast<int>(name.size()), nullptr, 0, nullptr, nullptr);
    utf8Name.resize(length);
    WideCharToMultiByte(CP_UTF8, 0, name.c_str(), static_cast<int>(name.size()), utf8Name.data(), length, nullptr, nullptr);
    j["FontName"] = utf8Name;
    j["<FontName>k__BackingField"] = utf8Name;
    j["FontSize"] = font.size;
    j["<FontSize>k__BackingField"] = font.size;
    j["FontStyle"] = font.style;
    j["<FontStyle>k__BackingField"] = font.style;
    return j;
}

StringList ParseStringList(const json& j) {
    StringList list;
    if (j.is_array()) {
        for (const auto& item : j) {
            if (item.is_string()) {
                std::string utf8 = item.get<std::string>();
                int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
                std::wstring wide(len, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), wide.data(), len);
                list.push_back(std::move(wide));
            }
        }
    }
    return list;
}

json SerializeStringList(const StringList& list) {
    json j = json::array();
    for (const auto& item : list) {
        int len = WideCharToMultiByte(CP_UTF8, 0, item.c_str(), static_cast<int>(item.size()), nullptr, 0, nullptr, nullptr);
        std::string utf8(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, item.c_str(), static_cast<int>(item.size()), utf8.data(), len, nullptr, nullptr);
        j.push_back(utf8);
    }
    return j;
}

MouseActionMap ParseMouseActionMap(const json& j) {
    MouseActionMap map;
    if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            MouseChord key = static_cast<MouseChord>(std::stoul(it.key()));
            BindAction value = static_cast<BindAction>(it.value().get<uint32_t>());
            map[key] = value;
        }
    } else if (j.is_array()) {
        for (const auto& entry : j) {
            MouseChord key = static_cast<MouseChord>(entry.value("Key", 0));
            BindAction value = static_cast<BindAction>(entry.value("Value", 0));
            map[key] = value;
        }
    }
    return map;
}

json SerializeMouseActionMap(const MouseActionMap& map) {
    json j = json::array();
    for (const auto& pair : map) {
        json entry;
        entry["Key"] = static_cast<uint32_t>(pair.first);
        entry["Value"] = static_cast<uint32_t>(pair.second);
        j.push_back(entry);
    }
    return j;
}

IntVector ParseIntVector(const json& j) {
    IntVector vec;
    if (j.is_array()) {
        for (const auto& entry : j) {
            vec.push_back(entry.get<int>());
        }
    }
    return vec;
}


ByteVector ParseByteVector(const json& j) {
    ByteVector data;
    if (j.is_array()) {
        for (const auto& value : j) {
            int v = value.get<int>();
            data.push_back(static_cast<uint8_t>(std::clamp(v, 0, 255)));
        }
    }
    return data;
}

json SerializeByteVector(const ByteVector& data) {
    json j = json::array();
    for (uint8_t v : data) {
        j.push_back(static_cast<int>(v));
    }
    return j;
}
json SerializeIntVector(const IntVector& vec) {
    json j = json::array();
    for (int value : vec) {
        j.push_back(value);
    }
    return j;
}

std::map<std::wstring, IntVector> ParsePluginShortcuts(const json& j) {
    std::map<std::wstring, IntVector> map;
    auto parseEntry = [&](const json& entry) {
        if (!entry.is_object()) return;
        auto keyIt = entry.find("Key");
        auto valueIt = entry.find("Value");
        if (keyIt == entry.end() || !keyIt->is_string()) return;
        std::string keyUtf8 = keyIt->get<std::string>();
        int len = MultiByteToWideChar(CP_UTF8, 0, keyUtf8.c_str(), static_cast<int>(keyUtf8.size()), nullptr, 0);
        std::wstring key(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, keyUtf8.c_str(), static_cast<int>(keyUtf8.size()), key.data(), len);
        IntVector values = ParseIntVector(valueIt != entry.end() ? *valueIt : json::array());
        map.emplace(std::move(key), std::move(values));
    };
    if (j.is_array()) {
        for (const auto& entry : j) {
            parseEntry(entry);
        }
    } else if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            json entry;
            entry["Key"] = it.key();
            entry["Value"] = it.value();
            parseEntry(entry);
        }
    }
    return map;
}

json SerializePluginShortcuts(const std::map<std::wstring, IntVector>& map) {
    json j = json::array();
    for (const auto& pair : map) {
        int len = WideCharToMultiByte(CP_UTF8, 0, pair.first.c_str(), static_cast<int>(pair.first.size()), nullptr, 0, nullptr, nullptr);
        std::string key(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, pair.first.c_str(), static_cast<int>(pair.first.size()), key.data(), len, nullptr, nullptr);
        json entry;
        entry["Key"] = key;
        entry["Value"] = SerializeIntVector(pair.second);
        j.push_back(entry);
    }
    return j;
}

std::wstring ToLowerCopy(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) { return std::towlower(ch); });
    return value;
}

std::optional<TabPos> ParseTabPos(const std::wstring& value) {
    std::wstring lower = ToLowerCopy(value);
    if (lower == L"rightmost") return TabPos::Rightmost;
    if (lower == L"right") return TabPos::Right;
    if (lower == L"left") return TabPos::Left;
    if (lower == L"leftmost") return TabPos::Leftmost;
    if (lower == L"lastactive") return TabPos::LastActive;
    return std::nullopt;
}

std::wstring TabPosToString(TabPos pos) {
    switch (pos) {
        case TabPos::Rightmost:
            return L"Rightmost";
        case TabPos::Right:
            return L"Right";
        case TabPos::Left:
            return L"Left";
        case TabPos::Leftmost:
            return L"Leftmost";
        case TabPos::LastActive:
            return L"LastActive";
    }
    return L"Rightmost";
}

std::optional<StretchMode> ParseStretchMode(const std::wstring& value) {
    std::wstring lower = ToLowerCopy(value);
    if (lower == L"full") return StretchMode::Full;
    if (lower == L"real") return StretchMode::Real;
    if (lower == L"tile") return StretchMode::Tile;
    return std::nullopt;
}

std::wstring StretchModeToString(StretchMode mode) {
    switch (mode) {
        case StretchMode::Full:
            return L"Full";
        case StretchMode::Real:
            return L"Real";
        case StretchMode::Tile:
            return L"Tile";
    }
    return L"Tile";
}

int ValidateMinMax(int value, int minValue, int maxValue) {
    int a = (std::min)(minValue, maxValue);
    int b = (std::max)(minValue, maxValue);
    if (value < a) return a;
    if (value > b) return b;
    return value;
}

int HiWord(int value) {
    return static_cast<int>((static_cast<uint32_t>(value) >> 16) & 0xFFFF);
}

bool IsValidIdList(const ByteVector& data) {
    if (data.empty()) {
        return false;
    }
    PIDLIST_ABSOLUTE pidl = reinterpret_cast<PIDLIST_ABSOLUTE>(CoTaskMemAlloc(data.size()));
    if (!pidl) {
        return false;
    }
    memcpy(pidl, data.data(), data.size());
    PCUITEMID_CHILD child = nullptr;
    IShellFolder* folder = nullptr;
    HRESULT hr = SHBindToParent(pidl, IID_PPV_ARGS(&folder), &child);
    if (SUCCEEDED(hr) && folder) {
        folder->Release();
    }
    CoTaskMemFree(pidl);
    return SUCCEEDED(hr);
}

void EnsureValidShellPath(std::wstring& path) {
    if (path.empty()) {
        return;
    }
    PIDLIST_ABSOLUTE pidl = nullptr;
    ULONG eaten = 0;
    HRESULT hr = SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, &eaten);
    if (FAILED(hr) || !pidl) {
        path.clear();
    }
    if (pidl) {
        CoTaskMemFree(pidl);
    }
}

bool IsWindowsXpOrEarlier() {
    return !IsWindowsVistaOrGreater();
}

bool IsWindows7OrGreaterOs() {
    return IsWindows7OrGreater();
}




}  // namespace

ConfigData::ConfigData() {
    window.captureWeChatSelection = true;
    window.defaultLocation = PidlFromDisplayName(L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}");
    tweaks.altRowBackgroundColor = ColorValue(MakeColor(0xfaf5f1));
    tips.textExt = DefaultTextExtensions();
    tips.imageExt = DefaultImageExtensions();

    // Button indexes default mirrors managed behavior.
    if (LOBYTE(LOWORD(GetVersion())) < 6) {
        bbar.buttonIndexes = {1, 2, 3, 4, 5, 6, 7, 11, 13, 12, 14, 15, 21, 9, 20};
    } else {
        bbar.buttonIndexes = {3, 4, 5, 6, 7, 17, 11, 12, 14, 15, 13, 21, 9, 19, 10};
    }

    mouse.globalMouseActions = {
        {MouseChord::X1, BindAction::GoBack},
        {MouseChord::X2, BindAction::GoForward},
        {MouseChord::X1 | MouseChord::Ctrl, BindAction::GoFirst},
        {MouseChord::X2 | MouseChord::Ctrl, BindAction::GoLast},
    };
    mouse.tabActions = {
        {MouseChord::Middle, BindAction::CloseTab},
        {MouseChord::Ctrl | MouseChord::Left, BindAction::LockTab},
        {MouseChord::Double, BindAction::UpOneLevelTab},
    };
    mouse.barActions = {
        {MouseChord::Double, BindAction::NewTab},
        {MouseChord::Middle, BindAction::RestoreLastClosed},
        {MouseChord::Ctrl | MouseChord::Middle, BindAction::TearOffCurrent},
    };
    mouse.linkActions = {
        {MouseChord::None, BindAction::ItemsOpenInNewTabNoSel},
        {MouseChord::Middle, BindAction::ItemOpenInNewTab},
        {MouseChord::Ctrl | MouseChord::Middle, BindAction::ItemOpenInNewWindow},
    };
    mouse.itemActions = {
        {MouseChord::Middle, BindAction::ItemOpenInNewTab},
        {MouseChord::Ctrl | MouseChord::Middle, BindAction::ItemsOpenInNewTabNoSel},
    };
    mouse.marginActions = {
        {MouseChord::Double, BindAction::UpOneLevel},
        {MouseChord::Middle, BindAction::BrowseFolder},
        {MouseChord::Ctrl | MouseChord::Double, BindAction::OpenCmd},
        {MouseChord::Ctrl | MouseChord::Middle, BindAction::ItemsOpenInNewTabNoSel},
    };

    keys.shortcuts.resize(static_cast<size_t>(BindAction::KEYBOARD_ACTION_COUNT));
    auto setShortcut = [this](BindAction action, int value) {
        size_t index = static_cast<size_t>(action);
        if (index >= keys.shortcuts.size()) {
            keys.shortcuts.resize(index + 1);
        }
        keys.shortcuts[index] = value;
    };
    const int enabledFlag = 1 << 30;
    auto encode = [enabledFlag](int key) { return key | enabledFlag; };
    setShortcut(BindAction::GoBack, encode(VK_LEFT | (MOD_ALT << 16)));
    setShortcut(BindAction::GoForward, encode(VK_RIGHT | (MOD_ALT << 16)));
    setShortcut(BindAction::GoFirst, encode(VK_LEFT | (MOD_ALT << 16) | (MOD_CONTROL << 16)));
    setShortcut(BindAction::GoLast, encode(VK_RIGHT | (MOD_ALT << 16) | (MOD_CONTROL << 16)));
    setShortcut(BindAction::NextTab, encode(VK_TAB | (MOD_CONTROL << 16)));
    setShortcut(BindAction::PreviousTab, encode(VK_TAB | (MOD_CONTROL << 16) | (MOD_SHIFT << 16)));
    setShortcut(BindAction::NewTab, encode('T' | (MOD_CONTROL << 16)));
    setShortcut(BindAction::NewWindow, encode('T' | (MOD_CONTROL << 16) | (MOD_SHIFT << 16)));
    setShortcut(BindAction::CloseCurrent, encode('W' | (MOD_CONTROL << 16)));
    setShortcut(BindAction::CloseAllButCurrent, encode('W' | (MOD_CONTROL << 16) | (MOD_SHIFT << 16)));
    setShortcut(BindAction::RestoreLastClosed, encode('Z' | (MOD_CONTROL << 16) | (MOD_SHIFT << 16)));
    setShortcut(BindAction::BrowseFolder, encode('O' | (MOD_CONTROL << 16)));
    setShortcut(BindAction::ShowOptions, encode('O' | (MOD_ALT << 16)));
    setShortcut(BindAction::ShowToolbarMenu, encode(VK_OEM_COMMA | (MOD_ALT << 16)));
    setShortcut(BindAction::ShowTabMenuCurrent, encode(VK_OEM_PERIOD | (MOD_ALT << 16)));
    setShortcut(BindAction::ShowGroupMenu, encode('G' | (MOD_ALT << 16)));
    setShortcut(BindAction::ShowUserAppsMenu, encode('H' | (MOD_ALT << 16)));
    setShortcut(BindAction::ShowRecentTabsMenu, encode('U' | (MOD_ALT << 16)));
    setShortcut(BindAction::ShowRecentFilesMenu, encode('F' | (MOD_ALT << 16)));
    setShortcut(BindAction::NewFile, encode('N' | (MOD_CONTROL << 16) | (MOD_ALT << 16)));
    setShortcut(BindAction::CreateNewGroup, encode('D' | (MOD_CONTROL << 16)));

    plugin.enabled.clear();
    lang.pluginLangFiles.clear();
}

static std::wstring MakeCategoryPath(const wchar_t* category) {
    std::wstring path = kRegRoot;
    path.append(L"\\");
    path.append(category);
    return path;
}

void ReadWindowSettings(HKEY key, WindowSettings& settings) {
    DWORD value = 0;
    if (ReadDwordValue(key, L"CaptureNewWindows", &value)) settings.captureNewWindows = value != 0;
    if (ReadDwordValue(key, L"CaptureWeChatSelection", &value)) settings.captureWeChatSelection = value != 0;
    if (ReadDwordValue(key, L"RestoreSession", &value)) settings.restoreSession = value != 0;
    if (ReadDwordValue(key, L"RestoreOnlyLocked", &value)) settings.restoreOnlyLocked = value != 0;
    if (ReadDwordValue(key, L"CloseBtnClosesUnlocked", &value)) settings.closeBtnClosesUnlocked = value != 0;
    if (ReadDwordValue(key, L"CloseBtnClosesSingleTab", &value)) settings.closeBtnClosesSingleTab = value != 0;
    if (ReadDwordValue(key, L"TrayOnClose", &value)) settings.trayOnClose = value != 0;
    if (ReadDwordValue(key, L"TrayOnMinimize", &value)) settings.trayOnMinimize = value != 0;
    if (ReadDwordValue(key, L"AutoHookWindow", &value)) settings.autoHookWindow = value != 0;
    if (ReadDwordValue(key, L"ShowFailNavMsg", &value)) settings.showFailNavMsg = value != 0;
    if (auto jsonValue = ReadJsonValue(key, L"DefaultLocation")) {
        settings.defaultLocation = ParseByteVector(*jsonValue);
    }
}

void WriteWindowSettings(HKEY key, const WindowSettings& settings) {
    WriteDwordValue(key, L"CaptureNewWindows", (settings.captureNewWindows ? 1u : 0u));
    WriteDwordValue(key, L"CaptureWeChatSelection", (settings.captureWeChatSelection ? 1u : 0u));
    WriteDwordValue(key, L"RestoreSession", (settings.restoreSession ? 1u : 0u));
    WriteDwordValue(key, L"RestoreOnlyLocked", (settings.restoreOnlyLocked ? 1u : 0u));
    WriteDwordValue(key, L"CloseBtnClosesUnlocked", (settings.closeBtnClosesUnlocked ? 1u : 0u));
    WriteDwordValue(key, L"CloseBtnClosesSingleTab", (settings.closeBtnClosesSingleTab ? 1u : 0u));
    WriteDwordValue(key, L"TrayOnClose", (settings.trayOnClose ? 1u : 0u));
    WriteDwordValue(key, L"TrayOnMinimize", (settings.trayOnMinimize ? 1u : 0u));
    WriteDwordValue(key, L"AutoHookWindow", (settings.autoHookWindow ? 1u : 0u));
    WriteDwordValue(key, L"ShowFailNavMsg", (settings.showFailNavMsg ? 1u : 0u));
    json value = SerializeByteVector(settings.defaultLocation);
    WriteJsonValue(key, L"DefaultLocation", value);
}

void ReadTabsSettings(HKEY key, TabsSettings& settings) {
    DWORD value = 0;
    auto readTabPos = [&](const wchar_t* name, TabPos& field) {
        std::wstring str;
        if (ReadStringValue(key, name, &str)) {
            if (auto parsed = ParseTabPos(str)) {
                field = *parsed;
                return;
            }
        }
        if (ReadDwordValue(key, name, &value)) {
            field = static_cast<TabPos>(value);
        }
    };
    readTabPos(L"NewTabPosition", settings.newTabPosition);
    readTabPos(L"NextAfterClosed", settings.nextAfterClosed);
    if (ReadDwordValue(key, L"ActivateNewTab", &value)) settings.activateNewTab = value != 0;
    if (ReadDwordValue(key, L"NeverOpenSame", &value)) settings.neverOpenSame = value != 0;
    if (ReadDwordValue(key, L"RenameAmbTabs", &value)) settings.renameAmbTabs = value != 0;
    if (ReadDwordValue(key, L"DragOverTabOpensSDT", &value)) settings.dragOverTabOpensSDT = value != 0;
    if (ReadDwordValue(key, L"ShowFolderIcon", &value)) settings.showFolderIcon = value != 0;
    if (ReadDwordValue(key, L"ShowSubDirTipOnTab", &value)) settings.showSubDirTipOnTab = value != 0;
    if (ReadDwordValue(key, L"ShowDriveLetters", &value)) settings.showDriveLetters = value != 0;
    if (ReadDwordValue(key, L"ShowCloseButtons", &value)) settings.showCloseButtons = value != 0;
    if (ReadDwordValue(key, L"CloseBtnsWithAlt", &value)) settings.closeBtnsWithAlt = value != 0;
    if (ReadDwordValue(key, L"CloseBtnsOnHover", &value)) settings.closeBtnsOnHover = value != 0;
    if (ReadDwordValue(key, L"ShowNavButtons", &value)) settings.showNavButtons = value != 0;
    if (ReadDwordValue(key, L"NavButtonsOnRight", &value)) settings.navButtonsOnRight = value != 0;
    if (ReadDwordValue(key, L"MultipleTabRows", &value)) settings.multipleTabRows = value != 0;
    if (ReadDwordValue(key, L"ActiveTabOnBottomRow", &value)) settings.activeTabOnBottomRow = value != 0;
    if (ReadDwordValue(key, L"NeedPlusButton", &value)) settings.needPlusButton = value != 0;
}

void WriteTabsSettings(HKEY key, const TabsSettings& settings) {
    WriteStringValue(key, L"NewTabPosition", TabPosToString(settings.newTabPosition));
    WriteStringValue(key, L"NextAfterClosed", TabPosToString(settings.nextAfterClosed));
    WriteDwordValue(key, L"ActivateNewTab", (settings.activateNewTab ? 1u : 0u));
    WriteDwordValue(key, L"NeverOpenSame", (settings.neverOpenSame ? 1u : 0u));
    WriteDwordValue(key, L"RenameAmbTabs", (settings.renameAmbTabs ? 1u : 0u));
    WriteDwordValue(key, L"DragOverTabOpensSDT", (settings.dragOverTabOpensSDT ? 1u : 0u));
    WriteDwordValue(key, L"ShowFolderIcon", (settings.showFolderIcon ? 1u : 0u));
    WriteDwordValue(key, L"ShowSubDirTipOnTab", (settings.showSubDirTipOnTab ? 1u : 0u));
    WriteDwordValue(key, L"ShowDriveLetters", (settings.showDriveLetters ? 1u : 0u));
    WriteDwordValue(key, L"ShowCloseButtons", (settings.showCloseButtons ? 1u : 0u));
    WriteDwordValue(key, L"CloseBtnsWithAlt", (settings.closeBtnsWithAlt ? 1u : 0u));
    WriteDwordValue(key, L"CloseBtnsOnHover", (settings.closeBtnsOnHover ? 1u : 0u));
    WriteDwordValue(key, L"ShowNavButtons", (settings.showNavButtons ? 1u : 0u));
    WriteDwordValue(key, L"NavButtonsOnRight", (settings.navButtonsOnRight ? 1u : 0u));
    WriteDwordValue(key, L"MultipleTabRows", (settings.multipleTabRows ? 1u : 0u));
    WriteDwordValue(key, L"ActiveTabOnBottomRow", (settings.activeTabOnBottomRow ? 1u : 0u));
    WriteDwordValue(key, L"NeedPlusButton", (settings.needPlusButton ? 1u : 0u));
}

void ReadTweaksSettings(HKEY key, TweaksSettings& settings) {
    DWORD value = 0;
    if (ReadDwordValue(key, L"AlwaysShowHeaders", &value)) settings.alwaysShowHeaders = value != 0;
    if (ReadDwordValue(key, L"KillExtWhileRenaming", &value)) settings.killExtWhileRenaming = value != 0;
    if (ReadDwordValue(key, L"RedirectLibraryFolders", &value)) settings.redirectLibraryFolders = value != 0;
    if (ReadDwordValue(key, L"F2Selection", &value)) settings.f2Selection = value != 0;
    if (ReadDwordValue(key, L"WrapArrowKeySelection", &value)) settings.wrapArrowKeySelection = value != 0;
    if (ReadDwordValue(key, L"BackspaceUpLevel", &value)) settings.backspaceUpLevel = value != 0;
    if (ReadDwordValue(key, L"HorizontalScroll", &value)) settings.horizontalScroll = value != 0;
    if (ReadDwordValue(key, L"ForceSysListView", &value)) settings.forceSysListView = value != 0;
    if (ReadDwordValue(key, L"ToggleFullRowSelect", &value)) settings.toggleFullRowSelect = value != 0;
    if (ReadDwordValue(key, L"DetailsGridLines", &value)) settings.detailsGridLines = value != 0;
    if (ReadDwordValue(key, L"AlternateRowColors", &value)) settings.alternateRowColors = value != 0;
    if (auto jsonValue = ReadJsonValue(key, L"AltRowBackgroundColor")) settings.altRowBackgroundColor = ParseColor(*jsonValue);
    if (auto jsonValue = ReadJsonValue(key, L"AltRowForegroundColor")) settings.altRowForegroundColor = ParseColor(*jsonValue);
}

void WriteTweaksSettings(HKEY key, const TweaksSettings& settings) {
    WriteDwordValue(key, L"AlwaysShowHeaders", (settings.alwaysShowHeaders ? 1u : 0u));
    WriteDwordValue(key, L"KillExtWhileRenaming", (settings.killExtWhileRenaming ? 1u : 0u));
    WriteDwordValue(key, L"RedirectLibraryFolders", (settings.redirectLibraryFolders ? 1u : 0u));
    WriteDwordValue(key, L"F2Selection", (settings.f2Selection ? 1u : 0u));
    WriteDwordValue(key, L"WrapArrowKeySelection", (settings.wrapArrowKeySelection ? 1u : 0u));
    WriteDwordValue(key, L"BackspaceUpLevel", (settings.backspaceUpLevel ? 1u : 0u));
    WriteDwordValue(key, L"HorizontalScroll", (settings.horizontalScroll ? 1u : 0u));
    WriteDwordValue(key, L"ForceSysListView", (settings.forceSysListView ? 1u : 0u));
    WriteDwordValue(key, L"ToggleFullRowSelect", (settings.toggleFullRowSelect ? 1u : 0u));
    WriteDwordValue(key, L"DetailsGridLines", (settings.detailsGridLines ? 1u : 0u));
    WriteDwordValue(key, L"AlternateRowColors", (settings.alternateRowColors ? 1u : 0u));
    WriteJsonValue(key, L"AltRowBackgroundColor", SerializeColor(settings.altRowBackgroundColor));
    WriteJsonValue(key, L"AltRowForegroundColor", SerializeColor(settings.altRowForegroundColor));
}

void ReadTipsSettings(HKEY key, TipsSettings& settings) {
    DWORD value = 0;
    if (ReadDwordValue(key, L"ShowSubDirTips", &value)) settings.showSubDirTips = value != 0;
    if (ReadDwordValue(key, L"SubDirTipsPreview", &value)) settings.subDirTipsPreview = value != 0;
    if (ReadDwordValue(key, L"SubDirTipsFiles", &value)) settings.subDirTipsFiles = value != 0;
    if (ReadDwordValue(key, L"SubDirTipsWithShift", &value)) settings.subDirTipsWithShift = value != 0;
    if (ReadDwordValue(key, L"ShowTooltipPreviews", &value)) settings.showTooltipPreviews = value != 0;
    if (ReadDwordValue(key, L"ShowPreviewsWithShift", &value)) settings.showPreviewsWithShift = value != 0;
    if (ReadDwordValue(key, L"ShowPreviewInfo", &value)) settings.showPreviewInfo = value != 0;
    if (ReadDwordValue(key, L"PreviewMaxWidth", &value)) settings.previewMaxWidth = static_cast<int>(value);
    if (ReadDwordValue(key, L"PreviewMaxHeight", &value)) settings.previewMaxHeight = static_cast<int>(value);
    if (auto jsonValue = ReadJsonValue(key, L"PreviewFont")) settings.previewFont = ParseFont(*jsonValue);
    if (auto jsonValue = ReadJsonValue(key, L"TextExt")) settings.textExt = ParseStringList(*jsonValue);
    if (auto jsonValue = ReadJsonValue(key, L"ImageExt")) settings.imageExt = ParseStringList(*jsonValue);
}

void WriteTipsSettings(HKEY key, const TipsSettings& settings) {
    WriteDwordValue(key, L"ShowSubDirTips", (settings.showSubDirTips ? 1u : 0u));
    WriteDwordValue(key, L"SubDirTipsPreview", (settings.subDirTipsPreview ? 1u : 0u));
    WriteDwordValue(key, L"SubDirTipsFiles", (settings.subDirTipsFiles ? 1u : 0u));
    WriteDwordValue(key, L"SubDirTipsWithShift", (settings.subDirTipsWithShift ? 1u : 0u));
    WriteDwordValue(key, L"ShowTooltipPreviews", (settings.showTooltipPreviews ? 1u : 0u));
    WriteDwordValue(key, L"ShowPreviewsWithShift", (settings.showPreviewsWithShift ? 1u : 0u));
    WriteDwordValue(key, L"ShowPreviewInfo", (settings.showPreviewInfo ? 1u : 0u));
    WriteDwordValue(key, L"PreviewMaxWidth", static_cast<DWORD>(settings.previewMaxWidth));
    WriteDwordValue(key, L"PreviewMaxHeight", static_cast<DWORD>(settings.previewMaxHeight));
    WriteJsonValue(key, L"PreviewFont", SerializeFont(settings.previewFont));
    WriteJsonValue(key, L"TextExt", SerializeStringList(settings.textExt));
    WriteJsonValue(key, L"ImageExt", SerializeStringList(settings.imageExt));
}

void ReadMiscSettings(HKEY key, MiscSettings& settings) {
    DWORD value = 0;
    if (ReadDwordValue(key, L"TaskbarThumbnails", &value)) settings.taskbarThumbnails = value != 0;
    if (ReadDwordValue(key, L"KeepHistory", &value)) settings.keepHistory = value != 0;
    if (ReadDwordValue(key, L"TabHistoryCount", &value)) settings.tabHistoryCount = static_cast<int>(value);
    if (ReadDwordValue(key, L"KeepRecentFiles", &value)) settings.keepRecentFiles = value != 0;
    if (ReadDwordValue(key, L"FileHistoryCount", &value)) settings.fileHistoryCount = static_cast<int>(value);
    if (ReadDwordValue(key, L"NetworkTimeout", &value)) settings.networkTimeout = static_cast<int>(value);
    if (ReadDwordValue(key, L"AutoUpdate", &value)) settings.autoUpdate = value != 0;
    if (ReadDwordValue(key, L"SoundBox", &value)) settings.soundBox = value != 0;
    if (ReadDwordValue(key, L"EnableLog", &value)) settings.enableLog = value != 0;
}

void WriteMiscSettings(HKEY key, const MiscSettings& settings) {
    WriteDwordValue(key, L"TaskbarThumbnails", (settings.taskbarThumbnails ? 1u : 0u));
    WriteDwordValue(key, L"KeepHistory", (settings.keepHistory ? 1u : 0u));
    WriteDwordValue(key, L"TabHistoryCount", static_cast<DWORD>(settings.tabHistoryCount));
    WriteDwordValue(key, L"KeepRecentFiles", (settings.keepRecentFiles ? 1u : 0u));
    WriteDwordValue(key, L"FileHistoryCount", static_cast<DWORD>(settings.fileHistoryCount));
    WriteDwordValue(key, L"NetworkTimeout", static_cast<DWORD>(settings.networkTimeout));
    WriteDwordValue(key, L"AutoUpdate", (settings.autoUpdate ? 1u : 0u));
    WriteDwordValue(key, L"SoundBox", (settings.soundBox ? 1u : 0u));
    WriteDwordValue(key, L"EnableLog", (settings.enableLog ? 1u : 0u));
}

void ReadSkinSettings(HKEY key, SkinSettings& settings) {
    DWORD value = 0;
    if (ReadDwordValue(key, L"UseTabSkin", &value)) settings.useTabSkin = value != 0;
    ReadStringValue(key, L"TabImageFile", &settings.tabImageFile);
    if (auto jsonValue = ReadJsonValue(key, L"TabSizeMargin")) settings.tabSizeMargin = ParsePadding(*jsonValue);
    if (auto jsonValue = ReadJsonValue(key, L"TabContentMargin")) settings.tabContentMargin = ParsePadding(*jsonValue);
    if (ReadDwordValue(key, L"OverlapPixels", &value)) settings.overlapPixels = static_cast<int>(value);
    if (ReadDwordValue(key, L"HitTestTransparent", &value)) settings.hitTestTransparent = value != 0;
    if (ReadDwordValue(key, L"TabHeight", &value)) settings.tabHeight = static_cast<int>(value);
    if (ReadDwordValue(key, L"TabMinWidth", &value)) settings.tabMinWidth = static_cast<int>(value);
    if (ReadDwordValue(key, L"TabMaxWidth", &value)) settings.tabMaxWidth = static_cast<int>(value);
    if (ReadDwordValue(key, L"FixedWidthTabs", &value)) settings.fixedWidthTabs = value != 0;
    if (auto jsonValue = ReadJsonValue(key, L"TabTextFont")) settings.tabTextFont = ParseFont(*jsonValue);
    if (auto jsonValue = ReadJsonValue(key, L"ToolBarTextColor")) settings.toolBarTextColor = ParseColor(*jsonValue);
    if (auto jsonValue = ReadJsonValue(key, L"TabTextActiveColor")) settings.tabTextActiveColor = ParseColor(*jsonValue);
    if (auto jsonValue = ReadJsonValue(key, L"TabTextInactiveColor")) settings.tabTextInactiveColor = ParseColor(*jsonValue);
    if (auto jsonValue = ReadJsonValue(key, L"TabTextHotColor")) settings.tabTextHotColor = ParseColor(*jsonValue);
    if (auto jsonValue = ReadJsonValue(key, L"TabShadActiveColor")) settings.tabShadActiveColor = ParseColor(*jsonValue);
    if (auto jsonValue = ReadJsonValue(key, L"TabShadInactiveColor")) settings.tabShadInactiveColor = ParseColor(*jsonValue);
    if (auto jsonValue = ReadJsonValue(key, L"TabShadHotColor")) settings.tabShadHotColor = ParseColor(*jsonValue);
    if (ReadDwordValue(key, L"TabTitleShadows", &value)) settings.tabTitleShadows = value != 0;
    if (ReadDwordValue(key, L"TabActiveTitleShadow", &value)) settings.tabActiveTitleShadow = value != 0;
    if (ReadDwordValue(key, L"TabInactiveTitleShadow", &value)) settings.tabInactiveTitleShadow = value != 0;
    if (ReadDwordValue(key, L"TabHotTitleShadow", &value)) settings.tabHotTitleShadow = value != 0;
    if (ReadDwordValue(key, L"TabTextCentered", &value)) settings.tabTextCentered = value != 0;
    if (ReadDwordValue(key, L"UseRebarBGColor", &value)) settings.useRebarBGColor = value != 0;
    if (auto jsonValue = ReadJsonValue(key, L"RebarColor")) settings.rebarColor = ParseColor(*jsonValue);
    if (ReadDwordValue(key, L"UseRebarImage", &value)) settings.useRebarImage = value != 0;
    std::wstring stretch;
    if (ReadStringValue(key, L"RebarStretchMode", &stretch)) {
        if (auto mode = ParseStretchMode(stretch)) settings.rebarStretchMode = *mode;
    } else if (ReadDwordValue(key, L"RebarStretchMode", &value)) {
        settings.rebarStretchMode = static_cast<StretchMode>(value);
    }
    ReadStringValue(key, L"RebarImageFile", &settings.rebarImageFile);
    if (ReadDwordValue(key, L"RebarImageSeperateBars", &value)) settings.rebarImageSeperateBars = value != 0;
    if (auto jsonValue = ReadJsonValue(key, L"RebarSizeMargin")) settings.rebarSizeMargin = ParsePadding(*jsonValue);
    if (ReadDwordValue(key, L"ActiveTabInBold", &value)) settings.activeTabInBold = value != 0;
    if (ReadDwordValue(key, L"SkinAutoColorChangeClose", &value)) settings.skinAutoColorChangeClose = value != 0;
    else if (ReadDwordValue(key, L"SkinAutoColorChange", &value)) settings.skinAutoColorChangeClose = value != 0;
    if (ReadDwordValue(key, L"DrawHorizontalExplorerBarBgColor", &value)) settings.drawHorizontalExplorerBarBgColor = value != 0;
    if (ReadDwordValue(key, L"DrawVerticalExplorerBarBgColor", &value)) settings.drawVerticalExplorerBarBgColor = value != 0;
}

void WriteSkinSettings(HKEY key, const SkinSettings& settings) {
    WriteDwordValue(key, L"UseTabSkin", (settings.useTabSkin ? 1u : 0u));
    WriteStringValue(key, L"TabImageFile", settings.tabImageFile);
    WriteJsonValue(key, L"TabSizeMargin", SerializePadding(settings.tabSizeMargin));
    WriteJsonValue(key, L"TabContentMargin", SerializePadding(settings.tabContentMargin));
    WriteDwordValue(key, L"OverlapPixels", static_cast<DWORD>(settings.overlapPixels));
    WriteDwordValue(key, L"HitTestTransparent", (settings.hitTestTransparent ? 1u : 0u));
    WriteDwordValue(key, L"TabHeight", static_cast<DWORD>(settings.tabHeight));
    WriteDwordValue(key, L"TabMinWidth", static_cast<DWORD>(settings.tabMinWidth));
    WriteDwordValue(key, L"TabMaxWidth", static_cast<DWORD>(settings.tabMaxWidth));
    WriteDwordValue(key, L"FixedWidthTabs", (settings.fixedWidthTabs ? 1u : 0u));
    WriteJsonValue(key, L"TabTextFont", SerializeFont(settings.tabTextFont));
    WriteJsonValue(key, L"ToolBarTextColor", SerializeColor(settings.toolBarTextColor));
    WriteJsonValue(key, L"TabTextActiveColor", SerializeColor(settings.tabTextActiveColor));
    WriteJsonValue(key, L"TabTextInactiveColor", SerializeColor(settings.tabTextInactiveColor));
    WriteJsonValue(key, L"TabTextHotColor", SerializeColor(settings.tabTextHotColor));
    WriteJsonValue(key, L"TabShadActiveColor", SerializeColor(settings.tabShadActiveColor));
    WriteJsonValue(key, L"TabShadInactiveColor", SerializeColor(settings.tabShadInactiveColor));
    WriteJsonValue(key, L"TabShadHotColor", SerializeColor(settings.tabShadHotColor));
    WriteDwordValue(key, L"TabTitleShadows", (settings.tabTitleShadows ? 1u : 0u));
    WriteDwordValue(key, L"TabActiveTitleShadow", (settings.tabActiveTitleShadow ? 1u : 0u));
    WriteDwordValue(key, L"TabInactiveTitleShadow", (settings.tabInactiveTitleShadow ? 1u : 0u));
    WriteDwordValue(key, L"TabHotTitleShadow", (settings.tabHotTitleShadow ? 1u : 0u));
    WriteDwordValue(key, L"TabTextCentered", (settings.tabTextCentered ? 1u : 0u));
    WriteDwordValue(key, L"UseRebarBGColor", (settings.useRebarBGColor ? 1u : 0u));
    WriteJsonValue(key, L"RebarColor", SerializeColor(settings.rebarColor));
    WriteDwordValue(key, L"UseRebarImage", (settings.useRebarImage ? 1u : 0u));
    WriteStringValue(key, L"RebarStretchMode", StretchModeToString(settings.rebarStretchMode));
    WriteStringValue(key, L"RebarImageFile", settings.rebarImageFile);
    WriteDwordValue(key, L"RebarImageSeperateBars", (settings.rebarImageSeperateBars ? 1u : 0u));
    WriteJsonValue(key, L"RebarSizeMargin", SerializePadding(settings.rebarSizeMargin));
    WriteDwordValue(key, L"ActiveTabInBold", (settings.activeTabInBold ? 1u : 0u));
    WriteDwordValue(key, L"SkinAutoColorChangeClose", (settings.skinAutoColorChangeClose ? 1u : 0u));
    WriteDwordValue(key, L"SkinAutoColorChange", (settings.skinAutoColorChangeClose ? 1u : 0u));
    WriteDwordValue(key, L"DrawHorizontalExplorerBarBgColor", (settings.drawHorizontalExplorerBarBgColor ? 1u : 0u));
    WriteDwordValue(key, L"DrawVerticalExplorerBarBgColor", (settings.drawVerticalExplorerBarBgColor ? 1u : 0u));
}

void ReadBBarSettings(HKEY key, BBarSettings& settings) {
    if (auto jsonValue = ReadJsonValue(key, L"ButtonIndexes")) settings.buttonIndexes = ParseIntVector(*jsonValue);
    if (auto jsonValue = ReadJsonValue(key, L"ActivePluginIDs")) settings.activePluginIDs = ParseStringList(*jsonValue);
    DWORD value = 0;
    if (ReadDwordValue(key, L"LargeButtons", &value)) settings.largeButtons = value != 0;
    if (ReadDwordValue(key, L"LockSearchBarWidth", &value)) settings.lockSearchBarWidth = value != 0;
    if (ReadDwordValue(key, L"LockDropDownButtons", &value)) settings.lockDropDownButtons = value != 0;
    if (ReadDwordValue(key, L"ShowButtonLabels", &value)) settings.showButtonLabels = value != 0;
    ReadStringValue(key, L"ImageStripPath", &settings.imageStripPath);
}

void WriteBBarSettings(HKEY key, const BBarSettings& settings) {
    WriteJsonValue(key, L"ButtonIndexes", SerializeIntVector(settings.buttonIndexes));
    WriteJsonValue(key, L"ActivePluginIDs", SerializeStringList(settings.activePluginIDs));
    WriteDwordValue(key, L"LargeButtons", (settings.largeButtons ? 1u : 0u));
    WriteDwordValue(key, L"LockSearchBarWidth", (settings.lockSearchBarWidth ? 1u : 0u));
    WriteDwordValue(key, L"LockDropDownButtons", (settings.lockDropDownButtons ? 1u : 0u));
    WriteDwordValue(key, L"ShowButtonLabels", (settings.showButtonLabels ? 1u : 0u));
    WriteStringValue(key, L"ImageStripPath", settings.imageStripPath);
}

void ReadMouseSettings(HKEY key, MouseSettings& settings) {
    DWORD value = 0;
    if (ReadDwordValue(key, L"MouseScrollsHotWnd", &value)) settings.mouseScrollsHotWnd = value != 0;
    if (auto jsonValue = ReadJsonValue(key, L"GlobalMouseActions")) settings.globalMouseActions = ParseMouseActionMap(*jsonValue);
    if (auto jsonValue = ReadJsonValue(key, L"TabActions")) settings.tabActions = ParseMouseActionMap(*jsonValue);
    if (auto jsonValue = ReadJsonValue(key, L"BarActions")) settings.barActions = ParseMouseActionMap(*jsonValue);
    if (auto jsonValue = ReadJsonValue(key, L"LinkActions")) settings.linkActions = ParseMouseActionMap(*jsonValue);
    if (auto jsonValue = ReadJsonValue(key, L"ItemActions")) settings.itemActions = ParseMouseActionMap(*jsonValue);
    if (auto jsonValue = ReadJsonValue(key, L"MarginActions")) settings.marginActions = ParseMouseActionMap(*jsonValue);
}

void WriteMouseSettings(HKEY key, const MouseSettings& settings) {
    WriteDwordValue(key, L"MouseScrollsHotWnd", (settings.mouseScrollsHotWnd ? 1u : 0u));
    WriteJsonValue(key, L"GlobalMouseActions", SerializeMouseActionMap(settings.globalMouseActions));
    WriteJsonValue(key, L"TabActions", SerializeMouseActionMap(settings.tabActions));
    WriteJsonValue(key, L"BarActions", SerializeMouseActionMap(settings.barActions));
    WriteJsonValue(key, L"LinkActions", SerializeMouseActionMap(settings.linkActions));
    WriteJsonValue(key, L"ItemActions", SerializeMouseActionMap(settings.itemActions));
    WriteJsonValue(key, L"MarginActions", SerializeMouseActionMap(settings.marginActions));
}

void ReadKeysSettings(HKEY key, KeysSettings& settings) {
    if (auto jsonValue = ReadJsonValue(key, L"Shortcuts")) settings.shortcuts = ParseIntVector(*jsonValue);
    if (auto jsonValue = ReadJsonValue(key, L"PluginShortcuts")) settings.pluginShortcuts = ParsePluginShortcuts(*jsonValue);
    DWORD value = 0;
    if (ReadDwordValue(key, L"UseTabSwitcher", &value)) settings.useTabSwitcher = value != 0;
}

void WriteKeysSettings(HKEY key, const KeysSettings& settings) {
    WriteJsonValue(key, L"Shortcuts", SerializeIntVector(settings.shortcuts));
    WriteJsonValue(key, L"PluginShortcuts", SerializePluginShortcuts(settings.pluginShortcuts));
    WriteDwordValue(key, L"UseTabSwitcher", (settings.useTabSwitcher ? 1u : 0u));
}

void ReadPluginSettings(HKEY key, PluginSettings& settings) {
    if (auto jsonValue = ReadJsonValue(key, L"Enabled")) settings.enabled = ParseStringList(*jsonValue);
}

void WritePluginSettings(HKEY key, const PluginSettings& settings) {
    WriteJsonValue(key, L"Enabled", SerializeStringList(settings.enabled));
}

void ReadLangSettings(HKEY key, LangSettings& settings) {
    if (auto jsonValue = ReadJsonValue(key, L"PluginLangFiles")) settings.pluginLangFiles = ParseStringList(*jsonValue);
    DWORD value = 0;
    if (ReadDwordValue(key, L"UseLangFile", &value)) settings.useLangFile = value != 0;
    ReadStringValue(key, L"LangFile", &settings.langFile);
    ReadStringValue(key, L"BuiltInLang", &settings.builtInLang);
    if (ReadDwordValue(key, L"BuiltInLangSelectedIndex", &value)) settings.builtInLangSelectedIndex = static_cast<int>(value);
}

void WriteLangSettings(HKEY key, const LangSettings& settings) {
    WriteJsonValue(key, L"PluginLangFiles", SerializeStringList(settings.pluginLangFiles));
    WriteDwordValue(key, L"UseLangFile", (settings.useLangFile ? 1u : 0u));
    WriteStringValue(key, L"LangFile", settings.langFile);
    WriteStringValue(key, L"BuiltInLang", settings.builtInLang);
    WriteDwordValue(key, L"BuiltInLangSelectedIndex", static_cast<DWORD>(settings.builtInLangSelectedIndex));
}

void ReadDesktopSettings(HKEY key, DesktopSettings& settings) {
    DWORD value = 0;
    if (ReadDwordValue(key, L"FirstItem", &value)) settings.firstItem = static_cast<int>(value);
    if (ReadDwordValue(key, L"SecondItem", &value)) settings.secondItem = static_cast<int>(value);
    if (ReadDwordValue(key, L"ThirdItem", &value)) settings.thirdItem = static_cast<int>(value);
    if (ReadDwordValue(key, L"FourthItem", &value)) settings.fourthItem = static_cast<int>(value);
    if (ReadDwordValue(key, L"GroupExpanded", &value)) settings.groupExpanded = value != 0;
    if (ReadDwordValue(key, L"RecentTabExpanded", &value)) settings.recentTabExpanded = value != 0;
    if (ReadDwordValue(key, L"ApplicationExpanded", &value)) settings.applicationExpanded = value != 0;
    if (ReadDwordValue(key, L"RecentFileExpanded", &value)) settings.recentFileExpanded = value != 0;
    if (ReadDwordValue(key, L"TaskBarDblClickEnabled", &value)) settings.taskBarDblClickEnabled = value != 0;
    if (ReadDwordValue(key, L"DesktopDblClickEnabled", &value)) settings.desktopDblClickEnabled = value != 0;
    if (ReadDwordValue(key, L"LockMenu", &value)) settings.lockMenu = value != 0;
    if (ReadDwordValue(key, L"TitleBackground", &value)) settings.titleBackground = value != 0;
    if (ReadDwordValue(key, L"IncludeGroup", &value)) settings.includeGroup = value != 0;
    if (ReadDwordValue(key, L"IncludeRecentTab", &value)) settings.includeRecentTab = value != 0;
    if (ReadDwordValue(key, L"IncludeApplication", &value)) settings.includeApplication = value != 0;
    if (ReadDwordValue(key, L"IncludeRecentFile", &value)) settings.includeRecentFile = value != 0;
    if (ReadDwordValue(key, L"OneClickMenu", &value)) settings.oneClickMenu = value != 0;
    if (ReadDwordValue(key, L"EnableAppShortcuts", &value)) settings.enableAppShortcuts = value != 0;
    if (ReadDwordValue(key, L"Width", &value)) settings.width = static_cast<int>(value);
    if (ReadDwordValue(key, L"lstSelectedIndex", &value)) settings.lstSelectedIndex = static_cast<int>(value);
}

void WriteDesktopSettings(HKEY key, const DesktopSettings& settings) {
    WriteDwordValue(key, L"FirstItem", static_cast<DWORD>(settings.firstItem));
    WriteDwordValue(key, L"SecondItem", static_cast<DWORD>(settings.secondItem));
    WriteDwordValue(key, L"ThirdItem", static_cast<DWORD>(settings.thirdItem));
    WriteDwordValue(key, L"FourthItem", static_cast<DWORD>(settings.fourthItem));
    WriteDwordValue(key, L"GroupExpanded", (settings.groupExpanded ? 1u : 0u));
    WriteDwordValue(key, L"RecentTabExpanded", (settings.recentTabExpanded ? 1u : 0u));
    WriteDwordValue(key, L"ApplicationExpanded", (settings.applicationExpanded ? 1u : 0u));
    WriteDwordValue(key, L"RecentFileExpanded", (settings.recentFileExpanded ? 1u : 0u));
    WriteDwordValue(key, L"TaskBarDblClickEnabled", (settings.taskBarDblClickEnabled ? 1u : 0u));
    WriteDwordValue(key, L"DesktopDblClickEnabled", (settings.desktopDblClickEnabled ? 1u : 0u));
    WriteDwordValue(key, L"LockMenu", (settings.lockMenu ? 1u : 0u));
    WriteDwordValue(key, L"TitleBackground", (settings.titleBackground ? 1u : 0u));
    WriteDwordValue(key, L"IncludeGroup", (settings.includeGroup ? 1u : 0u));
    WriteDwordValue(key, L"IncludeRecentTab", (settings.includeRecentTab ? 1u : 0u));
    WriteDwordValue(key, L"IncludeApplication", (settings.includeApplication ? 1u : 0u));
    WriteDwordValue(key, L"IncludeRecentFile", (settings.includeRecentFile ? 1u : 0u));
    WriteDwordValue(key, L"OneClickMenu", (settings.oneClickMenu ? 1u : 0u));
    WriteDwordValue(key, L"EnableAppShortcuts", (settings.enableAppShortcuts ? 1u : 0u));
    WriteDwordValue(key, L"Width", static_cast<DWORD>(settings.width));
    WriteDwordValue(key, L"lstSelectedIndex", static_cast<DWORD>(settings.lstSelectedIndex));
}

void ApplyConfigValidation(ConfigData& config) {
    if (!IsValidIdList(config.window.defaultLocation)) {
        config.window.defaultLocation = PidlFromDisplayName(L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}");
    }

    auto ensureFont = [](FontConfig& font) {
        if (font.family.empty() || font.size <= 0.0f) {
            font.family = L"微软雅黑";
            font.size = 9.0f;
            font.style = 0;
        }
    };
    ensureFont(config.tips.previewFont);
    ensureFont(config.skin.tabTextFont);

    config.tips.previewMaxWidth = ValidateMinMax(config.tips.previewMaxWidth, 128, 1920);
    config.tips.previewMaxHeight = ValidateMinMax(config.tips.previewMaxHeight, 96, 1200);
    config.misc.tabHistoryCount = ValidateMinMax(config.misc.tabHistoryCount, 1, 30);
    config.misc.fileHistoryCount = ValidateMinMax(config.misc.fileHistoryCount, 1, 30);
    config.misc.networkTimeout = ValidateMinMax(config.misc.networkTimeout, 0, 120);

    config.skin.tabHeight = ValidateMinMax(config.skin.tabHeight, 10, 50);
    config.skin.tabMinWidth = ValidateMinMax(config.skin.tabMinWidth, 10, 100);
    config.skin.tabMaxWidth = ValidateMinMax(config.skin.tabMaxWidth, 50, 999);
    config.skin.overlapPixels = ValidateMinMax(config.skin.overlapPixels, 0, 20);

    auto clampPadding = [](Padding& padding) {
        padding.left = ValidateMinMax(padding.left, 0, 99);
        padding.top = ValidateMinMax(padding.top, 0, 99);
        padding.right = ValidateMinMax(padding.right, 0, 99);
        padding.bottom = ValidateMinMax(padding.bottom, 0, 99);
    };
    clampPadding(config.skin.tabSizeMargin);
    clampPadding(config.skin.tabContentMargin);
    clampPadding(config.skin.rebarSizeMargin);

    EnsureValidShellPath(config.skin.tabImageFile);
    EnsureValidShellPath(config.skin.rebarImageFile);
    EnsureValidShellPath(config.bbar.imageStripPath);

    const size_t pluginCount = config.bbar.activePluginIDs.size();
    auto& indexes = config.bbar.buttonIndexes;
    indexes.erase(std::remove_if(indexes.begin(), indexes.end(), [pluginCount](int index) {
                      int pluginIndex = HiWord(index) - 1;
                      return pluginIndex >= 0 && static_cast<size_t>(pluginIndex) >= pluginCount;
                  }),
                  indexes.end());

    size_t requiredShortcuts = static_cast<size_t>(BindAction::KEYBOARD_ACTION_COUNT);
    if (config.keys.shortcuts.size() < requiredShortcuts) {
        config.keys.shortcuts.resize(requiredShortcuts);
    }

    if (IsWindowsXpOrEarlier()) {
        config.tweaks.alwaysShowHeaders = false;
        config.tweaks.backspaceUpLevel = true;
    } else {
        config.tweaks.killExtWhileRenaming = true;
    }
    if (!IsWindows7OrGreaterOs()) {
        config.tweaks.redirectLibraryFolders = false;
        config.tweaks.forceSysListView = true;
    }
}

ConfigData LoadConfigFromRegistry() {
    ConfigData config;
    HKEY root = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegRoot, 0, KEY_READ, &root) != ERROR_SUCCESS) {
        return config;
    }

    auto readCategory = [&](const wchar_t* name, auto&& reader, auto& settings) {
        std::wstring path = MakeCategoryPath(name);
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_READ, &key) == ERROR_SUCCESS) {
            reader(key, settings);
            RegCloseKey(key);
        }
    };

    readCategory(L"Window", ReadWindowSettings, config.window);
    readCategory(L"Tabs", ReadTabsSettings, config.tabs);
    readCategory(L"Tweaks", ReadTweaksSettings, config.tweaks);
    readCategory(L"Tips", ReadTipsSettings, config.tips);
    readCategory(L"Misc", ReadMiscSettings, config.misc);
    readCategory(L"Skin", ReadSkinSettings, config.skin);
    readCategory(L"BBar", ReadBBarSettings, config.bbar);
    readCategory(L"Mouse", ReadMouseSettings, config.mouse);
    readCategory(L"Keys", ReadKeysSettings, config.keys);
    readCategory(L"Plugin", ReadPluginSettings, config.plugin);
    readCategory(L"Lang", ReadLangSettings, config.lang);
    readCategory(L"Desktop", ReadDesktopSettings, config.desktop);

    RegCloseKey(root);
    ApplyConfigValidation(config);
    return config;
}

void WriteConfigToRegistry(const ConfigData& config, bool desktopOnly) {
    ConfigData sanitized = config;
    ApplyConfigValidation(sanitized);

    HKEY root = nullptr;
    RegCreateKeyExW(HKEY_CURRENT_USER, kRegRoot, 0, nullptr, 0, KEY_WRITE, nullptr, &root, nullptr);
    auto writeCategory = [&](const wchar_t* name, auto&& writer, const auto& settings) {
        std::wstring path = MakeCategoryPath(name);
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
            writer(key, settings);
            RegCloseKey(key);
        }
    };

    if (!desktopOnly) {
        writeCategory(L"Window", WriteWindowSettings, sanitized.window);
        writeCategory(L"Tabs", WriteTabsSettings, sanitized.tabs);
        writeCategory(L"Tweaks", WriteTweaksSettings, sanitized.tweaks);
        writeCategory(L"Tips", WriteTipsSettings, sanitized.tips);
        writeCategory(L"Misc", WriteMiscSettings, sanitized.misc);
        writeCategory(L"Skin", WriteSkinSettings, sanitized.skin);
        writeCategory(L"BBar", WriteBBarSettings, sanitized.bbar);
        writeCategory(L"Mouse", WriteMouseSettings, sanitized.mouse);
        writeCategory(L"Keys", WriteKeysSettings, sanitized.keys);
        writeCategory(L"Plugin", WritePluginSettings, sanitized.plugin);
        writeCategory(L"Lang", WriteLangSettings, sanitized.lang);
        writeCategory(L"Desktop", WriteDesktopSettings, sanitized.desktop);
    } else {
        writeCategory(L"Desktop", WriteDesktopSettings, sanitized.desktop);
    }

    if (root) {
        RegCloseKey(root);
    }
}

void UpdateConfigSideEffects(ConfigData& config, bool broadcastChanges) {
    ApplyConfigValidation(config);
    hooks::HookManagerNative::Instance().ReloadConfiguration(config);
    if(!broadcastChanges) {
        return;
    }
    static constexpr wchar_t kNotificationName[] = L"QTTabBar.ConfigChanged";
    ::SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                          reinterpret_cast<LPARAM>(kNotificationName),
                          SMTO_ABORTIFHUNG, 100, nullptr);
}

}  // namespace qttabbar
