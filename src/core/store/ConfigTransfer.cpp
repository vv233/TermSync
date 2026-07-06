#include "store/ConfigTransfer.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "store/ProfileStore.h"

namespace termsync::core {

QByteArray serializeProfiles(const QVector<ConnectionProfile> &profiles)
{
    QJsonArray arr;
    for (const ConnectionProfile &p : profiles) {
        QJsonObject o;
        o["id"] = p.id;
        o["name"] = p.name;
        o["folderPath"] = p.folderPath;
        o["protocol"] = static_cast<int>(p.protocol);
        o["host"] = p.host;
        o["port"] = p.port;
        o["username"] = p.username;
        o["authMethod"] = static_cast<int>(p.authMethod);
        o["savePassword"] = p.savePassword;
        o["privateKeyPath"] = p.privateKeyPath;
        o["cols"] = p.cols;
        o["rows"] = p.rows;
        arr.append(o);
    }
    QJsonObject root;
    root["version"] = 1;
    root["profiles"] = arr;
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

QVector<ConnectionProfile> deserializeProfiles(const QByteArray &json)
{
    QVector<ConnectionProfile> out;
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    for (const QJsonValue &v : root["profiles"].toArray()) {
        const QJsonObject o = v.toObject();
        ConnectionProfile p;
        p.id = o["id"].toString();
        p.name = o["name"].toString();
        p.folderPath = o["folderPath"].toString();
        p.protocol = static_cast<Protocol>(o["protocol"].toInt());
        p.host = o["host"].toString();
        p.port = static_cast<quint16>(o["port"].toInt());
        p.username = o["username"].toString();
        p.authMethod = static_cast<AuthMethod>(o["authMethod"].toInt());
        p.savePassword = o["savePassword"].toBool();
        p.privateKeyPath = o["privateKeyPath"].toString();
        p.cols = o["cols"].toInt(80);
        p.rows = o["rows"].toInt(24);
        out.append(p);
    }
    return out;
}

bool exportProfilesToFile(const ProfileStore &store, const QString &filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(serializeProfiles(store.allProfiles())) >= 0;
}

int importProfilesFromFile(ProfileStore &store, const QString &filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return -1;
    const QVector<ConnectionProfile> profiles = deserializeProfiles(f.readAll());
    int imported = 0;
    for (const ConnectionProfile &p : profiles)
        if (store.upsert(p))
            ++imported;
    return imported;
}

} // namespace termsync::core
