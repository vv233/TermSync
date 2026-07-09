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
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>

#include "log/SessionLogger.h"
#include "terminal_view/ConnectingOverlay.h"
#include "text/HexView.h"

namespace termsync::ui {

namespace {
// Cap on the rolling hex-view buffer: keep roughly the last 128 KiB of stream.
constexpr int kHexBufferCap = 128 * 1024;

QFont normalizedTerminalFont(QFont font)
{
    QFontDatabase db;
    const QStringList installedFamilies = db.families();
    const QStringList preferredFamilies = {
        QStringLiteral("Cascadia Mono"),
        QStringLiteral("Cascadia Code"),
        QStringLiteral("Consolas"),
        QStringLiteral("Courier New"),
        QStringLiteral("DejaVu Sans Mono"),
        QStringLiteral("Menlo"),
    };
    auto installedFamily = [&installedFamilies](const QString &candidate) {
        for (const QString &installed : installedFamilies) {
            if (installed.compare(candidate, Qt::CaseInsensitive) == 0)
                return installed;
        }
        return QString();
    };

    QString family;
    for (const QString &candidate : font.families()) {
        if (db.isFixedPitch(candidate)) {
            family = candidate;
            break;
        }
    }
    if (family.isEmpty() && db.isFixedPitch(font.family()))
        family = font.family();
    if (family.isEmpty()) {
        for (const QString &candidate : preferredFamilies) {
            family = installedFamily(candidate);
            if (!family.isEmpty())
                break;
        }
    }
    if (family.isEmpty())
        family = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();

    const int pointSize = font.pointSize() > 0 ? font.pointSize() : 11;
    font = QFont(family);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setKerning(false);
    font.setPointSize(pointSize);

    return font;
}

qreal terminalCellWidth(const QFontMetricsF &fm)
{
    const qreal digitWidth = fm.horizontalAdvance(QLatin1Char('0'));
    if (digitWidth > 0.0)
        return std::ceil(digitWidth);
    return std::max<qreal>(1.0, std::ceil(fm.averageCharWidth()));
}
} // namespace

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
    , m_defaultFg(QColor(0xc8, 0xd0, 0xe8))
    , m_defaultBg(QColor(0x1a, 0x1b, 0x26))
{
    initView();

    auto *ssh = new core::SshConnection(this);
    m_ssh = ssh;
    m_connection = ssh;
    wireConnection();

    // SSH-specific: host-key trust-on-first-use, then authenticate.
    connect(ssh, &core::SshConnection::hostKeyFingerprint, this,
            [this, ssh](const QString &fp) {
                m_parser->parse(QByteArray("[host key SHA256: ") + fp.toUtf8() +
                                "]\r\n");
                update();
                if (m_hostKeyVerifier)
                    m_hostKeyVerifier(fp, [ssh](bool accept) {
                        ssh->approveHostKey(accept);
                    });
                else
                    ssh->approveHostKey(true); // auto-trust (tests)
            });
    connect(ssh, &core::SshConnection::authenticationFailed, this,
            [this](const QString &r) {
                m_parser->parse(QByteArray("\r\n[authentication failed: ") +
                                r.toUtf8() + "]\r\n");
                emit statusMessage(tr("Authentication failed"));
                update();
            });

    // Termius-style connecting overlay until the shell opens.
    m_connecting = new ConnectingOverlay(this);
    m_connecting->setTitle(params.host);
    m_connecting->setSubtitle(
        tr("SSH %1:%2").arg(params.host).arg(params.port));
    connect(m_connecting, &ConnectingOverlay::dismissed, this,
            &TerminalWidget::dismissConnecting);
    m_connecting->setGeometry(rect());
    m_connecting->raise();
    m_connecting->show();

    ssh->connectToHost(params);
}

TerminalWidget::TerminalWidget(core::AbstractTerminalConnection *connection,
                               QWidget *parent)
    : QWidget(parent)
    , m_screen(std::make_unique<terminal::ScreenBuffer>(80, 24, 5000))
    , m_parser(std::make_unique<terminal::VtParser>(m_screen.get()))
    , m_palette(buildPalette())
    , m_defaultFg(QColor(0xc8, 0xd0, 0xe8))
    , m_defaultBg(QColor(0x1a, 0x1b, 0x26))
{
    initView();
    m_connection = connection;
    connection->setParent(this);
    wireConnection();
    // The caller initiates the connection after construction so its signals are
    // already wired here.
}

TerminalWidget::~TerminalWidget() = default;

void TerminalWidget::initView()
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setCursor(Qt::IBeamCursor);

