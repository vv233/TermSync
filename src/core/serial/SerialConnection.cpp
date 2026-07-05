#include "serial/SerialConnection.h"

#include <QMetaObject>
#include <QThread>
#include <QTimer>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <termios.h>
#  include <unistd.h>
#endif

namespace termsync::core {

// ---------------------------------------------------------------------------
// SerialWorker — owns the OS serial handle on a worker thread.
// ---------------------------------------------------------------------------
class SerialWorker : public QObject
{
    Q_OBJECT

public:
    ~SerialWorker() override { closePort(); }

public slots:
    void start(const termsync::core::SerialParams &params)
    {
        if (!openPort(params)) {
            emit errorOccurred(tr("Could not open serial port %1").arg(params.portName));
            return;
        }
        emit connected();
        m_pump = new QTimer(this);
        m_pump->setInterval(15);
        connect(m_pump, &QTimer::timeout, this, &SerialWorker::pump);
        m_pump->start();
    }

    void writeData(const QByteArray &data)
    {
#ifdef _WIN32
        if (m_handle != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(m_handle, data.constData(), static_cast<DWORD>(data.size()),
                      &written, nullptr);
        }
#else
        if (m_fd >= 0)
            ::write(m_fd, data.constData(), static_cast<size_t>(data.size()));
#endif
    }

    void stop() { closePort(); emit disconnected(); }

signals:
    void connected();
    void dataReceived(const QByteArray &data);
    void errorOccurred(const QString &message);
    void disconnected();

private slots:
    void pump()
    {
        char buf[4096];
#ifdef _WIN32
        DWORD got = 0;
        if (ReadFile(m_handle, buf, sizeof(buf), &got, nullptr) && got > 0)
            emit dataReceived(QByteArray(buf, static_cast<int>(got)));
#else
        const ssize_t got = ::read(m_fd, buf, sizeof(buf));
        if (got > 0)
            emit dataReceived(QByteArray(buf, static_cast<int>(got)));
#endif
    }

private:
#ifdef _WIN32
    bool openPort(const SerialParams &p)
    {
        const QString path = QStringLiteral("\\\\.\\") + p.portName;
        m_handle = CreateFileW(reinterpret_cast<const wchar_t *>(path.utf16()),
                               GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        if (m_handle == INVALID_HANDLE_VALUE)
            return false;

        DCB dcb{};
        dcb.DCBlength = sizeof(dcb);
        GetCommState(m_handle, &dcb);
        dcb.BaudRate = static_cast<DWORD>(p.baudRate);
        dcb.ByteSize = static_cast<BYTE>(p.dataBits);
        dcb.StopBits = p.stopBits == 2 ? TWOSTOPBITS : ONESTOPBIT;
        dcb.Parity = p.parity == 'E' ? EVENPARITY : p.parity == 'O' ? ODDPARITY : NOPARITY;
        dcb.fParity = p.parity != 'N';
        dcb.fBinary = TRUE;
        dcb.fRtsControl = p.rtsCtsFlow ? RTS_CONTROL_HANDSHAKE : RTS_CONTROL_ENABLE;
        dcb.fOutxCtsFlow = p.rtsCtsFlow ? TRUE : FALSE;
        SetCommState(m_handle, &dcb);

        // Non-blocking-ish reads: return immediately with whatever is available.
        COMMTIMEOUTS to{};
        to.ReadIntervalTimeout = MAXDWORD;
        to.ReadTotalTimeoutConstant = 0;
        to.ReadTotalTimeoutMultiplier = 0;
        SetCommTimeouts(m_handle, &to);
        return true;
    }
    void closePort()
    {
        if (m_pump) { m_pump->stop(); m_pump->deleteLater(); m_pump = nullptr; }
        if (m_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(m_handle);
            m_handle = INVALID_HANDLE_VALUE;
        }
    }
    HANDLE m_handle = INVALID_HANDLE_VALUE;
#else
    bool openPort(const SerialParams &p)
    {
        m_fd = ::open(p.portName.toUtf8().constData(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (m_fd < 0)
            return false;
        struct termios tio{};
        tcgetattr(m_fd, &tio);
        cfmakeraw(&tio);
        speed_t speed = B115200;
        switch (p.baudRate) {
        case 9600: speed = B9600; break;
        case 19200: speed = B19200; break;
        case 38400: speed = B38400; break;
        case 57600: speed = B57600; break;
        case 115200: speed = B115200; break;
        default: break;
        }
        cfsetispeed(&tio, speed);
        cfsetospeed(&tio, speed);
        tio.c_cflag &= ~CSIZE;
        tio.c_cflag |= (p.dataBits == 7 ? CS7 : CS8);
        if (p.stopBits == 2) tio.c_cflag |= CSTOPB; else tio.c_cflag &= ~CSTOPB;
        if (p.parity == 'N') tio.c_cflag &= ~PARENB;
        else { tio.c_cflag |= PARENB; if (p.parity == 'O') tio.c_cflag |= PARODD; else tio.c_cflag &= ~PARODD; }
        tio.c_cflag |= (CLOCAL | CREAD);
        tcsetattr(m_fd, TCSANOW, &tio);
        return true;
    }
    void closePort()
    {
        if (m_pump) { m_pump->stop(); m_pump->deleteLater(); m_pump = nullptr; }
        if (m_fd >= 0) { ::close(m_fd); m_fd = -1; }
    }
    int m_fd = -1;
#endif
    QTimer *m_pump = nullptr;
};

// ---------------------------------------------------------------------------
SerialConnection::SerialConnection(QObject *parent)
    : AbstractTerminalConnection(parent)
{
    qRegisterMetaType<termsync::core::SerialParams>();
    m_thread = new QThread(this);
    m_worker = new SerialWorker;
    m_worker->moveToThread(m_thread);
    connect(m_worker, &SerialWorker::connected, this, [this] {
        m_connected = true;
        emit connected();
    });
    connect(m_worker, &SerialWorker::dataReceived, this,
            &SerialConnection::dataReceived);
    connect(m_worker, &SerialWorker::errorOccurred, this,
            &SerialConnection::errorOccurred);
    connect(m_worker, &SerialWorker::disconnected, this, [this] {
        m_connected = false;
        emit disconnected();
    });
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread->start();
}

SerialConnection::~SerialConnection()
{
    disconnectFromHost();
    m_thread->quit();
    m_thread->wait();
}

void SerialConnection::open(const SerialParams &params)
{
    QMetaObject::invokeMethod(m_worker, "start", Qt::QueuedConnection,
                              Q_ARG(termsync::core::SerialParams, params));
}

void SerialConnection::sendData(const QByteArray &data)
{
    QMetaObject::invokeMethod(m_worker, "writeData", Qt::QueuedConnection,
                              Q_ARG(QByteArray, data));
}

void SerialConnection::resize(int, int) {} // serial has no window size

void SerialConnection::disconnectFromHost()
{
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "stop", Qt::QueuedConnection);
}

} // namespace termsync::core

#include "SerialConnection.moc"
