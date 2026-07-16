#include "mainwindow/MainWindow.h"

#include "common/Icons.h"
#include "mainwindow/ChromeTabWidget.h"

#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QHash>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <windowsx.h> // GET_X_LPARAM
#  include <dwmapi.h>
#endif

#include <QEvent>
#include <QHBoxLayout>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QWindow>

#include "ScriptEngine.h"
#include "home/HostsHomeWidget.h"
#include "quick_commands/QuickCommandsWidget.h"
#include "local/LocalShellConnection.h"
#include "script/TerminalScriptContext.h"
#include "session_dialogs/KeywordHighlightDialog.h"
#include "session_dialogs/TerminalAppearanceDialog.h"
#include "session_dialogs/TftpServerDialog.h"
#include "store/ConfigTransfer.h"
#include "theme/ColorScheme.h"
#include "serial/SerialConnection.h"
#include "session_dialogs/QuickConnectDialog.h"
#include "telnet/TelnetConnection.h"
#include "tn3270/Tn3270Connection.h"
#include "tn3270/Tn5250Connection.h"
#include "terminal_view/FindBar.h"
#include "terminal_view/TerminalWidget.h"
#include "transfer_view/DualPaneBrowser.h"
#include "transfer_view/ExplorerSftpBrowser.h"
#include "transfer_view/SftpBrowserWidget.h"

namespace {
// Role used to stash a profile id on a session-tree leaf item.
constexpr int kProfileIdRole = Qt::UserRole + 1;

#ifdef _WIN32
// Paint the native window title bar dark so it matches the app instead of
// clashing as a bright bar under a dark UI (Windows draws it light by default
// when the OS is in light mode).
void applyDarkTitleBar(QWidget *w)
{
    const auto hwnd = reinterpret_cast<HWND>(w->winId());
    BOOL dark = TRUE;
    // DWMWA_USE_IMMERSIVE_DARK_MODE is 20 on Windows 10 2004+, 19 on older 10.
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark))))
        DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
}
#endif
} // namespace

namespace termsync::ui {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("TermSync"));
    resize(1100, 720);

    createCentralArea();
    createToolBar();
    createSessionManagerDock();
    createQuickCommandsDock();
    createMenus();
    createStatusBar();

    loadAppearance();
    initStores();
    addHomeTab();
    loadProfilesIntoTree();

    // After the first tab exists, so the QTabWidget lays the corner widget out.
    createWindowControls();

#ifdef _WIN32
    applyDarkTitleBar(this);
#endif
}

MainWindow::~MainWindow() = default;

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
#ifdef _WIN32
    // Reapply on first show so the title bar is dark from the very first paint,
    // and force a frame recompute so WM_NCCALCSIZE strips the native caption.
    applyDarkTitleBar(this);
    updateMaximizeIcon();
#endif
}

