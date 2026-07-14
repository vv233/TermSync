#include "ssh/X11Auth.h"

#include <QDir>
#include <QFile>
#include <QRandomGenerator>

namespace termsync::core::x11 {

namespace {

// Pad a length up to the next multiple of 4 (X11 field alignment).
int pad4(int n) { return (4 - (n & 3)) & 3; }

uint16_t readU16(const QByteArray &b, int off, bool bigEndian)
{
    const auto lo = static_cast<uint8_t>(b[off]);
    const auto hi = static_cast<uint8_t>(b[off + 1]);
    return bigEndian ? uint16_t((lo << 8) | hi) : uint16_t((hi << 8) | lo);
}

// Xauthority integers are always 16-bit big-endian.
uint16_t readXauthU16(const QByteArray &b, int off)
{
    return uint16_t((static_cast<uint8_t>(b[off]) << 8) |
                    static_cast<uint8_t>(b[off + 1]));
}

void appendXauthU16(QByteArray &b, int value)
{
    b.append(char((value >> 8) & 0xff));
    b.append(char(value & 0xff));
}

void appendXauthField(QByteArray &b, const QByteArray &value)
{
    appendXauthU16(b, value.size());
    b.append(value);
}

} // namespace

QByteArray generateCookie()
{
    QByteArray c(kCookieBytes, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(
        reinterpret_cast<quint32 *>(c.data()), kCookieBytes / sizeof(quint32));
    return c;
}

QString cookieToHex(const QByteArray &cookie)
{
    return QString::fromLatin1(cookie.toHex());
}

QByteArray makeXauthority(int display, const QByteArray &cookie)
{
    if (display < 0 || display > 65535 || cookie.size() != kCookieBytes)
        return {};

    QByteArray blob;
    appendXauthU16(blob, 0xffff); // FamilyWild
    appendXauthField(blob, {});  // address
    appendXauthField(blob, QByteArray::number(display));
    appendXauthField(blob, QByteArray(kAuthProtocol()));
    appendXauthField(blob, cookie);
    return blob;
}

QByteArray parseXauthority(const QByteArray &blob, int display)
{
    // Each entry: family(2) addrLen(2) addr numberLen(2) number nameLen(2) name
    //             dataLen(2) data — all lengths 16-bit big-endian.
    QByteArray anyCookie;
    int off = 0;
    const QByteArray wantName(kAuthProtocol());
    const QByteArray wantNumber = QByteArray::number(display);
    while (off + 2 <= blob.size()) {
        off += 2; // family
        auto field = [&](QByteArray *out) -> bool {
            if (off + 2 > blob.size())
                return false;
            const int len = readXauthU16(blob, off);
            off += 2;
            if (off + len > blob.size())
                return false;
            if (out)
                *out = blob.mid(off, len);
            off += len;
            return true;
        };
        QByteArray addr, number, name, data;
        if (!field(&addr) || !field(&number) || !field(&name) || !field(&data))
            break;
        if (name == wantName) {
            if (number == wantNumber)
                return data; // exact display match wins
            if (anyCookie.isEmpty())
                anyCookie = data;
        }
    }
    return anyCookie;
}

QByteArray readLocalCookie(int display, const QString &xauthorityPath)
{
    QString path = xauthorityPath;
    if (path.isEmpty())
        path = qEnvironmentVariable("XAUTHORITY");
    if (path.isEmpty()) {
        const QString home = QDir::homePath();
        if (!home.isEmpty())
            path = home + QStringLiteral("/.Xauthority");
    }
    if (path.isEmpty())
        return {};
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return parseXauthority(f.readAll(), display);
}

RewriteResult rewriteSetup(const QByteArray &data, const QByteArray &proxyCookie,
                           const QByteArray &localCookie)
{
    RewriteResult r;
    if (data.size() < 12) {
        r.status = RewriteStatus::NeedMore;
        return r;
    }
    const char order = data[0];
    bool bigEndian;
    if (order == 0x42) // 'B'
        bigEndian = true;
    else if (order == 0x6C) // 'l'
        bigEndian = false;
    else {
        r.status = RewriteStatus::Malformed;
        return r;
    }

    const int nameLen = readU16(data, 6, bigEndian);
    const int dataLen = readU16(data, 8, bigEndian);
    const int nameOff = 12;
    const int dataOff = nameOff + nameLen + pad4(nameLen);
    const int total = dataOff + dataLen + pad4(dataLen);
    if (data.size() < total) {
        r.status = RewriteStatus::NeedMore;
        return r;
    }

    r.consumed = total;

    // No auth data in the packet, or we have no local cookie to substitute:
    // forward the setup packet unchanged (works with -ac X servers).
    if (dataLen == 0 || localCookie.isEmpty()) {
        r.status = RewriteStatus::Passthrough;
        r.rewritten = data.left(total);
        return r;
    }

    const QByteArray presented = data.mid(dataOff, dataLen);
    if (!proxyCookie.isEmpty() && presented != proxyCookie) {
        r.status = RewriteStatus::Mismatch; // possible spoofing attempt
        return r;
    }
    if (localCookie.size() != dataLen) {
        // Different cookie length would require rebuilding the packet; the only
        // protocol we advertise is MIT-MAGIC-COOKIE-1 (16 bytes), so treat a
        // mismatch as malformed rather than silently corrupting the stream.
        r.status = RewriteStatus::Malformed;
        return r;
    }

    QByteArray out = data.left(total);
    out.replace(dataOff, dataLen, localCookie);
    r.status = RewriteStatus::Ok;
    r.rewritten = out;
    return r;
}

} // namespace termsync::core::x11
