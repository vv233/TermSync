#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace termsync::core {

// A minimal TN5250 Write-to-Display parser (first pass). It renders the screen
// text from the common WTD orders (SBA / RA / IC / displayable EBCDIC); field
// format tables and inbound AID/field submission are follow-ups. Kept pure so
// the core parsing is unit-testable without an AS/400 host.
class Tn5250Stream
{
public:
    Tn5250Stream(int rows = 24, int cols = 80);

    void reset(int rows = 24, int cols = 80);
    void processRecord(const QByteArray &record);
    QByteArray renderAsVt() const;
    QString plainText() const;
    int cursor() const { return m_cursor; }

private:
    int cellCount() const { return m_rows * m_cols; }
    void put(unsigned char ebcdic);
    void setPos(int row, int col); // 1-based row/col

    int m_rows;
    int m_cols;
    int m_cursor = 0;
    QVector<char32_t> m_cells;
};

} // namespace termsync::core
