#pragma once

#include <QString>
#include <QVector>

namespace termsync::terminal {

// A single highlight rule. `colorId` is caller-defined (the renderer maps it to
// an actual colour); this keeps the matcher independent of the screen palette.
struct HighlightRule
{
    QString pattern;
    bool regex = false;
    bool caseSensitive = false;
    bool wholeWord = false;   // substring rules only; require word boundaries
    int colorId = 0;
};

struct HighlightSpan
{
    int start = 0;
    int length = 0;
    int colorId = 0;
};

// Real-time keyword highlighting. Given a set of rules, it
// returns the spans to colour on a line. Overlaps are resolved deterministically:
// earlier rules win, and within a rule the leftmost match wins. Pure + testable.
class KeywordHighlighter
{
public:
    void setRules(const QVector<HighlightRule> &rules) { m_rules = rules; }
    void addRule(const HighlightRule &rule) { m_rules.append(rule); }
    const QVector<HighlightRule> &rules() const { return m_rules; }
    void clear() { m_rules.clear(); }

    // Spans to colour, sorted by start, non-overlapping.
    QVector<HighlightSpan> highlight(const QString &line) const;

private:
    QVector<HighlightRule> m_rules;
};

} // namespace termsync::terminal
