#include "ScriptEngine.h"

#include <QJSEngine>
#include <QJSValue>
#include <QObject>

#include "ScriptContext.h"

namespace termsync::script {

// The QObject exposed to JavaScript as crt.Screen / crt.Session. Its invokable
// methods forward to the ScriptContext.
class ScriptBridge : public QObject
{
    Q_OBJECT

public:
    explicit ScriptBridge(ScriptContext *ctx, QObject *parent = nullptr)
        : QObject(parent), m_ctx(ctx) {}

public slots:
    void Send(const QString &text) { m_ctx->send(text); }
    void SendLine(const QString &text) { m_ctx->send(text + QChar('\r')); }
    QString Get() const { return m_ctx->screenText(); }
    bool WaitForString(const QString &needle, int timeoutMs = 5000)
    {
        return m_ctx->waitForString(needle, timeoutMs);
    }
    void Sleep(int ms) { m_ctx->sleepMs(ms); }

private:
    ScriptContext *m_ctx;
};

ScriptEngine::ScriptEngine(ScriptContext *context)
    : m_context(context)
    , m_engine(std::make_unique<QJSEngine>())
{
    m_engine->installExtensions(QJSEngine::ConsoleExtension);
}

ScriptEngine::~ScriptEngine() = default;

ScriptEngine::Result ScriptEngine::run(const QString &script)
{
    auto *bridge = new ScriptBridge(m_context, m_engine.get());
    const QJSValue bridgeValue = m_engine->newQObject(bridge);

    // Build the `crt` namespace with Screen/Session both mapping to the bridge.
    QJSValue crt = m_engine->newObject();
    crt.setProperty(QStringLiteral("Screen"), bridgeValue);
    crt.setProperty(QStringLiteral("Session"), bridgeValue);
    // crt.Sleep(ms) convenience alias.
    crt.setProperty(QStringLiteral("Sleep"), bridgeValue.property(QStringLiteral("Sleep")));
    m_engine->globalObject().setProperty(QStringLiteral("crt"), crt);

    const QJSValue result = m_engine->evaluate(script);

    Result out;
    if (result.isError()) {
        out.ok = false;
        out.error = QStringLiteral("%1 (line %2)")
                        .arg(result.toString())
                        .arg(result.property(QStringLiteral("lineNumber")).toInt());
    } else {
        out.ok = true;
        out.value = result.isUndefined() ? QString() : result.toString();
    }
    return out;
}

} // namespace termsync::script

#include "ScriptEngine.moc"
