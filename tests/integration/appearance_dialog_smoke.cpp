// Render smoke for the Terminal Appearance dialog (M20 themes + font picker).
// Constructs the dialog, selects a scheme, and grabs it to a PNG. No server.
//
// Usage: appearance_dialog_smoke <out.png> [scheme]

#include <QApplication>
#include <QFont>
#include <QTimer>
#include <cstdio>

#include "common/Theme.h"
#include "session_dialogs/TerminalAppearanceDialog.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    termsync::ui::applyDarkTheme(app);
    if (argc < 2) {
        std::fprintf(stderr, "usage: appearance_dialog_smoke <out.png> [scheme]\n");
        return 2;
    }
    const QString scheme =
        argc > 2 ? QString::fromLocal8Bit(argv[2]) : QStringLiteral("Kanagawa Wave");

    QFont font(QStringLiteral("Cascadia Mono"), 12);
    auto *dlg = new termsync::ui::TerminalAppearanceDialog(scheme, font);
    dlg->show();

    QTimer::singleShot(500, [&] {
        const QPixmap pm = dlg->grab();
        std::fprintf(stderr, pm.save(argv[1]) ? "[saved]\n" : "[save failed]\n");
        QCoreApplication::quit();
    });
    return app.exec();
}
