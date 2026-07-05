#include "tn3270/Tn5250Stream.h"

namespace termsync::core {

namespace {

// Compact EBCDIC -> Unicode for displayable characters.
char32_t ebcdic(unsigned char c)
{
    if (c == 0x40 || c == 0x00) return U' ';
    if (c >= 0xF0 && c <= 0xF9) return U'0' + (c - 0xF0);
    if (c >= 0xC1 && c <= 0xC9) return U'A' + (c - 0xC1);
    if (c >= 0xD1 && c <= 0xD9) return U'J' + (c - 0xD1);
    if (c >= 0xE2 && c <= 0xE9) return U'S' + (c - 0xE2);
    if (c >= 0x81 && c <= 0x89) return U'a' + (c - 0x81);
    if (c >= 0x91 && c <= 0x99) return U'j' + (c - 0x91);
    if (c >= 0xA2 && c <= 0xA9) return U's' + (c - 0xA2);
    switch (c) {
    case 0x4B: return U'.'; case 0x6B: return U','; case 0x60: return U'-';
    case 0x61: return U'/'; case 0x7A: return U':'; case 0x5C: return U'*';
    case 0x4D: return U'('; case 0x5D: return U')'; case 0x50: return U'&';
    case 0x7C: return U'@'; case 0x5B: return U'$'; case 0x6C: return U'%';
    default: return U' ';
    }
}

// WTD orders.
constexpr unsigned char ESC = 0x04;
constexpr unsigned char CMD_WTD = 0x11;
constexpr unsigned char ORDER_SBA = 0x11; // set buffer address (row,col)
constexpr unsigned char ORDER_RA  = 0x02; // repeat to address
constexpr unsigned char ORDER_SF  = 0x1D; // start of field
constexpr unsigned char ORDER_IC  = 0x13; // insert cursor
constexpr unsigned char ORDER_SOH = 0x01;

} // namespace

Tn5250Stream::Tn5250Stream(int rows, int cols) { reset(rows, cols); }

void Tn5250Stream::reset(int rows, int cols)
{
    m_rows = rows;
    m_cols = cols;
    m_cursor = 0;
    m_cells.fill(U' ', cellCount());
}

void Tn5250Stream::setPos(int row, int col)
{
    if (row < 1) row = 1;
    if (col < 1) col = 1;
    m_cursor = ((row - 1) * m_cols + (col - 1)) % cellCount();
    if (m_cursor < 0) m_cursor = 0;
}

void Tn5250Stream::put(unsigned char c)
{
    if (m_cursor >= 0 && m_cursor < cellCount())
        m_cells[m_cursor] = ebcdic(c);
    m_cursor = (m_cursor + 1) % cellCount();
}

void Tn5250Stream::processRecord(const QByteArray &record)
{
    int i = 0;
    while (i < record.size()) {
        const auto b = static_cast<unsigned char>(record[i]);
        if (b == ESC && i + 1 < record.size()) {
            const auto cmd = static_cast<unsigned char>(record[i + 1]);
            i += 2;
            if (cmd == CMD_WTD) {
                // Two control characters follow the WTD command.
                if (i + 1 < record.size())
                    i += 2;
            }
            continue;
        }
        switch (b) {
        case ORDER_SBA:
            if (i + 2 < record.size()) {
                setPos(static_cast<unsigned char>(record[i + 1]),
                       static_cast<unsigned char>(record[i + 2]));
                i += 3;
            } else { i = record.size(); }
            break;
        case ORDER_RA:
            if (i + 3 < record.size()) {
                const int r = static_cast<unsigned char>(record[i + 1]);
                const int c = static_cast<unsigned char>(record[i + 2]);
                const auto fill = static_cast<unsigned char>(record[i + 3]);
                const int stop = ((r - 1) * m_cols + (c - 1)) % cellCount();
                i += 4;
                int guard = cellCount();
                while (m_cursor != stop && guard-- > 0)
                    put(fill);
            } else { i = record.size(); }
            break;
        case ORDER_SF:
            // Skip the field format attribute byte(s) (first pass: 1 byte).
            i += 2;
            break;
        case ORDER_IC:
            i += 1;
            break;
        case ORDER_SOH:
            // Start-of-header: skip its length byte + that many bytes.
            if (i + 1 < record.size()) {
                const int len = static_cast<unsigned char>(record[i + 1]);
                i += 2 + len;
            } else { i = record.size(); }
            break;
        default:
            put(b);
            ++i;
            break;
        }
    }
}

QByteArray Tn5250Stream::renderAsVt() const
{
    QByteArray out("\x1b[2J\x1b[H");
    for (int row = 0; row < m_rows; ++row) {
        out += "\x1b[" + QByteArray::number(row + 1) + ";1H";
        QString line;
        for (int col = 0; col < m_cols; ++col)
            line += QChar(static_cast<char16_t>(m_cells[row * m_cols + col]));
        out += line.toUtf8();
    }
    return out;
}

QString Tn5250Stream::plainText() const
{
    QString out;
    for (int row = 0; row < m_rows; ++row) {
        for (int col = 0; col < m_cols; ++col)
            out += QChar(static_cast<char16_t>(m_cells[row * m_cols + col]));
        out += '\n';
    }
    return out;
}

} // namespace termsync::core