#ifdef _WIN32
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message,
                            qintptr *result)
{
    // Custom frameless caption disabled: Qt would not render the window controls
    // on the right of this shell's top bar. The native (DWM-darkened) title bar
    // is used instead. Left here for a future retry.
    return QMainWindow::nativeEvent(eventType, message, result);

    if (eventType != "windows_generic_MSG")
        return QMainWindow::nativeEvent(eventType, message, result);
    auto *msg = static_cast<MSG *>(message);

    switch (msg->message) {
    case WM_NCCALCSIZE:
        if (msg->wParam == TRUE) {
            // Reclaim the caption strip into the client area (removing the native
            // title bar) while keeping the frame for resize / snap / shadow. When
            // maximized, inset by the frame so content isn't clipped offscreen.
            if (IsZoomed(msg->hwnd)) {
                auto *p = reinterpret_cast<NCCALCSIZE_PARAMS *>(msg->lParam);
                const int fx = GetSystemMetrics(SM_CXFRAME) +
                               GetSystemMetrics(SM_CXPADDEDBORDER);
                const int fy = GetSystemMetrics(SM_CYFRAME) +
                               GetSystemMetrics(SM_CXPADDEDBORDER);
                p->rgrc[0].left += fx;
                p->rgrc[0].right -= fx;
                p->rgrc[0].top += fy;
                p->rgrc[0].bottom -= fy;
            }
            *result = 0;
            return true;
        }
        break;
    case WM_NCHITTEST: {
        const LONG gx = GET_X_LPARAM(msg->lParam);
        const LONG gy = GET_Y_LPARAM(msg->lParam);
        RECT wr;
        GetWindowRect(msg->hwnd, &wr);

        // Native resize grips along the edges (not while maximized).
        if (!IsZoomed(msg->hwnd)) {
            const int b = GetSystemMetrics(SM_CXSIZEFRAME) +
                          GetSystemMetrics(SM_CXPADDEDBORDER);
            const bool L = gx < wr.left + b, R = gx >= wr.right - b;
            const bool T = gy < wr.top + b, B = gy >= wr.bottom - b;
            qintptr hit = 0;
            if (T && L) hit = HTTOPLEFT;
            else if (T && R) hit = HTTOPRIGHT;
            else if (B && L) hit = HTBOTTOMLEFT;
            else if (B && R) hit = HTBOTTOMRIGHT;
            else if (L) hit = HTLEFT;
            else if (R) hit = HTRIGHT;
            else if (T) hit = HTTOP;
            else if (B) hit = HTBOTTOM;
            if (hit) {
                *result = hit;
                return true;
            }
        }

        // The tab strip doubles as the title bar: empty areas drag the window
        // (and double-click maximizes), but tabs / buttons stay clickable.
        const qreal dpr = devicePixelRatioF();
        const QPoint local(qRound((gx - wr.left) / dpr),
                           qRound((gy - wr.top) / dpr));
        if (local.y() >= 0 && local.y() < titleBarHeight()) {
            QWidget *child = childAt(local);
            if (!child || child == m_titleBar) {
                *result = HTCAPTION; // empty title-bar area drags the window
                return true;
            }
        }
        return false; // HTCLIENT — the widget under the cursor handles it
    }
    default:
        break;
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif

void MainWindow::createMenus()
{
    // Termius-style: the classic top-level menus live under a single "≡"
    // hamburger button in the tab-strip corner (built at the end); appMenu is
    // their common parent instead of the (hidden) menu bar.
    QMenu *appMenu = new QMenu(this);

    auto placeholder = [this](QMenu *menu, const QString &text) {
        QAction *act = menu->addAction(text);
        act->setEnabled(false);
        return act;
    };

    // --- File ---
    QMenu *fileMenu = appMenu->addMenu(tr("&File"));
    placeholder(fileMenu, tr("Connect..."));
    QAction *quickConnectAct = fileMenu->addAction(tr("Quick Connect..."));
    quickConnectAct->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Q));
    connect(quickConnectAct, &QAction::triggered, this,
            &MainWindow::openQuickConnect);
    placeholder(fileMenu, tr("Connect in Tab..."));
    QAction *connectSftpAct = fileMenu->addAction(tr("Connect SFTP Session"));
    connect(connectSftpAct, &QAction::triggered, this, &MainWindow::openQuickSftp);
    QAction *localShellAct = fileMenu->addAction(tr("Local Shell"));
    connect(localShellAct, &QAction::triggered, this, &MainWindow::openLocalShell);
    fileMenu->addSeparator();
    m_reconnectAct = fileMenu->addAction(tr("Reconnect"));
    connect(m_reconnectAct, &QAction::triggered, this,
            &MainWindow::reconnectCurrentSession);
    m_disconnectAct = fileMenu->addAction(tr("Disconnect"));
    connect(m_disconnectAct, &QAction::triggered, this,
            &MainWindow::disconnectCurrentSession);
    m_cloneAct = fileMenu->addAction(tr("Clone Session"));
    connect(m_cloneAct, &QAction::triggered, this,
            &MainWindow::cloneCurrentSession);
    fileMenu->addSeparator();
    QAction *logSessionAct = fileMenu->addAction(tr("Log Session..."));
    connect(logSessionAct, &QAction::triggered, this,
            &MainWindow::toggleSessionLog);
    placeholder(fileMenu, tr("Print..."));
    fileMenu->addSeparator();
    QAction *exitAct = fileMenu->addAction(tr("E&xit"));
    exitAct->setShortcut(QKeySequence::Quit);
    connect(exitAct, &QAction::triggered, this, &QWidget::close);

    // --- Edit ---
    QMenu *editMenu = appMenu->addMenu(tr("&Edit"));
    m_copyAct = editMenu->addAction(tr("Copy"));
    m_copyAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
    connect(m_copyAct, &QAction::triggered, this, [this] {
        if (auto *t = currentTerminal())
            t->editCopy();
    });
    m_pasteAct = editMenu->addAction(tr("Paste"));
    m_pasteAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
    connect(m_pasteAct, &QAction::triggered, this, [this] {
        if (auto *t = currentTerminal())
            t->editPaste();
    });
    m_selectAllAct = editMenu->addAction(tr("Select All"));
    connect(m_selectAllAct, &QAction::triggered, this, [this] {
        if (auto *t = currentTerminal())
            t->editSelectAll();
    });
    editMenu->addSeparator();
    m_clearScreenAct = editMenu->addAction(tr("Clear Screen"));
    connect(m_clearScreenAct, &QAction::triggered, this, [this] {
        if (auto *t = currentTerminal())
            t->clearScreen();
    });
    m_clearScrollbackAct = editMenu->addAction(tr("Clear Scrollback"));
    connect(m_clearScrollbackAct, &QAction::triggered, this, [this] {
        if (auto *t = currentTerminal())
            t->clearScrollback();
    });
    m_findAct = editMenu->addAction(tr("Find..."));
    m_findAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
    connect(m_findAct, &QAction::triggered, this, &MainWindow::showFindBar);
    addAction(m_findAct); // keep Ctrl+Shift+F live with the menu hidden
    // Keep the terminal-only actions greyed out unless a terminal tab is active.
    connect(appMenu, &QMenu::aboutToShow, this, &MainWindow::updateEditActions);
    editMenu->addSeparator();
    QAction *highlightAct = editMenu->addAction(tr("Keyword Highlighting..."));
    connect(highlightAct, &QAction::triggered, this,
            &MainWindow::editKeywordHighlighting);

    // --- View ---
    QMenu *viewMenu = appMenu->addMenu(tr("&View"));
    if (m_toolbar) {
        QAction *tb = m_toolbar->toggleViewAction();
        tb->setText(tr("Toolbar"));
        viewMenu->addAction(tb);
    }
    if (m_sessionManagerDock) {
        QAction *sm = m_sessionManagerDock->toggleViewAction();
        sm->setText(tr("Session Manager"));
        viewMenu->addAction(sm);
    }
    if (m_quickCommandsDock) {
        QAction *qc = m_quickCommandsDock->toggleViewAction();
        qc->setText(tr("Quick Commands"));
        viewMenu->addAction(qc);
    }
    QAction *statusAct = viewMenu->addAction(tr("Status Bar"));
    statusAct->setCheckable(true);
    statusAct->setChecked(true);
    connect(statusAct, &QAction::toggled, this,
            [this](bool on) { statusBar()->setVisible(on); });
    viewMenu->addSeparator();
    m_hexViewAct = viewMenu->addAction(tr("Hex View"));
    m_hexViewAct->setCheckable(true);
    connect(m_hexViewAct, &QAction::toggled, this,
            &MainWindow::setHexViewForCurrent);
    viewMenu->addSeparator();
    m_fullScreenAct = viewMenu->addAction(tr("Full Screen"));
    m_fullScreenAct->setCheckable(true);
    m_fullScreenAct->setShortcut(QKeySequence(Qt::Key_F11));
    connect(m_fullScreenAct, &QAction::toggled, this,
            &MainWindow::toggleFullScreen);
    addAction(m_fullScreenAct); // keep the F11 shortcut live with the menu hidden

    // --- Options ---
    QMenu *optionsMenu = appMenu->addMenu(tr("&Options"));
    placeholder(optionsMenu, tr("Session Options..."));
    placeholder(optionsMenu, tr("Global Options..."));
    placeholder(optionsMenu, tr("Edit Default Session..."));
    optionsMenu->addSeparator();
    QAction *appearanceAct = optionsMenu->addAction(tr("Terminal Appearance..."));
    connect(appearanceAct, &QAction::triggered, this,
            &MainWindow::openTerminalAppearance);

    // SFTP browser style: Windows-11 Explorer (default) vs classic dual-pane.
    QMenu *sftpStyleMenu = optionsMenu->addMenu(tr("SFTP Browser Style"));
    auto *styleGroup = new QActionGroup(this);
    QAction *explorerAct = sftpStyleMenu->addAction(tr("Explorer (Windows 11)"));
    QAction *dualAct = sftpStyleMenu->addAction(tr("Traditional (dual-pane)"));
    for (QAction *a : {explorerAct, dualAct}) {
        a->setCheckable(true);
        styleGroup->addAction(a);
    }
    {
        QSettings settings(QStringLiteral("TermSync"), QStringLiteral("TermSync"));
        const bool traditional =
            settings.value(QStringLiteral("sftp/style")).toString() ==
            QStringLiteral("traditional");
        (traditional ? dualAct : explorerAct)->setChecked(true);
    }
    auto setStyle = [this](const QString &style) {
        QSettings settings(QStringLiteral("TermSync"), QStringLiteral("TermSync"));
        settings.setValue(QStringLiteral("sftp/style"), style);
        statusBar()->showMessage(
            tr("SFTP browser style applies to new SFTP sessions"), 4000);
    };
    connect(explorerAct, &QAction::triggered, this,
            [setStyle] { setStyle(QStringLiteral("explorer")); });
    connect(dualAct, &QAction::triggered, this,
            [setStyle] { setStyle(QStringLiteral("traditional")); });

    // --- Transfer ---
    QMenu *transferMenu = appMenu->addMenu(tr("&Transfer"));
    placeholder(transferMenu, tr("Send ASCII..."));
    placeholder(transferMenu, tr("Receive ASCII..."));
    placeholder(transferMenu, tr("Send Binary..."));
    transferMenu->addSeparator();
    placeholder(transferMenu, tr("Start Zmodem Upload..."));

    // --- Script ---
    QMenu *scriptMenu = appMenu->addMenu(tr("&Script"));
    QAction *runScriptAct = scriptMenu->addAction(tr("Run..."));
    connect(runScriptAct, &QAction::triggered, this, &MainWindow::runScript);
    placeholder(scriptMenu, tr("Start Recording Script"));
    placeholder(scriptMenu, tr("Stop Recording Script"));

    // --- Tools ---
    QMenu *toolsMenu = appMenu->addMenu(tr("T&ools"));
    placeholder(toolsMenu, tr("Create Public Key..."));
    placeholder(toolsMenu, tr("Public-Key Assistant..."));
    placeholder(toolsMenu, tr("Manage Agent Keys..."));
    placeholder(toolsMenu, tr("Keymap Editor..."));
    toolsMenu->addSeparator();
    QAction *importSettingsAct = toolsMenu->addAction(tr("Import Settings..."));
    connect(importSettingsAct, &QAction::triggered, this,
            &MainWindow::importSettings);
    QAction *exportSettingsAct = toolsMenu->addAction(tr("Export Settings..."));
    connect(exportSettingsAct, &QAction::triggered, this,
            &MainWindow::exportSettings);
    toolsMenu->addSeparator();
    QAction *tftpAct = toolsMenu->addAction(tr("TFTP Server..."));
    connect(tftpAct, &QAction::triggered, this, &MainWindow::openTftpServer);

    // --- Window ---
    QMenu *windowMenu = appMenu->addMenu(tr("&Window"));
    placeholder(windowMenu, tr("Cascade"));
    placeholder(windowMenu, tr("Tile Horizontally"));
    placeholder(windowMenu, tr("Tile Vertically"));

    // --- Help ---
    QMenu *helpMenu = appMenu->addMenu(tr("&Help"));
    placeholder(helpMenu, tr("Help Topics"));
    QAction *aboutAct = helpMenu->addAction(tr("About TermSync"));
    connect(aboutAct, &QAction::triggered, this, [this] {
        QMessageBox::about(
            this, tr("About TermSync"),
            tr("<h3>TermSync %1</h3>"
               "<p>Open-source SSH terminal + SFTP/FTP client.</p>"
               "<p>SSH2 terminal emulation, session management, a dual-pane "
               "file browser, and directory synchronization in one app.</p>"
               "<p>Built with Qt %2, libssh2, and libcurl. Licensed under the "
               "MIT License.</p>"
               "<p><i>Not affiliated with VanDyke Software.</i></p>")
                .arg(QCoreApplication::applicationVersion(),
                     QStringLiteral(QT_VERSION_STR)));
    });

    // Collapse everything into a single "≡" hamburger, hosted in the custom
    // title bar (created by createWindowControls). Hide the classic menu bar.
    m_hamburger = new QToolButton(this);
    m_hamburger->setText(QStringLiteral("≡"));
    m_hamburger->setToolTip(tr("Menu"));
    m_hamburger->setPopupMode(QToolButton::InstantPopup);
    m_hamburger->setAutoRaise(true);
    m_hamburger->setMenu(appMenu);
    m_hamburger->setStyleSheet(QStringLiteral(
        "QToolButton { font-size:16pt; color:#c8d0e8; padding:2px 12px;"
        " border:0; background:transparent; }"
        "QToolButton:hover { color:#2dd4bf; }"
        "QToolButton::menu-indicator { image:none; }"));
    menuBar()->hide();
}

