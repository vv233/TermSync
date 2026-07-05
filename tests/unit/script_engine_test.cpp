// Unit tests for the JavaScript automation engine and its crt object model,
// driven by a fake ScriptContext (no live session).

#include <gtest/gtest.h>

#include <QStringList>

#include "ScriptContext.h"
#include "ScriptEngine.h"

using namespace termsync::script;

namespace {

class FakeContext : public ScriptContext
{
public:
    QStringList sent;
    QString screen = "user@host:~$ ";
    QStringList waitedFor;
    int slept = 0;

    void send(const QString &text) override { sent << text; }
    QString screenText() const override { return screen; }
    bool waitForString(const QString &needle, int) override
    {
        waitedFor << needle;
        return screen.contains(needle);
    }
    void sleepMs(int ms) override { slept += ms; }
};

} // namespace

TEST(ScriptEngine, SendReachesContext)
{
    FakeContext ctx;
    ScriptEngine engine(&ctx);
    const auto r = engine.run("crt.Screen.Send('ls\\r');");
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    ASSERT_EQ(ctx.sent.size(), 1);
    EXPECT_EQ(ctx.sent[0], "ls\r");
}

TEST(ScriptEngine, SendLineAppendsCarriageReturn)
{
    FakeContext ctx;
    ScriptEngine engine(&ctx);
    ASSERT_TRUE(engine.run("crt.Session.SendLine('whoami');").ok);
    ASSERT_EQ(ctx.sent.size(), 1);
    EXPECT_EQ(ctx.sent[0], "whoami\r");
}

TEST(ScriptEngine, WaitForStringReadsScreen)
{
    FakeContext ctx;
    ctx.screen = "login: ";
    ScriptEngine engine(&ctx);
    const auto r = engine.run("crt.Screen.WaitForString('login:', 1000);");
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    EXPECT_EQ(r.value, "true");
    EXPECT_EQ(ctx.waitedFor.size(), 1);
    EXPECT_EQ(ctx.waitedFor[0], "login:");
}

TEST(ScriptEngine, GetReturnsScreenText)
{
    FakeContext ctx;
    ctx.screen = "hello world";
    ScriptEngine engine(&ctx);
    const auto r = engine.run("crt.Screen.Get();");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.value, "hello world");
}

TEST(ScriptEngine, ControlFlowAndBranching)
{
    FakeContext ctx;
    ctx.screen = "Password:";
    ScriptEngine engine(&ctx);
    // A realistic snippet: wait for a prompt, then send a response.
    const QString script = R"(
        if (crt.Screen.WaitForString('Password:', 2000)) {
            crt.Screen.Send('secret\r');
        } else {
            crt.Screen.Send('\x03'); // Ctrl-C
        }
    )";
    ASSERT_TRUE(engine.run(script).ok);
    ASSERT_EQ(ctx.sent.size(), 1);
    EXPECT_EQ(ctx.sent[0], "secret\r");
}

TEST(ScriptEngine, SleepForwarded)
{
    FakeContext ctx;
    ScriptEngine engine(&ctx);
    ASSERT_TRUE(engine.run("crt.Sleep(250);").ok);
    EXPECT_EQ(ctx.slept, 250);
}

TEST(ScriptEngine, SyntaxErrorReported)
{
    FakeContext ctx;
    ScriptEngine engine(&ctx);
    const auto r = engine.run("this is not valid javascript {{{");
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.isEmpty());
}
