#include "mainwindow/MainWindow.h"

#include <QAction>
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
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include "ScriptEngine.h"
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
#include "terminal_view/TerminalWidget.h"
#include "transfer_view/DualPaneBrowser.h"
#include "transfer_view/SftpBrowserWidget.h"

namespace {
// Role used to stash a profile id on a session-tree leaf item.
constexpr int kProfileIdRole = Qt::UserRole + 1;
} // namespace

namespace termsync::ui {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("TermSync"));
    resize(1100, 720);

    createCentralArea();
    createMenus();
    createToolBar();
    createSessionManagerDock();
    createStatusBar();

    loadAppearance();
    initStores();
    loadProfilesIntoTree();
    addWelcomeTab();
}

MainWindow::~MainWindow() = default;

void MainWindow::createMenus()
{
    // SecureCRT-style top-level menu structure. Actions are placeholders
    // (disabled) in M1; milestones wire them up. See docs/ui-parity.md.
    auto placeholder = [this](QMenu *menu, const QString &text) {
        QAction *act = menu->addAction(text);
        act->setEnabled(false);
        return act;
    };

    // --- File ---
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
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
    placeholder(fileMenu, tr("Reconnect"));
    placeholder(fileMenu, tr("Disconnect"));
    placeholder(fileMenu, tr("Clone Session"));
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
    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    placeholder(editMenu, tr("Copy"));
    placeholder(editMenu, tr("Paste"));
    placeholder(editMenu, tr("Select All"));
    editMenu->addSeparator();
    placeholder(editMenu, tr("Clear Screen"));
    placeholder(editMenu, tr("Clear Scrollback"));
    placeholder(editMenu, tr("Find..."));
    editMenu->addSeparator();
    QAction *highlightAct = editMenu->addAction(tr("Keyword Highlighting..."));
    connect(highlightAct, &QAction::triggered, this,
            &MainWindow::editKeywordHighlighting);

    // --- View ---
    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    placeholder(viewMenu, tr("Toolbar"));
    placeholder(viewMenu, tr("Command Window"));
    placeholder(viewMenu, tr("Button Bar"));
    placeholder(viewMenu, tr("Status Bar"));
    viewMenu->addSeparator();
    m_hexViewAct = viewMenu->addAction(tr("Hex View"));
    m_hexViewAct->setCheckable(true);
    connect(m_hexViewAct, &QAction::toggled, this,
            &MainWindow::setHexViewForCurrent);
    viewMenu->addSeparator();
    placeholder(viewMenu, tr("Full Screen"));

    // --- Options ---
    QMenu *optionsMenu = menuBar()->addMenu(tr("&Options"));
    placeholder(optionsMenu, tr("Session Options..."));
    placeholder(optionsMenu, tr("Global Options..."));
    placeholder(optionsMenu, tr("Edit Default Session..."));
    optionsMenu->addSeparator();
    QAction *appearanceAct = optionsMenu->addAction(tr("Terminal Appearance..."));
    connect(appearanceAct, &QAction::triggered, this,
            &MainWindow::openTerminalAppearance);

    // --- Transfer ---
    QMenu *transferMenu = menuBar()->addMenu(tr("&Transfer"));
    placeholder(transferMenu, tr("Send ASCII..."));
    placeholder(transferMenu, tr("Receive ASCII..."));
    placeholder(transferMenu, tr("Send Binary..."));
    transferMenu->addSeparator();
    placeholder(transferMenu, tr("Start Zmodem Upload..."));

    // --- Script ---
    QMenu *scriptMenu = menuBar()->addMenu(tr("&Script"));
    QAction *runScriptAct = scriptMenu->addAction(tr("Run..."));
    connect(runScriptAct, &QAction::triggered, this, &MainWindow::runScript);
    placeholder(scriptMenu, tr("Start Recording Script"));
    placeholder(scriptMenu, tr("Stop Recording Script"));

    // --- Tools ---
    QMenu *toolsMenu = menuBar()->addMenu(tr("T&ools"));
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
    QMenu *windowMenu = menuBar()->addMenu(tr("&Window"));
    placeholder(windowMenu, tr("Cascade"));
    placeholder(windowMenu, tr("Tile Horizontally"));
    placeholder(windowMenu, tr("Tile Vertically"));

    // --- Help ---
    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
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
}

void MainWindow::createToolBar()
{
    QToolBar *toolbar = addToolBar(tr("Main"));
    toolbar->setObjectName("mainToolBar");
    // Toolbar mirroring the SecureCRT layout. Quick Connect is wired up;
    // the rest are placeholders until their milestones.
    QAction *connectAct = toolbar->addAction(tr("Connect"));
    connectAct->setEnabled(false);
    QAction *quickConnectAct = toolbar->addAction(tr("Quick Connect"));
    connect(quickConnectAct, &QAction::triggered, this,
            &MainWindow::openQuickConnect);
    QAction *sftpAct = toolbar->addAction(tr("SFTP"));
    connect(sftpAct, &QAction::triggered, this, &MainWindow::openQuickSftp);
    for (const QString &name : {tr("Disconnect"), tr("Session Manager")}) {
        QAction *act = toolbar->addAction(name);
        act->setEnabled(false);
    }
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
}

void MainWindow::createCentralArea()
{
    m_sessionTabs = new QTabWidget(this);
    m_sessionTabs->setTabsClosable(true);
    m_sessionTabs->setMovable(true);
    connect(m_sessionTabs, &QTabWidget::tabCloseRequested, this,
            [this](int index) {
                QWidget *w = m_sessionTabs->widget(index);
                m_sessionTabs->removeTab(index);
                w->deleteLater();
            });
    connect(m_sessionTabs, &QTabWidget::currentChanged, this,
            [this](int) { syncHexViewAction(); });
    setCentralWidget(m_sessionTabs);
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

    auto *view = new DualPaneBrowser(params, expectedFp, profile.protocol, this);

    // Trust-on-first-use: persist a newly-seen fingerprint; warn on mismatch.
    connect(view, &DualPaneBrowser::hostKeyFingerprintReceived, this,
            [this, host, port](const QString &fp) { verifyHostKey(host, port, fp); });
    connect(view, &DualPaneBrowser::statusMessage, this,
            [this](const QString &msg) { statusBar()->showMessage(msg, 5000); });

    const QString baseTitle = profile.name.isEmpty() ? profile.host : profile.name;
    const int index = m_sessionTabs->addTab(view, tr("%1 Files").arg(baseTitle));
    m_sessionTabs->setCurrentIndex(index);
    statusBar()->showMessage(tr("Opening SFTP for %1...").arg(profile.host), 4000);
}

void MainWindow::startTelnetSession(const core::ConnectionProfile &profile)
{
    auto *conn = new core::TelnetConnection;
    auto *view = new TerminalWidget(conn, this); // takes ownership of conn
    connect(view, &TerminalWidget::statusMessage, this,
            [this](const QString &msg) { statusBar()->showMessage(msg, 4000); });

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

    // Default name uses SecureCRT-style tokens, expanded by the terminal
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

void MainWindow::addWelcomeTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *label = new QLabel(
        tr("<h2>TermSync</h2>"
           "<p>Open-source SSH terminal + SFTP/FTP client.</p>"
           "<p>This is the <b>M1 scaffold</b>: the window shell is in place, "
           "but connecting is not implemented yet.</p>"
           "<p>Next milestone (M2) adds SSH2 connect and raw shell passthrough.</p>"),
        page);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    layout->addWidget(label);
    m_sessionTabs->addTab(page, tr("Welcome"));
}

} // namespace termsync::ui