    // Prefer a good programming font; fall back to the platform's fixed font.
    QFont f;
    f.setFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("Cascadia Code"),
                   QStringLiteral("Consolas"), QStringLiteral("Menlo"),
                   QStringLiteral("DejaVu Sans Mono"),
                   QFontDatabase::systemFont(QFontDatabase::FixedFont).family()});
    f.setStyleHint(QFont::Monospace);
    f.setFixedPitch(true);
    f.setKerning(false);
    f.setPointSize(11);
    setFont(normalizedTerminalFont(f));
    recomputeCellMetrics();

    m_blinkTimer = new QTimer(this);
    m_blinkTimer->setInterval(530);
    connect(m_blinkTimer, &QTimer::timeout, this, [this] {
        m_cursorBlinkOn = !m_cursorBlinkOn;
        update();
    });
    m_blinkTimer->start();

    m_parser->onTitleChanged = [this](const QString &t) { emit titleChanged(t); };
    m_parser->onApplicationCursorKeys = [this](bool on) { m_appCursorKeys = on; };

    // Start on the default colour scheme (MainWindow may override per app prefs).
    if (const terminal::ColorScheme *s =
            terminal::findScheme(terminal::defaultSchemeName()))
        applyColorScheme(*s);

    // Default keyword-highlight palette (colorId wraps over these).
    m_highlightColors = {
        QColor(0xff, 0xd7, 0x00), // amber
        QColor(0xff, 0x5f, 0x5f), // red
        QColor(0x5f, 0xff, 0x87), // green
        QColor(0x5f, 0xd7, 0xff), // cyan
        QColor(0xff, 0x87, 0xff), // magenta
    };
}

void TerminalWidget::wireConnection()
{
    connect(m_connection, &core::AbstractTerminalConnection::dataReceived, this,
            &TerminalWidget::onDataReceived);
    connect(m_connection, &core::AbstractTerminalConnection::connected, this, [this] {
        m_connected = true;
        dismissConnecting();
        emit statusMessage(tr("Connected"));
    });
    connect(m_connection, &core::AbstractTerminalConnection::errorOccurred, this,
            [this](const QString &m) {
                m_parser->parse(QByteArray("\r\n[error: ") + m.toUtf8() + "]\r\n");
                if (m_connecting)
                    m_connecting->setFailed(m);
                emit statusMessage(tr("Error: %1").arg(m));
                update();
            });
    connect(m_connection, &core::AbstractTerminalConnection::disconnected, this, [this] {
        m_connected = false;
        m_parser->parse("\r\n[disconnected]\r\n");
        emit statusMessage(tr("Disconnected"));
        update();
    });
}

void TerminalWidget::recomputeCellMetrics()
{
    QFontMetricsF fm(font());
    m_cellW = terminalCellWidth(fm);
    m_cellH = std::ceil(fm.height()) + 2.0; // a little line spacing
    m_baseline = fm.ascent() + 1.0;
}

int TerminalWidget::visibleCols() const
{
    return std::max(1, static_cast<int>((width() - 2 * m_padX) / m_cellW));
}

