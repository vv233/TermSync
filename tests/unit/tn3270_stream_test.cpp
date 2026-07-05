#include <gtest/gtest.h>

#include "tn3270/Tn3270Stream.h"

using termsync::core::Tn3270Stream;

namespace {

QByteArray ba(std::initializer_list<unsigned char> bytes)
{
    QByteArray out;
    for (unsigned char b : bytes)
        out.append(static_cast<char>(b));
    return out;
}

} // namespace

TEST(Tn3270Stream, ParsesEraseWriteTextAndInputField)
{
    // EW, WCC, SBA 0, text "LOGON ", SF unprotected, four blanks.
    const QByteArray record = ba({
        0xF5, 0x00,
        0x11, 0x40, 0x40,
        0xD3, 0xD6, 0xC7, 0xD6, 0xD5, 0x40,
        0x1D, 0x00,
        0x40, 0x40, 0x40, 0x40,
    });

    Tn3270Stream stream;
    stream.processRecord(record);

    EXPECT_TRUE(stream.plainText().contains(QStringLiteral("LOGON")));
    ASSERT_EQ(stream.fields().size(), 1);
    EXPECT_FALSE(stream.fields()[0].protectedField);
    EXPECT_EQ(stream.cursor(), 7);
}

TEST(Tn3270Stream, BuildsEnterModifiedFieldRecord)
{
    const QByteArray record = ba({
        0xF5, 0x00,
        0x11, 0x40, 0x40,
        0xD3, 0xD6, 0xC7, 0xD6, 0xD5, 0x40,
        0x1D, 0x00,
        0x40, 0x40, 0x40, 0x40,
    });

    Tn3270Stream stream;
    stream.processRecord(record);
    stream.insertText("USER");
    const QByteArray submit = stream.submitEnter();

    ASSERT_GE(submit.size(), 3);
    EXPECT_EQ(static_cast<unsigned char>(submit[0]), 0x7D); // AID Enter
    EXPECT_TRUE(submit.contains(static_cast<char>(0x11)));  // SBA before field data
    EXPECT_TRUE(submit.contains(static_cast<char>(0xE4)));  // U
    EXPECT_TRUE(submit.contains(static_cast<char>(0xE2)));  // S
    EXPECT_TRUE(submit.contains(static_cast<char>(0xC5)));  // E
    EXPECT_TRUE(submit.contains(static_cast<char>(0xD9)));  // R
}
