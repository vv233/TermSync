#include <gtest/gtest.h>

#include <QTemporaryDir>

#include "browse/PathMirror.h"
#include "store/BookmarkStore.h"

using namespace termsync::core;

namespace {

Bookmark mk(const QString &id, const QString &name, const QString &host,
            const QString &remote)
{
    Bookmark b;
    b.id = id;
    b.name = name;
    b.host = host;
    b.remotePath = remote;
    return b;
}

} // namespace

TEST(BookmarkStore, AddFindRemove)
{
    BookmarkStore s;
    s.add(mk("1", "logs", "h1", "/var/log"));
    s.add(mk("2", "home", "h1", "/home/u"));
    ASSERT_EQ(s.bookmarks().size(), 2);
    ASSERT_TRUE(s.find("1"));
    EXPECT_EQ(s.find("1")->remotePath, "/var/log");
    EXPECT_TRUE(s.remove("1"));
    EXPECT_FALSE(s.remove("1"));
    EXPECT_EQ(s.bookmarks().size(), 1);
}

TEST(BookmarkStore, AddReplacesSameId)
{
    BookmarkStore s;
    s.add(mk("1", "logs", "h1", "/var/log"));
    s.add(mk("1", "logs2", "h1", "/var/log/nginx"));
    ASSERT_EQ(s.bookmarks().size(), 1);
    EXPECT_EQ(s.bookmarks().first().name, "logs2");
}

TEST(BookmarkStore, ForHostIncludesGlobal)
{
    BookmarkStore s;
    s.add(mk("1", "onH1", "h1", "/a"));
    s.add(mk("2", "onH2", "h2", "/b"));
    s.add(mk("3", "global", "", "/c"));
    const auto h1 = s.forHost("h1");
    ASSERT_EQ(h1.size(), 2); // h1 + global
    EXPECT_EQ(s.forHost("h2").size(), 2);
    EXPECT_EQ(s.forHost("unknown").size(), 1); // only global
}

TEST(BookmarkStore, JsonRoundTrip)
{
    QTemporaryDir tmp;
    const QString path = tmp.filePath("bm.json");
    BookmarkStore a;
    Bookmark b = mk("1", "logs", "h1", "/var/log");
    b.localPath = "/home/me/logs";
    a.add(b);
    ASSERT_TRUE(a.save(path));

    BookmarkStore c;
    ASSERT_TRUE(c.load(path));
    ASSERT_EQ(c.bookmarks().size(), 1);
    EXPECT_EQ(c.bookmarks().first().localPath, "/home/me/logs");
    EXPECT_EQ(c.bookmarks().first().host, "h1");
}

TEST(BookmarkStore, LoadMissingIsEmptyOk)
{
    BookmarkStore s;
    EXPECT_TRUE(s.load("/no/such/file.json"));
    EXPECT_TRUE(s.bookmarks().isEmpty());
}

// --- synchronized browsing path mirror ---

TEST(PathMirror, MirrorsSubPath)
{
    EXPECT_EQ(mirrorPath("/home/a", "/home/a/docs/x", "/backup/a"),
              QString("/backup/a/docs/x"));
}

TEST(PathMirror, RootMapsToRoot)
{
    EXPECT_EQ(mirrorPath("/home/a", "/home/a", "/backup/a"), QString("/backup/a"));
    EXPECT_EQ(mirrorPath("/home/a/", "/home/a", "/backup/a/"), QString("/backup/a"));
}

TEST(PathMirror, OutsideSubtreeReturnsEmpty)
{
    EXPECT_TRUE(mirrorPath("/home/a", "/etc/passwd", "/backup/a").isEmpty());
    // A sibling that merely shares a prefix string must not match.
    EXPECT_TRUE(mirrorPath("/home/a", "/home/ab/x", "/backup/a").isEmpty());
}
