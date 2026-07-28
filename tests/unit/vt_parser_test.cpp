// Unit tests for the VT parser + screen buffer. Fully deterministic and
// headless: feed byte sequences, assert the resulting grid / cursor / pen.

#include <gtest/gtest.h>

#include "screen/ScreenBuffer.h"
#include "vt/VtParser.h"

using namespace termsync::terminal;

namespace {

// Convenience: feed a byte string into a fresh parser/screen.
struct Term
{
    ScreenBuffer screen{80, 24};
    VtParser parser{&screen};
    void feed(const char *s) { parser.parse(QByteArray(s)); }
    void feed(const QByteArray &b) { parser.parse(b); }

    QString lineText(int row) const
    {
        QString out;
        const Line &l = screen.line(row);
        for (const Cell &c : l)
            out += QChar(static_cast<char16_t>(c.ch));
        return out;
    }
    QString lineTrimmed(int row) const { return lineText(row).trimmed(); }
};

} // namespace

TEST(VtParser, PlainTextWrites)
{
    Term t;
    t.feed("hello");
    EXPECT_EQ(t.lineTrimmed(0), "hello");
    EXPECT_EQ(t.screen.cursorCol(), 5);
    EXPECT_EQ(t.screen.cursorRow(), 0);
}

TEST(VtParser, CarriageReturnLineFeed)
{
    Term t;
    t.feed("abc\r\ndef");
    EXPECT_EQ(t.lineTrimmed(0), "abc");
    EXPECT_EQ(t.lineTrimmed(1), "def");
    EXPECT_EQ(t.screen.cursorRow(), 1);
    EXPECT_EQ(t.screen.cursorCol(), 3);
}

TEST(VtParser, CursorPositionCUP)
{
    Term t;
    t.feed("\x1b[3;5HX"); // row 3, col 5 (1-based)
    EXPECT_EQ(t.screen.cursorRow(), 2);   // was col5→wrote X→col6
    EXPECT_EQ(QChar(static_cast<char16_t>(t.screen.line(2)[4].ch)), QChar('X'));
}

TEST(VtParser, CupDefaultsToHome)
{
    Term t;
    t.feed("xxxx\x1b[HY");
    // ESC[H homes to (0,0); Y overwrites first cell.
    EXPECT_EQ(QChar(static_cast<char16_t>(t.screen.line(0)[0].ch)), QChar('Y'));
}

TEST(VtParser, EmptyLeadingParam)
{
    Term t;
    t.feed("\x1b[;5HZ"); // row default(1), col 5
    EXPECT_EQ(t.screen.cursorRow(), 0);
    EXPECT_EQ(QChar(static_cast<char16_t>(t.screen.line(0)[4].ch)), QChar('Z'));
}

TEST(VtParser, EraseInLineToRight)
{
    Term t;
    t.feed("abcdef\r");     // cursor back to col 0
    t.feed("\x1b[3C");      // forward 3 -> col 3
    t.feed("\x1b[0K");      // erase to end of line
    EXPECT_EQ(t.lineTrimmed(0), "abc");
}

TEST(VtParser, EraseInDisplayAll)
{
    Term t;
    t.feed("line1\r\nline2\r\nline3");
    t.feed("\x1b[2J");
    EXPECT_EQ(t.lineTrimmed(0), "");
    EXPECT_EQ(t.lineTrimmed(1), "");
    EXPECT_EQ(t.lineTrimmed(2), "");
}

TEST(VtParser, AutoWrapAtRightMargin)
{
    ScreenBuffer screen{5, 3};
    VtParser parser{&screen};
    parser.parse("ABCDEF"); // 6 chars into a 5-wide screen
    // "ABCDE" on row 0, "F" wrapped to row 1.
    QString row0;
    for (const Cell &c : screen.line(0))
        row0 += QChar(static_cast<char16_t>(c.ch));
    EXPECT_EQ(row0, "ABCDE");
    EXPECT_EQ(QChar(static_cast<char16_t>(screen.line(1)[0].ch)), QChar('F'));
    EXPECT_EQ(screen.cursorRow(), 1);
    EXPECT_EQ(screen.cursorCol(), 1);
}

TEST(VtParser, ScrollOnLastLinePushesScrollback)
{
    ScreenBuffer screen{10, 2};   // 2 rows only
    VtParser parser{&screen};
    parser.parse("one\r\ntwo\r\nthree");
    // "one" scrolled off into scrollback; visible rows now "two"/"three".
    EXPECT_EQ(screen.scrollbackSize(), 1);
    QString sb;
    for (const Cell &c : screen.scrollback().front())
        sb += QChar(static_cast<char16_t>(c.ch));
    EXPECT_EQ(sb.trimmed(), "one");
}

// Redraw-heavy TUIs (Ink, used by many CLIs) repaint an input line by returning
// to its start, writing the new — often shorter — content, then erasing to the
// end of line. If erase-to-EOL leaves the tail behind, the old text bleeds
// through where the user is typing. Guard that exact scenario.
TEST(VtParser, EraseToEolWipesStaleTailUnderRedraw)
{
    ScreenBuffer screen{40, 2};
    VtParser parser{&screen};
    parser.parse("OLD_STALE_CONTENT_1234567890"); // long content on row 0
    parser.parse("\rhi\x1b[K");                    // CR + shorter text + erase-to-EOL

    QString row;
    for (const Cell &c : screen.line(0))
        row += QChar(static_cast<char16_t>(c.ch));
    EXPECT_EQ(row.trimmed(), "hi");        // no "STALE" tail survives
    EXPECT_EQ(screen.line(0)[2].ch, U' '); // the cell just past "hi" is blank
    EXPECT_EQ(screen.line(0)[20].ch, U' ');
}

