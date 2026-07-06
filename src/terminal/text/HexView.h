#pragma once

#include <QByteArray>
#include <QString>

namespace termsync::terminal {

// Format a byte buffer as a classic hex dump (offset, hex columns, ASCII gutter)
// — the Hex View feature. Non-printable bytes render as '.'. Pure + testable.
//
//   00000000  48 65 6c 6c 6f 20 77 6f  72 6c 64 0a              |Hello world.|
QString formatHexDump(const QByteArray &data, quint64 baseOffset = 0,
                      int bytesPerRow = 16);

} // namespace termsync::terminal
