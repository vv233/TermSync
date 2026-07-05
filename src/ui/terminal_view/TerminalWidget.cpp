#include "terminal_view/TerminalWidget.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>
#include <algorithm>

namespace termsync::ui {

using terminal::Cell;
using terminal::CellFlag;
using terminal::Color;
using terminal::Line;

namespace {

// Build the standard xterm 256-color palette: 16 base + 6x6x6 cube + grays.
QVector<QColor> buildPalette()
{
    QVector<QColor> p(256);
    static const int base[16][3] = {
        {0, 0, 0},       {205, 0, 0},     {0, 205, 0},     {205, 205, 0},
        {0, 0, 238},     {205, 0, 205},   {0, 205, 205},   {229, 229, 229},
        {127, 127, 127}, {255, 0, 0},     {0, 255, 0},     {255, 255, 0},
        {92, 92, 255},   {255, 0, 255},   {0, 255, 255},   {255, 255, 255},
    };
    for (int i = 0; i < 16; ++i)
        p[i] = QColor(base[i][0], base[i][1], base[i][2]);

    const int steps[6] = {0, 95, 135, 175, 215, 255};
    int idx = 16;
    for (int r = 0; r < 6; ++r)
        for (int g = 0; g < 6; ++g)
            for (int b = 0; b < 6; ++b)
                p[idx++] = QColor(steps[r], steps[g], steps[b]);

    for (int i = 0; i < 24; ++i) {
        const int v = 8 + i * 10;
        p[232 + i] = QColor(v, v, v);
    }
    return p;
}

} // namespace

TerminalWidget::TerminalWidget(const core::SshConnectionParams &params,
                               QWidget *parent)
    : QWidget(parent)
    , m_screen(std::make_unique<terminal::ScreenBuffer>(params.cols, params.rows,
                                                        5000))
    , m_parser(std::make_unique<terminal::VtParser>(m_screen.get()))
    , m_palette(buildPalette())
    , m_defaultFg(QColor(0xcc, 0xcc, 0xcc))
    , m_defaultBg(QColor(0x1e, 0x1e, 0x1e))
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setCursor(Qt::IBeamCursor);

    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setPointSize(11);
    setFont(f);
    recomputeCellMetrics();

    // Parser -> widget notifications.
    m_parser->onTitleChanged = [this](const QString &t) { emit titleChanged(t); };
    m_parser->onApplicationCursorKeys = [this](bool on) { m_appCursorKeys = on; };

    // SSH wiring.
    m_connection = new core::SshConnection(this);
    connect(m_connection, &core::SshConnection::dataReceived, this,
            &TerminalWidget::onDataReceived);
    connect(m_connection, &core::SshConnection::connected, this, [this] {
        m_connected = true;
        emit statusMessage(tr("Connected"));
    });
    connect(m_connection, &core::SshConnection::hostKeyFingerprint, this,
            [this](const QString &fp) {
                m_parser->parse(QByteArray("[host key SHA256: ") + fp.toUtf8() +
                                "]\r\n");
                update();
            });
    connect(m_connection, &core::SshConnection::authenticationFailed, this,
            [this](const QString &r) {
                m_parser->parse(QByteArray("\r\n[authentication failed: ") +
                                r.toUtf8() + "]\r\n");
                emit statusMessage(tr("Authentication failed"));
                update();
            });
    connect(m_connection, &core::SshConnection::errorOccurred, this,
            [this](const QString &m) {
                m_parser->parse(QByteArray("\r\n[error: ") + m.toUtf8() + "]\r\n");
                emit statusMessage(tr("Error: %1").arg(m));
                update();
            });
    connect(m_connection, &core::SshConnection::disconnected, this, [this] {
        m_connected = false;
        m_parser->parse("\r\n[disconnected]\r\n");
        emit statusMessage(tr("Disconnected"));
        update();
    });

    m_connection->connectToHost(params);
}

TerminalWidget::~TerminalWidget() = default;

void TerminalWidget::recomputeCellMetrics()
{
    QFontMetricsF fm(font());
    m_cellW = std::ceil(fm.horizontalAdvance(QLatin1Char('M')));
    m_cellH = std::ceil(fm.height());
    m_baseline = fm.ascent();
}

int TerminalWidget::visibleCols() const
{
    return std::max(1, static_cast<int>(width() / m_cellW));
}

int TerminalWidget::visibleRows() const
{
    return std::max(1, static_cast<int>(height() / m_cellH));
}

int TerminalWidget::totalDocRows() const
{
    return m_screen->scrollbackSize() + m_screen->rows();
}

const Line &TerminalWidget::docLine(int docRow) const
{
    const int sb = m_screen->scrollbackSize();
    if (docRow < sb)
        return m_screen->scrollback()[docRow];
    return m_screen->line(docRow - sb);
}

