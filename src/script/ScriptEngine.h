#pragma once

#include <QString>
#include <memory>

class QJSEngine;

namespace termsync::script {

class ScriptContext;

// Runs automation scripts against a session. Scripts are JavaScript (via Qt's
// built-in QJSEngine - no external interpreter) and see the TermSync automation
// object model:
//
//   crt.Screen.Send("ls\r");
//   crt.Screen.WaitForString("$", 5000);
//   var text = crt.Screen.Get();
//   crt.Sleep(200);
//
// crt.Session is provided as an alias so existing-style scripts read naturally.
class ScriptEngine
{
public:
    explicit ScriptEngine(ScriptContext *context);
    ~ScriptEngine();

    struct Result
    {
        bool ok = false;
        QString value;   // the script's return value (stringified)
        QString error;   // error message when !ok
    };

    // Evaluates a script and returns its result / error.
    Result run(const QString &script);

private:
    ScriptContext *m_context;
    std::unique_ptr<QJSEngine> m_engine;
};

} // namespace termsync::script
