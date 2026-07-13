// Round-trip tests for the in-process tar bundler used by the SFTP bulk
// folder-transfer path. No network: everything runs against temp directories,
// covering plain + gzip, nested trees, long paths, empty dirs, and the
// path-traversal guard.

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "archive/TarArchive.h"

using namespace termsync::transfer::archive;

namespace {

void writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(data);
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

// Builds a small tree under `dir`, tars it as `top`, extracts into a fresh
// directory, and asserts the round-trip reproduces every file byte-for-byte.
void roundTrip(bool gzip)
{
    QTemporaryDir src;
    QTemporaryDir dst;
    ASSERT_TRUE(src.isValid());
    ASSERT_TRUE(dst.isValid());

    const QString root = src.filePath("myfolder");
    writeFile(root + "/hello.txt", "hello world");
    writeFile(root + "/sub/a.bin", QByteArray(5000, '\x01'));
    writeFile(root + "/sub/deep/nested/note.md", "# note\n");
    writeFile(root + "/empty.dat", QByteArray()); // zero-length file
    QDir().mkpath(root + "/emptydir");            // empty directory

    const QString archive = dst.filePath(gzip ? "a.tar.gz" : "a.tar");
    QString err;
    ASSERT_TRUE(createTarFile(root, "myfolder", archive, gzip, &err)) << err.toStdString();

    const QString out = dst.filePath("out");
    ASSERT_TRUE(extractTarFile(archive, out, &err)) << err.toStdString();

    const QString base = out + "/myfolder";
    EXPECT_EQ(readFile(base + "/hello.txt"), QByteArray("hello world"));
    EXPECT_EQ(readFile(base + "/sub/a.bin"), QByteArray(5000, '\x01'));
    EXPECT_EQ(readFile(base + "/sub/deep/nested/note.md"), QByteArray("# note\n"));
    EXPECT_TRUE(QFile::exists(base + "/empty.dat"));
    EXPECT_EQ(readFile(base + "/empty.dat").size(), 0);
    EXPECT_TRUE(QFileInfo(base + "/emptydir").isDir());
}

} // namespace

TEST(TarArchive, RoundTripPlain)
{
    roundTrip(false);
}

TEST(TarArchive, RoundTripGzip)
{
    roundTrip(true);
}

TEST(TarArchive, GzipIsSmallerForCompressibleData)
{
    QTemporaryDir src, dst;
    const QString root = src.filePath("t");
    writeFile(root + "/big.txt", QByteArray(200000, 'A')); // highly compressible

    QString err;
    const QString plain = dst.filePath("p.tar");
    const QString gz = dst.filePath("g.tar.gz");
    ASSERT_TRUE(createTarFile(root, "t", plain, false, &err)) << err.toStdString();
    ASSERT_TRUE(createTarFile(root, "t", gz, true, &err)) << err.toStdString();
    EXPECT_LT(QFileInfo(gz).size(), QFileInfo(plain).size());
}

TEST(TarArchive, LongPathsSurviveRoundTrip)
{
    QTemporaryDir src, dst;
    // A path well beyond the 100-char ustar name field (exercises prefix split
    // and/or the GNU long-name fallback).
    QString deep = src.filePath("top");
    QString rel;
    for (int i = 0; i < 12; ++i)
        rel += QStringLiteral("a_directory_segment_%1/").arg(i);
    rel += "final_file_with_a_reasonably_long_name.txt";
    writeFile(deep + "/" + rel, "payload");

    QString err;
    const QString archive = dst.filePath("a.tar");
    ASSERT_TRUE(createTarFile(deep, "top", archive, false, &err)) << err.toStdString();
    const QString out = dst.filePath("out");
    ASSERT_TRUE(extractTarFile(archive, out, &err)) << err.toStdString();
    EXPECT_EQ(readFile(out + "/top/" + rel), QByteArray("payload"));
}

TEST(TarArchive, RejectsPathTraversal)
{
    // Hand-craft a tar whose single member is "../escape.txt" and confirm
    // extraction refuses it rather than writing outside destDir.
    QTemporaryDir dst;
    const QString archive = dst.filePath("evil.tar");

    // Build a minimal ustar header for "../escape.txt", size 3, type '0'.
    QByteArray block(512, '\0');
    const QByteArray name = "../escape.txt";
    memcpy(block.data(), name.constData(), name.size());
    memcpy(block.data() + 100, "0000644", 7);      // mode
    memcpy(block.data() + 124, "00000000003", 11); // size = 3 (octal)
    memcpy(block.data() + 136, "00000000000", 11); // mtime
    block[156] = '0';
    memcpy(block.data() + 257, "ustar", 5);
    block[263] = '0';
    block[264] = '0';
    // checksum
    memset(block.data() + 148, ' ', 8);
    unsigned sum = 0;
    for (unsigned char c : block)
        sum += c;
    char chk[8];
    snprintf(chk, sizeof(chk), "%06o", sum);
    memcpy(block.data() + 148, chk, 6);
    block[154] = '\0';
    block[155] = ' ';

    QByteArray tar = block;
    QByteArray data(512, '\0');
    memcpy(data.data(), "abc", 3);
    tar += data;               // file content block
    tar += QByteArray(512, '\0'); // end marker
    tar += QByteArray(512, '\0');

    QFile f(archive);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(tar);
    f.close();

    QString err;
    const QString out = dst.filePath("out");
    EXPECT_FALSE(extractTarFile(archive, out, &err));
    EXPECT_FALSE(QFile::exists(dst.filePath("escape.txt")));
}
