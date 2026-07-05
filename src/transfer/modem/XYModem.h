#pragma once

#include <QByteArray>
#include <cstdint>

// X/Y/ZMODEM low-level primitives. The framing/CRC layer is pure and
// unit-tested here; wiring a full interactive transfer session into the
// terminal (rz/sz auto-detect) builds on top of it.
namespace termsync::transfer::modem {

// Control bytes (shared by X/YMODEM).
enum : unsigned char {
    SOH = 0x01,  // 128-byte data block
    STX = 0x02,  // 1024-byte data block
    EOT = 0x04,
    ACK = 0x06,
    NAK = 0x15,
    CAN = 0x18,
    C   = 0x43,  // 'C' — request CRC mode
};

// CRC-16/XMODEM (poly 0x1021, init 0x0000).
uint16_t crc16Xmodem(const QByteArray &data);

// CRC-32 (as used by ZMODEM, poly 0xEDB88320 reflected).
uint32_t crc32(const QByteArray &data, uint32_t seed = 0xFFFFFFFFu);

// Builds one X/YMODEM data block: <SOH|STX> <blk> <~blk> <payload padded to
// 128/1024 with 0x1A> <crc16-hi> <crc16-lo>. `use1k` selects STX/1024.
QByteArray makeDataBlock(uint8_t blockNumber, const QByteArray &payload,
                         bool use1k);

struct ParsedBlock
{
    bool ok = false;
    uint8_t blockNumber = 0;
    QByteArray payload;   // fixed 128/1024 (trailing SUB padding not stripped)
};

// Parses a complete data block (validates the ~blk complement and CRC16).
ParsedBlock parseDataBlock(const QByteArray &frame);

// ZMODEM ZDLE escaping: control-sensitive bytes are escaped with ZDLE(0x18).
QByteArray zdleEncode(const QByteArray &data);
QByteArray zdleDecode(const QByteArray &data);

} // namespace termsync::transfer::modem
