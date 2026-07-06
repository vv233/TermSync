// End-to-end smoke for the built-in TFTP server (M20). Starts a TftpServer on a
// loopback ephemeral port, then drives a full WRQ upload followed by an RRQ
// download of the same file and verifies the bytes round-trip. Everything runs
// in one event loop (the server is async), so no threads/admin/external client.
//
// Exit 0 = pass, 1 = fail.

#include <QByteArray>
#include <QCoreApplication>
#include <QHostAddress>
#include <QTemporaryDir>
#include <QTimer>
#include <QUdpSocket>
#include <cstdio>

#include "tftp/TftpProtocol.h"
#include "tftp/TftpServer.h"

using namespace termsync::core::tftp;

namespace {

// Splits payload into TFTP blocks (each <= 512), always including a final short
// (possibly empty) block that terminates the transfer.
QVector<QByteArray> toBlocks(const QByteArray &data)
{
    QVector<QByteArray> blocks;
    int off = 0;
    while (off + kBlockSize <= data.size()) {
        blocks.append(data.mid(off, kBlockSize));
        off += kBlockSize;
    }
    blocks.append(data.mid(off)); // terminating short/empty block
    return blocks;
}

// Async client: uploads `data` as `name`, then downloads it back and compares.
class Tester : public QObject
{
    Q_OBJECT
public:
    Tester(quint16 serverPort, QString name, QByteArray data)
        : m_serverPort(serverPort), m_name(std::move(name)),
          m_data(std::move(data)), m_blocks(toBlocks(m_data))
    {
        m_sock = new QUdpSocket(this);
        m_sock->bind(QHostAddress(QHostAddress::LocalHost), 0);
        connect(m_sock, &QUdpSocket::readyRead, this, &Tester::onReadyRead);
    }

    void start()
    {
        // Kick off the upload.
        m_phase = Upload;
        m_sock->writeDatagram(buildRequest(OpCode::Wrq, m_name, QStringLiteral("octet")),
                              QHostAddress(QHostAddress::LocalHost), m_serverPort);
    }

signals:
    void done(bool ok, const QString &msg);

private:
    enum Phase { Upload, Download };

    void send(const QByteArray &dgram)
    {
        m_sock->writeDatagram(dgram, QHostAddress(QHostAddress::LocalHost), m_tid);
    }

    void beginDownload()
    {
        m_phase = Download;
        m_tid = 0;
        m_expected = 1;
        m_received.clear();
        m_sock->writeDatagram(buildRequest(OpCode::Rrq, m_name, QStringLiteral("octet")),
                              QHostAddress(QHostAddress::LocalHost), m_serverPort);
    }

    void onReadyRead()
    {
        while (m_sock->hasPendingDatagrams()) {
            QByteArray buf(int(m_sock->pendingDatagramSize()), Qt::Uninitialized);
            QHostAddress from;
            quint16 fromPort = 0;
            m_sock->readDatagram(buf.data(), buf.size(), &from, &fromPort);
            const Packet p = parse(buf);
            if (!p.valid)
                continue;
            if (p.op == OpCode::Error) {
                emit done(false, QStringLiteral("server error: %1").arg(p.message));
                return;
            }
            m_tid = fromPort; // adopt the server's transfer id

            if (m_phase == Upload) {
                if (p.op != OpCode::Ack)
                    continue;
                // ACK of block N -> send block N+1 (block 0 -> send block 1).
                const int next = p.block + 1;
                if (p.block == m_blocks.size()) { // last block acked
                    beginDownload();
                    return;
                }
                if (next >= 1 && next <= m_blocks.size())
                    send(buildData(uint16_t(next), m_blocks[next - 1]));
            } else { // Download
                if (p.op != OpCode::Data || p.block != m_expected)
                    continue;
                m_received.append(p.payload);
                send(buildAck(p.block));
                if (p.payload.size() < kBlockSize) {
                    const bool ok = (m_received == m_data);
                    emit done(ok, ok ? QStringLiteral("round-trip OK (%1 bytes)")
                                           .arg(m_received.size())
                                      : QStringLiteral("mismatch: got %1 of %2 bytes")
                                           .arg(m_received.size())
                                           .arg(m_data.size()));
                    return;
                }
                ++m_expected;
            }
        }
    }

    QUdpSocket *m_sock = nullptr;
    quint16 m_serverPort = 0;
    quint16 m_tid = 0;
    QString m_name;
    QByteArray m_data;
    QVector<QByteArray> m_blocks;
    Phase m_phase = Upload;
    uint16_t m_expected = 1;
    QByteArray m_received;
};

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::fprintf(stderr, "[FAIL] no temp dir\n");
        return 1;
    }

    TftpServer server;
    server.setRootDirectory(dir.path());
    if (!server.start(0, QHostAddress(QHostAddress::LocalHost))) {
        std::fprintf(stderr, "[FAIL] server start: %s\n",
                     server.lastError().toUtf8().constData());
        return 1;
    }

    // 1200 bytes exercises multiple blocks + a short final block.
    QByteArray data;
    for (int i = 0; i < 1200; ++i)
        data.append(char('0' + (i % 64)));

    int rc = 1;
    Tester tester(server.port(), QStringLiteral("round.bin"), data);
    QObject::connect(&tester, &Tester::done, [&](bool ok, const QString &msg) {
        std::fprintf(stderr, "%s %s\n", ok ? "[PASS]" : "[FAIL]",
                     msg.toUtf8().constData());
        rc = ok ? 0 : 1;
        app.quit();
    });
    tester.start();

    QTimer::singleShot(8000, [&] {
        if (rc != 0)
            std::fprintf(stderr, "[FAIL] timed out\n");
        app.quit();
    });

    app.exec();
    return rc;
}

#include "tftp_smoke.moc"
