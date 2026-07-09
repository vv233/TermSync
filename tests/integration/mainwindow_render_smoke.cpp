// Renders the whole MainWindow to a PNG so the overall UI can be eyeballed.
// Usage: mainwindow_render_smoke <out.png>

#include <QApplication>
#include <QTimer>
#include <cstdio>

#include "common/Theme.h"
#include "mainwindow/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    termsync::ui::applyDarkTheme(app);
    if (argc < 2) {
        std::fprintf(stderr, "usage: mainwindow_render_smoke <out.png>\n");
        return 2;
    }
    termsync::ui::MainWindow w;
    w.resize(1200, 760);
    w.show();
    QTimer::singleShot(700, [&] {
        const QPixmap pm = w.grab();
        std::fprintf(stderr, pm.save(argv[1]) ? "[saved]\n" : "[save failed]\n");
        QCoreApplication::quit();
    });
    return app.exec();
}
