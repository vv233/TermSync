#pragma once

#include <QDialog>
#include <QFont>
#include <QString>

class QCheckBox;
class QFontComboBox;
class QLabel;
class QListWidget;
class QSpinBox;

namespace termsync::ui {

// Terminal appearance picker (M20): choose a colour scheme from the built-in
// set (rendered as mini-terminal thumbnails, like Termius) and a monospace font
// + size, with a live preview. Returns the selection to MainWindow, which
// applies it and persists it.
class TerminalAppearanceDialog : public QDialog
{
    Q_OBJECT

public:
    TerminalAppearanceDialog(const QString &currentScheme,
                             const QFont &currentFont, QWidget *parent = nullptr);

    QString selectedScheme() const;
    QFont selectedFont() const;
    bool applyToAll() const;

private:
    void updatePreview();

    QListWidget *m_list = nullptr;
    QFontComboBox *m_fontCombo = nullptr;
    QSpinBox *m_sizeSpin = nullptr;
    QLabel *m_preview = nullptr;
    QCheckBox *m_applyAll = nullptr;
};

} // namespace termsync::ui
