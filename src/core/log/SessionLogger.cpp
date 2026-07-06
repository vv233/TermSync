#include "log/SessionLogger.h"

#include <QFileInfo>

namespace termsync::core {

QString expandLogFilename(const QString &tmpl, const QString &host,
                          const QString &session, const QDateTime &when)
{
    const QDate d = when.date();
    const QTime t = when.time();
    const auto pad2 = [](int v) { return QString("%1").arg(v, 2, 10, QLatin1Char('0')); };

    QString out;
    out.reserve(tmpl.size());
    for (int i = 0; i < tmpl.size(); ++i) {
        if (tmpl[i] != '%' || i + 1 >= tmpl.size()) {
            out += tmpl[i];
            continue;
        }
        const QChar c = tmpl[++i];
        switch (c.unicode()) {
        case 'H': out += host; break;
        case 'S': out += session; break;
        case 'Y': out += QString::number(d.year()); break;
        case 'M': out += pad2(d.month()); break;
        case 'D': out += pad2(d.day()); break;
        case 'h': out += pad2(t.hour()); break;
        case 'm': out += pad2(t.minute()); break;
        case 's': out += pad2(t.second()); break;
        case '%': out += '%'; break;
        default:  out += '%'; out += c; break; // unknown token: keep literally
        }
    }
    return out;
}

QByteArray applyLineTimestamps(const QByteArray &data, const QString &format,
                               const QDateTime &when, bool *atLineStart)
{
    const QByteArray stamp = when.toString(format).toUtf8();
    QByteArray out;
    out.reserve(data.size() + stamp.size());
    bool lineStart = atLineStart ? *atLineStart : true;
    for (char ch : data) {
        if (lineStart) {
            out += stamp;
            lineStart = false;
        }
        out += ch;
        if (ch == '\n')
            lineStart = true;
    }
    if (atLineStart)
        *atLineStart = lineStart;
    return out;
}

bool SessionLogger::open(const QString &path, Options opts)
{
    close();
    m_path = path;
    m_opts = opts;
    m_atLineStart = true;
    m_file.setFileName(path);
    return m_file.open(QIODevice::WriteOnly | QIODevice::Append);
}

bool SessionLogger::write(const QByteArray &data, const QDateTime &when)
{
    if (!m_file.isOpen())
        return false;
    const QByteArray payload = m_opts.timestampLines
        ? applyLineTimestamps(data, m_opts.timestampFormat, when, &m_atLineStart)
        : data;
    if (m_file.write(payload) != payload.size())
        return false;
    m_file.flush();
    if (m_opts.maxBytes > 0 && m_file.size() >= m_opts.maxBytes)
        rotate();
    return true;
}

void SessionLogger::close()
{
    if (m_file.isOpen())
        m_file.close();
}

void SessionLogger::rotate()
{
    m_file.close();
    // Drop the oldest, shift the rest up: path.(n-1) -> path.n.
    const int keep = qMax(1, m_opts.maxFiles);
    QFile::remove(QString("%1.%2").arg(m_path).arg(keep - 1));
    for (int i = keep - 2; i >= 1; --i)
        QFile::rename(QString("%1.%2").arg(m_path).arg(i),
                      QString("%1.%2").arg(m_path).arg(i + 1));
    QFile::rename(m_path, m_path + ".1");
    m_file.setFileName(m_path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return; // reopen failed; subsequent writes will report the closed file
    m_atLineStart = true;
}

} // namespace termsync::core
