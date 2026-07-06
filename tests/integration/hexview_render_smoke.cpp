// Render smoke test for TerminalWidget Hex View (M20a GUI wiring). Feeds bytes
// through a fake connection, switches the widget to hex mode, and grabs a PNG
// so the hex dump layout can be eyeballed. No server needed.
//
// Usage: hexview_render_smoke <out.png>

#include <QApplication>
#include <QTimer>
#include <cstdio>

#include "AbstractTerminalConnection.h"
#include "terminal_view/TerminalWidget.h"

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
        std::fprintf(stderr, "usage: hexview_render_smoke <out.png>\n");
        return 2;
    }

    auto *conn = new FakeConnection;
    auto *w = new termsync::ui::TerminalWidget(conn);
    w->resize(720, 260);
    w->show();

    QByteArray data;
    for (int i = 0; i < 8; ++i)
        data.append("The quick brown fox jumps 0123456789!\r\n");
    data.append(QByteArray("\x00\x01\x02\x1b[31m\xff\xfe", 8)); // non-printables
    conn->feed(data);

    w->setHexView(true);

    QTimer::singleShot(600, [&] {
        const QPixmap pm = w->grab();
        std::fprintf(stderr, pm.save(argv[1]) ? "[saved]\n" : "[save failed]\n");
        QCoreApplication::quit();
    });
    return app.exec();
}
