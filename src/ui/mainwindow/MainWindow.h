#pragma once

#include <QMainWindow>
#include <memory>

#include "credential/CredentialStore.h"
#include "model/ConnectionProfile.h"
#include "store/ProfileStore.h"

class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QDockWidget;
class QPoint;

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

    // Runs a JavaScript automation script against the active terminal tab.
    void runScript();

    // Import/Export Settings (M20c): serialise connection profiles to/from a
    // portable JSON document via core::ConfigTransfer.
    void importSettings();
    void exportSettings();

    // Synchronous trust-on-first-use check against the known-hosts store.
    // Prompts on unknown/changed keys; persists accepted keys.
    bool verifyHostKey(const QString &host, quint16 port,
                       const QString &fingerprint);

    QTabWidget *m_sessionTabs = nullptr;
    QDockWidget *m_sessionManagerDock = nullptr;
    QTreeWidget *m_sessionTree = nullptr;

    std::unique_ptr<core::ProfileStore> m_profileStore;
    std::unique_ptr<core::CredentialStore> m_credentialStore;
    QVector<core::ConnectionProfile> m_profiles;
};

} // namespace termsync::ui
