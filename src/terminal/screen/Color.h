#pragma once

#include <cstdint>

namespace termsync::terminal {

// A terminal color: either the terminal default, a palette index (0..255),
// or a 24-bit true color. The widget/renderer (M3b) maps Default/Indexed to
// concrete RGB using the active color scheme; the buffer stays scheme-agnostic.
struct Color
{
    enum class Type : uint8_t { Default, Indexed, Rgb };

    Type type = Type::Default;
    uint8_t index = 0;              // valid when type == Indexed
    uint8_t r = 0, g = 0, b = 0;    // valid when type == Rgb

    static Color defaultColor() { return Color{}; }
    static Color indexed(uint8_t i) { return Color{Type::Indexed, i, 0, 0, 0}; }
    static Color rgb(uint8_t r, uint8_t g, uint8_t b)
    {
        return Color{Type::Rgb, 0, r, g, b};
    }

    bool operator==(const Color &o) const
    {
        if (type != o.type)
            return false;
        switch (type) {
        case Type::Default: return true;
        case Type::Indexed: return index == o.index;
        case Type::Rgb: return r == o.r && g == o.g && b == o.b;
        }
        return false;
    }
    bool operator!=(const Color &o) const { return !(*this == o); }
};

} // namespace termsync::terminal
