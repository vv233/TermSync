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
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QDockWidget;
class QPoint;

namespace termsync::ui {

class TerminalWidget;
class FindBar;
class HostsHomeWidget;

// The application main window.
//
// Main application shell: menus, tabs, host navigation, session management,
// status reporting, and dockable tools. Unavailable actions stay disabled until
// their complete workflow is present.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;
#ifdef _WIN32
    // Custom window frame: removes the native caption so the tab strip becomes
    // the title bar (Termius-style), while keeping native resize / snap / shadow.
    bool nativeEvent(const QByteArray &eventType, void *message,
                     qintptr *result) override;
#endif

private:
    void createMenus();
    // Builds the min / maximize / close buttons that sit in the tab strip.
    void createWindowControls();
    void toggleMaximizeRestore();
    void updateMaximizeIcon();
    // Height of the draggable title strip (the tab-bar row).
    int titleBarHeight() const;
    void createToolBar();
    void createSessionManagerDock();
    void createQuickCommandsDock();
    void createCentralArea();
    void createStatusBar();
    // Sends a quick-command snippet to the active terminal tab (if any).
    void runQuickCommand(const QString &command, bool execute);

    // Adds the (non-closable) Hosts home tab — the app's landing page.
    void addHomeTab();
    // Connects a saved profile by id, dispatching by protocol (terminal vs SFTP).
    void connectById(const QString &id, bool sftp);
    // Edit / delete a saved host from the Hosts home page.
    void editHost(const QString &id);
    void deleteHost(const QString &id);
    // Parses "user@host[:port]" from the home connect bar and connects.
    void quickConnectFromText(const QString &text);

    // Persists a host's detected OS id and refreshes the home cards' icons.
    void rememberHostOs(const QString &profileId, const QString &osId);

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

    // SecureCRT-style Options menu (M20 polish): a categorised per-session
    // properties editor and an app-wide preferences dialog.
    void openSessionOptions();
    void openGlobalOptions();

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

    // Edit menu (M20 polish): act on the current terminal tab. updateEditActions
    // greys them out when the active tab is not a terminal.
    TerminalWidget *currentTerminal() const;
    void updateEditActions();
    void toggleFullScreen(bool on);
    void showFindBar();
    void printCurrentSession();

    // Session lifecycle (M20 polish): operate on the current terminal tab.
    void reconnectCurrentSession();
    void disconnectCurrentSession();
    void cloneCurrentSession();

    // Synchronous trust-on-first-use check against the known-hosts store.
    // Prompts on unknown/changed keys; persists accepted keys.
    bool verifyHostKey(const QString &host, quint16 port,
                       const QString &fingerprint);

    QTabWidget *m_sessionTabs = nullptr;
    QWidget *m_titleBar = nullptr;      // custom caption row (hamburger + controls)
    QToolButton *m_hamburger = nullptr; // app menu button, lives in the title bar
    QToolButton *m_maxButton = nullptr; // its icon toggles maximize/restore
    QDockWidget *m_sessionManagerDock = nullptr;
    QDockWidget *m_quickCommandsDock = nullptr;
    QTreeWidget *m_sessionTree = nullptr;
    HostsHomeWidget *m_home = nullptr;
    QToolBar *m_toolbar = nullptr;
    QAction *m_hexViewAct = nullptr;

    // Edit menu actions kept so their enabled state can track the active tab.
    QAction *m_copyAct = nullptr;
    QAction *m_pasteAct = nullptr;
    QAction *m_selectAllAct = nullptr;
    QAction *m_clearScreenAct = nullptr;
    QAction *m_clearScrollbackAct = nullptr;
    QAction *m_findAct = nullptr;
    QAction *m_printAct = nullptr;
    QAction *m_fullScreenAct = nullptr;
    FindBar *m_findBar = nullptr;

    // File menu session-lifecycle actions (enabled only on a terminal tab).
    QAction *m_reconnectAct = nullptr;
    QAction *m_disconnectAct = nullptr;
    QAction *m_cloneAct = nullptr;

    std::unique_ptr<core::ProfileStore> m_profileStore;
    std::unique_ptr<core::CredentialStore> m_credentialStore;
    QVector<core::ConnectionProfile> m_profiles;

    // Terminal appearance defaults (applied to new terminals). Font pointSize
    // <= 0 means "keep the widget's built-in font".
    QString m_terminalScheme;
    QFont m_terminalFont;
};

} // namespace termsync::ui
