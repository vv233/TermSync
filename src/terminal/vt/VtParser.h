#pragma once

#include <QString>
#include <QVector>
#include <functional>

#include "screen/ScreenBuffer.h"

namespace termsync::terminal {

// A VT100/ECMA-48 + common-xterm-extension parser.
//
// It consumes a byte stream (as delivered by the SSH channel) and mutates a
// ScreenBuffer. It is deliberately Qt-Widgets-free and side-effect-light so it
// can be unit-tested headlessly: feed bytes, inspect the resulting grid.
//
// Coverage (M3): CSI cursor/erase/edit/scroll, SGR (16/256/truecolor),
// DEC private modes (autowrap, cursor visibility, alt screen 1049/47/1047,
// bracketed paste, application cursor keys), OSC window title, UTF-8, and the
// common single-byte controls. Rare legacy sequences are ignored gracefully.
class VtParser
{
public:
    explicit VtParser(ScreenBuffer *screen);

    void parse(const QByteArray &bytes);

    // Signals surfaced to the widget layer (set by the owner). Kept as
    // std::function to avoid a QObject dependency in this pure-logic class.
    std::function<void(const QString &)> onTitleChanged;
    std::function<void(bool)> onBell;
    std::function<void(bool)> onApplicationCursorKeys;
    std::function<void(bool)> onBracketedPaste;

    // Exposed for tests.
    bool applicationCursorKeys() const { return m_appCursorKeys; }
    bool bracketedPaste() const { return m_bracketedPaste; }
    QString title() const { return m_title; }

private:
    enum class State {
        Ground,
        Escape,
        CsiEntry,      // collecting CSI params/intermediates
        OscString,     // collecting OSC string until BEL/ST
        EscapeIntermediate, // e.g. charset designators ( ) * +
    };

    void handleControl(unsigned char b);
    void handleGround(unsigned char b);
    void handleEscape(unsigned char b);
    void handleCsi(unsigned char b);
    void handleOsc(unsigned char b);

    void dispatchCsi(unsigned char finalByte);
    void applySgr();
    void setMode(bool set);   // handles the collected params (private or not)

    int param(int index, int fallback) const;

    // UTF-8 incremental decoding in ground state.
    void feedUtf8(unsigned char b);

    ScreenBuffer *m_screen;
    State m_state = State::Ground;

    // CSI collection.
    QVector<int> m_params;
    QByteArray m_intermediates;
    bool m_privateMarker = false;   // '?' seen
    bool m_paramStarted = false;

    // OSC collection.
    QByteArray m_oscBuffer;

    // Mode state.
    bool m_appCursorKeys = false;
    bool m_bracketedPaste = false;
    QString m_title;

    // UTF-8 decoder.
    char32_t m_utf8Acc = 0;
    int m_utf8Remaining = 0;
};

} // namespace termsync::terminal
