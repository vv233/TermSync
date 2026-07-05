// Unit tests for the X/Y/ZMODEM framing primitives.

#include <gtest/gtest.h>

#include "modem/XYModem.h"

using namespace termsync::transfer::modem;

TEST(XYModem, Crc16KnownVector)
{
    // CRC-16/XMODEM of "123456789" is 0x31C3.
    EXPECT_EQ(crc16Xmodem(QByteArray("123456789")), 0x31C3);
    EXPECT_EQ(crc16Xmodem(QByteArray()), 0x0000);
}

TEST(XYModem, Crc32KnownVector)
{
    // Standard CRC-32 of "123456789" is 0xCBF43926.
    EXPECT_EQ(crc32(QByteArray("123456789")), 0xCBF43926u);
}

TEST(XYModem, DataBlock128RoundTrip)
{
    QByteArray payload("hello xmodem");
    const QByteArray frame = makeDataBlock(1, payload, /*use1k=*/false);
    // SOH + blk + ~blk + 128 data + 2 crc.
    EXPECT_EQ(frame.size(), 1 + 1 + 1 + 128 + 2);
    EXPECT_EQ(static_cast<unsigned char>(frame[0]), SOH);

    const ParsedBlock p = parseDataBlock(frame);
    ASSERT_TRUE(p.ok);
    EXPECT_EQ(p.blockNumber, 1);
    EXPECT_EQ(p.payload.left(payload.size()), payload);
    EXPECT_EQ(p.payload.size(), 128);
}

TEST(XYModem, DataBlock1kUsesStx)
{
    const QByteArray frame = makeDataBlock(7, QByteArray("x"), /*use1k=*/true);
    EXPECT_EQ(static_cast<unsigned char>(frame[0]), STX);
    EXPECT_EQ(frame.size(), 1 + 1 + 1 + 1024 + 2);
    const ParsedBlock p = parseDataBlock(frame);
    ASSERT_TRUE(p.ok);
    EXPECT_EQ(p.blockNumber, 7);
}

TEST(XYModem, CorruptBlockRejected)
{
    QByteArray frame = makeDataBlock(2, QByteArray("data"), false);
    frame[10] = frame[10] ^ 0xFF; // flip a payload byte -> CRC mismatch
    EXPECT_FALSE(parseDataBlock(frame).ok);
}

TEST(XYModem, BadBlockComplementRejected)
{
    QByteArray frame = makeDataBlock(3, QByteArray("data"), false);
    frame[2] = 0x00; // wrong ~blk
    EXPECT_FALSE(parseDataBlock(frame).ok);
}

TEST(XYModem, ZdleRoundTrip)
{
    QByteArray raw;
    for (int b = 0; b < 256; ++b)
        raw.append(static_cast<char>(b));
    const QByteArray encoded = zdleEncode(raw);
    EXPECT_EQ(zdleDecode(encoded), raw);
    // Control bytes that must be escaped should not appear raw in the output
    // except as part of an escape sequence.
    EXPECT_GT(encoded.size(), raw.size());
}
