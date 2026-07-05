#include "schedule/JobScheduler.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace termsync::transfer::schedule {

namespace {

QJsonObject toJson(const ScheduledJob &j)
{
    QJsonObject o;
    o["id"] = j.id;
    o["name"] = j.name;
    o["protocol"] = j.protocol;
    o["host"] = j.host;
    o["port"] = j.port;
    o["user"] = j.user;
    o["password"] = j.password;
    o["keyPath"] = j.keyPath;
    o["passphrase"] = j.passphrase;
    o["command"] = j.command;
    o["localPath"] = j.localPath;
    o["remotePath"] = j.remotePath;
    o["down"] = j.down;
    o["recursive"] = j.recursive;
    o["deleteOrphans"] = j.deleteOrphans;
    o["resume"] = j.resume;
    o["relentless"] = j.relentless;
    o["ascii"] = j.ascii;
    o["preservePerms"] = j.preservePerms;
    o["throttleKbps"] = static_cast<double>(j.throttleKbps);
    o["intervalSecs"] = static_cast<double>(j.intervalSecs);
    o["lastRun"] = static_cast<double>(j.lastRun);
    o["enabled"] = j.enabled;
    return o;
}

ScheduledJob fromJson(const QJsonObject &o)
{
    ScheduledJob j;
    j.id = o["id"].toString();
    j.name = o["name"].toString();
    j.protocol = o["protocol"].toString("sftp");
    j.host = o["host"].toString();
    j.port = o["port"].toInt(22);
    j.user = o["user"].toString();
    j.password = o["password"].toString();
    j.keyPath = o["keyPath"].toString();
    j.passphrase = o["passphrase"].toString();
    j.command = o["command"].toString();
    j.localPath = o["localPath"].toString();
    j.remotePath = o["remotePath"].toString();
    j.down = o["down"].toBool();
    j.recursive = o["recursive"].toBool();
    j.deleteOrphans = o["deleteOrphans"].toBool();
    j.resume = o["resume"].toBool();
    j.relentless = o["relentless"].toBool();
    j.ascii = o["ascii"].toBool();
    j.preservePerms = o["preservePerms"].toBool();
    j.throttleKbps = static_cast<qint64>(o["throttleKbps"].toDouble());
    j.intervalSecs = static_cast<qint64>(o["intervalSecs"].toDouble());
    j.lastRun = static_cast<qint64>(o["lastRun"].toDouble());
    j.enabled = o["enabled"].toBool(true);
    return j;
}

} // namespace

bool JobScheduler::load(const QString &filePath)
{
    m_jobs.clear();
    QFile f(filePath);
    if (!f.exists())
        return true; // no file yet is fine
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    for (const QJsonValue &v : doc.array())
        m_jobs.append(fromJson(v.toObject()));
    return true;
}

bool JobScheduler::save(const QString &filePath) const
{
    QJsonArray arr;
    for (const ScheduledJob &j : m_jobs)
        arr.append(toJson(j));
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented)) >= 0;
}

void JobScheduler::add(const ScheduledJob &job)
{
    for (ScheduledJob &j : m_jobs) {
        if (j.id == job.id) {
            j = job;
            return;
        }
    }
    m_jobs.append(job);
}

bool JobScheduler::remove(const QString &id)
{
    for (int i = 0; i < m_jobs.size(); ++i) {
        if (m_jobs[i].id == id) {
            m_jobs.removeAt(i);
            return true;
        }
    }
    return false;
}

ScheduledJob *JobScheduler::find(const QString &id)
{
    for (ScheduledJob &j : m_jobs)
        if (j.id == id)
            return &j;
    return nullptr;
}

QVector<ScheduledJob> JobScheduler::dueJobs(qint64 nowSecs) const
{
    QVector<ScheduledJob> due;
    for (const ScheduledJob &j : m_jobs)
        if (j.isDue(nowSecs))
            due.append(j);
    return due;
}

void JobScheduler::markRan(const QString &id, qint64 nowSecs)
{
    if (ScheduledJob *j = find(id)) {
        j->lastRun = nowSecs;
        if (j->intervalSecs <= 0)
            j->enabled = false; // one-shot: don't run again
    }
}

} // namespace termsync::transfer::schedule
