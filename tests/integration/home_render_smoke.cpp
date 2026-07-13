// Renders the Hosts home page with demo host cards. Usage: home_render_smoke <out.png>

#include <QApplication>
#include <QSettings>
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
        host("ubuntu-box", "host.example.test", "demo", P::SSH2),
        host("debian-box", "d.example.com", "deploy", P::SSH2),
        host("fedora-box", "f.example.com", "admin", P::SSH2),
        host("rhel-box", "r.example.com", "admin", P::SSH2),
        host("arch-box", "a.example.com", "admin", P::SSH2),
        host("alpine-box", "al.example.com", "admin", P::SFTP_ONLY),
        host("mac-mini", "m.example.com", "admin", P::SSH2),
        host("win-server", "w.example.com", "Administrator", P::SSH2),
        host("suse-box", "s.example.com", "admin", P::SSH2),
        host("unknown-host", "x.example.com", "root", P::TELNET),
    };
    // Assign each a detected OS so every icon variant renders.
    const char *oses[] = {"ubuntu", "debian", "fedora", "rhel",  "arch",
                          "alpine", "macos",  "windows", "suse",  ""};
    QSettings settings(QStringLiteral("TermSync"), QStringLiteral("TermSync"));
    for (int i = 0; i < profiles.size(); ++i)
        settings.setValue(QStringLiteral("hostos/") + profiles[i].id,
                          QString::fromUtf8(oses[i]));

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
