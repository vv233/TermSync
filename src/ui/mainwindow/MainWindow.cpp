#include "mainwindow/MainWindow.h"

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QHash>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include "session_dialogs/QuickConnectDialog.h"
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
    fileMenu->addSeparator();
    placeholder(fileMenu, tr("Reconnect"));
    placeholder(fileMenu, tr("Disconnect"));
    placeholder(fileMenu, tr("Clone Session"));
    fileMenu->addSeparator();
    placeholder(fileMenu, tr("Log Session..."));
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

    // --- View ---
    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    placeholder(viewMenu, tr("Toolbar"));
    placeholder(viewMenu, tr("Command Window"));
    placeholder(viewMenu, tr("Button Bar"));
    placeholder(viewMenu, tr("Status Bar"));
    viewMenu->addSeparator();
    placeholder(viewMenu, tr("Full Screen"));

    // --- Options ---
    QMenu *optionsMenu = menuBar()->addMenu(tr("&Options"));
    placeholder(optionsMenu, tr("Session Options..."));
    placeholder(optionsMenu, tr("Global Options..."));
    placeholder(optionsMenu, tr("Edit Default Session..."));

    // --- Transfer ---
    QMenu *transferMenu = menuBar()->addMenu(tr("&Transfer"));
    placeholder(transferMenu, tr("Send ASCII..."));
    placeholder(transferMenu, tr("Receive ASCII..."));
    placeholder(transferMenu, tr("Send Binary..."));
    transferMenu->addSeparator();
    placeholder(transferMenu, tr("Start Zmodem Upload..."));

    // --- Script ---
    QMenu *scriptMenu = menuBar()->addMenu(tr("&Script"));
    placeholder(scriptMenu, tr("Run..."));
    placeholder(scriptMenu, tr("Start Recording Script"));
    placeholder(scriptMenu, tr("Stop Recording Script"));

    // --- Tools ---
    QMenu *toolsMenu = menuBar()->addMenu(tr("T&ools"));
    placeholder(toolsMenu, tr("Create Public Key..."));
    placeholder(toolsMenu, tr("Public-Key Assistant..."));
    placeholder(toolsMenu, tr("Manage Agent Keys..."));
    placeholder(toolsMenu, tr("Keymap Editor..."));

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

    // SSH2 opens a terminal; file-transfer protocols open the dual-pane browser.
    if (profile.protocol == core::Protocol::SSH2)
        startSession(profile, password);
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
