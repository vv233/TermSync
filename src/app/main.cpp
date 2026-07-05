// TermSync application entry point.
//
// Milestone M1: bootstrap Qt and show the (empty) main window shell.
// Later milestones wire a dependency container (profile store, credential
// store, session factory) in here before constructing the MainWindow.

#include <QApplication>

#include "mainwindow/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCoreApplication::setOrganizationName("TermSync");
    QCoreApplication::setApplicationName("TermSync");
    QCoreApplication::setApplicationVersion("0.1.0");

    termsync::ui::MainWindow window;
    window.show();

    return app.exec();
}