void MainWindow::createToolBar()
{
    m_toolbar = addToolBar(tr("Main"));
    m_toolbar->setObjectName("mainToolBar");
    QAction *quickConnectAct = m_toolbar->addAction(tr("Quick Connect"));
    connect(quickConnectAct, &QAction::triggered, this,
            &MainWindow::openQuickConnect);
    QAction *sftpAct = m_toolbar->addAction(tr("SFTP"));
    connect(sftpAct, &QAction::triggered, this, &MainWindow::openQuickSftp);
    QAction *shellAct = m_toolbar->addAction(tr("Local Terminal"));
    connect(shellAct, &QAction::triggered, this, &MainWindow::openLocalShell);
    // Hidden by default — the Hosts home tab provides these actions. Toggle from
    // View > Toolbar.
    m_toolbar->hide();
}

void MainWindow::createSessionManagerDock()
{
    m_sessionManagerDock = new QDockWidget(tr("Session Manager"), this);
    m_sessionManagerDock->setObjectName("sessionManagerDock");
    m_sessionManagerDock->setAllowedAreas(Qt::LeftDockWidgetArea |
                                          Qt::RightDockWidgetArea);

    m_sessionTree = new QTreeWidget(m_sessionManagerDock);
    m_sessionTree->setHeaderLabel(tr("Sessions"));
    m_sessionTree->setColumnCount(1);
    m_sessionTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_sessionTree, &QTreeWidget::itemActivated, this,
            &MainWindow::onSessionActivated);
    connect(m_sessionTree, &QTreeWidget::itemDoubleClicked, this,
            &MainWindow::onSessionActivated);
    connect(m_sessionTree, &QWidget::customContextMenuRequested, this,
            &MainWindow::showSessionContextMenu);
    m_sessionManagerDock->setWidget(m_sessionTree);

    addDockWidget(Qt::LeftDockWidgetArea, m_sessionManagerDock);
    // Hidden by default — the Hosts home tab is the primary host list. Toggle
    // from View > Session Manager.
    m_sessionManagerDock->hide();
}

void MainWindow::createQuickCommandsDock()
{
    m_quickCommandsDock = new QDockWidget(tr("Quick Commands"), this);
    m_quickCommandsDock->setObjectName(QStringLiteral("quickCommandsDock"));
    m_quickCommandsDock->setAllowedAreas(Qt::LeftDockWidgetArea |
                                         Qt::RightDockWidgetArea);
    auto *panel = new QuickCommandsWidget(m_quickCommandsDock);
    connect(panel, &QuickCommandsWidget::runCommand, this,
            &MainWindow::runQuickCommand);
    m_quickCommandsDock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, m_quickCommandsDock);
}

void MainWindow::runQuickCommand(const QString &command, bool execute)
{
    auto *term = qobject_cast<TerminalWidget *>(m_sessionTabs->currentWidget());
    if (!term) {
        statusBar()->showMessage(
            tr("Open a terminal tab to run a quick command"), 4000);
        return;
    }
    QByteArray bytes = command.toUtf8();
    if (execute)
        bytes.append('\n');
    term->sendText(bytes);
    term->setFocus();
}

