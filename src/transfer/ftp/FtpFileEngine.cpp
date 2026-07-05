#include "ftp/FtpFileEngine.h"

#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QRegularExpression>
#include <QStringList>

#include <curl/curl.h>

namespace termsync::transfer {

namespace {

void ensureCurlInit()
{
    static QMutex mutex;
    static bool done = false;
    QMutexLocker locker(&mutex);
    if (!done) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        done = true;
    }
}

size_t writeToByteArray(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *buffer = static_cast<QByteArray *>(userdata);
    const size_t total = size * nmemb;
    buffer->append(ptr, static_cast<int>(total));
    return total;
}

size_t writeToFile(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *file = static_cast<QFile *>(userdata);
    const size_t total = size * nmemb;
    return file->write(ptr, static_cast<qint64>(total)) == static_cast<qint64>(total)
               ? total
               : 0;
}

size_t readFromFile(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *file = static_cast<QFile *>(userdata);
    return static_cast<size_t>(file->read(ptr, static_cast<qint64>(size * nmemb)));
}

struct ProgressCtx
{
    FileEngine::ProgressFn fn;
    const std::atomic<bool> *cancel = nullptr;
    bool upload = false;
};

int xferInfo(void *p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal,
             curl_off_t ulnow)
{
    auto *ctx = static_cast<ProgressCtx *>(p);
    if (ctx->cancel && ctx->cancel->load())
        return 1; // abort
    if (ctx->fn) {
        const curl_off_t now = ctx->upload ? ulnow : dlnow;
        const curl_off_t total = ctx->upload ? ultotal : dltotal;
        if (total > 0)
            ctx->fn(static_cast<quint64>(now), static_cast<quint64>(total));
    }
    return 0;
}

// Parses one Unix `ls -l` LIST line into a FileEntry. Returns false if it does
// not look like such a line.
bool parseUnixListing(const QString &line, FileEntry *entry)
{
    // e.g. "drwxr-xr-x 2 owner group 4096 Mar 31 2023 pub"
    const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (parts.size() < 9)
        return false;
    const QString perms = parts[0];
    if (perms.size() < 10)
        return false;
    if (!QString("dl-").contains(perms[0]))
        return false;

    entry->isDirectory = perms[0] == 'd';
    entry->isSymlink = perms[0] == 'l';
    bool okSize = false;
    entry->size = parts[4].toULongLong(&okSize);
    // Name is everything from field 8 onward (handles spaces in names).
    QString name = parts.mid(8).join(' ');
    if (entry->isSymlink) {
        const int arrow = name.indexOf(" -> ");
        if (arrow >= 0)
            name = name.left(arrow);
    }
    entry->name = name;
    // Translate rwx perms into a numeric mode (best-effort).
    quint32 mode = 0;
    for (int i = 1; i <= 9; ++i)
        if (perms[i] != '-')
            mode |= (1u << (9 - i));
    entry->permissions = mode;
    return !name.isEmpty();
}

} // namespace

FtpFileEngine::FtpFileEngine()
{
    ensureCurlInit();
}

FtpFileEngine::~FtpFileEngine()
{
    disconnectFromHost();
}

QString FtpFileEngine::baseUrl() const
{
    const QString scheme = m_explicitTls ? QStringLiteral("ftp") // explicit TLS still ftp://
                                         : QStringLiteral("ftp");
    return QStringLiteral("%1://%2:%3").arg(scheme, m_host).arg(m_port);
}

QString FtpFileEngine::urlForPath(const QString &path, bool dirTrailingSlash) const
{
    QString p = path;
    if (p.startsWith('/'))
        p.remove(0, 1);
    QString url = baseUrl() + '/' + p;
    if (dirTrailingSlash && !url.endsWith('/'))
        url += '/';
    return url;
}

void FtpFileEngine::applyAuth(void *curlv) const
{
    auto *curl = static_cast<CURL *>(curlv);
    if (!m_user.isEmpty())
        curl_easy_setopt(curl, CURLOPT_USERNAME, m_user.toUtf8().constData());
    if (!m_password.isEmpty())
        curl_easy_setopt(curl, CURLOPT_PASSWORD, m_password.toUtf8().constData());
    if (m_explicitTls) {
        curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
        // Test servers often use self-signed certs; accept for now (M9/M10
        // add proper certificate verification/pinning UI).
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    curl_easy_setopt(curl, CURLOPT_FTP_RESPONSE_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
}

bool FtpFileEngine::connectToHost(const core::SshConnectionParams &params,
                                  HostKeyVerifier)
{
    m_host = params.host;
    m_port = params.port ? params.port : 21;
    m_user = params.username;
    m_password = params.password;
    m_lastError.clear();

    // Validate credentials with a lightweight listing of the root.
    QVector<FileEntry> probe;
    if (!listDirectory(QStringLiteral("/"), &probe)) {
        m_connected = false;
        return false;
    }
    m_connected = true;
    return true;
}

void FtpFileEngine::disconnectFromHost()
{
    m_connected = false;
}

bool FtpFileEngine::isConnected() const
{
    return m_connected;
}

bool FtpFileEngine::listDirectory(const QString &remotePath, QVector<FileEntry> *entries)
{
    if (!entries)
        return false;
    entries->clear();

    CURL *curl = curl_easy_init();
    if (!curl) {
        setError(QStringLiteral("curl init failed"));
        return false;
    }
    applyAuth(curl);
    QByteArray body;
    const QString url = urlForPath(remotePath, /*dirTrailingSlash=*/true);
    curl_easy_setopt(curl, CURLOPT_URL, url.toUtf8().constData());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToByteArray);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);

    const CURLcode rc = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK) {
        setError(QString::fromUtf8(curl_easy_strerror(rc)));
        return false;
    }

    const QStringList lines = QString::fromUtf8(body).split('\n', Qt::SkipEmptyParts);
    for (const QString &raw : lines) {
        QString line = raw;
        if (line.endsWith('\r'))
            line.chop(1);
        FileEntry e;
        if (parseUnixListing(line, &e)) {
            if (e.name == "." || e.name == "..")
                continue;
            entries->append(e);
        }
    }
    return true;
}

