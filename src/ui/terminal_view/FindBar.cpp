#include "FindBar.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>

namespace termsync::ui {

FindBar::FindBar(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("findBar"));
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(6);

    auto *label = new QLabel(tr("Find:"), this);
    m_edit = new QLineEdit(this);
    m_edit->setPlaceholderText(tr("Search terminal…"));
    m_edit->setClearButtonEnabled(true);

    auto *prev = new QToolButton(this);
    prev->setText(QStringLiteral("▲"));
    prev->setToolTip(tr("Find previous (Shift+Enter)"));
    auto *next = new QToolButton(this);
    next->setText(QStringLiteral("▼"));
    next->setToolTip(tr("Find next (Enter)"));

    m_caseBox = new QCheckBox(tr("Aa"), this);
    m_caseBox->setToolTip(tr("Match case"));

    m_status = new QLabel(this);
    m_status->setMinimumWidth(90);

    auto *close = new QToolButton(this);
    close->setText(QStringLiteral("✕"));
    close->setToolTip(tr("Close (Esc)"));

    layout->addWidget(label);
    layout->addWidget(m_edit, 1);
    layout->addWidget(prev);
    layout->addWidget(next);
    layout->addWidget(m_caseBox);
    layout->addWidget(m_status);
    layout->addWidget(close);

    setStyleSheet(QStringLiteral(
        "#findBar { background:#20242e; border-bottom:1px solid #333a48; }"
        "#findBar QLabel { color:#c8d0e8; }"
        "#findBar QLineEdit { background:#171a21; color:#e6e9f0;"
        " border:1px solid #333a48; border-radius:4px; padding:3px 6px; }"
        "#findBar QToolButton { color:#c8d0e8; border:0; padding:2px 8px;"
        " background:transparent; font-size:12pt; }"
        "#findBar QToolButton:hover { color:#2dd4bf; }"
        "#findBar QCheckBox { color:#c8d0e8; }"));

    connect(next, &QToolButton::clicked, this, [this] { emitSearch(true); });
    connect(prev, &QToolButton::clicked, this, [this] { emitSearch(false); });
    connect(close, &QToolButton::clicked, this, &FindBar::closed);
    // Live incremental search from the top of the document as the user types.
    connect(m_edit, &QLineEdit::textChanged, this, [this](const QString &t) {
        if (t.isEmpty())
            setNotFound(false); // clear the tint when the field is emptied
        else
            emit incrementalSearch(t, caseSensitive());
    });
}

void FindBar::activate()
{
    show();
    m_edit->setFocus();
    m_edit->selectAll();
}

QString FindBar::text() const
{
    return m_edit->text();
}

bool FindBar::caseSensitive() const
{
    return m_caseBox->isChecked();
}

void FindBar::setNotFound(bool notFound)
{
    if (m_edit->text().isEmpty()) {
        m_status->clear();
        m_edit->setStyleSheet(QString());
        return;
    }
    if (notFound) {
        m_status->setText(tr("No match"));
        m_edit->setStyleSheet(
            QStringLiteral("background:#3a1d1d; color:#f0c8c8;"
                           " border:1px solid #7a3a3a; border-radius:4px;"
                           " padding:3px 6px;"));
    } else {
        m_status->clear();
        m_edit->setStyleSheet(QString());
    }
}

void FindBar::emitSearch(bool forward)
{
    if (!m_edit->text().isEmpty())
        emit searchRequested(m_edit->text(), forward, caseSensitive());
    else
        setNotFound(false); // clear the tint when the field is emptied
}

void FindBar::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        emit closed();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        emitSearch(!(event->modifiers() & Qt::ShiftModifier));
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

} // namespace termsync::ui
