// Render smoke test for TerminalWidget keyword highlighting (M20a GUI wiring).
// Feeds a few lines through a fake connection with two highlight rules active,
// then grabs the widget to a PNG so the colouring can be eyeballed. No server.
//
// Usage: highlight_render_smoke <out.png>

#include <QApplication>
#include <QTimer>
#include <cstdio>

#include "AbstractTerminalConnection.h"
#include "terminal_view/TerminalWidget.h"
#include "text/KeywordHighlighter.h"

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
        std::fprintf(stderr, "usage: highlight_render_smoke <out.png>\n");
        return 2;
    }

    auto *conn = new FakeConnection;
    auto *w = new termsync::ui::TerminalWidget(conn);

    QVector<termsync::terminal::HighlightRule> rules;
    rules.push_back({QStringLiteral("ERROR"), false, false, false, 1}); // red
    rules.push_back({QStringLiteral("WARN"), false, false, false, 0});  // amber
    rules.push_back({QStringLiteral("OK"), false, false, true, 2});     // green, whole word
    w->setHighlightRules(rules);

    w->resize(720, 240);
    w->show();

    conn->feed(QByteArray(
        "normal line, nothing special here\r\n"
        "ERROR: disk failure on /dev/sda\r\n"
        "WARN: battery low, plug in soon\r\n"
        "status OK, all systems nominal\r\n"
        "the word OKAY should NOT match the OK whole-word rule\r\n"));

    QTimer::singleShot(600, [&] {
        const QPixmap pm = w->grab();
        std::fprintf(stderr, pm.save(argv[1]) ? "[saved]\n" : "[save failed]\n");
        QCoreApplication::quit();
    });
    return app.exec();
}
