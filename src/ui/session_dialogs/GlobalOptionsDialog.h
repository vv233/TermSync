#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QSpinBox;

namespace termsync::ui {

// Application-wide preferences (SecureCRT's "Global Options"). Consolidates the
// settings that were previously scattered across the Options menu — the default
// terminal colour scheme and font applied to new sessions, and the SFTP browser
// style. Loads from and saves to QSettings("TermSync","TermSync") directly; the
// caller refreshes its cached defaults after accept().
class GlobalOptionsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit GlobalOptionsDialog(QWidget *parent = nullptr);

    // Persist the current selections to QSettings. Called on accept().
    void save() const;

private:
    QComboBox *m_scheme = nullptr;
    QFontComboBox *m_fontCombo = nullptr;
    QSpinBox *m_fontSize = nullptr;
    QComboBox *m_sftpStyle = nullptr;
};

} // namespace termsync::ui
