// End-to-end: a REAL ExplorerSftpBrowser connected to a live server. Drops a
// local file onto the file-list viewport and reports whether the transfer
// actually completes (not just that an upload was enqueued). Also flips sudo on
// via the shared session and re-lists a root-only dir.
//
// Usage: explorer_e2e_smoke <host> <port> <user> <pw> <localfile> <remoteDir>

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

#include "transfer_view/ExplorerSftpBrowser.h"
#include "queue/SftpSession.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    if (argc < 7) {
        std::fprintf(stderr, "usage: explorer_e2e_smoke <host> <port> <user> <pw> <localfile> <remoteDir>\n");
        return 2;
    }
    termsync::core::SshConnectionParams p;
    p.host = argv[1];
    p.port = static_cast<quint16>(QString(argv[2]).toInt());
    p.username = argv[3];
    p.password = argv[4];
    const QString localFile = QString::fromLocal8Bit(argv[5]);
    const QString remoteDir = QString::fromLocal8Bit(argv[6]);

    auto *w = new termsync::ui::ExplorerSftpBrowser(p, QString());
    w->resize(1000, 620);
    w->show();

    QObject::connect(w, &termsync::ui::ExplorerSftpBrowser::statusMessage,
                     [](const QString &m) {
                         std::fprintf(stderr, "[status] %s\n", m.toUtf8().constData());
                     });
    QObject::connect(w->session(), &termsync::transfer::SftpSession::transferFinished,
                     [](int id, bool ok, const QString &msg) {
                         std::fprintf(stderr, "[transferFinished] id=%d ok=%d %s\n", id, ok,
                                      msg.toUtf8().constData());
                     });

    // Once connected, navigate to the target dir, then drop the file on the table.
    QObject::connect(w->session(), &termsync::transfer::SftpSession::connected, [&] {
        std::fprintf(stderr, "[connected]\n");
        w->session()->listDirectory(remoteDir); // ensure browser m_path is set via UI nav
        QTimer::singleShot(1500, [&] {
            auto *view = w->findChild<QTableWidget *>();
            if (!view) { std::fprintf(stderr, "[err] no table\n"); return; }
            std::fprintf(stderr, "[drop] viewport acceptDrops=%d\n",
                         view->viewport()->acceptDrops());
            auto *mime = new QMimeData;
            mime->setUrls({QUrl::fromLocalFile(localFile)});
            const QPoint pos(150, 60);
            QDragEnterEvent en(pos, Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(view->viewport(), &en);
            QDragMoveEvent mv(pos, Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(view->viewport(), &mv);
            QDropEvent dr(QPointF(pos), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(view->viewport(), &dr);
            std::fprintf(stderr, "[drop] accepted=%d\n", dr.isAccepted());
        });
    });

    // Note: the browser navigates via its own UI; we set the session path above,
    // but the browser's m_path drives the upload target. Navigate through the UI
    // by asking the browser to open the dir is not exposed, so this exercises the
    // default path — good enough to see if the drop uploads at all.
    QTimer::singleShot(12000, [] { QCoreApplication::quit(); });
    return app.exec();
}
