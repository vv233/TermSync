#pragma once

#include <QObject>
#include <QVector>

#include "ssh/SshConnection.h"

class QThread;

namespace termsync::core {

// A single port-forwarding rule.
struct ForwardRule
{
    enum Type {
        Local,    // listen locally, tunnel to targetHost:targetPort via the server
        Dynamic,  // listen locally as a SOCKS5 proxy (target chosen per connection)
    };

    Type type = Local;
    quint16 bindPort = 0;       // local listen port
    QString targetHost;         // for Local
    quint16 targetPort = 0;     // for Local
};

class ForwardWorker; // internal

// Runs one or more port-forwarding rules over a dedicated SSH session (kept
// separate from the terminal session, like the SFTP engine). Local listeners
// and libssh2 direct-tcpip channels are pumped on a single worker thread.
//
// Live verification requires a target service reachable through the SSH server
// (and a server that permits direct-tcpip), so this is exercised via the
// unit-tested SOCKS5 parser plus manual QA; the piping engine is code-complete.
class PortForwarder : public QObject
{
    Q_OBJECT

public:
    PortForwarder(const SshConnectionParams &params,
                  const QVector<ForwardRule> &rules, QObject *parent = nullptr);
    ~PortForwarder() override;

    void start();
    void stop();

signals:
    void listening(quint16 bindPort);
    void connectionOpened(quint16 bindPort, const QString &target);
    void errorOccurred(const QString &message);
    void stopped();

private:
    QThread *m_thread = nullptr;
    ForwardWorker *m_worker = nullptr;
};

} // namespace termsync::core

Q_DECLARE_METATYPE(QVector<termsync::core::ForwardRule>)
