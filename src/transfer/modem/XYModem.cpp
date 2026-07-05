#include "modem/XYModem.h"

namespace termsync::transfer::modem {

uint16_t crc16Xmodem(const QByteArray &data)
{
    uint16_t crc = 0;
    for (char ch : data) {
        crc ^= static_cast<uint16_t>(static_cast<unsigned char>(ch)) << 8;
        for (int i = 0; i < 8; ++i)
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

uint32_t crc32(const QByteArray &data, uint32_t seed)
{
    uint32_t crc = seed;
    for (char ch : data) {
        crc ^= static_cast<unsigned char>(ch);
        for (int i = 0; i < 8; ++i)
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    return crc ^ 0xFFFFFFFFu;
}

QByteArray makeDataBlock(uint8_t blockNumber, const QByteArray &payload, bool use1k)
{
    const int size = use1k ? 1024 : 128;
    QByteArray data = payload.left(size);
    while (data.size() < size)
        data.append(static_cast<char>(0x1A)); // SUB padding

    QByteArray frame;
    frame.append(static_cast<char>(use1k ? STX : SOH));
    frame.append(static_cast<char>(blockNumber));
    frame.append(static_cast<char>(0xFF - blockNumber));
    frame.append(data);
    const uint16_t crc = crc16Xmodem(data);
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    frame.append(static_cast<char>(crc & 0xFF));
    return frame;
}

ParsedBlock parseDataBlock(const QByteArray &frame)
{
    ParsedBlock out;
    if (frame.isEmpty())
        return out;
    const auto header = static_cast<unsigned char>(frame[0]);
    const int size = header == STX ? 1024 : header == SOH ? 128 : 0;
    if (size == 0)
        return out;
    // header + blk + ~blk + data + crc16
    if (frame.size() < 3 + size + 2)
        return out;

    const auto blk = static_cast<uint8_t>(frame[1]);
    const auto blkComp = static_cast<uint8_t>(frame[2]);
    if (static_cast<uint8_t>(0xFF - blk) != blkComp)
        return out;

    const QByteArray data = frame.mid(3, size);
    const uint16_t crc = (static_cast<uint8_t>(frame[3 + size]) << 8) |
                         static_cast<uint8_t>(frame[3 + size + 1]);
    if (crc16Xmodem(data) != crc)
        return out;

    out.ok = true;
    out.blockNumber = blk;
    out.payload = data;
    return out;
}

QByteArray zdleEncode(const QByteArray &data)
{
    // ZDLE = 0x18. Escape ZDLE itself and the control bytes that a serial line
    // may swallow (XON/XOFF, DLE, CR after '@'). Escaped byte is XORed with 0x40.
    static constexpr unsigned char ZDLE = 0x18;
    QByteArray out;
    for (char ch : data) {
        const auto b = static_cast<unsigned char>(ch);
        bool escape = false;
        switch (b) {
        case ZDLE:            // 0x18
        case 0x10:            // DLE
        case 0x11: case 0x13: // XON/XOFF
        case 0x90: case 0x93: // 8-bit XON/XOFF
            escape = true;
            break;
        default:
            break;
        }
        if (escape) {
            out.append(static_cast<char>(ZDLE));
            out.append(static_cast<char>(b ^ 0x40));
        } else {
            out.append(ch);
        }
    }
    return out;
}

QByteArray zdleDecode(const QByteArray &data)
{
    static constexpr unsigned char ZDLE = 0x18;
    QByteArray out;
    for (int i = 0; i < data.size(); ++i) {
        const auto b = static_cast<unsigned char>(data[i]);
        if (b == ZDLE && i + 1 < data.size()) {
            const auto next = static_cast<unsigned char>(data[++i]);
            out.append(static_cast<char>(next ^ 0x40));
        } else {
            out.append(data[i]);
        }
    }
    return out;
}

} // namespace termsync::transfer::modem
