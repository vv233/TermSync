// Smoke test — proves the test harness compiles, links and runs.
// Real unit suites arrive with their milestones (VtParser in M3, the sync
// DirectoryDiffer in M7, ProfileStore in M4, ...).

#include <gtest/gtest.h>

TEST(Smoke, TestHarnessWorks)
{
    EXPECT_EQ(1 + 1, 2);
}
