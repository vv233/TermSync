#include "text/KeywordHighlighter.h"

#include <QRegularExpression>
#include <algorithm>

namespace termsync::terminal {

namespace {

bool isWordChar(QChar c)
{
    return c.isLetterOrNumber() || c == '_';
}

bool wordBoundaryOk(const QString &line, int start, int length)
{
    const bool leftOk = start == 0 || !isWordChar(line[start - 1]);
    const int end = start + length;
    const bool rightOk = end >= line.size() || !isWordChar(line[end]);
    return leftOk && rightOk;
}

// All matches for one rule, in left-to-right order.
QVector<HighlightSpan> matchesFor(const HighlightRule &rule, const QString &line)
{
    QVector<HighlightSpan> spans;
    if (rule.pattern.isEmpty())
        return spans;

    if (rule.regex) {
        QRegularExpression::PatternOptions opts = QRegularExpression::NoPatternOption;
        if (!rule.caseSensitive)
            opts |= QRegularExpression::CaseInsensitiveOption;
        QRegularExpression re(rule.pattern, opts);
        if (!re.isValid())
            return spans;
        auto it = re.globalMatch(line);
        while (it.hasNext()) {
            const auto m = it.next();
            if (m.capturedLength() > 0)
                spans.append({static_cast<int>(m.capturedStart()),
                              static_cast<int>(m.capturedLength()), rule.colorId});
        }
        return spans;
    }

    const Qt::CaseSensitivity cs =
        rule.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    int from = 0;
    for (;;) {
        const int idx = line.indexOf(rule.pattern, from, cs);
        if (idx < 0)
            break;
        const int len = rule.pattern.size();
        if (!rule.wholeWord || wordBoundaryOk(line, idx, len))
            spans.append({idx, len, rule.colorId});
        from = idx + len; // non-overlapping within a rule
    }
    return spans;
}

} // namespace

QVector<HighlightSpan> KeywordHighlighter::highlight(const QString &line) const
{
    // Gather candidates in rule order (earlier rules take precedence), then
    // greedily accept non-overlapping spans.
    QVector<HighlightSpan> candidates;
    for (const HighlightRule &rule : m_rules)
        candidates += matchesFor(rule, line);

    // Stable order: by start, then by original rule precedence (already in
    // rule order within the vector, so a stable sort on start preserves it).
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const HighlightSpan &a, const HighlightSpan &b) {
                         return a.start < b.start;
                     });

    QVector<HighlightSpan> accepted;
    int coveredTo = -1;
    for (const HighlightSpan &s : candidates) {
        if (s.start > coveredTo) {
            accepted.append(s);
            coveredTo = s.start + s.length - 1;
        }
    }
    return accepted;
}

} // namespace termsync::terminal
