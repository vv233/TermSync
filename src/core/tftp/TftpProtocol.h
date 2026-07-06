#pragma once

#include <cstdint>

#include <QByteArray>
#include <QString>

// TFTP wire protocol (RFC 1350) — pure packet build/parse, no I/O, so it is
// fully unit-testable. The QUdpSocket server lives in TftpServer.
namespace termsync::core::tftp {

enum class OpCode : uint16_t {
    Rrq = 1,   // read request
    Wrq = 2,   // write request
    Data = 3,
    Ack = 4,
    Error = 5,
};

enum class ErrorCode : uint16_t {
    NotDefined = 0,
    FileNotFound = 1,
    AccessViolation = 2,
    DiskFull = 3,
    IllegalOperation = 4,
    UnknownTransferId = 5,
    FileAlreadyExists = 6,
    NoSuchUser = 7,
};

// The standard TFTP block size; a DATA packet carrying fewer bytes than this
// terminates the transfer.
constexpr int kBlockSize = 512;

// --- Builders --------------------------------------------------------------
QByteArray buildRequest(OpCode op, const QString &filename, const QString &mode);
QByteArray buildData(uint16_t block, const QByteArray &payload);
QByteArray buildAck(uint16_t block);
QByteArray buildError(ErrorCode code, const QString &message);

// --- Parser ----------------------------------------------------------------
struct Packet
{
    bool valid = false;
    OpCode op = OpCode::Error;

    // RRQ / WRQ
    QString filename;
    QString mode;

    // DATA / ACK
    uint16_t block = 0;
    QByteArray payload;

    // ERROR
    ErrorCode error = ErrorCode::NotDefined;
    QString message;
};

// Parses one datagram. `valid` is false (and op is left at Error) for malformed
// or unknown packets.
Packet parse(const QByteArray &datagram);

} // namespace termsync::core::tftp
