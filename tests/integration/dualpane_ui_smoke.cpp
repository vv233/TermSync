// UI smoke for the DualPaneBrowser toolbar (M20 bookmarks + synchronized
// browsing). Constructs the browser without a reachable server and grabs a PNG
// so the new "Bookmarks" and "Sync Browse" controls can be confirmed present.
//
// Usage: dualpane_ui_smoke <out.png>

#include <QApplication>
#include <QTimer>
#include <cstdio>

#include "transfer_view/DualPaneBrowser.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    if (argc < 2) {
        std::fprintf(stderr, "usage: dualpane_ui_smoke <out.png>\n");
        return 2;
    }

    // Unreachable endpoint: the connection fails quickly but the UI still builds.
    termsync::core::SshConnectionParams p;
    p.host = QStringLiteral("127.0.0.1");
    p.port = 1;
    p.username = QStringLiteral("nobody");

    auto *w = new termsync::ui::DualPaneBrowser(p, QString());
    w->resize(1000, 600);
    w->show();

    QTimer::singleShot(700, [&] {
        const QPixmap pm = w->grab();
        std::fprintf(stderr, pm.save(argv[1]) ? "[saved]\n" : "[save failed]\n");
        QCoreApplication::quit();
    });
    return app.exec();
}
