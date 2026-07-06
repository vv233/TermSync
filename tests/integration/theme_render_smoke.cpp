// Render smoke for terminal colour schemes + font (M20 appearance). Applies a
// named scheme to a TerminalWidget, feeds ANSI-coloured sample text, and grabs
// a PNG so the theme can be eyeballed. No server.
//
// Usage: theme_render_smoke <out.png> [scheme name] [font pt]

#include <QApplication>
#include <QFont>
#include <QTimer>
#include <cstdio>

#include "AbstractTerminalConnection.h"
#include "terminal_view/TerminalWidget.h"
#include "theme/ColorScheme.h"

namespace {
class FakeConnection : public termsync::core::AbstractTerminalConnection
{
public:
    void sendData(const QByteArray &) override {}
    void resize(int, int) override {}
    void disconnectFromHost() override {}
    bool isConnected() const override { return true; }
    void feed(const QByteArray &d) { emit dataReceived(d); }
};
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    if (argc < 2) {
        std::fprintf(stderr, "usage: theme_render_smoke <out.png> [scheme] [pt]\n");
        return 2;
    }
    const QString schemeName =
        argc > 2 ? QString::fromLocal8Bit(argv[2])
                 : termsync::terminal::defaultSchemeName();

    auto *conn = new FakeConnection;
    auto *w = new termsync::ui::TerminalWidget(conn);

    if (const auto *s = termsync::terminal::findScheme(schemeName))
        w->applyColorScheme(*s);
    else
        std::fprintf(stderr, "unknown scheme '%s', using default\n", argv[2]);

    if (argc > 3) {
        QFont f = w->terminalFont();
        f.setPointSize(QString::fromLocal8Bit(argv[3]).toInt());
        w->setTerminalFont(f);
    }

    w->resize(760, 300);
    w->show();

    // Title line + one line per ANSI colour + a bright row.
    QByteArray data;
    data.append("TermSync  theme: " + schemeName.toUtf8() + "\r\n\r\n");
    const char *names[8] = {"black", "red",     "green", "yellow",
                            "blue",  "magenta", "cyan",  "white"};
    for (int i = 0; i < 8; ++i)
        data.append(QByteArray("\x1b[3") + char('0' + i) + "m" + names[i] +
                    "  ");
    data.append("\x1b[0m\r\n");
    for (int i = 0; i < 8; ++i)
        data.append(QByteArray("\x1b[9") + char('0' + i) + "mBRIGHT ");
    data.append("\x1b[0m\r\n\r\nthe quick brown fox jumps over 1234567890\r\n");
    conn->feed(data);

    QTimer::singleShot(600, [&] {
        const QPixmap pm = w->grab();
        std::fprintf(stderr, pm.save(argv[1]) ? "[saved]\n" : "[save failed]\n");
        QCoreApplication::quit();
    });
    return app.exec();
}
