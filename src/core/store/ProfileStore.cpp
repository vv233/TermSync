#include "store/ProfileStore.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace termsync::core {

namespace {
constexpr int kSchemaVersion = 1;
int g_counter = 0;
} // namespace

ProfileStore::ProfileStore()
{
    // Unique connection name so multiple stores (e.g. tests) don't collide.
    m_connectionName = QStringLiteral("termsync_profiles_%1").arg(g_counter++);
}

ProfileStore::~ProfileStore()
{
    if (m_open) {
        {
            QSqlDatabase db = QSqlDatabase::database(m_connectionName);
            if (db.isOpen())
                db.close();
        }
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

QString ProfileStore::newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool ProfileStore::open(const QString &filePath)
{
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                m_connectionName);
    db.setDatabaseName(filePath);
    if (!db.open()) {
        m_lastError = db.lastError().text();
        return false;
    }
    QSqlQuery(QStringLiteral("PRAGMA foreign_keys = ON"), db);
    m_open = true;
    if (!applyMigrations()) {
        db.close();
        m_open = false;
        return false;
    }
    return true;
}

bool ProfileStore::applyMigrations()
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);

    QSqlQuery versionQuery(QStringLiteral("PRAGMA user_version"), db);
    int version = 0;
    if (versionQuery.next())
        version = versionQuery.value(0).toInt();

    if (version < 1) {
        QSqlQuery q(db);
        const char *createProfiles =
            "CREATE TABLE IF NOT EXISTS profiles ("
            " id TEXT PRIMARY KEY,"
            " name TEXT NOT NULL,"
            " folder_path TEXT,"
            " protocol INTEGER NOT NULL,"
            " host TEXT NOT NULL,"
            " port INTEGER NOT NULL,"
            " username TEXT,"
            " auth_method INTEGER NOT NULL,"
            " save_password INTEGER NOT NULL DEFAULT 0,"
            " private_key_path TEXT,"
            " cols INTEGER NOT NULL DEFAULT 80,"
            " rows INTEGER NOT NULL DEFAULT 24,"
            " created_at INTEGER,"
            " updated_at INTEGER)";
        if (!q.exec(QString::fromLatin1(createProfiles))) {
            m_lastError = q.lastError().text();
            return false;
        }
        const char *createKnownHosts =
            "CREATE TABLE IF NOT EXISTS known_hosts ("
            " host TEXT NOT NULL,"
            " port INTEGER NOT NULL,"
            " fingerprint TEXT NOT NULL,"
            " PRIMARY KEY (host, port))";
        if (!q.exec(QString::fromLatin1(createKnownHosts))) {
            m_lastError = q.lastError().text();
            return false;
        }
        q.exec(QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion));
    }
    return true;
}

QVector<ConnectionProfile> ProfileStore::allProfiles() const
{
    QVector<ConnectionProfile> result;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.exec(QStringLiteral(
        "SELECT id, name, folder_path, protocol, host, port, username,"
        " auth_method, save_password, private_key_path, cols, rows"
        " FROM profiles ORDER BY folder_path, name"));
    while (q.next()) {
        ConnectionProfile p;
        p.id = q.value(0).toString();
        p.name = q.value(1).toString();
        p.folderPath = q.value(2).toString();
        p.protocol = static_cast<Protocol>(q.value(3).toInt());
        p.host = q.value(4).toString();
        p.port = static_cast<quint16>(q.value(5).toUInt());
        p.username = q.value(6).toString();
        p.authMethod = static_cast<AuthMethod>(q.value(7).toInt());
        p.savePassword = q.value(8).toBool();
        p.privateKeyPath = q.value(9).toString();
        p.cols = q.value(10).toInt();
        p.rows = q.value(11).toInt();
        result.append(p);
    }
    return result;
}

bool ProfileStore::upsert(const ConnectionProfile &profile)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO profiles (id, name, folder_path, protocol, host, port,"
        " username, auth_method, save_password, private_key_path, cols, rows,"
        " created_at, updated_at)"
        " VALUES (:id, :name, :folder, :protocol, :host, :port, :username,"
        " :auth, :savepw, :key, :cols, :rows, :created, :updated)"
        " ON CONFLICT(id) DO UPDATE SET"
        " name=:name, folder_path=:folder, protocol=:protocol, host=:host,"
        " port=:port, username=:username, auth_method=:auth,"
        " save_password=:savepw, private_key_path=:key, cols=:cols, rows=:rows,"
        " updated_at=:updated"));
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    q.bindValue(":id", profile.id);
    q.bindValue(":name", profile.name);
    q.bindValue(":folder", profile.folderPath);
    q.bindValue(":protocol", static_cast<int>(profile.protocol));
    q.bindValue(":host", profile.host);
    q.bindValue(":port", profile.port);
    q.bindValue(":username", profile.username);
    q.bindValue(":auth", static_cast<int>(profile.authMethod));
    q.bindValue(":savepw", profile.savePassword ? 1 : 0);
    q.bindValue(":key", profile.privateKeyPath);
    q.bindValue(":cols", profile.cols);
    q.bindValue(":rows", profile.rows);
    q.bindValue(":created", now);
    q.bindValue(":updated", now);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

bool ProfileStore::remove(const QString &id)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM profiles WHERE id = :id"));
    q.bindValue(":id", id);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

QString ProfileStore::knownFingerprint(const QString &host, quint16 port) const
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT fingerprint FROM known_hosts WHERE host = :host AND port = :port"));
    q.bindValue(":host", host);
    q.bindValue(":port", port);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return {};
}

bool ProfileStore::setKnownFingerprint(const QString &host, quint16 port,
                                       const QString &fingerprint)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO known_hosts (host, port, fingerprint)"
        " VALUES (:host, :port, :fp)"
        " ON CONFLICT(host, port) DO UPDATE SET fingerprint = :fp"));
    q.bindValue(":host", host);
    q.bindValue(":port", port);
    q.bindValue(":fp", fingerprint);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

} // namespace termsync::core
