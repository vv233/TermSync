#pragma once

#include <QString>

namespace termsync::script {

// The bridge between a running script and a live session. The scripting engine
// exposes these operations to JavaScript as the crt.Screen / crt.Session object
// model (SecureCRT-compatible semantics). A real terminal implements this; unit
// tests provide a fake.
class ScriptContext
{
public:
    virtual ~ScriptContext() = default;

    // Sends text to the session (crt.Screen.Send).
    virtual void send(const QString &text) = 0;

    // The current on-screen text (crt.Screen.Get).
    virtual QString screenText() const = 0;

    // Blocks until `needle` appears in the incoming stream or the timeout
    // elapses; returns whether it was found (crt.Screen.WaitForString).
    virtual bool waitForString(const QString &needle, int timeoutMs) = 0;

    // Pauses the script (crt.Sleep).
    virtual void sleepMs(int ms) = 0;
};

} // namespace termsync::script
