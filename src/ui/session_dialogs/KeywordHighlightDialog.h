#pragma once

#include <QDialog>
#include <QVector>

#include "text/KeywordHighlighter.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;

namespace termsync::ui {

// Editor for a terminal's keyword-highlight rules (M20a). A small add/remove
// list dialog; the colour choices map to the TerminalWidget highlight palette
// (colorId 0..4). Pure UI over terminal::HighlightRule.
class KeywordHighlightDialog : public QDialog
{
    Q_OBJECT

public:
    explicit KeywordHighlightDialog(
        const QVector<terminal::HighlightRule> &initial,
        QWidget *parent = nullptr);

    QVector<terminal::HighlightRule> rules() const { return m_rules; }

private:
    void addRuleFromInputs();
    void removeSelected();
    void refreshList();

    QVector<terminal::HighlightRule> m_rules;

    QLineEdit *m_pattern = nullptr;
    QComboBox *m_color = nullptr;
    QCheckBox *m_regex = nullptr;
    QCheckBox *m_caseSensitive = nullptr;
    QCheckBox *m_wholeWord = nullptr;
    QListWidget *m_list = nullptr;
};

} // namespace termsync::ui
