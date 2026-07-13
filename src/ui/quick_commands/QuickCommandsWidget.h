#pragma once

#include <QWidget>

class QLineEdit;
class QListWidget;
class QListWidgetItem;

namespace termsync::ui {

// A right-side panel of saved, frequently-used commands (like Termius snippets).
// Double-click (or context-menu "Run") sends a command to the active terminal;
// the list persists across sessions in QSettings.
class QuickCommandsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit QuickCommandsWidget(QWidget *parent = nullptr);

signals:
    // Run `command` in the active terminal. When `execute` is true a newline is
    // appended so it runs immediately; otherwise it is only typed in.
    void runCommand(const QString &command, bool execute);

private slots:
    void addFromInput();
    void onItemActivated(QListWidgetItem *item);
    void showContextMenu(const QPoint &pos);

private:
    void load();
    void save();
    void addCommand(const QString &command, bool persist = true);

    QListWidget *m_list = nullptr;
    QLineEdit *m_input = nullptr;
};

} // namespace termsync::ui
