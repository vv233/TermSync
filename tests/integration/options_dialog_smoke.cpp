// Render smoke for the Session Options + Global Options dialogs (M20 polish).
// Constructs both, grabs each to a PNG. No server needed.
//
// Usage: options_dialog_smoke <session_out.png> <global_out.png>

#include <QApplication>
#include <QTimer>
#include <cstdio>

#include "common/Theme.h"
#include "model/ConnectionProfile.h"
#include "session_dialogs/GlobalOptionsDialog.h"
#include "session_dialogs/SessionOptionsDialog.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    termsync::ui::applyDarkTheme(app);
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: options_dialog_smoke <session.png> <global.png>\n");
        return 2;
    }

    termsync::core::ConnectionProfile p;
    p.id = QStringLiteral("demo-id");
    p.name = QStringLiteral("Prod Web 01");
    p.host = QStringLiteral("web01.example.com");
    p.port = 22;
    p.username = QStringLiteral("deploy");
    p.authMethod = termsync::core::AuthMethod::PublicKey;
    p.privateKeyPath = QStringLiteral("C:/keys/id_ed25519");
    p.cols = 120;
    p.rows = 40;

    auto *sess = new termsync::ui::SessionOptionsDialog(p);
    sess->show();
    auto *global = new termsync::ui::GlobalOptionsDialog();

    QTimer::singleShot(500, [&] {
        std::fprintf(stderr, sess->grab().save(argv[1]) ? "[session saved]\n"
                                                        : "[session failed]\n");
        global->show();
        QTimer::singleShot(400, [&] {
            std::fprintf(stderr, global->grab().save(argv[2])
                                     ? "[global saved]\n"
                                     : "[global failed]\n");
            QCoreApplication::quit();
        });
    });
    return app.exec();
}
