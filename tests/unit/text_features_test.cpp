#include <gtest/gtest.h>

#include "text/HexView.h"
#include "text/KeywordHighlighter.h"
#include "text/TextSearch.h"

#include <QStringList>

using namespace termsync::terminal;

namespace {

HighlightRule word(const QString &p, int color, bool wholeWord = false)
{
    HighlightRule r;
    r.pattern = p;
    r.colorId = color;
    r.wholeWord = wholeWord;
    return r;
}

} // namespace

TEST(KeywordHighlighter, SubstringCaseInsensitive)
{
    KeywordHighlighter h;
    h.addRule(word("error", 1));
    const auto spans = h.highlight("ERROR: an Error occurred");
    ASSERT_EQ(spans.size(), 2);
    EXPECT_EQ(spans[0].start, 0);
    EXPECT_EQ(spans[0].length, 5);
    EXPECT_EQ(spans[0].colorId, 1);
    EXPECT_EQ(spans[1].start, 10);
}

TEST(KeywordHighlighter, WholeWordBoundaries)
{
    KeywordHighlighter h;
    h.addRule(word("cat", 2, /*wholeWord=*/true));
    const auto spans = h.highlight("cat category cat");
    ASSERT_EQ(spans.size(), 2); // "cat" x2, not the "cat" inside "category"
    EXPECT_EQ(spans[0].start, 0);
    EXPECT_EQ(spans[1].start, 13);
}

TEST(KeywordHighlighter, EarlierRuleWinsOnOverlap)
{
    KeywordHighlighter h;
    h.addRule(word("abc", 1));
    h.addRule(word("bcd", 2)); // overlaps abc in "abcd"
    const auto spans = h.highlight("abcd");
    ASSERT_EQ(spans.size(), 1);
    EXPECT_EQ(spans[0].colorId, 1);
    EXPECT_EQ(spans[0].start, 0);
    EXPECT_EQ(spans[0].length, 3);
}

TEST(KeywordHighlighter, Regex)
{
    KeywordHighlighter h;
    HighlightRule r;
    r.pattern = "\\d+";
    r.regex = true;
    r.colorId = 5;
    h.addRule(r);
    const auto spans = h.highlight("code 404 at line 12");
    ASSERT_EQ(spans.size(), 2);
    EXPECT_EQ(spans[0].start, 5);  // "404"
    EXPECT_EQ(spans[0].length, 3);
    EXPECT_EQ(spans[1].length, 2); // "12"
}

TEST(KeywordHighlighter, NoRulesNoSpans)
{
    KeywordHighlighter h;
    EXPECT_TRUE(h.highlight("anything").isEmpty());
}

TEST(HexView, FormatsRowWithAsciiGutter)
{
    const QString dump = formatHexDump(QByteArray("Hello world\n"));
    // Single row (12 bytes): offset, hex, and printable-ASCII gutter with '.'
    // standing in for the newline.
    EXPECT_TRUE(dump.startsWith("00000000  48 65 6c 6c 6f 20"));
    EXPECT_TRUE(dump.contains("|Hello world.|"));
}

TEST(HexView, WrapsRowsAndTracksOffset)
{
    QByteArray data(20, 'A'); // 20 bytes -> two rows at 16/row
    const QString dump = formatHexDump(data, /*baseOffset=*/0x100);
    const QStringList lines = dump.split('\n');
    ASSERT_EQ(lines.size(), 2);
    EXPECT_TRUE(lines[0].startsWith("00000100  "));
    EXPECT_TRUE(lines[1].startsWith("00000110  ")); // 0x100 + 16
}

TEST(HexView, EmptyInputEmptyOutput)
{
    EXPECT_TRUE(formatHexDump(QByteArray()).isEmpty());
}

namespace {

// Document helper: searchDocument over a fixed list of rows.
SearchMatch findIn(const QStringList &doc, const QString &needle, bool forward,
                   Qt::CaseSensitivity cs, int startRow, int startCol)
{
    return searchDocument(
        doc.size(), [&doc](int r) { return doc.value(r); }, needle, forward, cs,
        startRow, startCol);
}

} // namespace

TEST(TextSearch, ForwardFindsFirstFromTop)
{
    const QStringList doc{"alpha bravo", "charlie bravo", "delta"};
    const SearchMatch m =
        findIn(doc, "bravo", true, Qt::CaseInsensitive, 0, 0);
    ASSERT_TRUE(m.found);
    EXPECT_EQ(m.row, 0);
    EXPECT_EQ(m.col, 6);
    EXPECT_EQ(m.length, 5);
}

TEST(TextSearch, ForwardAdvancesPastCurrentMatch)
{
    const QStringList doc{"alpha bravo", "charlie bravo", "delta"};
    // Starting just past the row-0 match, the next hit is on row 1.
    const SearchMatch m =
        findIn(doc, "bravo", true, Qt::CaseInsensitive, 0, 7);
    ASSERT_TRUE(m.found);
    EXPECT_EQ(m.row, 1);
    EXPECT_EQ(m.col, 8);
}

TEST(TextSearch, ForwardWrapsAround)
{
    const QStringList doc{"needle here", "nothing", "more"};
    // Start past everything on the last row; the only match is back on row 0.
    const SearchMatch m =
        findIn(doc, "needle", true, Qt::CaseInsensitive, 2, 100);
    ASSERT_TRUE(m.found);
    EXPECT_EQ(m.row, 0);
    EXPECT_EQ(m.col, 0);
}

TEST(TextSearch, BackwardFindsPreviousMatch)
{
    const QStringList doc{"one two", "two three", "two four"};
    // From row 2 col 0, the previous "two" is the one on row 1.
    const SearchMatch m =
        findIn(doc, "two", false, Qt::CaseInsensitive, 2, -1);
    ASSERT_TRUE(m.found);
    EXPECT_EQ(m.row, 1);
    EXPECT_EQ(m.col, 0);
}

TEST(TextSearch, CaseSensitivityRespected)
{
    const QStringList doc{"Foo foo FOO"};
    const SearchMatch cs =
        findIn(doc, "FOO", true, Qt::CaseSensitive, 0, 0);
    ASSERT_TRUE(cs.found);
    EXPECT_EQ(cs.col, 8); // only the upper-case FOO
    const SearchMatch ci =
        findIn(doc, "FOO", true, Qt::CaseInsensitive, 0, 0);
    ASSERT_TRUE(ci.found);
    EXPECT_EQ(ci.col, 0); // first case-insensitive hit
}

TEST(TextSearch, NoMatchReturnsNotFound)
{
    const QStringList doc{"abc", "def"};
    EXPECT_FALSE(findIn(doc, "xyz", true, Qt::CaseInsensitive, 0, 0).found);
    EXPECT_FALSE(findIn(doc, "", true, Qt::CaseInsensitive, 0, 0).found);
}
