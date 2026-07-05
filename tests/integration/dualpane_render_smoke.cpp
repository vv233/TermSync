// GUI render smoke test for the dual-pane browser: connect to a live SFTP
// server, let the remote listing arrive, then grab the widget to a PNG.
//
// Usage: dualpane_render_smoke <host> <port> <user> <pass> <out.png>

#include <QApplication>
#include <QTimer>
#include <cstdio>

#include "transfer_view/DualPaneBrowser.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    if (argc < 6) {
        std::fprintf(stderr,
                     "usage: dualpane_render_smoke <host> <port> <user> <pass> <out.png>\n");
        return 2;
    }

    termsync::core::SshConnectionParams p;
    p.host = argv[1];
    p.port = static_cast<quint16>(QString(argv[2]).toInt());
    p.username = argv[3];
    p.password = argv[4];
    const QString outPng = argv[5];

    auto *w = new termsync::ui::DualPaneBrowser(p, QString());
    w->resize(1000, 600);
    w->show();

    QObject::connect(w, &termsync::ui::DualPaneBrowser::statusMessage,
                     [](const QString &m) {
                         std::fprintf(stderr, "[status] %s\n", m.toUtf8().constData());
                     });

    QTimer::singleShot(5000, [&] {
        const QPixmap pm = w->grab();
        std::fprintf(stderr, pm.save(outPng) ? "[saved]\n" : "[save failed]\n");
        QCoreApplication::quit();
    });

    return app.exec();
}
