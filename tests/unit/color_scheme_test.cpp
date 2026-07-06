#include <gtest/gtest.h>

#include <QSet>

#include "theme/ColorScheme.h"

using namespace termsync::terminal;

TEST(ColorScheme, RegistryHasExpectedSchemes)
{
    const auto &all = builtinSchemes();
    EXPECT_EQ(all.size(), 14);
    // A few of the named schemes from the picker must exist.
    for (const char *name : {"Termius Dark", "Flexoki Dark", "Kanagawa Wave",
                             "Hacker Green", "Night Owl", "Everforest Light"})
        EXPECT_NE(findScheme(QString::fromLatin1(name)), nullptr) << name;
}

TEST(ColorScheme, NamesAreUnique)
{
    QSet<QString> names;
    for (const ColorScheme &s : builtinSchemes()) {
        EXPECT_FALSE(names.contains(s.name)) << s.name.toStdString();
        names.insert(s.name);
    }
}

TEST(ColorScheme, DefaultIsFirstAndResolvable)
{
    EXPECT_EQ(defaultSchemeName(), builtinSchemes().front().name);
    EXPECT_NE(findScheme(defaultSchemeName()), nullptr);
}

TEST(ColorScheme, MissingSchemeReturnsNull)
{
    EXPECT_EQ(findScheme(QStringLiteral("No Such Theme")), nullptr);
}

TEST(ColorScheme, DarkSchemesReadDarkAndLightReadLight)
{
    // Sanity: a dark scheme's background luminance is below its foreground's,
    // and vice-versa for light schemes.
    auto luma = [](uint32_t c) {
        const int r = (c >> 16) & 0xff, g = (c >> 8) & 0xff, b = c & 0xff;
        return 0.299 * r + 0.587 * g + 0.114 * b;
    };
    for (const ColorScheme &s : builtinSchemes()) {
        if (s.dark)
            EXPECT_LT(luma(s.background), luma(s.foreground)) << s.name.toStdString();
        else
            EXPECT_GT(luma(s.background), luma(s.foreground)) << s.name.toStdString();
    }
}
