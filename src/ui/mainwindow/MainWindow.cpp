#include "mainwindow/MainWindow.h"

#include <QAction>
#include <QDockWidget>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "session_dialogs/QuickConnectDialog.h"
#include "terminal_view/RawTerminalView.h"

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
        statusBar()->showMessage(
            tr("TermSync 0.1.0 — M1 scaffold. See docs/ui-parity.md for the roadmap."),
            5000);
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
    // Placeholder tree — populated from the ProfileStore starting in M4.
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

    const core::SshConnectionParams params = dialog.params();
    if (params.host.isEmpty()) {
        statusBar()->showMessage(tr("Quick Connect: hostname is required"), 4000);
        return;
    }

    auto *view = new RawTerminalView(params, this);
    connect(view, &RawTerminalView::statusMessage, this,
            [this](const QString &msg) { statusBar()->showMessage(msg, 4000); });

    const QString title = params.username.isEmpty()
                              ? params.host
                              : params.username + '@' + params.host;
    const int index = m_sessionTabs->addTab(view, title);
    m_sessionTabs->setCurrentIndex(index);
    view->setFocus();
    statusBar()->showMessage(tr("Connecting to %1...").arg(params.host), 4000);
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
