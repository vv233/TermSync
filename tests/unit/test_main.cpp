// Custom gtest entry point that installs a QCoreApplication, which the Qt SQL
// module (used by ProfileStore) needs for plugin loading and its event loop.

#include <QCoreApplication>
#include <gtest/gtest.h>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
