#include "session_dialogs/TerminalAppearanceDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QSpinBox>
#include <QVBoxLayout>

#include "theme/ColorScheme.h"

namespace termsync::ui {

namespace {

QColor toColor(uint32_t v)
{
    return QColor((v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);
}

// A small "mini terminal" thumbnail: rounded window with a few coloured lines,
// echoing the reference picker's cards.
QPixmap thumbnail(const terminal::ColorScheme &s, QSize size)
{
    QPixmap pm(size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF body(1, 1, size.width() - 2, size.height() - 2);
    p.setBrush(toColor(s.background));
    p.setPen(QPen(toColor(s.ansi[8]), 1)); // bright-black border
    p.drawRoundedRect(body, 6, 6);

    // Three "prompt" bars using foreground, green and a dim colour.
    const uint32_t bars[3] = {s.foreground, s.ansi[2], s.ansi[6]};
    const double x0 = body.left() + 8;
    double y = body.top() + 9;
    const double h = 4;
    for (int i = 0; i < 3; ++i) {
        const double w = body.width() * (i == 0 ? 0.62 : (i == 1 ? 0.44 : 0.30));
        p.setPen(Qt::NoPen);
        p.setBrush(toColor(bars[i]));
        p.drawRoundedRect(QRectF(x0, y, w, h), 2, 2);
        y += h + 5;
    }
    return pm;
}

// A larger live preview: a couple of sample lines rendered in the scheme + font.
QPixmap preview(const terminal::ColorScheme &s, const QFont &font, QSize size)
{
    QPixmap pm(size);
    QPainter p(&pm);
    p.fillRect(pm.rect(), toColor(s.background));
    p.setFont(font);
    const QFontMetrics fm(font);
    const int lh = fm.height() + 2;
    int y = 6 + fm.ascent();
    const int x = 8;

    auto line = [&](const QVector<QPair<uint32_t, QString>> &runs) {
        int cx = x;
        for (const auto &r : runs) {
            p.setPen(toColor(r.first));
            p.drawText(cx, y, r.second);
            cx += fm.horizontalAdvance(r.second);
        }
        y += lh;
    };

    line({{s.foreground, QStringLiteral("user@termsync")},
          {s.ansi[8], QStringLiteral(":")},
          {s.ansi[4], QStringLiteral("~/src")},
          {s.foreground, QStringLiteral("$ ls --color")}});
    line({{s.ansi[4], QStringLiteral("bin  ")},
          {s.ansi[2], QStringLiteral("build  ")},
          {s.ansi[1], QStringLiteral("error.log  ")},
          {s.ansi[3], QStringLiteral("TODO  ")},
          {s.ansi[6], QStringLiteral("readme.md")}});
    line({{s.ansi[5], QStringLiteral("git")},
          {s.foreground, QStringLiteral(":(")},
          {s.ansi[1], QStringLiteral("main")},
          {s.foreground, QStringLiteral(") the quick brown fox 0123456789")}});

    // A block cursor.
    p.fillRect(QRect(x, y - fm.ascent(), fm.horizontalAdvance(QLatin1Char('M')),
                     fm.height()),
               toColor(s.cursor));
    return pm;
}

QFont makeFont(const QString &family, int pt)
{
    QFont f(family);
    f.setStyleHint(QFont::Monospace);
    f.setFixedPitch(true);
    f.setKerning(false);
    f.setPointSize(pt);
    return f;
}

} // namespace

TerminalAppearanceDialog::TerminalAppearanceDialog(const QString &currentScheme,
                                                   const QFont &currentFont,
                                                   QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Terminal Appearance"));
    resize(620, 520);

    auto *outer = new QVBoxLayout(this);

    auto *top = new QHBoxLayout;

    // --- Theme list (left) ---
    m_list = new QListWidget(this);
    m_list->setIconSize(QSize(96, 60));
    m_list->setMinimumWidth(260);
    for (const terminal::ColorScheme &s : terminal::builtinSchemes()) {
        auto *item = new QListWidgetItem(thumbnail(s, QSize(96, 60)), s.name);
        item->setData(Qt::UserRole, s.name);
        m_list->addItem(item);
        if (s.name == currentScheme)
            m_list->setCurrentItem(item);
    }
    if (!m_list->currentItem() && m_list->count() > 0)
        m_list->setCurrentRow(0);
    top->addWidget(m_list, 1);

    // --- Font controls + live preview (right) ---
    auto *right = new QVBoxLayout;
    auto *form = new QFormLayout;
    m_fontCombo = new QFontComboBox(this);
    m_fontCombo->setFontFilters(QFontComboBox::MonospacedFonts);
    m_fontCombo->setCurrentFont(currentFont);
    form->addRow(tr("Font:"), m_fontCombo);

    m_sizeSpin = new QSpinBox(this);
    m_sizeSpin->setRange(6, 48);
    m_sizeSpin->setValue(currentFont.pointSize() > 0 ? currentFont.pointSize()
                                                      : 11);
    m_sizeSpin->setSuffix(tr(" pt"));
    form->addRow(tr("Size:"), m_sizeSpin);
    right->addLayout(form);

    right->addWidget(new QLabel(tr("Preview:"), this));
    m_preview = new QLabel(this);
    m_preview->setMinimumSize(300, 150);
    m_preview->setFrameShape(QFrame::StyledPanel);
    right->addWidget(m_preview, 1);
    top->addLayout(right, 1);

    outer->addLayout(top, 1);

    m_applyAll = new QCheckBox(tr("Apply to all open terminals"), this);
    m_applyAll->setChecked(true);
    outer->addWidget(m_applyAll);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);

    connect(m_list, &QListWidget::currentRowChanged, this,
            [this](int) { updatePreview(); });
    connect(m_fontCombo, &QFontComboBox::currentFontChanged, this,
            [this] { updatePreview(); });
    connect(m_sizeSpin, &QSpinBox::valueChanged, this,
            [this] { updatePreview(); });

    updatePreview();
}

void TerminalAppearanceDialog::updatePreview()
{
    const terminal::ColorScheme *s = terminal::findScheme(selectedScheme());
    if (!s || !m_preview)
        return;
    m_preview->setPixmap(preview(*s, selectedFont(), m_preview->size()));
}

QString TerminalAppearanceDialog::selectedScheme() const
{
    if (auto *item = m_list->currentItem())
        return item->data(Qt::UserRole).toString();
    return terminal::defaultSchemeName();
}

QFont TerminalAppearanceDialog::selectedFont() const
{
    return makeFont(m_fontCombo->currentFont().family(), m_sizeSpin->value());
}

bool TerminalAppearanceDialog::applyToAll() const
{
    return m_applyAll->isChecked();
}

} // namespace termsync::ui
