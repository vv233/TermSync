#include "text/HexView.h"

namespace termsync::terminal {

QString formatHexDump(const QByteArray &data, quint64 baseOffset, int bytesPerRow)
{
    if (bytesPerRow <= 0)
        bytesPerRow = 16;

    QString out;
    const int total = data.size();
    for (int row = 0; row < total; row += bytesPerRow) {
        out += QString("%1  ").arg(baseOffset + static_cast<quint64>(row), 8, 16,
                                   QLatin1Char('0'));

        QString ascii;
        for (int col = 0; col < bytesPerRow; ++col) {
            const int idx = row + col;
            if (col == bytesPerRow / 2)
                out += ' '; // gap between the two halves
            if (idx < total) {
                const unsigned char b = static_cast<unsigned char>(data[idx]);
                out += QString("%1 ").arg(b, 2, 16, QLatin1Char('0'));
                ascii += (b >= 0x20 && b < 0x7f) ? QChar(b) : QChar('.');
            } else {
                out += QStringLiteral("   "); // pad missing byte columns
            }
        }
        out += QStringLiteral(" |") + ascii + QLatin1Char('|');
        if (row + bytesPerRow < total)
            out += '\n';
    }
    return out;
}

} // namespace termsync::terminal
