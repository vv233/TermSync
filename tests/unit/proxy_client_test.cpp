// Unit tests for the SOCKS5 / HTTP CONNECT proxy client handshake builders.

#include <gtest/gtest.h>

#include "net/ProxyClient.h"

using namespace termsync::core::proxy;

namespace {
QByteArray ba(std::initializer_list<int> v)
{
    QByteArray b;
    for (int x : v)
        b.append(static_cast<char>(x));
    return b;
}
} // namespace

TEST(ProxyClient, Socks5GreetingNoAuth)
{
    EXPECT_EQ(socks5Greeting(false), ba({0x05, 0x01, 0x00}));
    EXPECT_EQ(socks5Greeting(true), ba({0x05, 0x02, 0x00, 0x02}));
}

TEST(ProxyClient, Socks5ParseMethod)
{
    EXPECT_EQ(socks5ParseMethod(ba({0x05, 0x00})), 0x00);
    EXPECT_EQ(socks5ParseMethod(ba({0x05, 0x02})), 0x02);
    EXPECT_EQ(socks5ParseMethod(ba({0x05, 0xFF})), -1); // no acceptable method
    EXPECT_EQ(socks5ParseMethod(ba({0x04, 0x00})), -1); // wrong version
}

TEST(ProxyClient, Socks5UserPass)
{
    const QByteArray r = socks5UserPass("ab", "xyz");
    // ver 0x01, ulen 2, "ab", plen 3, "xyz"
    ASSERT_EQ(r.size(), 1 + 1 + 2 + 1 + 3);
    EXPECT_EQ(static_cast<unsigned char>(r[0]), 0x01);
    EXPECT_EQ(static_cast<unsigned char>(r[1]), 2);
    EXPECT_TRUE(socks5UserPassOk(ba({0x01, 0x00})));
    EXPECT_FALSE(socks5UserPassOk(ba({0x01, 0x01})));
}

TEST(ProxyClient, Socks5ConnectRequestDomain)
{
    const QByteArray r = socks5ConnectRequest("host.io", 22);
    // 05 01 00 03 len "host.io" 00 16
    ASSERT_EQ(r.size(), 4 + 1 + 7 + 2);
    EXPECT_EQ(static_cast<unsigned char>(r[0]), 0x05);
    EXPECT_EQ(static_cast<unsigned char>(r[1]), 0x01);
    EXPECT_EQ(static_cast<unsigned char>(r[3]), 0x03);
    EXPECT_EQ(static_cast<unsigned char>(r[4]), 7);
    EXPECT_EQ(static_cast<unsigned char>(r[r.size() - 1]), 22); // port low byte
    EXPECT_TRUE(socks5ConnectOk(ba({0x05, 0x00, 0x00, 0x01})));
    EXPECT_FALSE(socks5ConnectOk(ba({0x05, 0x01, 0x00, 0x01}))); // general failure
}

TEST(ProxyClient, HttpConnectRequest)
{
    const QByteArray r = httpConnectRequest("host.io", 22, QString(), QString());
    EXPECT_TRUE(r.startsWith("CONNECT host.io:22 HTTP/1.1\r\n"));
    EXPECT_TRUE(r.contains("Host: host.io:22\r\n"));
    EXPECT_TRUE(r.endsWith("\r\n\r\n"));

    const QByteArray auth = httpConnectRequest("h", 1, "user", "pass");
    EXPECT_TRUE(auth.contains("Proxy-Authorization: Basic "));
}

TEST(ProxyClient, HttpConnectResponseStatus)
{
    EXPECT_TRUE(httpConnectOk("HTTP/1.1 200 Connection established\r\n\r\n"));
    EXPECT_FALSE(httpConnectOk("HTTP/1.1 407 Proxy Auth Required\r\n\r\n"));
    EXPECT_FALSE(httpConnectOk("garbage"));
}
