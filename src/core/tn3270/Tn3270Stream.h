#pragma once

#include <QByteArray>
#include <QVector>

namespace termsync::core {

class Tn3270Stream
{
public:
    struct Field
    {
        int start = 0;       // field attribute cell
        int end = 0;         // exclusive; wraps are not modelled in M16a
        bool protectedField = true;
        bool modified = false;
    };

    Tn3270Stream(int rows = 24, int cols = 80);

    void reset(int rows = 24, int cols = 80);
    void processRecord(const QByteArray &record);
    QByteArray renderAsVt() const;

    void insertText(const QByteArray &text);
    void backspace();
    void moveCursor(int delta);
    QByteArray submitEnter();

    QString plainText() const;
    int cursor() const { return m_cursor; }
    int rows() const { return m_rows; }
    int cols() const { return m_cols; }
    const QVector<Field> &fields() const { return m_fields; }

private:
    int cellCount() const { return m_rows * m_cols; }
    int clampCell(int cell) const;
    int decodeAddress(unsigned char hi, unsigned char lo) const;
    QByteArray encodeAddress(int cell) const;
    int fieldIndexForCell(int cell) const;
    int firstInputCell() const;
    void rebuildFieldEnds();
    void setCursor(int cell);
    void putChar(unsigned char ebcdic);
    void startField(unsigned char attr);

    int m_rows = 24;
    int m_cols = 80;
    int m_cursor = 0;
    QVector<char32_t> m_cells;
    QVector<bool> m_touched;
    QVector<Field> m_fields;
};

} // namespace termsync::core
