#include <gtest/gtest.h>

#include <QDateTime>
#include <QDir>
#include <QTemporaryDir>

#include "log/SessionLogger.h"

using namespace termsync::core;

namespace {

QDateTime fixed()
{
    return QDateTime(QDate(2026, 7, 6), QTime(9, 8, 7));
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

TEST(SessionLogger, ExpandFilenameTokens)
{
    EXPECT_EQ(expandLogFilename("%H-%S-%Y%M%D-%h%m%s.log", "srv1", "prod", fixed()),
              QString("srv1-prod-20260706-090807.log"));
}

TEST(SessionLogger, ExpandLiteralPercentAndUnknown)
{
    EXPECT_EQ(expandLogFilename("100%% %Q", "h", "s", fixed()), QString("100% %Q"));
}

TEST(SessionLogger, LineTimestampsAcrossChunks)
{
    bool atStart = true;
    const QByteArray a = applyLineTimestamps("hello\nwor", "[HH:mm:ss] ", fixed(), &atStart);
    EXPECT_EQ(a, QByteArray("[09:08:07] hello\n[09:08:07] wor"));
    EXPECT_FALSE(atStart); // mid-line
    // The next chunk must NOT get a leading stamp (still same line).
    const QByteArray b = applyLineTimestamps("ld\n", "[HH:mm:ss] ", fixed(), &atStart);
    EXPECT_EQ(b, QByteArray("ld\n"));
    EXPECT_TRUE(atStart);
}

TEST(SessionLogger, WritesAppendedContent)
{
    QTemporaryDir tmp;
    const QString path = tmp.filePath("s.log");
    SessionLogger log;
    ASSERT_TRUE(log.open(path));
    ASSERT_TRUE(log.write("line1\n", fixed()));
    ASSERT_TRUE(log.write("line2\n", fixed()));
    log.close();
    EXPECT_EQ(readAll(path), QByteArray("line1\nline2\n"));
}

TEST(SessionLogger, TimestampOption)
{
    QTemporaryDir tmp;
    const QString path = tmp.filePath("s.log");
    SessionLogger::Options o;
    o.timestampLines = true;
    o.timestampFormat = "[HH:mm:ss] ";
    SessionLogger log;
    ASSERT_TRUE(log.open(path, o));
    ASSERT_TRUE(log.write("a\nb\n", fixed()));
    log.close();
    EXPECT_EQ(readAll(path), QByteArray("[09:08:07] a\n[09:08:07] b\n"));
}

TEST(SessionLogger, SizeRotation)
{
    QTemporaryDir tmp;
    const QString path = tmp.filePath("s.log");
    SessionLogger::Options o;
    o.maxBytes = 10;
    o.maxFiles = 3;
    SessionLogger log;
    ASSERT_TRUE(log.open(path, o));
    ASSERT_TRUE(log.write("0123456789", fixed())); // hits maxBytes -> rotate
    ASSERT_TRUE(log.write("next", fixed()));
    log.close();
    EXPECT_EQ(readAll(path), QByteArray("next"));           // fresh current file
    EXPECT_EQ(readAll(path + ".1"), QByteArray("0123456789")); // rotated old
}
