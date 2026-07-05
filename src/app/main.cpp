// TermSync application entry point.

#include <QApplication>

#include "common/Theme.h"
#include "mainwindow/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCoreApplication::setOrganizationName("TermSync");
    QCoreApplication::setApplicationName("TermSync");
    QCoreApplication::setApplicationVersion("0.1.0");

    termsync::ui::applyDarkTheme(app);

    termsync::ui::MainWindow window;
    window.show();

    return app.exec();
}
