// Menu smoke for the M20 MainWindow wiring. Constructs the real MainWindow and
// pops up its menus so the newly wired items (Log Session, Keyword Highlighting,
// Hex View, Import/Export Settings) can be confirmed present and rendered. Also
// asserts each expected action exists by text. No server needed.
//
// Usage: mainwindow_menu_smoke <out-prefix>   -> writes <prefix>-<menu>.png

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QMenuBar>
#include <QTimer>
#include <cstdio>

#include "mainwindow/MainWindow.h"

namespace {
QMenu *menuByTitle(QMenuBar *bar, const QString &plain)
{
    const auto acts = bar->actions();
    for (QAction *a : acts) {
        QString t = a->text();
        t.remove(QLatin1Char('&'));
        if (t == plain)
            return a->menu();
    }
    return nullptr;
}

bool hasAction(QMenu *menu, const QString &plain)
{
    if (!menu)
        return false;
    const auto acts = menu->actions();
    for (QAction *a : acts) {
        QString t = a->text();
        t.remove(QLatin1Char('&'));
        if (t == plain)
            return true;
    }
    return false;
}

int failures = 0;
void check(bool ok, const char *what)
{
    std::fprintf(stderr, "  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++failures;
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    const QString prefix = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                     : QStringLiteral("menu");

    termsync::ui::MainWindow w;
    w.resize(1100, 720);
    w.show();
    QMenuBar *bar = w.menuBar();

    QMenu *file = menuByTitle(bar, QStringLiteral("File"));
    QMenu *edit = menuByTitle(bar, QStringLiteral("Edit"));
    QMenu *view = menuByTitle(bar, QStringLiteral("View"));
    QMenu *options = menuByTitle(bar, QStringLiteral("Options"));
    QMenu *tools = menuByTitle(bar, QStringLiteral("Tools"));

    check(hasAction(file, QStringLiteral("Local Shell")), "File > Local Shell");
    check(hasAction(file, QStringLiteral("Log Session...")), "File > Log Session...");
    check(hasAction(edit, QStringLiteral("Keyword Highlighting...")),
          "Edit > Keyword Highlighting...");
    check(hasAction(view, QStringLiteral("Hex View")), "View > Hex View");
    check(hasAction(options, QStringLiteral("Terminal Appearance...")),
          "Options > Terminal Appearance...");
    check(hasAction(tools, QStringLiteral("Import Settings...")),
          "Tools > Import Settings...");
    check(hasAction(tools, QStringLiteral("Export Settings...")),
          "Tools > Export Settings...");
    check(hasAction(tools, QStringLiteral("TFTP Server...")),
          "Tools > TFTP Server...");

    // Render the File, View and Tools menus so the items can be eyeballed.
    struct { QMenu *m; const char *name; } menus[] = {
        {file, "file"}, {view, "view"}, {tools, "tools"}};
    for (auto &e : menus) {
        if (!e.m)
            continue;
        e.m->popup(QPoint(0, 0));
        QCoreApplication::processEvents();
        const QPixmap pm = e.m->grab();
        pm.save(QStringLiteral("%1-%2.png").arg(prefix, QLatin1String(e.name)));
        e.m->close();
    }

    QTimer::singleShot(0, &app, &QCoreApplication::quit);
    app.exec();
    std::fprintf(stderr, failures ? "[FAIL] %d check(s) failed\n"
                                  : "[PASS] all menu checks passed\n",
                 failures);
    return failures ? 1 : 0;
}
