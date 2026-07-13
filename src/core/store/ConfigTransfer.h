#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include "model/ConnectionProfile.h"

namespace termsync::core {

class ProfileStore;

// Import/export settings support. Serialises connection profiles to a
// portable JSON document — secrets are never included (only the savePassword
// flag), matching the store's "no secrets in the profile row" design.
QByteArray serializeProfiles(const QVector<ConnectionProfile> &profiles);
QVector<ConnectionProfile> deserializeProfiles(const QByteArray &json);

// Convenience wrappers over a ProfileStore. exportToFile writes every profile;
// importFromFile upserts each parsed profile and returns the count imported
// (or -1 on read/parse failure).
bool exportProfilesToFile(const ProfileStore &store, const QString &filePath);
int importProfilesFromFile(ProfileStore &store, const QString &filePath);

} // namespace termsync::core
