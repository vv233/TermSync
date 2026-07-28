#pragma once

#include <cstdint>

namespace termsync::terminal {

// Terminal display width of a Unicode code point, in cells: 2 for East Asian
// Wide / Fullwidth characters (CJK, Kana, Hangul, fullwidth forms, most emoji),
// 1 otherwise. Zero-width combining marks are treated as width 1 for now (so
// they are never dropped). Mirrors the wcwidth() tables terminals rely on so the
// emulator's column accounting matches what the remote program assumes.
int charWidth(char32_t cp);

} // namespace termsync::terminal
