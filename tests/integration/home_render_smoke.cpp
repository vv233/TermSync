// Renders the Hosts home page with demo host cards. Usage: home_render_smoke <out.png>

#include <QApplication>
#include <QTimer>
#include <cstdio>

#include "common/Theme.h"
#include "home/HostsHomeWidget.h"
#include "store/ProfileStore.h"

static termsync::core::ConnectionProfile host(const QString &name,
                                              const QString &hostName,
                                              const QString &user,
                                              termsync::core::Protocol proto)
{
    termsync::core::ConnectionProfile p;
    p.id = termsync::core::ProfileStore::newId();
    p.name = name;
    p.host = hostName;
    p.username = user;
    p.protocol = proto;
    return p;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    termsync::ui::applyDarkTheme(app);
    if (argc < 2) {
        std::fprintf(stderr, "usage: home_render_smoke <out.png>\n");
        return 2;
    }
    using P = termsync::core::Protocol;
    QVector<termsync::core::ConnectionProfile> profiles = {
        host("linux-lab", "host.example.test", "demo", P::SSH2),
        host("prod-web-01", "web01.example.com", "deploy", P::SSH2),
        host("nas", "nas.local", "admin", P::SFTP_ONLY),
        host("switch-core", "10.0.0.1", "netadmin", P::TELNET),
    };

    auto *w = new termsync::ui::HostsHomeWidget;
    w->setProfiles(profiles);
    w->resize(900, 620);
    w->show();
    QTimer::singleShot(500, [&] {
        const QPixmap pm = w->grab();
        std::fprintf(stderr, pm.save(argv[1]) ? "[saved]\n" : "[save failed]\n");
        QCoreApplication::quit();
    });
    return app.exec();
}
