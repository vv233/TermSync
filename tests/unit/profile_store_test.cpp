// Unit tests for the SQLite ProfileStore: CRUD, ordering, known-hosts, and
// persistence across reopen.

#include <gtest/gtest.h>

#include <QTemporaryDir>

#include "store/ProfileStore.h"

using namespace termsync::core;

namespace {

ConnectionProfile makeProfile(const QString &name, const QString &host)
{
    ConnectionProfile p;
    p.id = ProfileStore::newId();
    p.name = name;
    p.host = host;
    p.port = 22;
    p.username = "user";
    p.protocol = Protocol::SSH2;
    p.authMethod = AuthMethod::Password;
    return p;
}

} // namespace

TEST(ProfileStore, OpenCreatesSchema)
{
    QTemporaryDir dir;
    ProfileStore store;
    ASSERT_TRUE(store.open(dir.filePath("p.db"))) << store.lastError().toStdString();
    EXPECT_TRUE(store.allProfiles().isEmpty());
}

TEST(ProfileStore, InsertAndList)
{
    QTemporaryDir dir;
    ProfileStore store;
    ASSERT_TRUE(store.open(dir.filePath("p.db")));

    ASSERT_TRUE(store.upsert(makeProfile("alpha", "a.example.com")));
    ASSERT_TRUE(store.upsert(makeProfile("beta", "b.example.com")));

    const auto profiles = store.allProfiles();
    ASSERT_EQ(profiles.size(), 2);
    // Ordered by folder_path, name.
    EXPECT_EQ(profiles[0].name, "alpha");
    EXPECT_EQ(profiles[1].name, "beta");
}

TEST(ProfileStore, UpsertUpdatesInPlace)
{
    QTemporaryDir dir;
    ProfileStore store;
    ASSERT_TRUE(store.open(dir.filePath("p.db")));

    ConnectionProfile p = makeProfile("srv", "old.example.com");
    ASSERT_TRUE(store.upsert(p));

    p.host = "new.example.com";
    p.port = 2222;
    ASSERT_TRUE(store.upsert(p));

    const auto profiles = store.allProfiles();
    ASSERT_EQ(profiles.size(), 1);
    EXPECT_EQ(profiles[0].host, "new.example.com");
    EXPECT_EQ(profiles[0].port, 2222);
}

TEST(ProfileStore, Remove)
{
    QTemporaryDir dir;
    ProfileStore store;
    ASSERT_TRUE(store.open(dir.filePath("p.db")));

    ConnectionProfile p = makeProfile("srv", "x.example.com");
    ASSERT_TRUE(store.upsert(p));
    ASSERT_EQ(store.allProfiles().size(), 1);

    ASSERT_TRUE(store.remove(p.id));
    EXPECT_TRUE(store.allProfiles().isEmpty());
}

TEST(ProfileStore, PersistsAcrossReopen)
{
    QTemporaryDir dir;
    const QString path = dir.filePath("p.db");
    ConnectionProfile p = makeProfile("persist", "p.example.com");
    p.savePassword = true;
    {
        ProfileStore store;
        ASSERT_TRUE(store.open(path));
        ASSERT_TRUE(store.upsert(p));
    }
    {
        ProfileStore store;
        ASSERT_TRUE(store.open(path));
        const auto profiles = store.allProfiles();
        ASSERT_EQ(profiles.size(), 1);
        EXPECT_EQ(profiles[0].name, "persist");
        EXPECT_TRUE(profiles[0].savePassword);
    }
}

TEST(ProfileStore, KnownHostsRoundTrip)
{
    QTemporaryDir dir;
    ProfileStore store;
    ASSERT_TRUE(store.open(dir.filePath("p.db")));

    EXPECT_TRUE(store.knownFingerprint("h.example.com", 22).isEmpty());
    ASSERT_TRUE(store.setKnownFingerprint("h.example.com", 22, "aa:bb:cc"));
    EXPECT_EQ(store.knownFingerprint("h.example.com", 22), "aa:bb:cc");

    // Update in place.
    ASSERT_TRUE(store.setKnownFingerprint("h.example.com", 22, "dd:ee:ff"));
    EXPECT_EQ(store.knownFingerprint("h.example.com", 22), "dd:ee:ff");
}