void MainWindow::createCentralArea()
{
    m_sessionTabs = new ChromeTabWidget(this);
    m_sessionTabs->setTabsClosable(true);
    m_sessionTabs->setMovable(true);
    m_sessionTabs->tabBar()->setExpanding(false);
    connect(m_sessionTabs, &QTabWidget::tabCloseRequested, this,
            [this](int index) {
                QWidget *w = m_sessionTabs->widget(index);
                m_sessionTabs->removeTab(index);
                w->deleteLater();
            });
    connect(m_sessionTabs, &QTabWidget::currentChanged, this,
            [this](int) { syncHexViewAction(); });

    // Central area: a hidden Find bar (Edit -> Find) stacked above the tabs.
    auto *central = new QWidget(this);
    auto *vbox = new QVBoxLayout(central);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);
    m_findBar = new FindBar(central);
    m_findBar->hide();
    vbox->addWidget(m_findBar);
    vbox->addWidget(m_sessionTabs, 1);
    setCentralWidget(central);

    connect(m_findBar, &FindBar::incrementalSearch, this,
            [this](const QString &needle, bool cs) {
                if (TerminalWidget *t = currentTerminal())
                    m_findBar->setNotFound(!t->find(needle, true, cs, true));
            });
    connect(m_findBar, &FindBar::searchRequested, this,
            [this](const QString &needle, bool forward, bool cs) {
                if (TerminalWidget *t = currentTerminal())
                    m_findBar->setNotFound(!t->find(needle, forward, cs));
            });
    connect(m_findBar, &FindBar::closed, this, [this] {
        m_findBar->hide();
        if (TerminalWidget *t = currentTerminal())
            t->setFocus();
    });
}

// The "≡" app menu lives in the tab strip's left corner. (A fully custom
// frameless title bar was attempted but Qt would not render widgets on the
// right side of this window's top bar; kept the native — now dark — title bar.)
void MainWindow::createWindowControls()
{
    if (m_hamburger)
        m_sessionTabs->setCornerWidget(m_hamburger, Qt::TopLeftCorner);
}

int MainWindow::titleBarHeight() const
{
    return m_titleBar && m_titleBar->height() > 8 ? m_titleBar->height() : 40;
}

void MainWindow::toggleMaximizeRestore()
{
    if (isMaximized())
        showNormal();
    else
        showMaximized();
}

void MainWindow::updateMaximizeIcon()
{
    if (m_maxButton)
        m_maxButton->setText(isMaximized() ? QStringLiteral("❐")
                                           : QStringLiteral("▢"));
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange)
        updateMaximizeIcon();
}

void MainWindow::createStatusBar()
{
    statusBar()->showMessage(tr("Ready"));
}

void MainWindow::openQuickConnect()
{
    QuickConnectDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    core::ConnectionProfile profile = dialog.toProfile();
    if (profile.host.isEmpty()) {
        statusBar()->showMessage(tr("Quick Connect: hostname is required"), 4000);
        return;
    }
    const QString password = dialog.password();

    // Persist the profile / password if requested.
    if (dialog.saveSession() && m_profileStore) {
        if (m_profileStore->upsert(profile)) {
            if (profile.savePassword && m_credentialStore)
                m_credentialStore->store(profile.id, password);
            loadProfilesIntoTree();
        }
    }

    // SSH2 opens a terminal; Telnet opens a (credential-less) terminal;
    // file-transfer protocols open the dual-pane browser.
    if (profile.protocol == core::Protocol::SSH2)
        startSession(profile, password);
    else if (profile.protocol == core::Protocol::TELNET)
        startTelnetSession(profile);
    else if (profile.protocol == core::Protocol::TN3270)
        startTn3270Session(profile);
    else if (profile.protocol == core::Protocol::TN5250)
        startTn5250Session(profile);
    else if (profile.protocol == core::Protocol::SERIAL)
        startSerialSession(profile);
    else
        startSftpSession(profile, password);
}

void MainWindow::openQuickSftp()
{
    QuickConnectDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    core::ConnectionProfile profile = dialog.toProfile();
    if (profile.host.isEmpty()) {
        statusBar()->showMessage(tr("SFTP: hostname is required"), 4000);
        return;
    }
    const QString password = dialog.password();

    if (dialog.saveSession() && m_profileStore) {
        if (m_profileStore->upsert(profile)) {
            if (profile.savePassword && m_credentialStore)
                m_credentialStore->store(profile.id, password);
            loadProfilesIntoTree();
        }
    }

    startSftpSession(profile, password);
}

void MainWindow::initStores()
{
    m_credentialStore = core::CredentialStore::createDefault();

    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    const QString dbPath = dir + QStringLiteral("/profiles.db");

    m_profileStore = std::make_unique<core::ProfileStore>();
    if (!m_profileStore->open(dbPath)) {
        statusBar()->showMessage(
            tr("Could not open session store: %1").arg(m_profileStore->lastError()),
            6000);
        m_profileStore.reset();
    }
}

void MainWindow::loadProfilesIntoTree()
{
    if (!m_sessionTree)
        return;
    m_sessionTree->clear();
    if (!m_profileStore)
        return;

    m_profiles = m_profileStore->allProfiles();

    // Build folder nodes on demand, then attach profile leaves.
    QHash<QString, QTreeWidgetItem *> folders;
    auto folderItem = [&](const QString &path) -> QTreeWidgetItem * {
        if (path.isEmpty())
            return nullptr;
        if (folders.contains(path))
            return folders.value(path);
        auto *item = new QTreeWidgetItem(QStringList{path.section('/', -1)});
        m_sessionTree->addTopLevelItem(item);
        folders.insert(path, item);
        return item;
    };

    for (const core::ConnectionProfile &p : m_profiles) {
        auto *leaf = new QTreeWidgetItem(QStringList{p.name});
        leaf->setData(0, kProfileIdRole, p.id);
        leaf->setToolTip(0, QStringLiteral("%1@%2:%3")
                                .arg(p.username, p.host)
                                .arg(p.port));
        if (QTreeWidgetItem *parent = folderItem(p.folderPath))
            parent->addChild(leaf);
        else
            m_sessionTree->addTopLevelItem(leaf);
    }
    m_sessionTree->expandAll();
    if (m_home)
        m_home->setProfiles(m_profiles);
}

void MainWindow::onSessionActivated(QTreeWidgetItem *item, int)
{
    if (!item)
        return;
    const QString id = item->data(0, kProfileIdRole).toString();
    if (id.isEmpty())
        return; // a folder node
    for (const core::ConnectionProfile &p : m_profiles) {
        if (p.id == id) {
            // SSH2 defaults to a terminal; file-transfer protocols open the
            // dual-pane browser.
            if (p.protocol == core::Protocol::SSH2)
                connectProfile(p);
            else if (p.protocol == core::Protocol::TELNET)
                startTelnetSession(p);
            else if (p.protocol == core::Protocol::TN3270)
                startTn3270Session(p);
            else if (p.protocol == core::Protocol::TN5250)
                startTn5250Session(p);
            else if (p.protocol == core::Protocol::SERIAL)
                startSerialSession(p);
            else
                connectProfileSftp(p);
            return;
        }
    }
}

