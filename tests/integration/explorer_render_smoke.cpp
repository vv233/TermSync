// Renders the Windows-11-Explorer-style SFTP browser. With a real host it
// connects live; pass host "demo" to inject a fabricated listing (no server) so
// the list rendering can be verified deterministically.
// Usage: explorer_render_smoke <host|demo> <port> <user> <pass> <out.png>

#include <QApplication>
#include <QDateTime>
#include <QTimer>
#include <cstdio>

#include "common/Theme.h"
#include "transfer_view/ExplorerSftpBrowser.h"

using termsync::transfer::SftpEntry;

static SftpEntry entry(const QString &name, quint64 size, bool dir,
                       const QString &when)
{
    SftpEntry e;
    e.name = name;
    e.size = size;
    e.isDirectory = dir;
    e.permissions = dir ? 0755 : 0644;
    e.modifiedAt = QDateTime::fromString(when, QStringLiteral("yyyy-MM-dd HH:mm"));
    return e;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    termsync::ui::applyDarkTheme(app);
    if (argc < 6) {
        std::fprintf(stderr,
                     "usage: explorer_render_smoke <host|demo> <port> <user> <pass> <out.png>\n");
        return 2;
    }
    const QString host = argv[1];
    const QString out = argv[5];

    termsync::core::SshConnectionParams p;
    p.host = host == QStringLiteral("demo") ? QStringLiteral("127.0.0.1") : host;
    p.port = static_cast<quint16>(QString(argv[2]).toInt());
    p.username = argv[3];
    p.password = argv[4];

    auto *w = new termsync::ui::ExplorerSftpBrowser(p, QString());
    w->resize(1040, 620);
    w->show();
    QObject::connect(w, &termsync::ui::ExplorerSftpBrowser::statusMessage,
                     [](const QString &m) {
                         std::fprintf(stderr, "[status] %s\n", m.toUtf8().constData());
                     });

    if (host == QStringLiteral("demo")) {
        QVector<SftpEntry> entries = {
            entry("docs", 4096, true, "2026-07-05 14:02"),
            entry("src", 4096, true, "2026-07-08 09:30"),
            entry("node_modules", 4096, true, "2026-07-01 11:15"),
            entry("app.py", 2048, false, "2026-07-09 10:07"),
            entry("readme.md", 1523, false, "2026-07-06 16:40"),
            entry("archive.zip", 5242880, false, "2026-07-04 18:26"),
            entry("photo.png", 819200, false, "2026-07-02 12:00"),
            entry("notes.txt", 342, false, "2026-07-09 23:45"),
            entry("Makefile", 1200, false, "2026-06-30 08:00"),
        };
        const int viewMode = argc > 6 ? QString::fromLocal8Bit(argv[6]).toInt() : -1;
        QTimer::singleShot(400, [w, entries, viewMode] {
            QMetaObject::invokeMethod(
                w, "onDirectoryListed", Qt::DirectConnection,
                Q_ARG(QString, QStringLiteral("/srv/demo/project")),
                Q_ARG(QVector<SftpEntry>, entries));
            if (viewMode >= 0)
                QMetaObject::invokeMethod(w, "setViewMode", Qt::DirectConnection,
                                          Q_ARG(int, viewMode));
        });
    }

    QTimer::singleShot(900, [&] {
        const QPixmap pm = w->grab();
        std::fprintf(stderr, pm.save(out) ? "[saved]\n" : "[save failed]\n");
        QCoreApplication::quit();
    });
    return app.exec();
}