int TerminalWidget::visibleRows() const
{
    return std::max(1, static_cast<int>((height() - 2 * m_padY) / m_cellH));
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

void TerminalWidget::sendText(const QByteArray &bytes)
{
    if (m_connection)
        m_connection->sendData(bytes);
}

QString TerminalWidget::screenPlainText() const
{
    QString out;
    for (int r = 0; r < m_screen->rows(); ++r) {
        const terminal::Line &line = m_screen->line(r);
        QString rowText;
        for (const terminal::Cell &c : line)
            rowText += QChar(static_cast<char16_t>(c.ch));
        out += rowText.trimmed().isEmpty() ? QString() : rowText;
        out += '\n';
    }
    return out;
}

void TerminalWidget::onDataReceived(const QByteArray &data)
{
    if (m_logger && m_logger->isOpen())
        m_logger->write(data);

    // Keep a rolling buffer of raw bytes for the Hex View, capped in size.
    m_hexBuffer.append(data);
    if (m_hexBuffer.size() > kHexBufferCap)
        m_hexBuffer.remove(0, m_hexBuffer.size() - kHexBufferCap);

    m_parser->parse(data);
    if (m_followTail)
        scrollToBottom();
    // Keep the cursor solid during active output, and restart the blink cycle.
    m_cursorBlinkOn = true;
    if (m_blinkTimer)
        m_blinkTimer->start();
    update();
}

void TerminalWidget::scrollToBottom()
{
    m_topLine = std::max(0, totalDocRows() - visibleRows());
    m_followTail = true;
}

void TerminalWidget::setLogContext(const QString &host, const QString &session)
{
    m_logHost = host;
    m_logSession = session;
    if (m_connecting)
        m_connecting->setTitle(session);
}

bool TerminalWidget::startLogging(const QString &pathTemplate, bool timestampLines)
{
    stopLogging();
    const QString path = core::expandLogFilename(
        pathTemplate, m_logHost, m_logSession, QDateTime::currentDateTime());

    auto logger = std::make_unique<core::SessionLogger>();
    core::SessionLogOptions opts;
    opts.timestampLines = timestampLines;
    if (!logger->open(path, opts)) {
        emit statusMessage(tr("Could not open log file: %1").arg(path));
        return false;
    }
    m_logger = std::move(logger);
    emit statusMessage(tr("Logging session to %1").arg(m_logger->path()));
    return true;
}

void TerminalWidget::stopLogging()
{
    if (m_logger) {
        m_logger->close();
        m_logger.reset();
    }
}

bool TerminalWidget::isLogging() const
{
    return m_logger && m_logger->isOpen();
}

QString TerminalWidget::logPath() const
{
    return m_logger ? m_logger->path() : QString();
}

void TerminalWidget::setHighlightRules(
    const QVector<terminal::HighlightRule> &rules)
{
    m_highlighter.setRules(rules);
    update();
}

const QVector<terminal::HighlightRule> &TerminalWidget::highlightRules() const
{
    return m_highlighter.rules();
}

void TerminalWidget::setHexView(bool on)
{
    if (m_hexView == on)
        return;
    m_hexView = on;
    update();
}

void TerminalWidget::applyColorScheme(const terminal::ColorScheme &scheme)
{
    auto toColor = [](uint32_t v) {
        return QColor((v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);
    };
    m_defaultBg = toColor(scheme.background);
    m_defaultFg = toColor(scheme.foreground);
    m_cursorColor = toColor(scheme.cursor);
    // The scheme reseeds the 16 base ANSI colours; the 6x6x6 cube + greys above
    // index 15 are scheme-independent and stay as built by buildPalette().
    for (int i = 0; i < 16 && i < m_palette.size(); ++i)
        m_palette[i] = toColor(scheme.ansi[i]);
    m_schemeName = scheme.name;
    update();
}

void TerminalWidget::setTerminalFont(const QFont &f)
{
    setFont(normalizedTerminalFont(f));
    recomputeCellMetrics();
    // Re-flow the screen to the new cell grid.
    const int cols = visibleCols();
    const int rows = visibleRows();
    m_screen->resize(cols, rows);
    if (m_connected)
        m_connection->resize(cols, rows);
    scrollToBottom();
    update();
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------
void TerminalWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.fillRect(event->rect(), m_defaultBg);

    if (m_hexView) {
        paintHexView(painter);
        return;
    }

    const int rows = visibleRows();
    const int cols = m_screen->cols();
    const int sb = m_screen->scrollbackSize();
    const int cursorDocRow = sb + m_screen->cursorRow();
    const bool focused = hasFocus();
    const bool showCursor = m_screen->cursorVisible() && m_followTail &&
                            (m_cursorBlinkOn || !focused);

    QFont normalFont = font();
    QFont boldFont = font();
    boldFont.setBold(true);

    for (int viewRow = 0; viewRow < rows; ++viewRow) {
        const int docRow = m_topLine + viewRow;
        if (docRow < 0 || docRow >= totalDocRows())
            continue;
        const Line &line = docLine(docRow);
        const qreal y = m_padY + viewRow * m_cellH;

        // Keyword highlighting: map each column to a highlight colour id (-1 =
        // none) by matching the line's plain text against the active rules.
        QVector<int> hlColorId;
        if (!m_highlighter.rules().isEmpty()) {
            QString text(cols, QLatin1Char(' '));
            const int n = std::min(cols, static_cast<int>(line.size()));
            for (int c = 0; c < n; ++c) {
                const char32_t ch = line[c].ch;
                text[c] = QChar(ch == 0 ? u' ' : static_cast<char16_t>(ch));
            }
            hlColorId = QVector<int>(cols, -1);
            for (const terminal::HighlightSpan &span : m_highlighter.highlight(text)) {
                for (int i = span.start;
                     i < span.start + span.length && i < cols; ++i)
                    hlColorId[i] = span.colorId;
            }
        }

        for (int col = 0; col < cols && col < line.size(); ++col) {
            const qreal x = m_padX + col * m_cellW;
            const Cell &cell = line[col];
            const bool bold = cell.hasFlag(CellFlag::Bold);

            QColor fg = cell.fg.type == Color::Type::Default
                            ? m_defaultFg
                            : toQColor(cell.fg, bold);
            QColor bg = cell.bg.type == Color::Type::Default
                            ? m_defaultBg
                            : toQColor(cell.bg, false);

            // Keyword highlight overrides the foreground for matched columns.
            if (!hlColorId.isEmpty() && hlColorId[col] >= 0 &&
                !m_highlightColors.isEmpty())
                fg = m_highlightColors[hlColorId[col] % m_highlightColors.size()];

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

            const QRectF cellRect(x, y, m_cellW, m_cellH);
            if (bg != m_defaultBg || selected)
                painter.fillRect(cellRect, bg);

            const bool isCursor = showCursor && docRow == cursorDocRow &&
                                  col == m_screen->cursorCol();
            if (isCursor) {
                painter.fillRect(cellRect, m_cursorColor);
                painter.setPen(m_defaultBg);
            } else {
                painter.setPen(fg);
            }

            if (cell.ch != U' ' && !cell.hasFlag(CellFlag::Invisible)) {
                painter.setFont(bold ? boldFont : normalFont);
                painter.drawText(QPointF(x, y + m_baseline),
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

void TerminalWidget::paintHexView(QPainter &painter)
{
    const QString dump = terminal::formatHexDump(m_hexBuffer);
    const QList<QStringView> lines = QStringView(dump).split(u'\n');

    const int rows = visibleRows();
    // Show the tail of the dump (most recent bytes), like a live console.
    int first = 0;
    if (lines.size() > rows)
        first = lines.size() - rows;

    painter.setFont(font());
    painter.setPen(m_defaultFg);
    int viewRow = 0;
    for (int i = first; i < lines.size() && viewRow < rows; ++i, ++viewRow) {
        const qreal y = m_padY + viewRow * m_cellH;
        painter.drawText(QPointF(m_padX, y + m_baseline), lines.at(i).toString());
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
    if (m_connecting)
        m_connecting->setGeometry(rect());
    scrollToBottom();
    update();
}

void TerminalWidget::dismissConnecting()
{
    if (m_connecting) {
        m_connecting->deleteLater();
        m_connecting = nullptr;
    }
    setFocus();
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
    case Qt::Key_Backtab:  bytes = "\x1b[Z"; break;
    // Function keys (xterm sequences): F1-F4 use SS3, F5-F12 use CSI ~.
    case Qt::Key_F1:  bytes = "\x1bOP"; break;
    case Qt::Key_F2:  bytes = "\x1bOQ"; break;
    case Qt::Key_F3:  bytes = "\x1bOR"; break;
    case Qt::Key_F4:  bytes = "\x1bOS"; break;
    case Qt::Key_F5:  bytes = "\x1b[15~"; break;
    case Qt::Key_F6:  bytes = "\x1b[17~"; break;
    case Qt::Key_F7:  bytes = "\x1b[18~"; break;
    case Qt::Key_F8:  bytes = "\x1b[19~"; break;
    case Qt::Key_F9:  bytes = "\x1b[20~"; break;
    case Qt::Key_F10: bytes = "\x1b[21~"; break;
    case Qt::Key_F11: bytes = "\x1b[23~"; break;
    case Qt::Key_F12: bytes = "\x1b[24~"; break;
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
    const int viewRow = static_cast<int>((pos.y() - m_padY) / m_cellH);
    const int col = static_cast<int>((pos.x() - m_padX) / m_cellW);
    const int docRow = m_topLine + std::max(0, viewRow);
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
