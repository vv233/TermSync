#include "session_dialogs/GlobalOptionsDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

#include "theme/ColorScheme.h"

namespace termsync::ui {

GlobalOptionsDialog::GlobalOptionsDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Global Options"));
    QSettings settings(QStringLiteral("TermSync"), QStringLiteral("TermSync"));

    // --- Terminal defaults --------------------------------------------------
    auto *terminalBox = new QGroupBox(tr("Terminal defaults (new sessions)"), this);
    auto *tform = new QFormLayout(terminalBox);

    m_scheme = new QComboBox(terminalBox);
    for (const terminal::ColorScheme &s : terminal::builtinSchemes())
        m_scheme->addItem(s.name);
    const QString scheme = settings
                               .value(QStringLiteral("terminal/scheme"),
                                      terminal::defaultSchemeName())
                               .toString();
    if (int i = m_scheme->findText(scheme); i >= 0)
        m_scheme->setCurrentIndex(i);

    m_fontCombo = new QFontComboBox(terminalBox);
    m_fontCombo->setFontFilters(QFontComboBox::MonospacedFonts);
    const QString family =
        settings.value(QStringLiteral("terminal/fontFamily")).toString();
    if (!family.isEmpty())
        m_fontCombo->setCurrentFont(QFont(family));

    m_fontSize = new QSpinBox(terminalBox);
    m_fontSize->setRange(6, 48);
    m_fontSize->setSpecialValueText(tr("Default"));
    const int pt = settings.value(QStringLiteral("terminal/fontSize"), -1).toInt();
    m_fontSize->setValue(pt > 0 ? pt : m_fontSize->minimum());

    tform->addRow(tr("Colour scheme:"), m_scheme);
    tform->addRow(tr("Font:"), m_fontCombo);
    tform->addRow(tr("Font size:"), m_fontSize);

    // --- File transfer ------------------------------------------------------
    auto *transferBox = new QGroupBox(tr("File transfer"), this);
    auto *xform = new QFormLayout(transferBox);
    m_sftpStyle = new QComboBox(transferBox);
    m_sftpStyle->addItem(tr("Explorer (Windows 11)"), QStringLiteral("explorer"));
    m_sftpStyle->addItem(tr("Traditional (dual-pane)"),
                         QStringLiteral("traditional"));
    const QString style =
        settings.value(QStringLiteral("sftp/style")).toString() ==
                QStringLiteral("traditional")
            ? QStringLiteral("traditional")
            : QStringLiteral("explorer");
    if (int i = m_sftpStyle->findData(style); i >= 0)
        m_sftpStyle->setCurrentIndex(i);
    xform->addRow(tr("SFTP browser style:"), m_sftpStyle);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *root = new QVBoxLayout(this);
    root->addWidget(terminalBox);
    root->addWidget(transferBox);
    root->addStretch(1);
    root->addWidget(buttons);
    resize(420, 320);
}

void GlobalOptionsDialog::save() const
{
    QSettings settings(QStringLiteral("TermSync"), QStringLiteral("TermSync"));
    settings.setValue(QStringLiteral("terminal/scheme"), m_scheme->currentText());
    settings.setValue(QStringLiteral("terminal/fontFamily"),
                      m_fontCombo->currentFont().family());
    if (m_fontSize->value() > m_fontSize->minimum())
        settings.setValue(QStringLiteral("terminal/fontSize"), m_fontSize->value());
    else
        settings.remove(QStringLiteral("terminal/fontSize"));
    settings.setValue(QStringLiteral("sftp/style"),
                      m_sftpStyle->currentData().toString());
}

} // namespace termsync::ui
