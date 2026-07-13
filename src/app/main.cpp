// TermSync application entry point.

#include <QApplication>

#include "common/Icons.h"
#include "common/Theme.h"
#include "mainwindow/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCoreApplication::setOrganizationName("TermSync");
    QCoreApplication::setApplicationName("TermSync");
    QCoreApplication::setApplicationVersion(TERMSYNC_VERSION);

    app.setWindowIcon(termsync::ui::appIcon());
    termsync::ui::applyDarkTheme(app);

    termsync::ui::MainWindow window;
    window.show();

    return app.exec();
}
