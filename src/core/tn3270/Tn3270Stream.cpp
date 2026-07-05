#include "tn3270/Tn3270Stream.h"

#include <QString>
#include <algorithm>

namespace termsync::core {

namespace {

constexpr unsigned char CMD_WRITE = 0xF1;
constexpr unsigned char CMD_ERASE_WRITE = 0xF5;
constexpr unsigned char CMD_ERASE_WRITE_ALT = 0x7E;
constexpr unsigned char ORDER_SBA = 0x11;
constexpr unsigned char ORDER_EUA = 0x12;
constexpr unsigned char ORDER_IC = 0x13;
constexpr unsigned char ORDER_SF = 0x1D;
constexpr unsigned char ORDER_PT = 0x05;
constexpr unsigned char ORDER_RA = 0x3C;

char32_t ebcdicToUnicode(unsigned char c)
{
    if (c == 0x40 || c == 0x00)
        return U' ';
    if (c >= 0xF0 && c <= 0xF9)
        return U'0' + (c - 0xF0);
    if (c >= 0xC1 && c <= 0xC9)
        return U'A' + (c - 0xC1);
    if (c >= 0xD1 && c <= 0xD9)
        return U'J' + (c - 0xD1);
    if (c >= 0xE2 && c <= 0xE9)
        return U'S' + (c - 0xE2);
    if (c >= 0x81 && c <= 0x89)
        return U'a' + (c - 0x81);
    if (c >= 0x91 && c <= 0x99)
        return U'j' + (c - 0x91);
    if (c >= 0xA2 && c <= 0xA9)
        return U's' + (c - 0xA2);

    switch (c) {
    case 0x4B: return U'.';
    case 0x6B: return U',';
    case 0x5A: return U'!';
    case 0x6E: return U'>';
    case 0x4C: return U'<';
    case 0x5B: return U'$';
    case 0x7B: return U'#';
    case 0x7E: return U'=';
    case 0x60: return U'-';
    case 0x61: return U'/';
    case 0x6C: return U'%';
    case 0x7C: return U'@';
    case 0x4D: return U'(';
    case 0x5D: return U')';
    case 0x50: return U'&';
    case 0x7A: return U':';
    case 0x5E: return U';';
    case 0x6D: return U'_';
    case 0x7D: return U'\'';
    case 0x7F: return U'"';
    case 0x4E: return U'+';
    case 0x5C: return U'*';
    case 0x4F: return U'|';
    case 0x6F: return U'?';
    default: return U' ';
    }
}

unsigned char asciiToEbcdic(char c)
{
    if (c == ' ')
        return 0x40;
    if (c >= '0' && c <= '9')
        return static_cast<unsigned char>(0xF0 + (c - '0'));
    if (c >= 'A' && c <= 'I')
        return static_cast<unsigned char>(0xC1 + (c - 'A'));
    if (c >= 'J' && c <= 'R')
        return static_cast<unsigned char>(0xD1 + (c - 'J'));
    if (c >= 'S' && c <= 'Z')
        return static_cast<unsigned char>(0xE2 + (c - 'S'));
    if (c >= 'a' && c <= 'i')
        return static_cast<unsigned char>(0x81 + (c - 'a'));
    if (c >= 'j' && c <= 'r')
        return static_cast<unsigned char>(0x91 + (c - 'j'));
    if (c >= 's' && c <= 'z')
        return static_cast<unsigned char>(0xA2 + (c - 's'));

    switch (c) {
    case '.': return 0x4B;
    case ',': return 0x6B;
    case '!': return 0x5A;
    case '>': return 0x6E;
    case '<': return 0x4C;
    case '$': return 0x5B;
    case '#': return 0x7B;
    case '=': return 0x7E;
    case '-': return 0x60;
    case '/': return 0x61;
    case '%': return 0x6C;
    case '@': return 0x7C;
    case '(': return 0x4D;
    case ')': return 0x5D;
    case '&': return 0x50;
    case ':': return 0x7A;
    case ';': return 0x5E;
    case '_': return 0x6D;
    case '\'': return 0x7D;
    case '"': return 0x7F;
    case '+': return 0x4E;
    case '*': return 0x5C;
    case '|': return 0x4F;
    case '?': return 0x6F;
    default: return 0x40;
    }
}

} // namespace

Tn3270Stream::Tn3270Stream(int rows, int cols)
{
    reset(rows, cols);
}

void Tn3270Stream::reset(int rows, int cols)
{
    m_rows = rows;
    m_cols = cols;
    m_cursor = 0;
    m_cells.fill(U' ', cellCount());
    m_touched.fill(false, cellCount());
    m_fields.clear();
}

int Tn3270Stream::clampCell(int cell) const
{
    if (cell < 0)
        return 0;
    if (cell >= cellCount())
        return cellCount() - 1;
    return cell;
}

int Tn3270Stream::decodeAddress(unsigned char hi, unsigned char lo) const
{
    return clampCell(((hi & 0x3F) << 6) | (lo & 0x3F));
}

QByteArray Tn3270Stream::encodeAddress(int cell) const
{
    cell = clampCell(cell);
    QByteArray out;
    out.append(static_cast<char>(0x40 | ((cell >> 6) & 0x3F)));
    out.append(static_cast<char>(0x40 | (cell & 0x3F)));
    return out;
}

void Tn3270Stream::processRecord(const QByteArray &record)
{
    if (record.isEmpty())
        return;

    int i = 0;
    const auto command = static_cast<unsigned char>(record[i++]);
    if (command == CMD_ERASE_WRITE || command == CMD_ERASE_WRITE_ALT)
        reset(m_rows, m_cols);
    if (command == CMD_WRITE || command == CMD_ERASE_WRITE || command == CMD_ERASE_WRITE_ALT) {
        if (i < record.size())
            ++i; // WCC
    }

    while (i < record.size()) {
        const auto b = static_cast<unsigned char>(record[i++]);
        switch (b) {
        case ORDER_SBA:
            if (i + 1 < record.size()) {
                setCursor(decodeAddress(static_cast<unsigned char>(record[i]),
                                        static_cast<unsigned char>(record[i + 1])));
                i += 2;
            }
            break;
        case ORDER_SF:
            if (i < record.size())
                startField(static_cast<unsigned char>(record[i++]));
            break;
        case ORDER_IC:
            break;
        case ORDER_PT:
            setCursor(firstInputCell());
            break;
        case ORDER_RA:
            if (i + 2 < record.size()) {
                const int end = decodeAddress(static_cast<unsigned char>(record[i]),
                                              static_cast<unsigned char>(record[i + 1]));
                const auto fill = static_cast<unsigned char>(record[i + 2]);
                i += 3;
                while (m_cursor != end) {
                    putChar(fill);
                    if (m_cursor == 0)
                        break;
                }
            }
            break;
        case ORDER_EUA:
            if (i + 1 < record.size()) {
                const int end = decodeAddress(static_cast<unsigned char>(record[i]),
                                              static_cast<unsigned char>(record[i + 1]));
                i += 2;
                while (m_cursor != end) {
                    const int field = fieldIndexForCell(m_cursor);
                    if (field >= 0 && !m_fields[field].protectedField)
                        m_cells[m_cursor] = U' ';
                    setCursor(m_cursor + 1);
                    if (m_cursor == 0)
                        break;
                }
            }
            break;
        default:
            putChar(b);
            break;
        }
    }
    rebuildFieldEnds();
    if (!m_fields.isEmpty())
        setCursor(firstInputCell());
}

QByteArray Tn3270Stream::renderAsVt() const
{
    QByteArray out("\x1b[2J\x1b[H");
    for (int row = 0; row < m_rows; ++row) {
        out += "\x1b[" + QByteArray::number(row + 1) + ";1H";
        QString line;
        line.reserve(m_cols);
        for (int col = 0; col < m_cols; ++col)
            line += QChar(static_cast<char16_t>(m_cells[row * m_cols + col]));
        out += line.toUtf8();
    }
    out += "\x1b[" + QByteArray::number((m_cursor / m_cols) + 1) + ";" +
           QByteArray::number((m_cursor % m_cols) + 1) + "H";
    return out;
}

void Tn3270Stream::insertText(const QByteArray &text)
{
    for (char ch : text) {
        if (ch < 0x20 || ch == 0x7F)
            continue;
        const int field = fieldIndexForCell(m_cursor);
        if (field < 0 || m_fields[field].protectedField) {
            setCursor(firstInputCell());
            continue;
        }
        m_cells[m_cursor] = QChar::fromLatin1(ch).unicode();
        m_touched[m_cursor] = true;
        m_fields[field].modified = true;
        setCursor(m_cursor + 1);
        if (m_cursor >= m_fields[field].end)
            setCursor(m_fields[field].end - 1);
    }
}

void Tn3270Stream::backspace()
{
    const int field = fieldIndexForCell(m_cursor - 1);
    if (field < 0 || m_fields[field].protectedField)
        return;
    setCursor(m_cursor - 1);
    m_cells[m_cursor] = U' ';
    m_touched[m_cursor] = true;
    m_fields[field].modified = true;
}

void Tn3270Stream::moveCursor(int delta)
{
    setCursor(m_cursor + delta);
}

QByteArray Tn3270Stream::submitEnter()
{
    QByteArray out;
    out.append(static_cast<char>(0x7D)); // AID Enter
    out += encodeAddress(m_cursor);
    for (Field &field : m_fields) {
        if (field.protectedField || !field.modified)
            continue;
        const int first = field.start + 1;
        if (first >= field.end)
            continue;
        out.append(static_cast<char>(ORDER_SBA));
        out += encodeAddress(first);
        for (int cell = first; cell < field.end; ++cell) {
            const QChar ch(static_cast<char16_t>(m_cells[cell]));
            out.append(static_cast<char>(asciiToEbcdic(ch.toLatin1())));
            m_touched[cell] = false;
        }
        field.modified = false;
    }
    return out;
}

QString Tn3270Stream::plainText() const
{
    QString out;
    for (int row = 0; row < m_rows; ++row) {
        QString line;
        for (int col = 0; col < m_cols; ++col)
            line += QChar(static_cast<char16_t>(m_cells[row * m_cols + col]));
        out += line;
        out += '\n';
    }
    return out;
}

int Tn3270Stream::fieldIndexForCell(int cell) const
{
    if (m_fields.isEmpty())
        return -1;
    cell = clampCell(cell);
    for (int i = 0; i < m_fields.size(); ++i) {
        if (cell > m_fields[i].start && cell < m_fields[i].end)
            return i;
    }
    return -1;
}

int Tn3270Stream::firstInputCell() const
{
    for (const Field &field : m_fields) {
        if (!field.protectedField && field.start + 1 < field.end)
            return field.start + 1;
    }
    return 0;
}

void Tn3270Stream::rebuildFieldEnds()
{
    std::sort(m_fields.begin(), m_fields.end(), [](const Field &a, const Field &b) {
        return a.start < b.start;
    });
    for (int i = 0; i < m_fields.size(); ++i)
        m_fields[i].end = (i + 1 < m_fields.size()) ? m_fields[i + 1].start : cellCount();
}

void Tn3270Stream::setCursor(int cell)
{
    if (cell < 0)
        cell = 0;
    m_cursor = cell % cellCount();
}

void Tn3270Stream::putChar(unsigned char ebcdic)
{
    m_cells[m_cursor] = ebcdicToUnicode(ebcdic);
    setCursor(m_cursor + 1);
}

void Tn3270Stream::startField(unsigned char attr)
{
    Field field;
    field.start = m_cursor;
    field.protectedField = (attr & 0x20) != 0;
    m_cells[m_cursor] = U' ';
    m_touched[m_cursor] = false;
    m_fields.append(field);
    setCursor(m_cursor + 1);
}

} // namespace termsync::core