TEST(ScreenBuffer, ClearScrollbackDropsHistoryKeepsScreen)
{
    ScreenBuffer screen{10, 2};
    VtParser parser{&screen};
    parser.parse("one\r\ntwo\r\nthree"); // "one" -> scrollback
    ASSERT_EQ(screen.scrollbackSize(), 1);

    screen.clearScrollback();
    EXPECT_EQ(screen.scrollbackSize(), 0);

    // The visible screen is untouched.
    QString row0, row1;
    for (const Cell &c : screen.line(0))
        row0 += QChar(static_cast<char16_t>(c.ch));
    for (const Cell &c : screen.line(1))
        row1 += QChar(static_cast<char16_t>(c.ch));
    EXPECT_EQ(row0.trimmed(), "two");
    EXPECT_EQ(row1.trimmed(), "three");
}

TEST(VtParser, SgrBoldAndColor)
{
    Term t;
    t.feed("\x1b[1;31mR\x1b[0mN");
    const Cell &r = t.screen.line(0)[0];
    EXPECT_TRUE(r.hasFlag(Bold));
    EXPECT_EQ(r.fg.type, Color::Type::Indexed);
    EXPECT_EQ(r.fg.index, 1u); // red

    const Cell &n = t.screen.line(0)[1];
    EXPECT_FALSE(n.hasFlag(Bold));
    EXPECT_EQ(n.fg.type, Color::Type::Default);
}

TEST(VtParser, SgrTrueColor)
{
    Term t;
    t.feed("\x1b[38;2;10;20;30mX");
    const Cell &x = t.screen.line(0)[0];
    EXPECT_EQ(x.fg.type, Color::Type::Rgb);
    EXPECT_EQ(x.fg.r, 10u);
    EXPECT_EQ(x.fg.g, 20u);
    EXPECT_EQ(x.fg.b, 30u);
}

TEST(VtParser, Sgr256Color)
{
    Term t;
    t.feed("\x1b[48;5;200mX");
    const Cell &x = t.screen.line(0)[0];
    EXPECT_EQ(x.bg.type, Color::Type::Indexed);
    EXPECT_EQ(x.bg.index, 200u);
}

TEST(VtParser, Utf8MultiByte)
{
    Term t;
    // "é" (U+00E9) = 0xC3 0xA9, then a 3-byte "€" (U+20AC) = E2 82 AC.
    t.feed("\xC3\xA9\xE2\x82\xAC");
    EXPECT_EQ(t.screen.line(0)[0].ch, static_cast<char32_t>(0x00E9));
    EXPECT_EQ(t.screen.line(0)[1].ch, static_cast<char32_t>(0x20AC));
}

TEST(VtParser, CursorVisibilityMode)
{
    Term t;
    EXPECT_TRUE(t.screen.cursorVisible());
    t.feed("\x1b[?25l");
    EXPECT_FALSE(t.screen.cursorVisible());
    t.feed("\x1b[?25h");
    EXPECT_TRUE(t.screen.cursorVisible());
}

TEST(VtParser, AlternateScreenBuffer)
{
    Term t;
    t.feed("primary");
    t.feed("\x1b[?1049h");       // enter alt screen
    EXPECT_TRUE(t.screen.usingAltScreen());
    t.feed("alt");
    EXPECT_EQ(t.lineTrimmed(0), "alt");
    t.feed("\x1b[?1049l");       // leave alt screen
    EXPECT_FALSE(t.screen.usingAltScreen());
    EXPECT_EQ(t.lineTrimmed(0), "primary");
}

TEST(VtParser, OscWindowTitle)
{
    Term t;
    bool called = false;
    QString seen;
    t.parser.onTitleChanged = [&](const QString &s) { called = true; seen = s; };
    t.feed("\x1b]0;My Title\x07");
    EXPECT_TRUE(called);
    EXPECT_EQ(seen, "My Title");
    EXPECT_EQ(t.parser.title(), "My Title");
}

TEST(VtParser, ScrollRegionAndReverseIndex)
{
    ScreenBuffer screen{10, 5};
    VtParser parser{&screen};
    parser.parse("\x1b[2;4r");   // scroll region rows 2..4 (1-based)
    // Cursor homed to top of region (row index 1).
    EXPECT_EQ(screen.cursorRow(), 1);
    parser.parse("\x1b[3;1H");   // move into region row 3
    parser.parse("X");
    // Reverse index from top of region should scroll region down.
    parser.parse("\x1b[2;1H\x1bM"); // to region top, then RI
    EXPECT_EQ(screen.cursorRow(), 1);
}

TEST(VtParser, DeleteAndInsertChars)
{
    Term t;
    t.feed("abcdef\r");   // cursor col 0
    t.feed("\x1b[2P");    // delete 2 chars -> "cdef"
    EXPECT_EQ(t.lineTrimmed(0), "cdef");
    t.feed("\x1b[2@");    // insert 2 blanks at col 0
    EXPECT_EQ(t.lineText(0).mid(2, 4), "cdef");
}
