#pragma once

#include <QColor>
#include <QPoint>
#include <QVector>
#include <QWidget>
#include <functional>
#include <memory>

#include "AbstractTerminalConnection.h"
#include "screen/ScreenBuffer.h"
#include "ssh/SshConnection.h"
#include "text/KeywordHighlighter.h"
#include "theme/ColorScheme.h"
#include "vt/VtParser.h"

namespace termsync::core {
class SessionLogger;
}

namespace termsync::ui {

class ConnectingOverlay;

// The real terminal view (M3b): renders a ScreenBuffer with QPainter, feeds
// remote bytes through the VtParser, translates keystrokes back to the shell,
// supports scrollback, select-to-copy + right-click paste, and reports resizes to
// the remote PTY. Replaces the M2 RawTerminalView.
class TerminalWidget : public QWidget
{
    Q_OBJECT

public:
    // SSH session (creates and owns an SshConnection, with host-key handling).
    explicit TerminalWidget(const core::SshConnectionParams &params,
                            QWidget *parent = nullptr);
    // Generic session over any connection (Telnet, serial, ...). Takes
    // ownership; the caller initiates the connection after construction.
    explicit TerminalWidget(core::AbstractTerminalConnection *connection,
                            QWidget *parent = nullptr);
    ~TerminalWidget() override;

    // Host-key verifier: given the server's SHA-256 fingerprint, decide whether
    // to trust it and invoke the supplied callback with the result. If unset,
    // the widget auto-approves (used by standalone tests). MainWindow installs
    // one backed by the known-hosts store + a trust dialog.
    using HostKeyVerifier =
        std::function<void(const QString &fingerprint,
                           std::function<void(bool accept)> respond)>;
    void setHostKeyVerifier(HostKeyVerifier verifier)
    {
        m_hostKeyVerifier = std::move(verifier);
    }

    // Scripting hooks (M15): send bytes to the session and read the current
    // screen contents as plain text.
    void sendText(const QByteArray &bytes);
    QString screenPlainText() const;
    // Whole document (scrollback + screen) as plain text, trailing blank lines
    // trimmed. Backs File -> Print.
    QString documentPlainText() const;

    // Edit actions (M20 polish): backing for the app's Edit menu. copy/paste
    // mirror the mouse/keyboard behaviour, selectAll selects the whole document
    // (screen + scrollback), and the clear operations wipe the visible screen or
    // the scrollback history. hasSelection() reports whether Copy has anything.
    void editCopy();
    void editPaste();
    void editSelectAll();
    void clearScreen();
    void clearScrollback();
    bool hasSelection() const;

    // In-terminal search (M20 polish, backs Edit -> Find). Searches the document
    // (scrollback + screen) for `needle` starting from the current selection,
    // wrapping once. On a hit it selects the match and scrolls it into view.
    // Matching is single-line. With fromStart the current selection is ignored
    // and the scan begins at the top (used for incremental type-ahead search).
    // Returns true if a match was found.
    bool find(const QString &needle, bool forward, bool caseSensitive,
              bool fromStart = false);

    // Session lifecycle (M20 polish). isConnected() reflects the live link.
    // disconnectSession() tears it down. The respawn handler, installed by
    // MainWindow at creation, knows how to open an identical session (same
    // profile/params); it backs both Reconnect and Clone Session.
    bool isConnected() const { return m_connected; }
    void disconnectSession();
    void setRespawnHandler(std::function<void()> handler)
    {
        m_respawn = std::move(handler);
    }
    bool canRespawn() const { return static_cast<bool>(m_respawn); }
    void respawn() const
    {
        if (m_respawn)
            m_respawn();
    }

    // Session logging (M20b): tee raw received bytes to a file. `pathTemplate`
    // may contain TermSync tokens (%H host, %S session, %Y/%M/%D/%h/%m/%s
    // date-time) which are expanded using the context set by setLogContext().
    // Returns false if the file could not be opened.
    void setLogContext(const QString &host, const QString &session);
    bool startLogging(const QString &pathTemplate, bool timestampLines = false);
    void stopLogging();
    bool isLogging() const;
    QString logPath() const;

    // Keyword highlighting (M20a): colour matching text on each rendered line.
    // Each rule's colorId indexes into a small highlight palette (wrapping).
    void setHighlightRules(const QVector<terminal::HighlightRule> &rules);
    const QVector<terminal::HighlightRule> &highlightRules() const;

    // Hex View (M20a): render the raw incoming byte stream as a hex dump instead
    // of the emulated screen. The terminal keeps parsing underneath, so toggling
    // back restores the live screen.
    void setHexView(bool on);
    bool isHexView() const { return m_hexView; }

    // Appearance (M20): terminal colour scheme + font. applyColorScheme recolours
    // the background/foreground/cursor and the 16 ANSI palette entries;
    // setTerminalFont changes the glyph font and re-flows the screen.
    void applyColorScheme(const terminal::ColorScheme &scheme);
    QString colorSchemeName() const { return m_schemeName; }
    void setTerminalFont(const QFont &font);
    QFont terminalFont() const { return font(); }

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
    void clearSelection();

    void scrollToBottom();
    void ensureRowVisible(int docRow); // adjust m_topLine so docRow is on screen
    void paintHexView(class QPainter &painter); // renders m_hexBuffer as a dump

    void initView();       // shared widget setup (font, blink, metrics)
    void wireConnection(); // connect the common AbstractTerminalConnection signals

    core::AbstractTerminalConnection *m_connection = nullptr;
    core::SshConnection *m_ssh = nullptr; // non-null only for SSH sessions
    std::unique_ptr<terminal::ScreenBuffer> m_screen;
    std::unique_ptr<terminal::VtParser> m_parser;

    // Session logging (M20b).
    std::unique_ptr<core::SessionLogger> m_logger;
    QString m_logHost;
    QString m_logSession;

    // Keyword highlighting (M20a).
    terminal::KeywordHighlighter m_highlighter;
    QVector<QColor> m_highlightColors; // colorId -> colour (wraps)

    // Hex View (M20a): rolling buffer of raw received bytes + mode flag.
    bool m_hexView = false;
    QByteArray m_hexBuffer;

    // "Connecting…" overlay shown over the terminal until the session connects.
    ConnectingOverlay *m_connecting = nullptr;
    void dismissConnecting();

    // Cell metrics (pixels).
    qreal m_cellW = 8.0;
    qreal m_cellH = 16.0;
    qreal m_baseline = 12.0;
    qreal m_padX = 6.0;   // inner padding so glyphs don't hug the edge
    qreal m_padY = 4.0;

    // Cursor blink.
    class QTimer *m_blinkTimer = nullptr;
    bool m_cursorBlinkOn = true;

    // Scrollback view: index of the top visible document row.
    int m_topLine = 0;
    bool m_followTail = true;

    bool m_appCursorKeys = false;
    bool m_connected = false;
    HostKeyVerifier m_hostKeyVerifier;
    std::function<void()> m_respawn; // reopens an identical session (Reconnect/Clone)

    // Selection, in document coordinates (row, col). Inclusive-exclusive by
    // normal ordering; empty when anchor == caret and not selecting.
    bool m_selecting = false;
    bool m_hasSelection = false;
    QPoint m_selAnchor;   // (docRow, col)
    QPoint m_selCaret;    // (docRow, col)

    QVector<QColor> m_palette; // 256-entry xterm palette
    QColor m_defaultFg;
    QColor m_defaultBg;
    QColor m_cursorColor{0xc8, 0xd0, 0xe8};
    QString m_schemeName;
};

} // namespace termsync::ui