void MainWindow::showSessionContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_sessionTree->itemAt(pos);
    if (!item)
        return;
    const QString id = item->data(0, kProfileIdRole).toString();
    if (id.isEmpty())
        return;

    QMenu menu(this);
    QAction *connectAct = menu.addAction(tr("Connect"));
    QAction *connectSftpAct = menu.addAction(tr("Connect SFTP Session"));
    QAction *deleteAct = menu.addAction(tr("Delete"));
    QAction *chosen = menu.exec(m_sessionTree->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == connectAct) {
        onSessionActivated(item, 0);
    } else if (chosen == connectSftpAct) {
        for (const core::ConnectionProfile &p : m_profiles) {
            if (p.id == id) {
                connectProfileSftp(p);
                break;
            }
        }
    } else if (chosen == deleteAct) {
        if (m_profileStore) {
            m_profileStore->remove(id);
            if (m_credentialStore)
                m_credentialStore->remove(id);
            loadProfilesIntoTree();
        }
    }
}

void MainWindow::connectProfile(const core::ConnectionProfile &profile)
{
    QString password;
    if (profile.savePassword && m_credentialStore)
        password = m_credentialStore->retrieve(profile.id);

    if (password.isEmpty()) {
        bool ok = false;
        password = QInputDialog::getText(
            this, tr("Password"),
            tr("Password for %1@%2:").arg(profile.username, profile.host),
            QLineEdit::Password, QString(), &ok);
        if (!ok)
            return;
    }
    startSession(profile, password);
}

void MainWindow::connectProfileSftp(const core::ConnectionProfile &profile)
{
    QString password;
    if (profile.savePassword && m_credentialStore)
        password = m_credentialStore->retrieve(profile.id);

    if (password.isEmpty()) {
        bool ok = false;
        password = QInputDialog::getText(
            this, tr("Password"),
            tr("Password for %1@%2:").arg(profile.username, profile.host),
            QLineEdit::Password, QString(), &ok);
        if (!ok)
            return;
    }
    startSftpSession(profile, password);
}

void MainWindow::startSession(const core::ConnectionProfile &profile,
                              const QString &password)
{
    core::SshConnectionParams params;
    params.host = profile.host;
    params.port = profile.port;
    params.username = profile.username;
    params.cols = profile.cols;
    params.rows = profile.rows;
    // core::AuthMethod and core::SshAuthMethod share ordering.
    params.authMethod = static_cast<core::SshAuthMethod>(profile.authMethod);
    params.privateKeyPath = profile.privateKeyPath;
    params.x11Forwarding = profile.x11Forwarding;
    if (profile.authMethod == core::AuthMethod::PublicKey)
        params.passphrase = password;
    else
        params.password = password;

    auto *view = new TerminalWidget(params, this);

    // Trust-on-first-use: verify the host key against the known-hosts store.
    const QString host = profile.host;
    const quint16 port = profile.port;
    view->setHostKeyVerifier(
        [this, host, port](const QString &fp,
                           std::function<void(bool)> respond) {
            respond(verifyHostKey(host, port, fp));
        });

    connect(view, &TerminalWidget::statusMessage, this,
            [this](const QString &msg) { statusBar()->showMessage(msg, 4000); });

    view->setRespawnHandler(
        [this, profile, password] { startSession(profile, password); });

    const QString baseTitle = profile.name.isEmpty() ? profile.host : profile.name;
    view->setLogContext(profile.host, baseTitle);
    applyAppearance(view);
    const int index = m_sessionTabs->addTab(view, baseTitle);
    m_sessionTabs->setCurrentIndex(index);
    connect(view, &TerminalWidget::titleChanged, this,
            [this, view](const QString &t) {
                const int i = m_sessionTabs->indexOf(view);
                if (i >= 0 && !t.isEmpty())
                    m_sessionTabs->setTabText(i, t);
            });
    view->setFocus();
    statusBar()->showMessage(tr("Connecting to %1...").arg(profile.host), 4000);
}

void MainWindow::startSftpSession(const core::ConnectionProfile &profile,
                                  const QString &password)
{
    core::SshConnectionParams params;
    params.host = profile.host;
    params.port = profile.port;
    params.username = profile.username;
    params.authMethod = static_cast<core::SshAuthMethod>(profile.authMethod);
    params.privateKeyPath = profile.privateKeyPath;
    if (profile.authMethod == core::AuthMethod::PublicKey)
        params.passphrase = password;
    else
        params.password = password;

    const QString host = profile.host;
    const quint16 port = profile.port;
    const QString expectedFp =
        m_profileStore ? m_profileStore->knownFingerprint(host, port) : QString();

    auto onFingerprint = [this, host, port](const QString &fp) {
        verifyHostKey(host, port, fp);
    };
    auto onStatus = [this](const QString &msg) {
        statusBar()->showMessage(msg, 5000);
    };

    // Explorer (Windows 11 File Explorer) style by default; "traditional" gives
    // the classic dual-pane browser. Chosen via Options > SFTP Browser Style.
    QSettings settings(QStringLiteral("TermSync"), QStringLiteral("TermSync"));
    const bool traditional =
        settings.value(QStringLiteral("sftp/style")).toString() ==
        QStringLiteral("traditional");

    QWidget *view = nullptr;
    if (traditional) {
        auto *v = new DualPaneBrowser(params, expectedFp, profile.protocol, this);
        connect(v, &DualPaneBrowser::hostKeyFingerprintReceived, this, onFingerprint);
        connect(v, &DualPaneBrowser::statusMessage, this, onStatus);
        view = v;
    } else {
        auto *v = new ExplorerSftpBrowser(params, expectedFp, profile.protocol, this);
        connect(v, &ExplorerSftpBrowser::hostKeyFingerprintReceived, this, onFingerprint);
        connect(v, &ExplorerSftpBrowser::statusMessage, this, onStatus);
        connect(v, &ExplorerSftpBrowser::osDetected, this,
                [this, id = profile.id](const QString &os) { rememberHostOs(id, os); });
        view = v;
    }

    const QString baseTitle = profile.name.isEmpty() ? profile.host : profile.name;
    const int index = m_sessionTabs->addTab(view, tr("%1 Files").arg(baseTitle));
    m_sessionTabs->setCurrentIndex(index);
    statusBar()->showMessage(tr("Opening SFTP for %1...").arg(profile.host), 4000);
}

void MainWindow::rememberHostOs(const QString &profileId, const QString &osId)
{
    if (profileId.isEmpty() || osId.isEmpty())
        return;
    QSettings settings(QStringLiteral("TermSync"), QStringLiteral("TermSync"));
    const QString key = QStringLiteral("hostos/") + profileId;
    if (settings.value(key).toString() == osId)
        return; // unchanged — no need to rebuild the cards
    settings.setValue(key, osId);
    if (m_home)
        m_home->setProfiles(m_profiles); // rebuild cards so the icon updates
}

