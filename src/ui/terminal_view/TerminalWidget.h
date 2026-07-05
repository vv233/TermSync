#pragma once

#include <QColor>
#include <QPoint>
#include <QVector>
#include <QWidget>
#include <memory>

#include "screen/ScreenBuffer.h"
#include "ssh/SshConnection.h"
#include "vt/VtParser.h"

namespace termsync::ui {

// The real terminal view (M3b): renders a ScreenBuffer with QPainter, feeds
// remote bytes through the VtParser, translates keystrokes back to the shell,
// supports scrollback, mouse selection + copy/paste, and reports resizes to
// the remote PTY. Replaces the M2 RawTerminalView.
class TerminalWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TerminalWidget(const core::SshConnectionParams &params,
                            QWidget *parent = nullptr);
    ~TerminalWidget() override;

signals:
    void statusMessage(const QString &message);
    void titleChanged(const QString &title);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private slots:
    void onDataReceived(const QByteArray &data);

private:
    void recomputeCellMetrics();
    int visibleCols() const;
    int visibleRows() const;

    // Document-row access: rows [0..scrollback) are history, then the screen.
    int totalDocRows() const;
    const terminal::Line &docLine(int docRow) const;

    QColor toQColor(const terminal::Color &c, bool bold) const;
    QPoint cellAtPixel(const QPoint &pos) const; // -> (docRow, col)

    void copySelectionToClipboard();
    void pasteFromClipboard();
    QString selectionText() const;
    bool hasSelection() const;
    void clearSelection();

    void scrollToBottom();

    core::SshConnection *m_connection = nullptr;
    std::unique_ptr<terminal::ScreenBuffer> m_screen;
    std::unique_ptr<terminal::VtParser> m_parser;

    // Cell metrics (pixels).
    qreal m_cellW = 8.0;
    qreal m_cellH = 16.0;
    qreal m_baseline = 12.0;

    // Scrollback view: index of the top visible document row.
    int m_topLine = 0;
    bool m_followTail = true;

    bool m_appCursorKeys = false;
    bool m_connected = false;

    // Selection, in document coordinates (row, col). Inclusive-exclusive by
    // normal ordering; empty when anchor == caret and not selecting.
    bool m_selecting = false;
    bool m_hasSelection = false;
    QPoint m_selAnchor;   // (docRow, col)
    QPoint m_selCaret;    // (docRow, col)

    QVector<QColor> m_palette; // 256-entry xterm palette
    QColor m_defaultFg;
    QColor m_defaultBg;
};

} // namespace termsync::ui