QColor TerminalWidget::toQColor(const Color &c, bool bold) const
{
    switch (c.type) {
    case Color::Type::Default:
        return m_defaultFg; // caller decides fg vs bg; see paintEvent
    case Color::Type::Indexed: {
        int i = c.index;
        if (bold && i < 8) // brighten bold base colors, common convention
            i += 8;
        return m_palette[i];
    }
    case Color::Type::Rgb:
        return QColor(c.r, c.g, c.b);
    }
    return m_defaultFg;
}

void TerminalWidget::onDataReceived(const QByteArray &data)
{
    m_parser->parse(data);
    if (m_followTail)
        scrollToBottom();
    update();
}

void TerminalWidget::scrollToBottom()
{
    m_topLine = std::max(0, totalDocRows() - visibleRows());
    m_followTail = true;
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------
void TerminalWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.fillRect(event->rect(), m_defaultBg);

    const int rows = visibleRows();
    const int cols = m_screen->cols();
    const int sb = m_screen->scrollbackSize();
    const int cursorDocRow = sb + m_screen->cursorRow();
    const bool showCursor = m_screen->cursorVisible() && m_followTail;

    QFont normalFont = font();
    QFont boldFont = font();
    boldFont.setBold(true);

    for (int viewRow = 0; viewRow < rows; ++viewRow) {
        const int docRow = m_topLine + viewRow;
        if (docRow < 0 || docRow >= totalDocRows())
            continue;
        const Line &line = docLine(docRow);
        const qreal y = viewRow * m_cellH;

        for (int col = 0; col < cols && col < line.size(); ++col) {
            const Cell &cell = line[col];
            const bool bold = cell.hasFlag(CellFlag::Bold);

            QColor fg = cell.fg.type == Color::Type::Default
                            ? m_defaultFg
                            : toQColor(cell.fg, bold);
            QColor bg = cell.bg.type == Color::Type::Default
                            ? m_defaultBg
                            : toQColor(cell.bg, false);

            if (cell.hasFlag(CellFlag::Reverse))
                std::swap(fg, bg);

            // Selection highlight.
            bool selected = false;
            if (m_hasSelection) {
                QPoint a = m_selAnchor, b = m_selCaret;
                if (a.x() > b.x() || (a.x() == b.x() && a.y() > b.y()))
                    std::swap(a, b);
                const QPoint here(docRow, col);
                const bool afterStart =
                    here.x() > a.x() || (here.x() == a.x() && here.y() >= a.y());
                const bool beforeEnd =
                    here.x() < b.x() || (here.x() == b.x() && here.y() < b.y());
                selected = afterStart && beforeEnd;
            }
            if (selected)
                std::swap(fg, bg);

            const QRectF cellRect(col * m_cellW, y, m_cellW, m_cellH);
            if (bg != m_defaultBg || selected)
                painter.fillRect(cellRect, bg);

            const bool isCursor = showCursor && docRow == cursorDocRow &&
                                  col == m_screen->cursorCol();
            if (isCursor) {
                painter.fillRect(cellRect, fg);
                painter.setPen(bg);
            } else {
                painter.setPen(fg);
            }

            if (cell.ch != U' ' && !cell.hasFlag(CellFlag::Invisible)) {
                painter.setFont(bold ? boldFont : normalFont);
                painter.drawText(QPointF(col * m_cellW, y + m_baseline),
                                 QString(QChar(static_cast<char16_t>(cell.ch))));
            }

            if (cell.hasFlag(CellFlag::Underline)) {
                painter.setPen(fg);
                const qreal uy = y + m_baseline + 1.5;
                painter.drawLine(QPointF(cellRect.left(), uy),
                                 QPointF(cellRect.right(), uy));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Resize -> PTY
// ---------------------------------------------------------------------------
void TerminalWidget::resizeEvent(QResizeEvent *)
{
    const int cols = visibleCols();
    const int rows = visibleRows();
    m_screen->resize(cols, rows);
    if (m_connected)
        m_connection->resize(cols, rows);
    scrollToBottom();
    update();
}

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------
void TerminalWidget::keyPressEvent(QKeyEvent *event)
{
    // Copy/paste shortcuts (terminal convention: Ctrl+Shift+C/V).
    if (event->modifiers().testFlag(Qt::ControlModifier) &&
        event->modifiers().testFlag(Qt::ShiftModifier)) {
        if (event->key() == Qt::Key_C) { copySelectionToClipboard(); return; }
        if (event->key() == Qt::Key_V) { pasteFromClipboard(); return; }
    }

    if (!m_connected) {
        QWidget::keyPressEvent(event);
        return;
    }

    const char *appPrefix = m_appCursorKeys ? "\x1bO" : "\x1b[";
    QByteArray bytes;
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:    bytes = "\r"; break;
    case Qt::Key_Backspace: bytes = "\x7f"; break;
    case Qt::Key_Tab:      bytes = "\t"; break;
    case Qt::Key_Escape:   bytes = "\x1b"; break;
    case Qt::Key_Up:       bytes = QByteArray(appPrefix) + "A"; break;
    case Qt::Key_Down:     bytes = QByteArray(appPrefix) + "B"; break;
    case Qt::Key_Right:    bytes = QByteArray(appPrefix) + "C"; break;
    case Qt::Key_Left:     bytes = QByteArray(appPrefix) + "D"; break;
    case Qt::Key_Home:     bytes = QByteArray(appPrefix) + "H"; break;
    case Qt::Key_End:      bytes = QByteArray(appPrefix) + "F"; break;
    case Qt::Key_PageUp:   bytes = "\x1b[5~"; break;
    case Qt::Key_PageDown: bytes = "\x1b[6~"; break;
    case Qt::Key_Delete:   bytes = "\x1b[3~"; break;
    case Qt::Key_Insert:   bytes = "\x1b[2~"; break;
    default:
        if (event->modifiers().testFlag(Qt::ControlModifier) &&
            !event->text().isEmpty()) {
            const QChar c = event->text().at(0).toUpper();
            if (c >= '@' && c <= '_')
                bytes = QByteArray(1, static_cast<char>(c.toLatin1() - '@'));
            else if (c >= 'a' && c <= 'z')
                bytes = QByteArray(1, static_cast<char>(c.toLatin1() - 'a' + 1));
        }
        if (bytes.isEmpty() && !event->text().isEmpty())
            bytes = event->text().toUtf8();
        break;
    }

    if (!bytes.isEmpty()) {
        m_connection->sendData(bytes);
        scrollToBottom();
    }
}

// ---------------------------------------------------------------------------
// Scrollback
// ---------------------------------------------------------------------------
void TerminalWidget::wheelEvent(QWheelEvent *event)
{
    const int lines = event->angleDelta().y() / 40; // ~3 lines per notch
    if (lines == 0)
        return;
    const int maxTop = std::max(0, totalDocRows() - visibleRows());
    m_topLine = std::clamp(m_topLine - lines, 0, maxTop);
    m_followTail = (m_topLine >= maxTop);
    update();
}

// ---------------------------------------------------------------------------
// Mouse selection
// ---------------------------------------------------------------------------
QPoint TerminalWidget::cellAtPixel(const QPoint &pos) const
{
    const int viewRow = static_cast<int>(pos.y() / m_cellH);
    const int col = static_cast<int>(pos.x() / m_cellW);
    const int docRow = m_topLine + viewRow;
    return QPoint(docRow, std::clamp(col, 0, m_screen->cols() - 1));
}

void TerminalWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_selecting = true;
        m_selAnchor = cellAtPixel(event->pos());
        m_selCaret = m_selAnchor;
        m_hasSelection = false;
        update();
    } else if (event->button() == Qt::MiddleButton) {
        // X11-style middle-click paste.
        pasteFromClipboard();
    }
    setFocus();
}

void TerminalWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_selecting)
        return;
    m_selCaret = cellAtPixel(event->pos());
    m_hasSelection = (m_selCaret != m_selAnchor);
    update();
}

void TerminalWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        m_selecting = false;
}

void TerminalWidget::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    QAction *copyAct = menu.addAction(tr("Copy"));
    copyAct->setEnabled(hasSelection());
    QAction *pasteAct = menu.addAction(tr("Paste"));
    connect(copyAct, &QAction::triggered, this,
            &TerminalWidget::copySelectionToClipboard);
    connect(pasteAct, &QAction::triggered, this,
            &TerminalWidget::pasteFromClipboard);
    menu.exec(event->globalPos());
}

bool TerminalWidget::hasSelection() const
{
    return m_hasSelection;
}

void TerminalWidget::clearSelection()
{
    m_hasSelection = false;
    update();
}

QString TerminalWidget::selectionText() const
{
    if (!m_hasSelection)
        return {};
    QPoint a = m_selAnchor, b = m_selCaret;
    if (a.x() > b.x() || (a.x() == b.x() && a.y() > b.y()))
        std::swap(a, b);

    QString out;
    for (int row = a.x(); row <= b.x(); ++row) {
        if (row < 0 || row >= totalDocRows())
            continue;
        const Line &line = docLine(row);
        const int startCol = (row == a.x()) ? a.y() : 0;
        const int endCol = (row == b.x()) ? b.y() : line.size(); // exclusive
        QString rowText;
        for (int c = startCol; c < endCol && c < line.size(); ++c)
            rowText += QChar(static_cast<char16_t>(line[c].ch));
        if (row != b.x())
            out += rowText + '\n';
        else
            out += rowText;
    }
    return out;
}

void TerminalWidget::copySelectionToClipboard()
{
    const QString text = selectionText();
    if (!text.isEmpty())
        QApplication::clipboard()->setText(text);
}

void TerminalWidget::pasteFromClipboard()
{
    if (!m_connected)
        return;
    const QString text = QApplication::clipboard()->text();
    if (text.isEmpty())
        return;
    QByteArray payload = text.toUtf8();
    if (m_parser->bracketedPaste())
        payload = QByteArray("\x1b[200~") + payload + "\x1b[201~";
    m_connection->sendData(payload);
    scrollToBottom();
}

} // namespace termsync::ui
