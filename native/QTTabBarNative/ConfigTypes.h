#pragma once

#include <map>
#include <string>
#include <vector>

#include "ConfigEnums.h"

namespace qttabbar {

using ByteVector = std::vector<uint8_t>;
using StringList = std::vector<std::wstring>;
using IntVector = std::vector<int>;
using StringVector = std::vector<std::wstring>;
using MouseActionMap = std::map<MouseChord, BindAction>;

struct Padding {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

struct FontConfig {
    std::wstring family;
    float size = 0.0f;
    uint32_t style = 0;
};

struct ColorValue {
    uint32_t argb = 0;

    constexpr ColorValue() = default;
    constexpr explicit ColorValue(uint32_t value) : argb(value) {}
};

struct MouseConfiguration {
    bool mouseScrollsHotWnd = false;
    MouseActionMap globalMouseActions;
    MouseActionMap tabActions;
    MouseActionMap barActions;
    MouseActionMap linkActions;
    MouseActionMap itemActions;
    MouseActionMap marginActions;
};

}  // namespace qttabbar

