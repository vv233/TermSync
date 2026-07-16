// Render smoke for the global Transfer Queue panel. Drives synthetic transfer
// signals through a TransferQueueWidget and grabs it to a PNG. No server.
//
// Usage: transfer_queue_smoke <out.png>

#include <QApplication>
#include <QTimer>
#include <cstdio>

#include "common/Theme.h"
#include "queue/SftpSession.h"
#include "transfer_view/TransferQueueWidget.h"

using termsync::transfer::SftpSession;
using termsync::transfer::TransferItem;

// Exposes the protected signals so the harness can emit synthetic progress.
class FakeSession : public SftpSession
{
public:
    using SftpSession::SftpSession;
    void fireQueued(const TransferItem &i) { emit transferQueued(i); }
    void fireProgress(int id, quint64 d, quint64 t) { emit transferProgress(id, d, t); }
    void fireFinished(int id, bool ok, const QString &m) { emit transferFinished(id, ok, m); }
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    termsync::ui::applyDarkTheme(app);
    if (argc < 2) {
        std::fprintf(stderr, "usage: transfer_queue_smoke <out.png>\n");
        return 2;
    }

    auto *panel = new termsync::ui::TransferQueueWidget();
    panel->resize(780, 240);
    panel->show();

    termsync::core::SshConnectionParams params; // never connected
    auto *s = new FakeSession(params, QString());
    panel->attachSession(s, QStringLiteral("web01.example.com"));

    TransferItem a;
    a.id = 1;
    a.direction = TransferItem::Download;
    a.displayName = QStringLiteral("ubuntu-24.04.iso");
    a.size = 2ull * 1024 * 1024 * 1024;
    TransferItem b;
    b.id = 2;
    b.direction = TransferItem::Upload;
    b.displayName = QStringLiteral("deploy-bundle.tar.gz");
    b.size = 48ull * 1024 * 1024;
    TransferItem c;
    c.id = 3;
    c.direction = TransferItem::Download;
    c.displayName = QStringLiteral("app.log");
    c.size = 3ull * 1024 * 1024;
    s->fireQueued(a);
    s->fireQueued(b);
    s->fireQueued(c);

    // Emit spaced progress so the panel computes a transfer rate.
    int tick = 0;
    auto *timer = new QTimer(&app);
    QObject::connect(timer, &QTimer::timeout, [&, s] {
        ++tick;
        s->fireProgress(1, quint64(tick) * 90ull * 1024 * 1024, a.size);
        s->fireProgress(2, quint64(tick) * 8ull * 1024 * 1024, b.size);
        if (tick == 3)
            s->fireFinished(3, true, QString());
        else
            s->fireProgress(3, quint64(tick) * 1024 * 1024, c.size);
        if (tick >= 5) {
            timer->stop();
            std::fprintf(stderr, panel->grab().save(argv[1]) ? "[saved]\n"
                                                             : "[failed]\n");
            QCoreApplication::quit();
        }
    });
    timer->start(250);
    return app.exec();
}
