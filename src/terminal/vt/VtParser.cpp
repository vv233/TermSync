#include "vt/VtParser.h"

namespace termsync::terminal {

namespace {
constexpr unsigned char BEL = 0x07;
constexpr unsigned char BS = 0x08;
constexpr unsigned char HT = 0x09;
constexpr unsigned char LF = 0x0a;
constexpr unsigned char VT = 0x0b;
constexpr unsigned char FF = 0x0c;
constexpr unsigned char CR = 0x0d;
constexpr unsigned char ESC = 0x1b;
} // namespace

VtParser::VtParser(ScreenBuffer *screen)
    : m_screen(screen)
{
}

int VtParser::param(int index, int fallback) const
{
    // A missing param, or an explicit 0, means "use the default". This is the
    // standard interpretation for cursor/erase/scroll commands (the callers of
    // this helper). SGR and mode-setting read m_params directly, where an
    // explicit 0 keeps its literal meaning.
    if (index >= m_params.size())
        return fallback;
    return m_params[index] == 0 ? fallback : m_params[index];
}

void VtParser::parse(const QByteArray &bytes)
{
    for (unsigned char b : bytes) {
        switch (m_state) {
        case State::Ground:
            if (b == ESC) {
                m_state = State::Escape;
            } else if (b < 0x20) {
                handleControl(b);
            } else {
                feedUtf8(b);
            }
            break;
        case State::Escape:
            handleEscape(b);
            break;
        case State::EscapeIntermediate:
            // Second byte of a charset designator etc. — consume and ignore.
            m_state = State::Ground;
            break;
        case State::CsiEntry:
            handleCsi(b);
            break;
        case State::OscString:
            handleOsc(b);
            break;
        }
    }
}

void VtParser::feedUtf8(unsigned char b)
{
    if (m_utf8Remaining > 0) {
        if ((b & 0xc0) == 0x80) {
            m_utf8Acc = (m_utf8Acc << 6) | (b & 0x3f);
            if (--m_utf8Remaining == 0)
                m_screen->writeChar(m_utf8Acc);
            return;
        }
        // Malformed continuation: emit replacement and reprocess b.
        m_screen->writeChar(U'�');
        m_utf8Remaining = 0;
        // fall through to treat b as a new starter
    }

    if (b < 0x80) {
        m_screen->writeChar(b);
    } else if ((b & 0xe0) == 0xc0) {
        m_utf8Acc = b & 0x1f;
        m_utf8Remaining = 1;
    } else if ((b & 0xf0) == 0xe0) {
        m_utf8Acc = b & 0x0f;
        m_utf8Remaining = 2;
    } else if ((b & 0xf8) == 0xf0) {
        m_utf8Acc = b & 0x07;
        m_utf8Remaining = 3;
    } else {
        m_screen->writeChar(U'�');
    }
}

void VtParser::handleControl(unsigned char b)
{
    switch (b) {
    case BEL:
        if (onBell)
            onBell(true);
        break;
    case BS:
        m_screen->backspace();
        break;
    case HT:
        m_screen->tab();
        break;
    case LF:
    case VT:
    case FF:
        m_screen->lineFeed();
        break;
    case CR:
        m_screen->carriageReturn();
        break;
    default:
        break; // SO/SI and others: ignored in M3
    }
}

void VtParser::handleEscape(unsigned char b)
{
    switch (b) {
    case '[':
        m_params.clear();
        m_intermediates.clear();
        m_privateMarker = false;
        m_paramStarted = false;
        m_state = State::CsiEntry;
        return;
    case ']':
        m_oscBuffer.clear();
        m_state = State::OscString;
        return;
    case '(': // designate G0 charset
    case ')': // G1
    case '*': // G2
    case '+': // G3
        m_state = State::EscapeIntermediate;
        return;
    case 'M': // RI
        m_screen->reverseIndex();
        break;
    case 'D': // IND
        m_screen->index();
        break;
    case 'E': // NEL
        m_screen->carriageReturn();
        m_screen->lineFeed();
        break;
    case '7': // DECSC
        m_screen->saveCursor();
        break;
    case '8': // DECRC
        m_screen->restoreCursor();
        break;
    case 'c': // RIS
        m_screen->reset();
        break;
    case '=': // application keypad
    case '>': // normal keypad
    default:
        break;
    }
    m_state = State::Ground;
}

void VtParser::handleCsi(unsigned char b)
{
    if (b == '?') {
        m_privateMarker = true;
        return;
    }
    if (b >= '0' && b <= '9') {
        if (!m_paramStarted) {
            m_params.append(0);
            m_paramStarted = true;
        }
        m_params.last() = m_params.last() * 10 + (b - '0');
        return;
    }
    if (b == ';') {
        // Finalize the current param (append a 0 if this slot was empty),
        // then start a fresh slot for what follows.
        if (!m_paramStarted)
            m_params.append(0);
        m_paramStarted = false;
        return;
    }
    if (b >= 0x20 && b <= 0x2f) { // intermediate bytes
        m_intermediates.append(static_cast<char>(b));
        return;
    }
    if (b >= 0x40 && b <= 0x7e) { // final byte
        dispatchCsi(b);
        m_state = State::Ground;
        return;
    }
    // Anything else aborts the sequence.
    m_state = State::Ground;
}

void VtParser::dispatchCsi(unsigned char finalByte)
{
    switch (finalByte) {
    case 'A': m_screen->moveCursorUp(param(0, 1)); break;
    case 'B': m_screen->moveCursorDown(param(0, 1)); break;
    case 'C': m_screen->moveCursorForward(param(0, 1)); break;
    case 'D': m_screen->moveCursorBack(param(0, 1)); break;
    case 'E': // CNL
        m_screen->moveCursorDown(param(0, 1));
        m_screen->carriageReturn();
        break;
    case 'F': // CPL
        m_screen->moveCursorUp(param(0, 1));
        m_screen->carriageReturn();
        break;
    case 'G': // CHA
    case '`': // HPA
        m_screen->setCursorColumn(param(0, 1) - 1);
        break;
    case 'd': // VPA
        m_screen->setCursorRow(param(0, 1) - 1);
        break;
    case 'H': // CUP
    case 'f': // HVP
        m_screen->moveCursor(param(0, 1) - 1, param(1, 1) - 1);
        break;
    case 'J': m_screen->eraseInDisplay(param(0, 0)); break;
    case 'K': m_screen->eraseInLine(param(0, 0)); break;
    case 'L': m_screen->insertLines(param(0, 1)); break;
    case 'M': m_screen->deleteLines(param(0, 1)); break;
    case 'P': m_screen->deleteChars(param(0, 1)); break;
    case '@': m_screen->insertChars(param(0, 1)); break;
    case 'X': m_screen->eraseChars(param(0, 1)); break;
    case 'S': m_screen->scrollUp(param(0, 1)); break;
    case 'T': m_screen->scrollDown(param(0, 1)); break;
    case 'r': // DECSTBM
        m_screen->setScrollRegion(param(0, 0), param(1, 0));
        break;
    case 'm': applySgr(); break;
    case 'h': setMode(true); break;
    case 'l': setMode(false); break;
    case 's': m_screen->saveCursor(); break;
    case 'u': m_screen->restoreCursor(); break;
    case 'n': // DSR — device status; a real reply needs a write-back channel
    case 'c': // DA — device attributes
    default:
        break;
    }
}

void VtParser::setMode(bool set)
{
    for (int i = 0; i < m_params.size(); ++i) {
        const int p = m_params[i];
        if (m_privateMarker) {
            switch (p) {
            case 1: // DECCKM — application cursor keys
                m_appCursorKeys = set;
                if (onApplicationCursorKeys)
                    onApplicationCursorKeys(set);
                break;
            case 7: // DECAWM — autowrap
                m_screen->setAutoWrap(set);
                break;
            case 25: // DECTCEM — cursor visibility
                m_screen->setCursorVisible(set);
                break;
            case 47:
            case 1047:
                m_screen->useAltScreen(set, /*clearOnEnter=*/set);
                break;
            case 1049:
                if (set)
                    m_screen->saveCursor();
                m_screen->useAltScreen(set, /*clearOnEnter=*/true);
                if (!set)
                    m_screen->restoreCursor();
                break;
            case 2004: // bracketed paste
                m_bracketedPaste = set;
                if (onBracketedPaste)
                    onBracketedPaste(set);
                break;
            default:
                break;
            }
        } else {
            // ANSI (non-private) modes: 4 = insert mode, etc. — rarely used;
            // ignored in M3.
        }
    }
    if (m_params.isEmpty())
        return;
}

void VtParser::applySgr()
{
    Pen &pen = m_screen->pen();
    if (m_params.isEmpty())
        m_params.append(0);

    for (int i = 0; i < m_params.size(); ++i) {
        const int p = m_params[i];
        switch (p) {
        case 0: pen.reset(); break;
        case 1: pen.flags |= Bold; break;
        case 2: pen.flags |= Faint; break;
        case 3: pen.flags |= Italic; break;
        case 4: pen.flags |= Underline; break;
        case 5: pen.flags |= Blink; break;
        case 7: pen.flags |= Reverse; break;
        case 8: pen.flags |= Invisible; break;
        case 9: pen.flags |= Strikethrough; break;
        case 22: pen.flags &= ~(Bold | Faint); break;
        case 23: pen.flags &= ~Italic; break;
        case 24: pen.flags &= ~Underline; break;
        case 25: pen.flags &= ~Blink; break;
        case 27: pen.flags &= ~Reverse; break;
        case 28: pen.flags &= ~Invisible; break;
        case 29: pen.flags &= ~Strikethrough; break;
        case 39: pen.fg = Color::defaultColor(); break;
        case 49: pen.bg = Color::defaultColor(); break;
        case 38: // extended fg
        case 48: { // extended bg
            Color c;
            bool ok = false;
            if (i + 1 < m_params.size() && m_params[i + 1] == 5) {
                if (i + 2 < m_params.size()) {
                    c = Color::indexed(static_cast<uint8_t>(m_params[i + 2]));
                    ok = true;
                    i += 2;
                }
            } else if (i + 1 < m_params.size() && m_params[i + 1] == 2) {
                if (i + 4 < m_params.size()) {
                    c = Color::rgb(static_cast<uint8_t>(m_params[i + 2]),
                                   static_cast<uint8_t>(m_params[i + 3]),
                                   static_cast<uint8_t>(m_params[i + 4]));
                    ok = true;
                    i += 4;
                }
            }
            if (ok) {
                if (p == 38)
                    pen.fg = c;
                else
                    pen.bg = c;
            }
            break;
        }
        default:
            if (p >= 30 && p <= 37)
                pen.fg = Color::indexed(static_cast<uint8_t>(p - 30));
            else if (p >= 40 && p <= 47)
                pen.bg = Color::indexed(static_cast<uint8_t>(p - 40));
            else if (p >= 90 && p <= 97)
                pen.fg = Color::indexed(static_cast<uint8_t>(p - 90 + 8));
            else if (p >= 100 && p <= 107)
                pen.bg = Color::indexed(static_cast<uint8_t>(p - 100 + 8));
            break;
        }
    }
}

void VtParser::handleOsc(unsigned char b)
{
    // OSC terminates on BEL or ST (ESC \). We approximate ST by watching for
    // ESC here and letting the following '\' fall through harmlessly.
    if (b == BEL || b == 0x9c /* ST */) {
        // Format: Ps ; Pt
        const int sep = m_oscBuffer.indexOf(';');
        if (sep >= 0) {
            const int code = m_oscBuffer.left(sep).toInt();
            const QString text = QString::fromUtf8(m_oscBuffer.mid(sep + 1));
            if (code == 0 || code == 1 || code == 2) {
                m_title = text;
                if (onTitleChanged)
                    onTitleChanged(text);
            }
        }
        m_state = State::Ground;
        return;
    }
    if (b == ESC) {
        // Likely the start of ST (ESC \). Terminate now; the trailing '\'
        // will be swallowed by the Escape state.
        const int sep = m_oscBuffer.indexOf(';');
        if (sep >= 0) {
            const int code = m_oscBuffer.left(sep).toInt();
            const QString text = QString::fromUtf8(m_oscBuffer.mid(sep + 1));
            if (code == 0 || code == 1 || code == 2) {
                m_title = text;
                if (onTitleChanged)
                    onTitleChanged(text);
            }
        }
        m_state = State::Escape;
        return;
    }
    m_oscBuffer.append(static_cast<char>(b));
}

} // namespace termsync::terminal
