#include <gtest/gtest.h>

#include "tftp/TftpProtocol.h"

using namespace termsync::core::tftp;

TEST(TftpProtocol, RequestRoundTrip)
{
    const QByteArray b =
        buildRequest(OpCode::Rrq, QStringLiteral("dir/file.bin"),
                     QStringLiteral("octet"));
    const Packet p = parse(b);
    ASSERT_TRUE(p.valid);
    EXPECT_EQ(p.op, OpCode::Rrq);
    EXPECT_EQ(p.filename, QStringLiteral("dir/file.bin"));
    EXPECT_EQ(p.mode, QStringLiteral("octet"));
}

TEST(TftpProtocol, WriteRequestParsed)
{
    const Packet p = parse(
        buildRequest(OpCode::Wrq, QStringLiteral("x"), QStringLiteral("netascii")));
    ASSERT_TRUE(p.valid);
    EXPECT_EQ(p.op, OpCode::Wrq);
    EXPECT_EQ(p.mode, QStringLiteral("netascii"));
}

TEST(TftpProtocol, DataRoundTrip)
{
    QByteArray payload(512, 'A');
    payload[0] = '\0'; // ensure binary-safe
    const Packet p = parse(buildData(0x0102, payload));
    ASSERT_TRUE(p.valid);
    EXPECT_EQ(p.op, OpCode::Data);
    EXPECT_EQ(p.block, 0x0102);
    EXPECT_EQ(p.payload, payload);
}

TEST(TftpProtocol, EmptyDataIsValid)
{
    const Packet p = parse(buildData(7, QByteArray()));
    ASSERT_TRUE(p.valid);
    EXPECT_EQ(p.op, OpCode::Data);
    EXPECT_EQ(p.block, 7);
    EXPECT_TRUE(p.payload.isEmpty());
}

TEST(TftpProtocol, AckRoundTrip)
{
    const Packet p = parse(buildAck(65535));
    ASSERT_TRUE(p.valid);
    EXPECT_EQ(p.op, OpCode::Ack);
    EXPECT_EQ(p.block, 65535);
}

TEST(TftpProtocol, ErrorRoundTrip)
{
    const Packet p = parse(
        buildError(ErrorCode::FileNotFound, QStringLiteral("nope")));
    ASSERT_TRUE(p.valid);
    EXPECT_EQ(p.op, OpCode::Error);
    EXPECT_EQ(p.error, ErrorCode::FileNotFound);
    EXPECT_EQ(p.message, QStringLiteral("nope"));
}

TEST(TftpProtocol, RejectsTruncatedAndUnknown)
{
    EXPECT_FALSE(parse(QByteArray()).valid);
    EXPECT_FALSE(parse(QByteArray("\x00", 1)).valid);      // 1 byte
    EXPECT_FALSE(parse(QByteArray("\x00\x09", 2)).valid);  // unknown opcode
    EXPECT_FALSE(parse(QByteArray("\x00\x03\x00", 3)).valid); // short DATA
    // RRQ with no mode terminator.
    EXPECT_FALSE(parse(QByteArray("\x00\x01"
                                  "file\x00",
                                  7))
                     .valid);
}
