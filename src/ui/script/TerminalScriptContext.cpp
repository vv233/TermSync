#include "script/TerminalScriptContext.h"

#include <QCoreApplication>
#include <QElapsedTimer>

namespace termsync::ui {

void TerminalScriptContext::send(const QString &text)
{
    m_widget->sendText(text.toUtf8());
}

QString TerminalScriptContext::screenText() const
{
    return m_widget->screenPlainText();
}

bool TerminalScriptContext::waitForString(const QString &needle, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (m_widget->screenPlainText().contains(needle))
            return true;
        // Let incoming data be processed while we wait.
        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
    }
    return m_widget->screenPlainText().contains(needle);
}

void TerminalScriptContext::sleepMs(int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

} // namespace termsync::ui
