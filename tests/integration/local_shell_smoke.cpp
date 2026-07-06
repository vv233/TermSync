// Headless smoke for LocalShellConnection (M20 local shell). Starts the platform
// shell, runs an `echo` of a unique marker, and confirms the marker comes back
// on the data stream. No server needed. Exit 0 = pass, 1 = fail.

#include <QByteArray>
#include <QCoreApplication>
#include <QTimer>
#include <cstdio>

#include "local/LocalShellConnection.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    termsync::core::LocalShellConnection shell;
    QByteArray output;
    const QByteArray marker = "termsync-shell-ok-42";
    int rc = 1;

    QObject::connect(&shell, &termsync::core::LocalShellConnection::dataReceived,
                     [&](const QByteArray &d) {
                         output += d;
                         if (output.contains(marker)) {
                             std::fprintf(stderr, "[PASS] marker echoed by %s\n",
                                          shell.shellProgram().toUtf8().constData());
                             rc = 0;
                             app.quit();
                         }
                     });
    QObject::connect(&shell, &termsync::core::LocalShellConnection::connected,
                     [&] {
                         // Line-based command; \r\n is safe for cmd.exe and POSIX.
                         shell.sendData("echo " + marker + "\r\n");
                         shell.sendData("exit\r\n");
                     });

    shell.start();

    QTimer::singleShot(8000, [&] {
        if (rc != 0) {
            std::fprintf(stderr, "[FAIL] marker not seen. Captured %lld bytes:\n%s\n",
                         static_cast<long long>(output.size()),
                         output.left(400).constData());
        }
        app.quit();
    });

    app.exec();
    return rc;
}
