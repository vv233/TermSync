#pragma once

#include <QMainWindow>

class QTabWidget;
class QTreeWidget;
class QDockWidget;

namespace termsync::ui {

// The application main window.
//
// M1 scope: assemble the SecureCRT/SecureFX-style outer shell only —
// menu bar, toolbar, dockable Session Manager, a tabbed session area,
// and a status bar. Actions are present but not yet functional; each
// milestone fills them in (see docs/ui-parity.md).
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    void createMenus();
    void createToolBar();
    void createSessionManagerDock();
    void createCentralArea();
    void createStatusBar();

    // Adds a placeholder "welcome" tab so the empty shell is not blank.
    void addWelcomeTab();

    // Opens the Quick Connect dialog and, on accept, starts an SSH2 session
    // in a new tab.
    void openQuickConnect();

    QTabWidget *m_sessionTabs = nullptr;
    QDockWidget *m_sessionManagerDock = nullptr;
    QTreeWidget *m_sessionTree = nullptr;
};

} // namespace termsync::ui
