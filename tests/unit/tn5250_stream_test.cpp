#include <gtest/gtest.h>

#include "tn3270/Tn5250Stream.h"

using termsync::core::Tn5250Stream;

namespace {
QByteArray ba(std::initializer_list<unsigned char> bytes)
{
    QByteArray out;
    for (unsigned char b : bytes)
        out.append(static_cast<char>(b));
    return out;
}
} // namespace

TEST(Tn5250Stream, WriteToDisplayRendersTextAtAddress)
{
    // ESC WTD, CC1, CC2, SBA row=2 col=3, text "HI" (EBCDIC C8 C9).
    const QByteArray record = ba({
        0x04, 0x11, 0x00, 0x00,
        0x11, 0x02, 0x03,
        0xC8, 0xC9,
    });
    Tn5250Stream stream;
    stream.processRecord(record);

    const QString text = stream.plainText();
    EXPECT_TRUE(text.contains(QStringLiteral("HI")));
    // Row 2 (index 1), starting at column 3 (index 2).
    const QStringList lines = text.split('\n');
    ASSERT_GE(lines.size(), 2);
    EXPECT_EQ(lines[1].mid(2, 2), QStringLiteral("HI"));
}

TEST(Tn5250Stream, RepeatToAddressFills)
{
    // SBA 1,1 then RA to row1,col6 with '*' (EBCDIC 0x5C).
    const QByteArray record = ba({
        0x04, 0x11, 0x00, 0x00,
        0x11, 0x01, 0x01,
        0x02, 0x01, 0x06, 0x5C,
    });
    Tn5250Stream stream;
    stream.processRecord(record);
    const QString line0 = stream.plainText().split('\n')[0];
    EXPECT_EQ(line0.left(5), QStringLiteral("*****"));
}
