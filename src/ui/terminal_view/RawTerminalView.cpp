#include "terminal_view/RawTerminalView.h"

#include <QFont>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QScrollBar>

namespace termsync::ui {

RawTerminalView::RawTerminalView(const core::SshConnectionParams &params,
                                 QWidget *parent)
    : QPlainTextEdit(parent)
{
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setUndoRedoEnabled(false);
    // We render remote output and translate our own key events, so the
    // built-in text cursor editing is not used.
    setTextInteractionFlags(Qt::TextSelectableByMouse |
                            Qt::TextSelectableByKeyboard);

    m_connection = new core::SshConnection(this);

    connect(m_connection, &core::SshConnection::dataReceived, this,
            &RawTerminalView::onDataReceived);
    connect(m_connection, &core::SshConnection::connected, this, [this] {
        emit statusMessage(tr("Connected"));
    });
    connect(m_connection, &core::SshConnection::hostKeyFingerprint, this,
            [this](const QString &fp) {
                appendPlainText(tr("[host key SHA256: %1]").arg(fp));
            });
    connect(m_connection, &core::SshConnection::authenticationFailed, this,
            [this](const QString &reason) {
                appendPlainText(tr("[authentication failed: %1]").arg(reason));
                emit statusMessage(tr("Authentication failed"));
            });
    connect(m_connection, &core::SshConnection::errorOccurred, this,
            [this](const QString &msg) {
                appendPlainText(tr("[error: %1]").arg(msg));
                emit statusMessage(tr("Error: %1").arg(msg));
            });
    connect(m_connection, &core::SshConnection::disconnected, this, [this] {
        appendPlainText(tr("\n[disconnected]"));
        emit statusMessage(tr("Disconnected"));
    });

    m_connection->connectToHost(params);
}

void RawTerminalView::onDataReceived(const QByteArray &data)
{
    // Append at the end and keep the view scrolled to the bottom. Raw bytes
    // are decoded as UTF-8; control/escape characters remain visible in M2.
    moveCursor(QTextCursor::End);
    insertPlainText(QString::fromUtf8(data));
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void RawTerminalView::keyPressEvent(QKeyEvent *event)
{
    if (!m_connection || !m_connection->isConnected()) {
        QPlainTextEdit::keyPressEvent(event);
        return;
    }

    QByteArray bytes;
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        bytes = "\r";
        break;
    case Qt::Key_Backspace:
        bytes = "\x7f";
        break;
    case Qt::Key_Tab:
        bytes = "\t";
        break;
    case Qt::Key_Escape:
        bytes = "\x1b";
        break;
    case Qt::Key_Up:
        bytes = "\x1b[A";
        break;
    case Qt::Key_Down:
        bytes = "\x1b[B";
        break;
    case Qt::Key_Right:
        bytes = "\x1b[C";
        break;
    case Qt::Key_Left:
        bytes = "\x1b[D";
        break;
    case Qt::Key_Home:
        bytes = "\x1b[H";
        break;
    case Qt::Key_End:
        bytes = "\x1b[F";
        break;
    case Qt::Key_Delete:
        bytes = "\x1b[3~";
        break;
    default:
        if (event->modifiers().testFlag(Qt::ControlModifier) &&
            !event->text().isEmpty()) {
            // Ctrl+A..Z -> 0x01..0x1a
            const QChar c = event->text().at(0).toUpper();
            if (c >= 'A' && c <= 'Z')
                bytes = QByteArray(1, static_cast<char>(c.toLatin1() - 'A' + 1));
        }
        if (bytes.isEmpty() && !event->text().isEmpty())
            bytes = event->text().toUtf8();
        break;
    }

    if (!bytes.isEmpty())
        m_connection->sendData(bytes);
    // Swallow the event — no local editing.
}

} // namespace termsync::ui
