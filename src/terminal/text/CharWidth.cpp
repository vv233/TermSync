#include "text/CharWidth.h"

#include <array>
#include <cstddef>

namespace termsync::terminal {

namespace {

struct Range
{
    char32_t lo;
    char32_t hi;
};

// East Asian Wide + Fullwidth ranges (sorted). Derived from the Unicode East
// Asian Width property; good enough for terminal column accounting.
constexpr std::array<Range, 44> kWide = {{
    {0x1100, 0x115F},   {0x231A, 0x231B},   {0x2329, 0x232A},
    {0x23E9, 0x23EC},   {0x23F0, 0x23F0},   {0x23F3, 0x23F3},
    {0x25FD, 0x25FE},   {0x2614, 0x2615},   {0x2648, 0x2653},
    {0x267F, 0x267F},   {0x2693, 0x2693},   {0x26A1, 0x26A1},
    {0x26AA, 0x26AB},   {0x26BD, 0x26BE},   {0x26C4, 0x26C5},
    {0x26CE, 0x26CE},   {0x26D4, 0x26D4},   {0x26EA, 0x26EA},
    {0x26F2, 0x26F3},   {0x26F5, 0x26F5},   {0x26FA, 0x26FA},
    {0x26FD, 0x26FD},   {0x2705, 0x2705},   {0x270A, 0x270B},
    {0x2728, 0x2728},   {0x274C, 0x274C},   {0x274E, 0x274E},
    {0x2753, 0x2755},   {0x2757, 0x2757},   {0x2795, 0x2797},
    {0x27B0, 0x27B0},   {0x27BF, 0x27BF},   {0x2B1B, 0x2B1C},
    {0x2B50, 0x2B50},   {0x2B55, 0x2B55},   {0x2E80, 0x303E},
    {0x3041, 0x33FF},   {0x3400, 0x4DBF},   {0x4E00, 0xA4CF},
    {0xA960, 0xA97F},   {0xAC00, 0xD7A3},   {0xF900, 0xFAFF},
    {0xFE10, 0xFE6F},   {0xFF00, 0xFF60},
}};

bool inWideTable(char32_t cp)
{
    // Binary search the sorted ranges.
    std::size_t lo = 0, hi = kWide.size();
    while (lo < hi) {
        const std::size_t mid = (lo + hi) / 2;
        if (cp < kWide[mid].lo)
            hi = mid;
        else if (cp > kWide[mid].hi)
            lo = mid + 1;
        else
            return true;
    }
    return false;
}

} // namespace

int charWidth(char32_t cp)
{
    if (cp < 0x1100)
        return 1; // fast path for ASCII/Latin
    if (inWideTable(cp))
        return 2;
    // Fullwidth signs, and astral CJK / emoji planes.
    if ((cp >= 0xFFE0 && cp <= 0xFFE6) ||
        (cp >= 0x1F300 && cp <= 0x1FAFF) ||
        (cp >= 0x20000 && cp <= 0x3FFFD))
        return 2;
    return 1;
}

} // namespace termsync::terminal
