#include <gtest/gtest.h>

#include <QTemporaryDir>

#include "store/ConfigTransfer.h"
#include "store/ProfileStore.h"

using namespace termsync::core;

namespace {

ConnectionProfile mk(const QString &id, const QString &name, Protocol proto)
{
    ConnectionProfile p;
    p.id = id;
    p.name = name;
    p.folderPath = "Work/Prod";
    p.protocol = proto;
    p.host = "srv." + name;
    p.port = 2222;
    p.username = "u_" + name;
    p.authMethod = AuthMethod::PublicKey;
    p.savePassword = true;
    p.privateKeyPath = "/keys/" + name;
    p.cols = 132;
    p.rows = 43;
    return p;
}

} // namespace

TEST(ConfigTransfer, SerializeRoundTrip)
{
    QVector<ConnectionProfile> in{mk("1", "alpha", Protocol::SSH2),
                                  mk("2", "beta", Protocol::FTPS)};
    const QVector<ConnectionProfile> out = deserializeProfiles(serializeProfiles(in));
    ASSERT_EQ(out.size(), 2);
    EXPECT_EQ(out[0].name, "alpha");
    EXPECT_EQ(out[0].protocol, Protocol::SSH2);
    EXPECT_EQ(out[0].port, 2222);
    EXPECT_EQ(out[0].authMethod, AuthMethod::PublicKey);
    EXPECT_TRUE(out[0].savePassword);
    EXPECT_EQ(out[0].privateKeyPath, "/keys/alpha");
    EXPECT_EQ(out[1].protocol, Protocol::FTPS);
    EXPECT_EQ(out[1].cols, 132);
}

TEST(ConfigTransfer, ExportImportThroughStore)
{
    QTemporaryDir tmp;
    const QString exportFile = tmp.filePath("settings.json");

    ProfileStore src;
    ASSERT_TRUE(src.open(tmp.filePath("src.db")));
    ASSERT_TRUE(src.upsert(mk("1", "alpha", Protocol::SSH2)));
    ASSERT_TRUE(src.upsert(mk("2", "beta", Protocol::TELNET)));
    ASSERT_TRUE(exportProfilesToFile(src, exportFile));

    ProfileStore dst;
    ASSERT_TRUE(dst.open(tmp.filePath("dst.db")));
    EXPECT_EQ(importProfilesFromFile(dst, exportFile), 2);
    EXPECT_EQ(dst.allProfiles().size(), 2);

    // Re-importing is idempotent (upsert by id).
    EXPECT_EQ(importProfilesFromFile(dst, exportFile), 2);
    EXPECT_EQ(dst.allProfiles().size(), 2);
}

TEST(ConfigTransfer, ImportMissingFileReturnsMinusOne)
{
    ProfileStore dst;
    QTemporaryDir tmp;
    ASSERT_TRUE(dst.open(tmp.filePath("dst.db")));
    EXPECT_EQ(importProfilesFromFile(dst, "/no/such/settings.json"), -1);
}
