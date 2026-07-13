// Headless smoke test for the TerminalWidget session-logging tee (M20b GUI
// wiring). Feeds bytes through a fake connection and checks they land in the
// log file, with TermSync filename tokens expanded. No server needed;
// run with QT_QPA_PLATFORM=offscreen.
//
// Usage: session_log_smoke   (exit 0 = pass, 1 = fail)

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

#include "AbstractTerminalConnection.h"
#include "terminal_view/TerminalWidget.h"

namespace {

// Minimal connection that lets the test push bytes to the widget.
class FakeConnection : public termsync::core::AbstractTerminalConnection
{
public:
    void sendData(const QByteArray &) override {}
    void resize(int, int) override {}
    void disconnectFromHost() override {}
    bool isConnected() const override { return true; }
    void feed(const QByteArray &data) { emit dataReceived(data); }
};

bool fail(const char *msg)
{
    std::fprintf(stderr, "[FAIL] %s\n", msg);
    return false;
}

bool run()
{
    QTemporaryDir tmp;
    if (!tmp.isValid())
        return fail("could not create temp dir");

    auto *conn = new FakeConnection;
    termsync::ui::TerminalWidget view(conn); // takes ownership
    view.setLogContext(QStringLiteral("host.example"), QStringLiteral("mysession"));

    // Template exercises %S (session) + %H (host) token expansion.
    const QString tmpl = tmp.path() + QStringLiteral("/%S-%H.log");
    if (!view.startLogging(tmpl))
        return fail("startLogging returned false");
    if (!view.isLogging())
        return fail("isLogging() false after start");

    const QString expected =
        tmp.path() + QStringLiteral("/mysession-host.example.log");
    if (QDir::cleanPath(view.logPath()) != QDir::cleanPath(expected)) {
        std::fprintf(stderr, "  logPath=%s expected=%s\n",
                     view.logPath().toUtf8().constData(),
                     expected.toUtf8().constData());
        return fail("filename tokens not expanded as expected");
    }

    conn->feed(QByteArray("Hello\r\n"));
    conn->feed(QByteArray("World\r\n"));
    QCoreApplication::processEvents();
    view.stopLogging();
    if (view.isLogging())
        return fail("isLogging() true after stop");

    QFile f(expected);
    if (!f.open(QIODevice::ReadOnly))
        return fail("log file was not created");
    const QByteArray body = f.readAll();
    if (!body.contains("Hello") || !body.contains("World")) {
        std::fprintf(stderr, "  body=%s\n", body.constData());
        return fail("logged bytes missing from file");
    }

    std::fprintf(stderr, "[PASS] logged %lld bytes to %s\n",
                 static_cast<long long>(body.size()),
                 QDir::toNativeSeparators(expected).toUtf8().constData());
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    return run() ? 0 : 1;
}
