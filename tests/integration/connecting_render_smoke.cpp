// Renders the TerminalWidget "connecting…" overlay. Points at an unroutable host
// so the session stays connecting while we grab. Usage: connecting_render_smoke <out.png>

#include <QApplication>
#include <QTimer>
#include <cstdio>

#include "common/Theme.h"
#include "terminal_view/TerminalWidget.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    termsync::ui::applyDarkTheme(app);
    if (argc < 2) {
        std::fprintf(stderr, "usage: connecting_render_smoke <out.png>\n");
        return 2;
    }
    termsync::core::SshConnectionParams p;
    p.host = QStringLiteral("10.255.255.1"); // unroutable -> stays connecting
    p.port = 22;
    p.username = QStringLiteral("demo");
    p.password = QStringLiteral("x");

    auto *w = new termsync::ui::TerminalWidget(p);
    w->setLogContext(p.host, QStringLiteral("prod-web-01"));
    w->resize(900, 480);
    w->show();
    QTimer::singleShot(700, [&] {
        const QPixmap pm = w->grab();
        std::fprintf(stderr, pm.save(argv[1]) ? "[saved]\n" : "[save failed]\n");
        QCoreApplication::quit();
    });
    return app.exec();
}
