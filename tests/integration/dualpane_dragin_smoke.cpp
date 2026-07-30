// Deterministic check that a file dropped onto the traditional dual-pane
// browser's remote table is wired to an upload (drag-in), offline. The
// dual-pane view previously had no drag-in at all.
//
// Usage: dualpane_dragin_smoke [localfile]

#include <QApplication>
#include <QCoreApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <cstdio>

#include "transfer_view/DualPaneBrowser.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    const QString localFile =
        argc > 1 ? QString::fromLocal8Bit(argv[1]) : app.applicationFilePath();

    termsync::core::SshConnectionParams p; // unreachable; wiring test only
    p.host = QStringLiteral("127.0.0.1");
    p.port = 1;
    p.username = QStringLiteral("nobody");

    auto *w = new termsync::ui::DualPaneBrowser(p, QString());
    w->resize(1000, 600);
    w->show();

    int rc = 1;
    QObject::connect(w, &termsync::ui::DualPaneBrowser::statusMessage,
                     [&](const QString &m) {
                         std::fprintf(stderr, "[status] %s\n", m.toUtf8().constData());
                         if (m.contains(QStringLiteral("Uploading"))) {
                             rc = 0;
                             QTimer::singleShot(100, [] { QCoreApplication::quit(); });
                         }
                     });

    QTimer::singleShot(300, [&] {
        auto *view = w->findChild<QTableWidget *>(QStringLiteral("remoteTable"));
        QWidget *target = view ? view->viewport() : static_cast<QWidget *>(w);
        std::fprintf(stderr, "[target] %s acceptDrops(view=%d viewport=%d)\n",
                     view ? "remote-table viewport" : "browser",
                     view ? view->acceptDrops() : -1,
                     view ? view->viewport()->acceptDrops() : -1);

        auto *mime = new QMimeData;
        mime->setUrls({QUrl::fromLocalFile(localFile)});
        const QPoint pos(120, 40);
        QDragEnterEvent en(pos, Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(target, &en);
        QDragMoveEvent mv(pos, Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(target, &mv);
        QDropEvent dr(QPointF(pos), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(target, &dr);
        std::fprintf(stderr, "[drop] accepted=%d\n", dr.isAccepted());
    });
    QTimer::singleShot(4000, [] { QCoreApplication::quit(); });

    app.exec();
    std::fprintf(stderr, "[%s] dual-pane drop -> upload wiring\n",
                 rc == 0 ? "PASS" : "FAIL");
    return rc;
}
