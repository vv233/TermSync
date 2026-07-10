// Deterministic check that a file-drop onto the Explorer SFTP browser is wired
// to an upload (drag-in), offline (no live connection needed). Sends the full
// dragEnter/dragMove/drop sequence carrying a local file URL and confirms the
// browser reports an upload. The network transfer uses the same enqueue path as
// the Upload button (proven separately).
//
// Usage: drag_upload_smoke [localfile]

#include <QApplication>
#include <QCoreApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QTimer>
#include <QUrl>
#include <cstdio>

#include "transfer_view/ExplorerSftpBrowser.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    const QString localFile =
        argc > 1 ? QString::fromLocal8Bit(argv[1]) : app.applicationFilePath();

    termsync::core::SshConnectionParams p; // unreachable; wiring test only
    p.host = QStringLiteral("127.0.0.1");
    p.port = 1;
    p.username = QStringLiteral("nobody");

    auto *w = new termsync::ui::ExplorerSftpBrowser(p, QString());
    w->resize(900, 560);
    w->show();

    int rc = 1;
    QObject::connect(w, &termsync::ui::ExplorerSftpBrowser::statusMessage,
                     [&](const QString &m) {
                         std::fprintf(stderr, "[status] %s\n", m.toUtf8().constData());
                         if (m.contains(QStringLiteral("Uploading"))) {
                             rc = 0;
                             QTimer::singleShot(100, [] { QCoreApplication::quit(); });
                         }
                     });

    QTimer::singleShot(300, [&] {
        auto *mime = new QMimeData;
        mime->setUrls({QUrl::fromLocalFile(localFile)});
        const QPoint pos(450, 300);
        QDragEnterEvent en(pos, Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(w, &en);
        QDragMoveEvent mv(pos, Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(w, &mv);
        QDropEvent dr(QPointF(pos), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(w, &dr);
        std::fprintf(stderr, "[drop] enter/move/drop sent; drop accepted=%d\n",
                     dr.isAccepted());
    });
    QTimer::singleShot(4000, [] { QCoreApplication::quit(); });

    app.exec();
    std::fprintf(stderr, "[%s] drop -> upload wiring\n", rc == 0 ? "PASS" : "FAIL");
    return rc;
}
