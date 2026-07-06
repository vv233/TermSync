// Render smoke for the local shell wired into TerminalWidget (M20). Opens a
// TerminalWidget backed by a LocalShellConnection, runs a couple of commands,
// and grabs a PNG so the shell output rendering can be eyeballed. No server.
//
// Usage: local_shell_render_smoke <out.png>

#include <QApplication>
#include <QTimer>
#include <cstdio>

#include "local/LocalShellConnection.h"
#include "terminal_view/TerminalWidget.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    if (argc < 2) {
        std::fprintf(stderr, "usage: local_shell_render_smoke <out.png>\n");
        return 2;
    }

    auto *conn = new termsync::core::LocalShellConnection;
    auto *w = new termsync::ui::TerminalWidget(conn); // takes ownership
    w->resize(760, 300);
    w->show();

    QObject::connect(conn, &termsync::core::LocalShellConnection::connected,
                     [conn] {
                         conn->sendData("echo TermSync local shell works\r\n");
                         conn->sendData("ver\r\n");
                     });
    conn->start();

    QTimer::singleShot(1500, [&] {
        const QPixmap pm = w->grab();
        std::fprintf(stderr, pm.save(argv[1]) ? "[saved]\n" : "[save failed]\n");
        QCoreApplication::quit();
    });
    return app.exec();
}
