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
constexpr unsigned char ORDER_SFE = 0x29;  // start field extended
constexpr unsigned char ORDER_SA = 0x28;   // set attribute
constexpr unsigned char ORDER_MF = 0x2C;   // modify field
constexpr unsigned char ORDER_PT = 0x05;
constexpr unsigned char ORDER_RA = 0x3C;

// Extended attribute type codes.
constexpr unsigned char ATTR_FIELD = 0xC0;
constexpr unsigned char ATTR_HIGHLIGHT = 0x41;
constexpr unsigned char ATTR_COLOR = 0x42;

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
        case ORDER_SFE: {
            // SFE: <count> then <type,value> pairs. Start a field and apply
            // the basic-field/color/highlight attributes from the pairs.
            if (i >= record.size())
                break;
            const int count = static_cast<unsigned char>(record[i++]);
            unsigned char attr = 0x20, color = 0, highlight = 0;
            for (int p = 0; p < count && i + 1 < record.size(); ++p) {
                const auto type = static_cast<unsigned char>(record[i]);
                const auto value = static_cast<unsigned char>(record[i + 1]);
                i += 2;
                if (type == ATTR_FIELD) attr = value;
                else if (type == ATTR_COLOR) color = value;
                else if (type == ATTR_HIGHLIGHT) highlight = value;
            }
            startFieldExtended(attr, color, highlight);
            break;
        }
        case ORDER_SA:
            // Set-attribute for following characters: skip the type/value pair
            // (character-level rendition is not yet applied to cells).
            if (i + 1 < record.size())
                i += 2;
            break;
        case ORDER_MF: {
            // Modify field: skip <count> type/value pairs.
            if (i >= record.size())
                break;
            const int count = static_cast<unsigned char>(record[i++]);
            i += count * 2;
            break;
        }
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

unsigned char Tn3270Stream::aidForPf(int n)
{
    if (n >= 1 && n <= 9)
        return static_cast<unsigned char>(0xF0 + n);   // PF1..PF9
    if (n == 10) return 0x7A;
    if (n == 11) return 0x7B;
    if (n == 12) return 0x7C;
    if (n >= 13 && n <= 21)
        return static_cast<unsigned char>(0xC0 + (n - 12)); // PF13..PF21 = C1..C9
    if (n == 22) return 0x4A;
    if (n == 23) return 0x4B;
    if (n == 24) return 0x4C;
    return AID_ENTER;
}

QByteArray Tn3270Stream::submit(unsigned char aid)
{
    QByteArray out;
    out.append(static_cast<char>(aid));

    // PA keys and Clear produce a "short read": AID + cursor address only.
    if (aid == AID_PA1 || aid == AID_PA2 || aid == AID_PA3 || aid == AID_CLEAR) {
        out += encodeAddress(m_cursor);
        return out;
    }

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

void Tn3270Stream::nextField()
{
    if (m_fields.isEmpty())
        return;
    const int n = m_fields.size();
    // Find the field the cursor is in (or after), then advance to the next
    // unprotected field's first input cell.
    for (int step = 1; step <= n; ++step) {
        // Look for the next field whose start is > cursor (wrapping).
        int bestStart = -1, bestIdx = -1;
        for (int i = 0; i < n; ++i) {
            if (m_fields[i].protectedField)
                continue;
            const int s = m_fields[i].start;
            if (s > m_cursor && (bestStart < 0 || s < bestStart)) {
                bestStart = s;
                bestIdx = i;
            }
        }
        if (bestIdx < 0) {
            // Wrap to the first unprotected field.
            setCursor(0);
            setCursor(firstInputCell());
            return;
        }
        setCursor(m_fields[bestIdx].start + 1);
        return;
    }
}

void Tn3270Stream::prevField()
{
    if (m_fields.isEmpty())
        return;
    int bestStart = -1, bestIdx = -1;
    for (int i = 0; i < m_fields.size(); ++i) {
        if (m_fields[i].protectedField)
            continue;
        const int s = m_fields[i].start;
        if (s < m_cursor - 1 && s > bestStart) {
            bestStart = s;
            bestIdx = i;
        }
    }
    if (bestIdx < 0) {
        // Wrap to the last unprotected field.
        for (int i = 0; i < m_fields.size(); ++i)
            if (!m_fields[i].protectedField && m_fields[i].start > bestStart) {
                bestStart = m_fields[i].start;
                bestIdx = i;
            }
    }
    if (bestIdx >= 0)
        setCursor(m_fields[bestIdx].start + 1);
}

void Tn3270Stream::home()
{
    setCursor(firstInputCell());
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
    startFieldExtended(attr, 0, 0);
}

void Tn3270Stream::startFieldExtended(unsigned char attr, unsigned char color,
                                      unsigned char highlight)
{
    Field field;
    field.start = m_cursor;
    field.protectedField = (attr & 0x20) != 0;
    field.attrByte = attr;
    field.color = color;
    field.highlight = highlight;
    m_cells[m_cursor] = U' ';
    m_touched[m_cursor] = false;
    m_fields.append(field);
    setCursor(m_cursor + 1);
}

} // namespace termsync::core