void MainWindow::startTelnetSession(const core::ConnectionProfile &profile)
{
    auto *conn = new core::TelnetConnection;
    auto *view = new TerminalWidget(conn, this); // takes ownership of conn
    connect(view, &TerminalWidget::statusMessage, this,
            [this](const QString &msg) { statusBar()->showMessage(msg, 4000); });
    view->setRespawnHandler(
        [this, profile] { startTelnetSession(profile); });

    const QString baseTitle = profile.name.isEmpty() ? profile.host : profile.name;
    const int index = m_sessionTabs->addTab(view, baseTitle);
    m_sessionTabs->setCurrentIndex(index);
    connect(view, &TerminalWidget::titleChanged, this,
            [this, view](const QString &t) {
                const int i = m_sessionTabs->indexOf(view);
                if (i >= 0 && !t.isEmpty())
                    m_sessionTabs->setTabText(i, t);
            });
    view->setFocus();

    view->setLogContext(profile.host, baseTitle);
    applyAppearance(view);
    conn->connectToHost(profile.host, profile.port ? profile.port : 23);
    statusBar()->showMessage(tr("Connecting (Telnet) to %1...").arg(profile.host), 4000);
}

void MainWindow::startTn3270Session(const core::ConnectionProfile &profile)
{
    auto *conn = new core::Tn3270Connection;
    auto *view = new TerminalWidget(conn, this);
    connect(view, &TerminalWidget::statusMessage, this,
            [this](const QString &msg) { statusBar()->showMessage(msg, 4000); });
    view->setRespawnHandler(
        [this, profile] { startTn3270Session(profile); });

    const QString baseTitle = profile.name.isEmpty() ? profile.host : profile.name;
    const int index = m_sessionTabs->addTab(view, baseTitle);
    m_sessionTabs->setCurrentIndex(index);
    view->setFocus();

    view->setLogContext(profile.host, baseTitle);
    applyAppearance(view);
    conn->connectToHost(profile.host, profile.port ? profile.port : 23);
    statusBar()->showMessage(tr("Connecting (TN3270) to %1...").arg(profile.host), 4000);
}

void MainWindow::startTn5250Session(const core::ConnectionProfile &profile)
{
    auto *conn = new core::Tn5250Connection;
    auto *view = new TerminalWidget(conn, this);
    connect(view, &TerminalWidget::statusMessage, this,
            [this](const QString &msg) { statusBar()->showMessage(msg, 4000); });
    view->setRespawnHandler(
        [this, profile] { startTn5250Session(profile); });

    const QString baseTitle = profile.name.isEmpty() ? profile.host : profile.name;
    const int index = m_sessionTabs->addTab(view, baseTitle);
    m_sessionTabs->setCurrentIndex(index);
    view->setFocus();

    view->setLogContext(profile.host, baseTitle);
    applyAppearance(view);
    conn->connectToHost(profile.host, profile.port ? profile.port : 23);
    statusBar()->showMessage(tr("Connecting (TN5250) to %1...").arg(profile.host), 4000);
}

void MainWindow::runScript()
{
    auto *terminal = qobject_cast<TerminalWidget *>(m_sessionTabs->currentWidget());
    if (!terminal) {
        statusBar()->showMessage(tr("Run Script: open a terminal tab first"), 4000);
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Run Script"), QString(), tr("Scripts (*.js);;All files (*)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        statusBar()->showMessage(tr("Could not open script: %1").arg(path), 4000);
        return;
    }
    const QString source = QString::fromUtf8(file.readAll());

    TerminalScriptContext context(terminal);
    script::ScriptEngine engine(&context);
    const auto result = engine.run(source);
    if (result.ok)
        statusBar()->showMessage(tr("Script finished"), 4000);
    else
        statusBar()->showMessage(tr("Script error: %1").arg(result.error), 8000);
}

void MainWindow::setHexViewForCurrent(bool on)
{
    auto *terminal =
        qobject_cast<TerminalWidget *>(m_sessionTabs->currentWidget());
    if (!terminal) {
        if (on) // only complain when the user tried to turn it on
            statusBar()->showMessage(
                tr("Hex View: open a terminal tab first"), 4000);
        return;
    }
    terminal->setHexView(on);
    statusBar()->showMessage(
        on ? tr("Hex View on") : tr("Hex View off"), 3000);
}

void MainWindow::syncHexViewAction()
{
    if (!m_hexViewAct)
        return;
    auto *terminal =
        qobject_cast<TerminalWidget *>(m_sessionTabs->currentWidget());
    QSignalBlocker block(m_hexViewAct); // don't re-toggle the terminal
    m_hexViewAct->setChecked(terminal && terminal->isHexView());
    m_hexViewAct->setEnabled(terminal != nullptr);
}

TerminalWidget *MainWindow::currentTerminal() const
{
    return qobject_cast<TerminalWidget *>(m_sessionTabs->currentWidget());
}

void MainWindow::updateEditActions()
{
    TerminalWidget *t = currentTerminal();
    const bool hasTerminal = t != nullptr;
    if (m_pasteAct)
        m_pasteAct->setEnabled(hasTerminal);
    if (m_selectAllAct)
        m_selectAllAct->setEnabled(hasTerminal);
    if (m_clearScreenAct)
        m_clearScreenAct->setEnabled(hasTerminal);
    if (m_clearScrollbackAct)
        m_clearScrollbackAct->setEnabled(hasTerminal);
    if (m_copyAct) // Copy needs an actual selection
        m_copyAct->setEnabled(hasTerminal && t->hasSelection());
    if (m_findAct)
        m_findAct->setEnabled(hasTerminal);

    if (m_reconnectAct)
        m_reconnectAct->setEnabled(hasTerminal && t->canRespawn());
    if (m_cloneAct)
        m_cloneAct->setEnabled(hasTerminal && t->canRespawn());
    if (m_disconnectAct)
        m_disconnectAct->setEnabled(hasTerminal && t->isConnected());
}

void MainWindow::toggleFullScreen(bool on)
{
    if (on)
        showFullScreen();
    else
        showNormal();
}

void MainWindow::showFindBar()
{
    if (!m_findBar)
        return;
    if (!currentTerminal()) {
        statusBar()->showMessage(tr("Find: open a terminal tab first"), 4000);
        return;
    }
    m_findBar->activate();
}

void MainWindow::reconnectCurrentSession()
{
    TerminalWidget *t = currentTerminal();
    if (!t || !t->canRespawn())
        return;
    const int index = m_sessionTabs->indexOf(t);
    // Reopen an identical session (respawn adds a fresh tab), then drop the
    // old one so Reconnect reads as an in-place replacement.
    t->respawn();
    if (index >= 0) {
        m_sessionTabs->removeTab(index);
        t->deleteLater();
    }
}

void MainWindow::disconnectCurrentSession()
{
    if (TerminalWidget *t = currentTerminal())
        t->disconnectSession();
}

void MainWindow::cloneCurrentSession()
{
    TerminalWidget *t = currentTerminal();
    if (t && t->canRespawn())
        t->respawn(); // opens a second identical session tab
}

