#pragma once

#include <QFont>
#include <QMainWindow>
#include <memory>

#include "credential/CredentialStore.h"
#include "model/ConnectionProfile.h"
#include "store/ProfileStore.h"

class QAction;
class QTabWidget;
class QToolBar;
class QTreeWidget;
class QTreeWidgetItem;
class QDockWidget;
class QPoint;

namespace termsync::ui {

class TerminalWidget;
class HostsHomeWidget;

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

    // Adds the (non-closable) Hosts home tab — the app's landing page.
    void addHomeTab();
    // Connects a saved profile by id, dispatching by protocol (terminal vs SFTP).
    void connectById(const QString &id, bool sftp);
    // Parses "user@host[:port]" from the home connect bar and connects.
    void quickConnectFromText(const QString &text);

    // Opens the Quick Connect dialog and, on accept, starts an SSH2 session
    // in a new tab.
    void openQuickConnect();
    void openQuickSftp();

    // Session store / tree.
    void initStores();
    void loadProfilesIntoTree();
    void onSessionActivated(QTreeWidgetItem *item, int column);
    void showSessionContextMenu(const QPoint &pos);

    // Connects a saved profile (retrieving/prompting for the password), and
    // the low-level "open a terminal tab for these params" helper.
    void connectProfile(const core::ConnectionProfile &profile);
    void connectProfileSftp(const core::ConnectionProfile &profile);
    void startSession(const core::ConnectionProfile &profile,
                      const QString &password);
    void startSftpSession(const core::ConnectionProfile &profile,
                          const QString &password);
    void startTelnetSession(const core::ConnectionProfile &profile);
    void startTn3270Session(const core::ConnectionProfile &profile);
    void startTn5250Session(const core::ConnectionProfile &profile);
    void startSerialSession(const core::ConnectionProfile &profile);

    // Local Shell (M20): open a terminal tab backed by the platform shell.
    void openLocalShell();

    // Terminal appearance (M20): colour scheme + font picker. loadAppearance
    // reads persisted prefs; applyAppearance seeds a freshly created terminal.
    void openTerminalAppearance();
    void loadAppearance();
    void applyAppearance(TerminalWidget *terminal) const;

    // TFTP server (M20): open the built-in TFTP server control panel.
    void openTftpServer();

    // Runs a JavaScript automation script against the active terminal tab.
    void runScript();

    // Import/Export Settings (M20c): serialise connection profiles to/from a
    // portable JSON document via core::ConfigTransfer.
    void importSettings();
    void exportSettings();

    // Log Session (M20b): toggle raw session logging on the active terminal tab.
    void toggleSessionLog();

    // Keyword Highlighting (M20a): edit the active terminal's highlight rules.
    void editKeywordHighlighting();

    // Hex View (M20a): toggle the raw-byte hex dump on the active terminal, and
    // keep the menu check in sync when the current tab changes.
    void setHexViewForCurrent(bool on);
    void syncHexViewAction();

    // Synchronous trust-on-first-use check against the known-hosts store.
    // Prompts on unknown/changed keys; persists accepted keys.
    bool verifyHostKey(const QString &host, quint16 port,
                       const QString &fingerprint);

    QTabWidget *m_sessionTabs = nullptr;
    QDockWidget *m_sessionManagerDock = nullptr;
    QTreeWidget *m_sessionTree = nullptr;
    HostsHomeWidget *m_home = nullptr;
    QToolBar *m_toolbar = nullptr;
    QAction *m_hexViewAct = nullptr;

    std::unique_ptr<core::ProfileStore> m_profileStore;
    std::unique_ptr<core::CredentialStore> m_credentialStore;
    QVector<core::ConnectionProfile> m_profiles;

    // Terminal appearance defaults (applied to new terminals). Font pointSize
    // <= 0 means "keep the widget's built-in font".
    QString m_terminalScheme;
    QFont m_terminalFont;
};

} // namespace termsync::ui
