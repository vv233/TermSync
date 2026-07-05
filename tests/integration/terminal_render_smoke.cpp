// GUI render smoke test: connect a TerminalWidget to a live SSH server, let
// the banner/prompt arrive, then grab the widget to a PNG for visual
// inspection. Not part of ctest (needs a live server + a GUI platform).
//
// Usage: terminal_render_smoke <host> <port> <user> <password> <out.png>

#include <QApplication>
#include <QTimer>
#include <cstdio>

#include "terminal_view/TerminalWidget.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    if (argc < 6) {
        std::fprintf(stderr,
                     "usage: terminal_render_smoke <host> <port> <user> <pass> <out.png>\n");
        return 2;
    }

    termsync::core::SshConnectionParams p;
    p.host = argv[1];
    p.port = static_cast<quint16>(QString(argv[2]).toInt());
    p.username = argv[3];
    p.password = argv[4];
    p.cols = 100;
    p.rows = 30;
    const QString outPng = argv[5];

    auto *w = new termsync::ui::TerminalWidget(p);
    w->resize(900, 500);
    w->show();

    QObject::connect(w, &termsync::ui::TerminalWidget::statusMessage,
                     [](const QString &m) {
                         std::fprintf(stderr, "[status] %s\n", m.toUtf8().constData());
                     });

    // Give the shell time to send its banner/prompt, then capture.
    QTimer::singleShot(5000, [&] {
        const QPixmap pm = w->grab();
        if (pm.save(outPng))
            std::fprintf(stderr, "[saved] %s\n", outPng.toUtf8().constData());
        else
            std::fprintf(stderr, "[save failed]\n");
        QCoreApplication::quit();
    });

    return app.exec();
}
