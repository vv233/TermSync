// Renders the redesigned top tab strip (ChromeTabWidget + hamburger corner) with
// a permanent tab and a couple of closable session tabs, so the tab shapes, the
// teal selected underline, and the custom close buttons can be eyeballed.
// Usage: tabbar_render_smoke <out.png>

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <cstdio>

#include "common/Icons.h"
#include "common/Theme.h"
#include "mainwindow/ChromeTabWidget.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    termsync::ui::applyDarkTheme(app);
    if (argc < 2) {
        std::fprintf(stderr, "usage: tabbar_render_smoke <out.png>\n");
        return 2;
    }

    auto *tabs = new termsync::ui::ChromeTabWidget;
    tabs->setTabsClosable(true);

    auto page = [](const QString &t) {
        auto *l = new QLabel(t);
        l->setAlignment(Qt::AlignCenter);
        return l;
    };
    const int hosts = tabs->addTab(page("Hosts page"), "Hosts");
    tabs->tabBar()->setTabButton(hosts, QTabBar::RightSide, nullptr); // permanent
    tabs->setCurrentIndex(0); // single tab, like the app's initial state

    // Hamburger in the corner, matching MainWindow.
    auto *ham = new QToolButton;
    ham->setText(QStringLiteral("≡"));
    ham->setAutoRaise(true);
    ham->setStyleSheet(QStringLiteral(
        "QToolButton { font-size:16pt; color:#c8d0e8; padding:2px 12px;"
        " border:0; background:transparent; }"
        "QToolButton:hover { color:#2dd4bf; }"));
    tabs->setCornerWidget(ham, Qt::TopLeftCorner);

    // Window controls in the right corner (mirrors MainWindow createWindowControls).
    auto *host = new QWidget;
    auto *row = new QHBoxLayout(host);
    row->setContentsMargins(4, 0, 4, 0);
    row->setSpacing(2);
    for (auto g : {termsync::ui::Glyph::WinMinimize, termsync::ui::Glyph::WinMaximize,
                   termsync::ui::Glyph::Close}) {
        auto *b = new QToolButton;
        b->setIcon(termsync::ui::lineIcon(g, QColor(0xc8, 0xd0, 0xe8)));
        b->setIconSize(QSize(16, 16));
        b->setFixedSize(46, 30);
        b->setStyleSheet(QStringLiteral(
            "QToolButton { border:0; border-radius:6px; background:transparent; }"
            "QToolButton:hover { background:#2a2c3a; }"));
        row->addWidget(b);
    }
    host->adjustSize();
    tabs->setCornerWidget(host, Qt::TopRightCorner);

    // Wrap in a QMainWindow (like the real app) to reproduce the corner-widget
    // visibility issue.
    auto *mw = new QMainWindow;
    mw->setCentralWidget(tabs);
    mw->resize(900, 220);
    mw->show();
    // Optional 2nd arg: dump the app icon at 128px for a visual check.
    if (argc > 2)
        termsync::ui::appIcon().pixmap(128, 128).save(argv[2]);
    QTimer::singleShot(500, [&] {
        std::fprintf(stderr, mw->grab().save(argv[1]) ? "[saved]\n" : "[fail]\n");
        QCoreApplication::quit();
    });
    return app.exec();
}