void MainWindow::editKeywordHighlighting()
{
    auto *terminal =
        qobject_cast<TerminalWidget *>(m_sessionTabs->currentWidget());
    if (!terminal) {
        statusBar()->showMessage(
            tr("Keyword Highlighting: open a terminal tab first"), 4000);
        return;
    }
    KeywordHighlightDialog dialog(terminal->highlightRules(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const auto rules = dialog.rules();
    terminal->setHighlightRules(rules);
    statusBar()->showMessage(
        tr("Applied %1 highlight rule(s)").arg(rules.size()), 4000);
}

void MainWindow::toggleSessionLog()
{
    auto *terminal =
        qobject_cast<TerminalWidget *>(m_sessionTabs->currentWidget());
    if (!terminal) {
        statusBar()->showMessage(
            tr("Log Session: open a terminal tab first"), 4000);
        return;
    }

    if (terminal->isLogging()) {
        const QString path = terminal->logPath();
        terminal->stopLogging();
        statusBar()->showMessage(
            tr("Stopped logging to %1").arg(QDir::toNativeSeparators(path)), 5000);
        return;
    }

    // Default name uses TermSync log tokens, expanded by the terminal
    // (%S session, %Y/%M/%D date) so a fresh, dated file is created.
    const QString suggested =
        QDir::home().filePath(QStringLiteral("%S-%Y%M%D-%h%m%s.log"));
    QString path = QFileDialog::getSaveFileName(
        this, tr("Log Session"), suggested,
        tr("Log files (*.log *.txt);;All files (*)"));
    if (path.isEmpty())
        return;

    if (!terminal->startLogging(path))
        return; // startLogging already reported the failure via statusMessage
}

void MainWindow::exportSettings()
{
    if (!m_profileStore) {
        statusBar()->showMessage(tr("Export Settings: session store unavailable"),
                                 4000);
        return;
    }
    QString path = QFileDialog::getSaveFileName(
        this, tr("Export Settings"),
        QDir::home().filePath(QStringLiteral("termsync-settings.json")),
        tr("TermSync settings (*.json);;All files (*)"));
    if (path.isEmpty())
        return;
    if (!path.contains('.'))
        path += QStringLiteral(".json");

    if (core::exportProfilesToFile(*m_profileStore, path)) {
        statusBar()->showMessage(
            tr("Exported %1 session(s) to %2")
                .arg(m_profiles.size())
                .arg(QDir::toNativeSeparators(path)),
            6000);
    } else {
        QMessageBox::warning(this, tr("Export Settings"),
                             tr("Could not write settings to:\n%1").arg(path));
    }
}

void MainWindow::importSettings()
{
    if (!m_profileStore) {
        statusBar()->showMessage(tr("Import Settings: session store unavailable"),
                                 4000);
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Settings"), QDir::homePath(),
        tr("TermSync settings (*.json);;All files (*)"));
    if (path.isEmpty())
        return;

    const int count = core::importProfilesFromFile(*m_profileStore, path);
    if (count < 0) {
        QMessageBox::warning(
            this, tr("Import Settings"),
            tr("Could not read or parse settings from:\n%1").arg(path));
        return;
    }
    loadProfilesIntoTree();
    statusBar()->showMessage(tr("Imported %1 session(s)").arg(count), 6000);
}

void MainWindow::startSerialSession(const core::ConnectionProfile &profile)
{
    core::SerialParams sp;
    sp.portName = profile.host;                 // host field holds the port name
    sp.baudRate = profile.port ? profile.port : 115200; // port field = baud rate

    auto *conn = new core::SerialConnection;
    auto *view = new TerminalWidget(conn, this);
    connect(view, &TerminalWidget::statusMessage, this,
            [this](const QString &msg) { statusBar()->showMessage(msg, 4000); });
    view->setRespawnHandler(
        [this, profile] { startSerialSession(profile); });

    const QString baseTitle = profile.name.isEmpty() ? sp.portName : profile.name;
    view->setLogContext(sp.portName, baseTitle);
    applyAppearance(view);
    const int index = m_sessionTabs->addTab(view, baseTitle);
    m_sessionTabs->setCurrentIndex(index);
    view->setFocus();

    conn->open(sp);
    statusBar()->showMessage(
        tr("Opening serial %1 @ %2 baud...").arg(sp.portName).arg(sp.baudRate), 4000);
}

void MainWindow::loadAppearance()
{
    QSettings settings(QStringLiteral("TermSync"), QStringLiteral("TermSync"));
    m_terminalScheme =
        settings.value(QStringLiteral("terminal/scheme"),
                       terminal::defaultSchemeName())
            .toString();
    const QString family =
        settings.value(QStringLiteral("terminal/fontFamily")).toString();
    const int pt = settings.value(QStringLiteral("terminal/fontSize"), -1).toInt();
    if (!family.isEmpty() && pt > 0) {
        m_terminalFont = QFont(family);
        m_terminalFont.setStyleHint(QFont::Monospace);
        m_terminalFont.setFixedPitch(true);
        m_terminalFont.setPointSize(pt);
    }
}

void MainWindow::applyAppearance(TerminalWidget *terminal) const
{
    if (!terminal)
        return;
    if (const terminal::ColorScheme *s = terminal::findScheme(m_terminalScheme))
        terminal->applyColorScheme(*s);
    if (m_terminalFont.pointSize() > 0)
        terminal->setTerminalFont(m_terminalFont);
}

void MainWindow::openTerminalAppearance()
{
    auto *current =
        qobject_cast<TerminalWidget *>(m_sessionTabs->currentWidget());
    const QString scheme = current ? current->colorSchemeName() : m_terminalScheme;
    const QFont font = current                    ? current->terminalFont()
                       : m_terminalFont.pointSize() > 0
                           ? m_terminalFont
                           : QFont(QStringLiteral("Cascadia Mono"), 11);

    TerminalAppearanceDialog dialog(scheme, font, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    m_terminalScheme = dialog.selectedScheme();
    m_terminalFont = dialog.selectedFont();

    QSettings settings(QStringLiteral("TermSync"), QStringLiteral("TermSync"));
    settings.setValue(QStringLiteral("terminal/scheme"), m_terminalScheme);
    settings.setValue(QStringLiteral("terminal/fontFamily"),
                      m_terminalFont.family());
    settings.setValue(QStringLiteral("terminal/fontSize"),
                      m_terminalFont.pointSize());

    // Apply to the current tab, or to every open terminal.
    if (dialog.applyToAll()) {
        for (int i = 0; i < m_sessionTabs->count(); ++i)
            applyAppearance(
                qobject_cast<TerminalWidget *>(m_sessionTabs->widget(i)));
    } else {
        applyAppearance(current);
    }
    statusBar()->showMessage(tr("Applied theme '%1'").arg(m_terminalScheme), 4000);
}

void MainWindow::openTftpServer()
{
    // Non-modal, self-owned (WA_DeleteOnClose); the server stops when closed.
    auto *dialog = new TftpServerDialog(this);
    dialog->show();
    dialog->raise();
}

void MainWindow::openLocalShell()
{
    auto *conn = new core::LocalShellConnection;
    auto *view = new TerminalWidget(conn, this); // takes ownership of conn
    connect(view, &TerminalWidget::statusMessage, this,
            [this](const QString &msg) { statusBar()->showMessage(msg, 4000); });
    view->setRespawnHandler([this] { openLocalShell(); });

    const QString title = tr("Local Shell");
    view->setLogContext(QStringLiteral("localhost"), title);
    applyAppearance(view);
    const int index = m_sessionTabs->addTab(view, title);
    m_sessionTabs->setCurrentIndex(index);
    connect(view, &TerminalWidget::titleChanged, this,
            [this, view](const QString &t) {
                const int i = m_sessionTabs->indexOf(view);
                if (i >= 0 && !t.isEmpty())
                    m_sessionTabs->setTabText(i, t);
            });
    view->setFocus();

    conn->start();
    statusBar()->showMessage(
        tr("Started local shell (%1)").arg(conn->shellProgram()), 4000);
}

bool MainWindow::verifyHostKey(const QString &host, quint16 port,
                               const QString &fingerprint)
{
    if (!m_profileStore)
        return true; // no store: accept (non-persistent)

    const QString known = m_profileStore->knownFingerprint(host, port);
    if (known.isEmpty()) {
        // First contact: trust and remember.
        m_profileStore->setKnownFingerprint(host, port, fingerprint);
        return true;
    }
    if (known == fingerprint)
        return true;

    // Mismatch — possible MITM. Ask the user explicitly.
    const auto answer = QMessageBox::warning(
        this, tr("Host Key Changed"),
        tr("The host key for %1:%2 has changed!\n\n"
           "Stored:   %3\nReceived: %4\n\n"
           "This could indicate a man-in-the-middle attack. Connect anyway?")
            .arg(host)
            .arg(port)
            .arg(known, fingerprint),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes) {
        m_profileStore->setKnownFingerprint(host, port, fingerprint);
        return true;
    }
    return false;
}

void MainWindow::addHomeTab()
{
    m_home = new HostsHomeWidget(this);
    connect(m_home, &HostsHomeWidget::newHostRequested, this,
            &MainWindow::openQuickConnect);
    connect(m_home, &HostsHomeWidget::localShellRequested, this,
            &MainWindow::openLocalShell);
    connect(m_home, &HostsHomeWidget::quickConnectRequested, this,
            &MainWindow::quickConnectFromText);
    connect(m_home, &HostsHomeWidget::hostActivated, this,
            [this](const QString &id) { connectById(id, /*sftp=*/false); });
    connect(m_home, &HostsHomeWidget::hostSftpRequested, this,
            [this](const QString &id) { connectById(id, /*sftp=*/true); });
    connect(m_home, &HostsHomeWidget::hostEditRequested, this,
            &MainWindow::editHost);
    connect(m_home, &HostsHomeWidget::hostDeleteRequested, this,
            &MainWindow::deleteHost);

    const int index = m_sessionTabs->addTab(m_home, tr("Hosts"));
    // The home tab is permanent — remove its close button.
    if (auto *bar = m_sessionTabs->tabBar())
        bar->setTabButton(index, QTabBar::RightSide, nullptr);
}

void MainWindow::connectById(const QString &id, bool sftp)
{
    for (const core::ConnectionProfile &p : m_profiles) {
        if (p.id != id)
            continue;
        if (sftp || p.protocol == core::Protocol::SFTP_ONLY ||
            p.protocol == core::Protocol::FTP ||
            p.protocol == core::Protocol::FTPS)
            connectProfileSftp(p);
        else if (p.protocol == core::Protocol::TELNET)
            startTelnetSession(p);
        else if (p.protocol == core::Protocol::TN3270)
            startTn3270Session(p);
        else if (p.protocol == core::Protocol::TN5250)
            startTn5250Session(p);
        else if (p.protocol == core::Protocol::SERIAL)
            startSerialSession(p);
        else
            connectProfile(p);
        return;
    }
}

void MainWindow::editHost(const QString &id)
{
    if (!m_profileStore)
        return;
    for (const core::ConnectionProfile &existing : m_profiles) {
        if (existing.id != id)
            continue;
        QuickConnectDialog dialog(this);
        dialog.loadProfile(existing);
        if (dialog.exec() != QDialog::Accepted)
            return;
        core::ConnectionProfile updated = dialog.toProfile(); // keeps the id
        if (updated.host.isEmpty())
            return;
        if (m_profileStore->upsert(updated)) {
            if (updated.savePassword && m_credentialStore)
                m_credentialStore->store(updated.id, dialog.password());
            loadProfilesIntoTree();
            statusBar()->showMessage(tr("Updated '%1'").arg(updated.name), 4000);
        }
        return;
    }
}

void MainWindow::deleteHost(const QString &id)
{
    if (!m_profileStore)
        return;
    QString name = id;
    for (const core::ConnectionProfile &p : m_profiles)
        if (p.id == id)
            name = p.name;
    if (QMessageBox::question(
            this, tr("Delete Host"),
            tr("Delete the saved host “%1”?").arg(name)) != QMessageBox::Yes)
        return;
    m_profileStore->remove(id);
    if (m_credentialStore)
        m_credentialStore->remove(id);
    loadProfilesIntoTree();
    statusBar()->showMessage(tr("Deleted '%1'").arg(name), 4000);
}

void MainWindow::quickConnectFromText(const QString &text)
{
    // Accept "ssh user@host", "user@host[:port]" or a bare hostname; anything
    // matching a saved host name connects that host instead.
    for (const core::ConnectionProfile &p : m_profiles) {
        if (p.name.compare(text, Qt::CaseInsensitive) == 0) {
            connectById(p.id, false);
            return;
        }
    }

    QString s = text;
    if (s.startsWith(QStringLiteral("ssh "), Qt::CaseInsensitive))
        s = s.mid(4).trimmed();

    core::ConnectionProfile p;
    p.protocol = core::Protocol::SSH2;
    p.authMethod = core::AuthMethod::Password;
    QString hostPart = s;
    if (const int at = s.indexOf('@'); at >= 0) {
        p.username = s.left(at);
        hostPart = s.mid(at + 1);
    }
    if (const int colon = hostPart.indexOf(':'); colon >= 0) {
        p.host = hostPart.left(colon);
        p.port = static_cast<quint16>(hostPart.mid(colon + 1).toUShort());
    } else {
        p.host = hostPart;
    }
    if (p.port == 0)
        p.port = 22;
    if (p.host.isEmpty()) {
        statusBar()->showMessage(tr("Enter a host, e.g. user@example.com"), 4000);
        return;
    }
    p.name = p.username.isEmpty() ? p.host : (p.username + '@' + p.host);
    p.id = core::ProfileStore::newId();

    QString password;
    if (!p.username.isEmpty()) {
        bool ok = false;
        password = QInputDialog::getText(
            this, tr("Password"),
            tr("Password for %1@%2:").arg(p.username, p.host),
            QLineEdit::Password, QString(), &ok);
        if (!ok)
            return;
    }
    startSession(p, password);
}

} // namespace termsync::ui
