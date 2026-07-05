#include <gtest/gtest.h>

#include <QTemporaryDir>

#include "schedule/JobScheduler.h"

using namespace termsync::transfer::schedule;

namespace {

ScheduledJob makeJob(const QString &id, qint64 interval)
{
    ScheduledJob j;
    j.id = id;
    j.name = "job-" + id;
    j.command = "sync";
    j.host = "example.com";
    j.user = "u";
    j.localPath = "/tmp/x";
    j.remotePath = "data";
    j.intervalSecs = interval;
    return j;
}

} // namespace

TEST(JobScheduler, NeverRunJobIsDueImmediately)
{
    ScheduledJob j = makeJob("a", 3600);
    EXPECT_EQ(j.nextRun(), 0);
    EXPECT_TRUE(j.isDue(0));
    EXPECT_TRUE(j.isDue(100000));
}

TEST(JobScheduler, IntervalGatesNextRun)
{
    JobScheduler s;
    s.add(makeJob("a", 100));
    // Runs at t=1000; not due again until t=1100.
    s.markRan("a", 1000);
    EXPECT_TRUE(s.dueJobs(1099).isEmpty());
    ASSERT_EQ(s.dueJobs(1100).size(), 1);
    EXPECT_EQ(s.dueJobs(1100).first().id, "a");
}

TEST(JobScheduler, OneShotDisablesAfterRun)
{
    JobScheduler s;
    s.add(makeJob("once", 0)); // interval 0 => one-shot
    EXPECT_EQ(s.dueJobs(0).size(), 1);
    s.markRan("once", 500);
    EXPECT_TRUE(s.dueJobs(100000).isEmpty());
    ASSERT_TRUE(s.find("once"));
    EXPECT_FALSE(s.find("once")->enabled);
}

TEST(JobScheduler, DisabledJobNeverDue)
{
    ScheduledJob j = makeJob("d", 10);
    j.enabled = false;
    EXPECT_FALSE(j.isDue(1'000'000'000));
}

TEST(JobScheduler, AddReplacesExistingId)
{
    JobScheduler s;
    s.add(makeJob("a", 100));
    ScheduledJob updated = makeJob("a", 200);
    updated.name = "renamed";
    s.add(updated);
    ASSERT_EQ(s.jobs().size(), 1);
    EXPECT_EQ(s.jobs().first().name, "renamed");
    EXPECT_EQ(s.jobs().first().intervalSecs, 200);
}

TEST(JobScheduler, RemoveJob)
{
    JobScheduler s;
    s.add(makeJob("a", 100));
    s.add(makeJob("b", 100));
    EXPECT_TRUE(s.remove("a"));
    EXPECT_FALSE(s.remove("a"));
    ASSERT_EQ(s.jobs().size(), 1);
    EXPECT_EQ(s.jobs().first().id, "b");
}

TEST(JobScheduler, JsonRoundTrip)
{
    QTemporaryDir tmp;
    const QString path = tmp.filePath("jobs.json");

    JobScheduler a;
    ScheduledJob j = makeJob("a", 3600);
    j.relentless = true;
    j.throttleKbps = 4096;
    j.lastRun = 12345;
    j.down = true;
    a.add(j);
    ASSERT_TRUE(a.save(path));

    JobScheduler b;
    ASSERT_TRUE(b.load(path));
    ASSERT_EQ(b.jobs().size(), 1);
    const ScheduledJob &r = b.jobs().first();
    EXPECT_EQ(r.id, "a");
    EXPECT_TRUE(r.relentless);
    EXPECT_EQ(r.throttleKbps, 4096);
    EXPECT_EQ(r.lastRun, 12345);
    EXPECT_TRUE(r.down);
}

TEST(JobScheduler, LoadMissingFileIsEmptyOk)
{
    JobScheduler s;
    EXPECT_TRUE(s.load("/nonexistent/path/jobs.json"));
    EXPECT_TRUE(s.jobs().isEmpty());
}
