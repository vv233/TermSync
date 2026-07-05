#pragma once

#include <QByteArray>
#include <QVector>

namespace termsync::core {

class Tn3270Stream
{
public:
    // 3270 Attention-Identifier (AID) codes.
    enum Aid : unsigned char {
        AID_ENTER = 0x7D,
        AID_CLEAR = 0x6D,
        AID_PA1 = 0x6C, AID_PA2 = 0x6E, AID_PA3 = 0x6B,
        // PF1..PF12 (PF1-9 = 0xF1..0xF9, PF10-12 = 0x7A/0x7B/0x7C).
    };
    // Returns the AID byte for PF key n (1..24).
    static unsigned char aidForPf(int n);

    struct Field
    {
        int start = 0;       // field attribute cell
        int end = 0;         // exclusive; wraps are not modelled
        bool protectedField = true;
        bool modified = false;
        // Extended attributes (M16b): 3270 field attribute byte + color/highlight.
        unsigned char attrByte = 0x20;
        unsigned char color = 0;      // 3270 color value (0 = default)
        unsigned char highlight = 0;  // 0=normal, F1=blink, F2=reverse, F4=underline
        bool intensified() const { return ((attrByte >> 2) & 0x03) == 2; }
        bool hidden() const { return ((attrByte >> 2) & 0x03) == 3; }
        bool numeric() const { return (attrByte & 0x10) != 0; }
    };

    Tn3270Stream(int rows = 24, int cols = 80);

    void reset(int rows = 24, int cols = 80);
    void processRecord(const QByteArray &record);
    QByteArray renderAsVt() const;

    void insertText(const QByteArray &text);
    void backspace();
    void moveCursor(int delta);
    // Field navigation (M16b).
    void nextField();
    void prevField();
    void home();
    // Builds the inbound record for the given AID (Enter/PF submit modified
    // fields; PA/Clear send a "short read" of just AID + cursor address).
    QByteArray submit(unsigned char aid);
    QByteArray submitEnter() { return submit(AID_ENTER); }

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
    void startFieldExtended(unsigned char attr, unsigned char color,
                            unsigned char highlight);

    int m_rows = 24;
    int m_cols = 80;
    int m_cursor = 0;
    QVector<char32_t> m_cells;
    QVector<bool> m_touched;
    QVector<Field> m_fields;
};

} // namespace termsync::core
