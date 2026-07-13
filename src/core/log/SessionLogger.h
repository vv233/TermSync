#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QString>

namespace termsync::core {

// Expand a log-filename template. TermSync tokens:
//   %H host   %S session   %Y year   %M month(01-12)   %D day(01-31)
//   %h hour(00-23)   %m minute   %s second   %% literal '%'
QString expandLogFilename(const QString &tmpl, const QString &host,
                          const QString &session, const QDateTime &when);

// Prefix each line in `data` with a timestamp. `atLineStart` carries state across
// calls (true when the previous chunk ended on a newline). Pure + testable.
QByteArray applyLineTimestamps(const QByteArray &data, const QString &format,
                               const QDateTime &when, bool *atLineStart);

struct SessionLogOptions
{
    bool timestampLines = false;
    QString timestampFormat = QStringLiteral("[yyyy-MM-dd HH:mm:ss] ");
    qint64 maxBytes = 0; // 0 = never rotate
    int maxFiles = 5;    // keep path.1 .. path.(maxFiles-1)
};

// Appends a terminal session's output to a file, with optional per-line
// timestamps and size-based rotation. File I/O is
// isolated so the templating/timestamp helpers above stay pure-testable.
class SessionLogger
{
public:
    using Options = SessionLogOptions;

    ~SessionLogger() { close(); }

    bool open(const QString &path, Options opts = Options());
    bool write(const QByteArray &data, const QDateTime &when = QDateTime::currentDateTime());
    void close();

    bool isOpen() const { return m_file.isOpen(); }
    QString path() const { return m_path; }

private:
    void rotate();

    QFile m_file;
    QString m_path;
    Options m_opts;
    bool m_atLineStart = true;
};

} // namespace termsync::core
