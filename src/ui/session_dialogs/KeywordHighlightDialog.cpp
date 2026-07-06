#include "session_dialogs/KeywordHighlightDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace termsync::ui {

namespace {
// Colour labels, indexed by colorId to match the TerminalWidget palette.
const QStringList &colorNames()
{
    static const QStringList names{
        QStringLiteral("Amber"), QStringLiteral("Red"), QStringLiteral("Green"),
        QStringLiteral("Cyan"), QStringLiteral("Magenta")};
    return names;
}

QString describe(const terminal::HighlightRule &r)
{
    QStringList flags;
    if (r.regex)
        flags << QStringLiteral("regex");
    if (r.caseSensitive)
        flags << QStringLiteral("case");
    if (r.wholeWord)
        flags << QStringLiteral("word");
    const QString color = r.colorId >= 0 && r.colorId < colorNames().size()
                              ? colorNames().at(r.colorId)
                              : QString::number(r.colorId);
    QString s = QStringLiteral("%1  [%2]").arg(r.pattern, color);
    if (!flags.isEmpty())
        s += QStringLiteral("  (%1)").arg(flags.join(", "));
    return s;
}
} // namespace

KeywordHighlightDialog::KeywordHighlightDialog(
    const QVector<terminal::HighlightRule> &initial, QWidget *parent)
    : QDialog(parent), m_rules(initial)
{
    setWindowTitle(tr("Keyword Highlighting"));
    resize(460, 380);

    auto *outer = new QVBoxLayout(this);

    m_list = new QListWidget(this);
    outer->addWidget(m_list, 1);

    auto *removeBtn = new QPushButton(tr("Remove Selected"), this);
    connect(removeBtn, &QPushButton::clicked, this,
            &KeywordHighlightDialog::removeSelected);
    outer->addWidget(removeBtn);

    // --- Add-rule row ---
    auto *form = new QFormLayout;
    m_pattern = new QLineEdit(this);
    m_pattern->setPlaceholderText(tr("Text or regular expression to match"));
    form->addRow(tr("Pattern:"), m_pattern);

    m_color = new QComboBox(this);
    m_color->addItems(colorNames());
    form->addRow(tr("Colour:"), m_color);

    auto *flags = new QHBoxLayout;
    m_regex = new QCheckBox(tr("Regex"), this);
    m_caseSensitive = new QCheckBox(tr("Case sensitive"), this);
    m_wholeWord = new QCheckBox(tr("Whole word"), this);
    flags->addWidget(m_regex);
    flags->addWidget(m_caseSensitive);
    flags->addWidget(m_wholeWord);
    form->addRow(tr("Options:"), flags);
    outer->addLayout(form);

    auto *addBtn = new QPushButton(tr("Add Rule"), this);
    connect(addBtn, &QPushButton::clicked, this,
            &KeywordHighlightDialog::addRuleFromInputs);
    connect(m_pattern, &QLineEdit::returnPressed, this,
            &KeywordHighlightDialog::addRuleFromInputs);
    outer->addWidget(addBtn);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);

    refreshList();
}

void KeywordHighlightDialog::addRuleFromInputs()
{
    const QString pattern = m_pattern->text();
    if (pattern.isEmpty())
        return;
    terminal::HighlightRule rule;
    rule.pattern = pattern;
    rule.regex = m_regex->isChecked();
    rule.caseSensitive = m_caseSensitive->isChecked();
    rule.wholeWord = m_wholeWord->isChecked();
    rule.colorId = m_color->currentIndex();
    m_rules.append(rule);

    m_pattern->clear();
    m_pattern->setFocus();
    refreshList();
}

void KeywordHighlightDialog::removeSelected()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_rules.size())
        return;
    m_rules.remove(row);
    refreshList();
}

void KeywordHighlightDialog::refreshList()
{
    m_list->clear();
    for (const terminal::HighlightRule &r : m_rules)
        m_list->addItem(describe(r));
}

} // namespace termsync::ui
