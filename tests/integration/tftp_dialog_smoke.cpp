// Render smoke for the TFTP Server control dialog (M20). Starts the server on a
// high loopback port, logs a line, and grabs the dialog to a PNG. No admin.
//
// Usage: tftp_dialog_smoke <out.png>

#include <QApplication>
#include <QTimer>
#include <cstdio>

#include "common/Theme.h"
#include "session_dialogs/TftpServerDialog.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    termsync::ui::applyDarkTheme(app);
    if (argc < 2) {
        std::fprintf(stderr, "usage: tftp_dialog_smoke <out.png>\n");
        return 2;
    }

    auto *dlg = new termsync::ui::TftpServerDialog;
    dlg->show();

    QTimer::singleShot(500, [&] {
        const QPixmap pm = dlg->grab();
        std::fprintf(stderr, pm.save(argv[1]) ? "[saved]\n" : "[save failed]\n");
        QCoreApplication::quit();
    });
    return app.exec();
}