bool FtpFileEngine::downloadFile(const QString &remotePath, const QString &localPath,
                                 ProgressFn progress, const std::atomic<bool> *cancel)
{
    QFile local(localPath);
    if (!local.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError(local.errorString());
        return false;
    }
    CURL *curl = curl_easy_init();
    if (!curl) { setError(QStringLiteral("curl init failed")); return false; }
    applyAuth(curl);
    curl_easy_setopt(curl, CURLOPT_URL, urlForPath(remotePath, false).toUtf8().constData());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &local);

    ProgressCtx ctx{progress, cancel, false};
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferInfo);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);

    const CURLcode rc = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK) {
        setError(QString::fromUtf8(curl_easy_strerror(rc)));
        return false;
    }
    return true;
}

bool FtpFileEngine::uploadFile(const QString &localPath, const QString &remotePath,
                               ProgressFn progress, const std::atomic<bool> *cancel)
{
    QFile local(localPath);
    if (!local.open(QIODevice::ReadOnly)) {
        setError(local.errorString());
        return false;
    }
    CURL *curl = curl_easy_init();
    if (!curl) { setError(QStringLiteral("curl init failed")); return false; }
    applyAuth(curl);
    curl_easy_setopt(curl, CURLOPT_URL, urlForPath(remotePath, false).toUtf8().constData());
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, readFromFile);
    curl_easy_setopt(curl, CURLOPT_READDATA, &local);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE,
                     static_cast<curl_off_t>(local.size()));
    curl_easy_setopt(curl, CURLOPT_FTP_CREATE_MISSING_DIRS, CURLFTP_CREATE_DIR);

    ProgressCtx ctx{progress, cancel, true};
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferInfo);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);

    const CURLcode rc = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK) {
        setError(QString::fromUtf8(curl_easy_strerror(rc)));
        return false;
    }
    return true;
}

bool FtpFileEngine::runQuote(const QStringList &commands)
{
    CURL *curl = curl_easy_init();
    if (!curl) { setError(QStringLiteral("curl init failed")); return false; }
    applyAuth(curl);
    curl_easy_setopt(curl, CURLOPT_URL, (baseUrl() + '/').toUtf8().constData());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);

    struct curl_slist *headers = nullptr;
    QVector<QByteArray> storage;
    storage.reserve(commands.size());
    for (const QString &cmd : commands) {
        storage.append(cmd.toUtf8());
        headers = curl_slist_append(headers, storage.last().constData());
    }
    curl_easy_setopt(curl, CURLOPT_QUOTE, headers);

    const CURLcode rc = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK) {
        setError(QString::fromUtf8(curl_easy_strerror(rc)));
        return false;
    }
    return true;
}

bool FtpFileEngine::makeDirectory(const QString &remotePath)
{
    return runQuote({QStringLiteral("MKD %1").arg(remotePath)});
}

bool FtpFileEngine::removeFile(const QString &remotePath)
{
    return runQuote({QStringLiteral("DELE %1").arg(remotePath)});
}

bool FtpFileEngine::removeDirectory(const QString &remotePath)
{
    return runQuote({QStringLiteral("RMD %1").arg(remotePath)});
}

bool FtpFileEngine::rename(const QString &fromPath, const QString &toPath)
{
    return runQuote({QStringLiteral("RNFR %1").arg(fromPath),
                     QStringLiteral("RNTO %1").arg(toPath)});
}

bool FtpFileEngine::setPermissions(const QString &remotePath, quint32 mode)
{
    // Not all FTP servers support SITE CHMOD; failure is non-fatal to callers.
    return runQuote({QStringLiteral("SITE CHMOD %1 %2")
                         .arg(QString::number(mode & 0777, 8), remotePath)});
}

bool FtpFileEngine::statSize(const QString &remotePath, quint64 *size)
{
    if (!size)
        return false;
    CURL *curl = curl_easy_init();
    if (!curl) return false;
    applyAuth(curl);
    curl_easy_setopt(curl, CURLOPT_URL, urlForPath(remotePath, false).toUtf8().constData());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    const CURLcode rc = curl_easy_perform(curl);
    if (rc == CURLE_OK) {
        curl_off_t len = 0;
        if (curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &len) == CURLE_OK
            && len >= 0) {
            *size = static_cast<quint64>(len);
            curl_easy_cleanup(curl);
            return true;
        }
    }
    curl_easy_cleanup(curl);
    return false;
}

void FtpFileEngine::setError(const QString &message)
{
    m_lastError = message;
}

} // namespace termsync::transfer
