#include "tftp/TftpProtocol.h"

namespace termsync::core::tftp {

namespace {
void appendU16(QByteArray &b, uint16_t v)
{
    b.append(static_cast<char>((v >> 8) & 0xff));
    b.append(static_cast<char>(v & 0xff));
}

uint16_t readU16(const QByteArray &b, int offset)
{
    return static_cast<uint16_t>((static_cast<uint8_t>(b[offset]) << 8) |
                                 static_cast<uint8_t>(b[offset + 1]));
}
} // namespace

QByteArray buildRequest(OpCode op, const QString &filename, const QString &mode)
{
    QByteArray b;
    appendU16(b, static_cast<uint16_t>(op));
    b.append(filename.toUtf8());
    b.append('\0');
    b.append(mode.toLatin1());
    b.append('\0');
    return b;
}

QByteArray buildData(uint16_t block, const QByteArray &payload)
{
    QByteArray b;
    appendU16(b, static_cast<uint16_t>(OpCode::Data));
    appendU16(b, block);
    b.append(payload);
    return b;
}

QByteArray buildAck(uint16_t block)
{
    QByteArray b;
    appendU16(b, static_cast<uint16_t>(OpCode::Ack));
    appendU16(b, block);
    return b;
}

QByteArray buildError(ErrorCode code, const QString &message)
{
    QByteArray b;
    appendU16(b, static_cast<uint16_t>(OpCode::Error));
    appendU16(b, static_cast<uint16_t>(code));
    b.append(message.toUtf8());
    b.append('\0');
    return b;
}

Packet parse(const QByteArray &d)
{
    Packet p;
    if (d.size() < 2)
        return p;
    const uint16_t opcode = readU16(d, 0);
    switch (opcode) {
    case static_cast<uint16_t>(OpCode::Rrq):
    case static_cast<uint16_t>(OpCode::Wrq): {
        // opcode | filename | 0 | mode | 0
        const int nameEnd = d.indexOf('\0', 2);
        if (nameEnd < 0)
            return p;
        const int modeEnd = d.indexOf('\0', nameEnd + 1);
        if (modeEnd < 0)
            return p;
        p.op = static_cast<OpCode>(opcode);
        p.filename = QString::fromUtf8(d.mid(2, nameEnd - 2));
        p.mode = QString::fromLatin1(d.mid(nameEnd + 1, modeEnd - nameEnd - 1));
        p.valid = !p.filename.isEmpty();
        return p;
    }
    case static_cast<uint16_t>(OpCode::Data): {
        if (d.size() < 4)
            return p;
        p.op = OpCode::Data;
        p.block = readU16(d, 2);
        p.payload = d.mid(4);
        p.valid = true;
        return p;
    }
    case static_cast<uint16_t>(OpCode::Ack): {
        if (d.size() < 4)
            return p;
        p.op = OpCode::Ack;
        p.block = readU16(d, 2);
        p.valid = true;
        return p;
    }
    case static_cast<uint16_t>(OpCode::Error): {
        if (d.size() < 4)
            return p;
        p.op = OpCode::Error;
        p.error = static_cast<ErrorCode>(readU16(d, 2));
        const int msgEnd = d.indexOf('\0', 4);
        p.message = QString::fromUtf8(
            msgEnd < 0 ? d.mid(4) : d.mid(4, msgEnd - 4));
        p.valid = true;
        return p;
    }
    default:
        return p;
    }
}

} // namespace termsync::core::tftp
