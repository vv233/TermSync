#pragma once

#include "ScriptContext.h"
#include "terminal_view/TerminalWidget.h"

namespace termsync::ui {

// Bridges the scripting engine to a live TerminalWidget: sends bytes to the
// session and reads the terminal screen. WaitForString/Sleep pump the event
// loop so incoming data is processed while the script waits.
class TerminalScriptContext : public script::ScriptContext
{
public:
    explicit TerminalScriptContext(TerminalWidget *widget) : m_widget(widget) {}

    void send(const QString &text) override;
    QString screenText() const override;
    bool waitForString(const QString &needle, int timeoutMs) override;
    void sleepMs(int ms) override;

private:
    TerminalWidget *m_widget;
};

} // namespace termsync::ui
