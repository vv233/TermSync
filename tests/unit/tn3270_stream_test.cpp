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

TEST(Tn3270Stream, AidCodesForFunctionKeys)
{
    EXPECT_EQ(Tn3270Stream::aidForPf(1), 0xF1);
    EXPECT_EQ(Tn3270Stream::aidForPf(9), 0xF9);
    EXPECT_EQ(Tn3270Stream::aidForPf(10), 0x7A);
    EXPECT_EQ(Tn3270Stream::aidForPf(12), 0x7C);
    EXPECT_EQ(Tn3270Stream::aidForPf(13), 0xC1);
}

TEST(Tn3270Stream, PaKeyIsShortRead)
{
    Tn3270Stream stream;
    const QByteArray submit = stream.submit(Tn3270Stream::AID_PA1);
    // Short read: AID + 2-byte cursor address, no field data.
    ASSERT_EQ(submit.size(), 3);
    EXPECT_EQ(static_cast<unsigned char>(submit[0]), Tn3270Stream::AID_PA1);
}

TEST(Tn3270Stream, ParsesExtendedFieldAttributes)
{
    // EW, WCC, SBA 0, SFE with 2 pairs: field-attr=unprotected(0x00),
    // color=red(0xF2).
    const QByteArray record = ba({
        0xF5, 0x00,
        0x11, 0x40, 0x40,
        0x29, 0x02, 0xC0, 0x00, 0x42, 0xF2,
        0xC1, 0xC2, 0xC3,
    });
    Tn3270Stream stream;
    stream.processRecord(record);
    ASSERT_EQ(stream.fields().size(), 1);
    EXPECT_FALSE(stream.fields()[0].protectedField);
    EXPECT_EQ(stream.fields()[0].color, 0xF2);
}

TEST(Tn3270Stream, FieldNavigationMovesBetweenInputFields)
{
    // Two unprotected fields separated by a protected one.
    const QByteArray record = ba({
        0xF5, 0x00,
        0x11, 0x40, 0x40, 0x1D, 0x00, 0x40, 0x40,       // field A (unprot) @ ~1
        0x11, 0x40, 0x50, 0x1D, 0x20, 0xC1, 0xC2,       // protected label
        0x11, 0x40, 0x60, 0x1D, 0x00, 0x40, 0x40,       // field B (unprot)
    });
    Tn3270Stream stream;
    stream.processRecord(record);
    const int firstCursor = stream.cursor();
    stream.nextField();
    EXPECT_NE(stream.cursor(), firstCursor);
    stream.prevField();
    // Back near the first input field.
    EXPECT_LE(stream.cursor(), firstCursor + 2);
}
