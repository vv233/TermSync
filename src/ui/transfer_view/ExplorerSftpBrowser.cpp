#include "transfer_view/ExplorerSftpBrowser.h"

#include "common/Icons.h"
#include "transfer_view/WinDragOut.h"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QListView>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyle>
#include <QTableWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace termsync::ui {

using transfer::SftpEntry;
using transfer::TransferItem;

namespace {

QString humanSize(quint64 bytes)
{
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
    return u == 0 ? QString::number(bytes) + " B"
                  : QString::number(v, 'f', 1) + ' ' + units[u];
}

// A human "type" label, Explorer-style ("Folder", "TXT File", "File folder").
QString typeLabel(const SftpEntry &e)
{
    if (e.isDirectory)
        return e.isSymlink ? QObject::tr("Folder (link)") : QObject::tr("File folder");
    const int dot = e.name.lastIndexOf('.');
    if (dot > 0 && dot < e.name.size() - 1)
        return e.name.mid(dot + 1).toUpper() + QObject::tr(" File");
    return QObject::tr("File");
}

QFileIconProvider &iconProvider()
{
    static QFileIconProvider p;
    return p;
}

// A details table that hands drag-out (to Windows Explorer) back to the browser,
// which downloads the files to temp before starting the OS drag.
class DragTable : public QTableWidget
{
public:
    using QTableWidget::QTableWidget;
    std::function<void()> onStartDrag;

protected:
    void startDrag(Qt::DropActions) override
    {
        if (onStartDrag)
            onStartDrag();
    }
};

// Same drag-out hook for the icon/list view.
class DragList : public QListWidget
{
public:
    using QListWidget::QListWidget;
    std::function<void()> onStartDrag;

protected:
    void startDrag(Qt::DropActions) override
    {
        if (onStartDrag)
            onStartDrag();
    }
};

// The left navigation pane: accepts a folder dragged from the file view and
// pins it as a bookmark. Acceptance is driven by an in-progress internal drag
// (canAccept), so it never touches / triggers the OS download payload.
class NavList : public QListWidget
{
public:
    using QListWidget::QListWidget;
    std::function<bool()> canAccept;
    std::function<void()> onDropPaths;

protected:
    void dragEnterEvent(QDragEnterEvent *e) override
    {
        if (canAccept && canAccept())
            e->acceptProposedAction();
    }
    void dragMoveEvent(QDragMoveEvent *e) override
    {
        if (canAccept && canAccept())
            e->acceptProposedAction();
    }
    void dropEvent(QDropEvent *e) override
    {
        if (canAccept && canAccept()) {
            e->acceptProposedAction();
            if (onDropPaths)
                onDropPaths();
        }
    }
};

} // namespace

ExplorerSftpBrowser::ExplorerSftpBrowser(const core::SshConnectionParams &params,
                                         const QString &expectedFingerprint,
                                         core::Protocol protocol, QWidget *parent)
    : QWidget(parent)
{
    m_bookmarkHost = params.username.isEmpty()
                         ? params.host
                         : params.username + QLatin1Char('@') + params.host;
    m_session = new transfer::SftpSession(params, expectedFingerprint, protocol, this);
    connect(m_session, &transfer::SftpSession::connected, this,
            &ExplorerSftpBrowser::onConnected);
    connect(m_session, &transfer::SftpSession::osDetected, this,
            &ExplorerSftpBrowser::osDetected);
    connect(m_session, &transfer::SftpSession::hostKeyFingerprint, this,
            &ExplorerSftpBrowser::hostKeyFingerprintReceived);
    connect(m_session, &transfer::SftpSession::connectionFailed, this,
            [this](const QString &r) {
                emit statusMessage(tr("SFTP connection failed: %1").arg(r));
            });
    connect(m_session, &transfer::SftpSession::directoryListed, this,
            &ExplorerSftpBrowser::onDirectoryListed);
    connect(m_session, &transfer::SftpSession::operationFinished, this,
            &ExplorerSftpBrowser::onOperationFinished);
    connect(m_session, &transfer::SftpSession::transferQueued, this,
            &ExplorerSftpBrowser::onTransferQueued);
    connect(m_session, &transfer::SftpSession::transferProgress, this,
            &ExplorerSftpBrowser::onTransferProgress);
    connect(m_session, &transfer::SftpSession::transferFinished, this,
            &ExplorerSftpBrowser::onTransferFinished);
    connect(m_session, &transfer::SftpSession::syncListingReady, this,
            &ExplorerSftpBrowser::onSyncListingReady);
    connect(m_session, &transfer::SftpSession::sudoModeChanged, this,
            &ExplorerSftpBrowser::onSudoModeChanged);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(buildNavBar());
    outer->addWidget(buildCommandBar());

    auto *split = new QSplitter(Qt::Horizontal, this);
    split->addWidget(buildNavPane());
    split->addWidget(buildFileView());
    split->setStretchFactor(1, 1);
    split->setSizes({190, 760});
    outer->addWidget(split, 1);

    outer->addWidget(buildStatusStrip());

    setStyleSheet(QStringLiteral(
        "QToolButton { border:0; border-radius:6px; padding:5px 8px; color:#e6e9f2; }"
        "QToolButton:hover { background:#2a2c3a; }"
        "QToolButton:disabled { color:#5a6078; }"
        "#crumbBtn { padding:4px 6px; color:#c8d0e8; }"
        "#crumbBtn:hover { background:#2a2c3a; }"
        "#crumbSep { color:#5a6078; padding:0 1px; }"
        "#explorerBar { background:#16171f; border-bottom:1px solid #2a2c3a; }"));

    // Accept local files dropped onto the browser (upload). The table is
    // DragOnly (starts remote-out drags; does not accept drops), so drops fall
    // through to this widget.
    setAcceptDrops(true);

    auto shortcut = [this](QKeySequence::StandardKey k, void (ExplorerSftpBrowser::*fn)()) {
        auto *s = new QShortcut(k, this);
        s->setContext(Qt::WidgetWithChildrenShortcut);
        connect(s, &QShortcut::activated, this, fn);
    };
    shortcut(QKeySequence::Copy, &ExplorerSftpBrowser::copySelectionToClipboard);
    shortcut(QKeySequence::Paste, &ExplorerSftpBrowser::pasteFromClipboard);
    shortcut(QKeySequence::Delete, &ExplorerSftpBrowser::deleteSelected);
    shortcut(QKeySequence::Refresh, &ExplorerSftpBrowser::refresh);

    m_table->horizontalHeader()->setSortIndicatorShown(true);
    m_table->horizontalHeader()->setSortIndicator(m_sortColumn, m_sortOrder);

    m_session->connectToHost();
    updateCommandState();
}

// ---------------------------------------------------------------------------
// Navigation bar (back/forward/up + breadcrumb + search)
// ---------------------------------------------------------------------------
QWidget *ExplorerSftpBrowser::buildNavBar()
{
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("explorerBar"));
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(8, 6, 8, 6);
    row->setSpacing(4);

    auto navButton = [&](Glyph glyph, const QString &tip) {
        auto *b = new QToolButton(bar);
        b->setIcon(lineIcon(glyph));
        b->setIconSize(QSize(18, 18));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        return b;
    };
    m_back = navButton(Glyph::Back, tr("Back"));
    m_forward = navButton(Glyph::Forward, tr("Forward"));
    m_up = navButton(Glyph::Up, tr("Up"));
    auto *refreshBtn = navButton(Glyph::Refresh, tr("Refresh"));
    connect(m_back, &QToolButton::clicked, this, &ExplorerSftpBrowser::goBack);
    connect(m_forward, &QToolButton::clicked, this, &ExplorerSftpBrowser::goForward);
    connect(m_up, &QToolButton::clicked, this, &ExplorerSftpBrowser::goUp);
    connect(refreshBtn, &QToolButton::clicked, this, &ExplorerSftpBrowser::refresh);
    row->addWidget(m_back);
    row->addWidget(m_forward);
    row->addWidget(m_up);
    row->addWidget(refreshBtn);

    // Breadcrumb / editable address, switched via a stacked widget (click the
    // empty area to edit, Win11-style).
    m_crumbStack = new QStackedWidget(bar);
    m_crumbStack->setMinimumHeight(30);

    m_crumbBar = new QWidget(m_crumbStack);
    m_crumbBar->setStyleSheet(QStringLiteral(
        "background:#14151d; border:1px solid #2a2c3a; border-radius:6px;"));
    m_crumbLayout = new QHBoxLayout(m_crumbBar);
    m_crumbLayout->setContentsMargins(6, 0, 6, 0);
    m_crumbLayout->setSpacing(0);

    m_addressEdit = new QLineEdit(m_crumbStack);
    m_addressEdit->setPlaceholderText(tr("Enter a remote path"));
    connect(m_addressEdit, &QLineEdit::returnPressed, this, [this] {
        const QString p = m_addressEdit->text().trimmed();
        m_crumbStack->setCurrentWidget(m_crumbBar);
        if (!p.isEmpty())
            navigateTo(p);
    });
    m_crumbStack->addWidget(m_crumbBar);
    m_crumbStack->addWidget(m_addressEdit);
    row->addWidget(m_crumbStack, 1);

    m_search = new QLineEdit(bar);
    m_search->setPlaceholderText(tr("Search"));
    m_search->setClearButtonEnabled(true);
    m_search->setMaximumWidth(220);
    connect(m_search, &QLineEdit::textChanged, this, [this] { populate(); });
    row->addWidget(m_search);
    return bar;
}

// ---------------------------------------------------------------------------
// Command bar (New folder / Upload / Download / Rename / Delete / View)
// ---------------------------------------------------------------------------
QWidget *ExplorerSftpBrowser::buildCommandBar()
{
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("explorerBar"));
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(8, 4, 8, 4);
    row->setSpacing(2);

    auto cmd = [&](Glyph glyph, const QString &text) {
        auto *b = new QToolButton(bar);
        b->setIcon(lineIcon(glyph));
        b->setIconSize(QSize(18, 18));
        b->setText(text);
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setAutoRaise(true);
        return b;
    };
    auto *newFolderBtn = cmd(Glyph::NewFolder, tr("New folder"));
    auto *uploadBtn = cmd(Glyph::Upload, tr("Upload"));
    m_downloadBtn = cmd(Glyph::Download, tr("Download"));
    m_renameBtn = cmd(Glyph::Rename, tr("Rename"));
    m_deleteBtn = cmd(Glyph::Trash, tr("Delete"));
    connect(newFolderBtn, &QToolButton::clicked, this, &ExplorerSftpBrowser::newFolder);
    connect(uploadBtn, &QToolButton::clicked, this, &ExplorerSftpBrowser::uploadFiles);
    connect(m_downloadBtn, &QToolButton::clicked, this, &ExplorerSftpBrowser::downloadSelected);
    connect(m_renameBtn, &QToolButton::clicked, this, &ExplorerSftpBrowser::renameSelected);
    connect(m_deleteBtn, &QToolButton::clicked, this, &ExplorerSftpBrowser::deleteSelected);
    row->addWidget(newFolderBtn);
    row->addWidget(uploadBtn);
    row->addWidget(m_downloadBtn);
    auto *sep = new QLabel(QStringLiteral("|"), bar);
    sep->setStyleSheet(QStringLiteral("color:#2a2c3a;"));
    row->addWidget(sep);
    row->addWidget(m_renameBtn);
    row->addWidget(m_deleteBtn);

    auto *sudoSep = new QLabel(QStringLiteral("|"), bar);
    sudoSep->setStyleSheet(QStringLiteral("color:#2a2c3a;"));
    row->addWidget(sudoSep);
    m_sudoBtn = new QToolButton(bar);
    m_sudoBtn->setText(tr("Sudo"));
    m_sudoBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_sudoBtn->setCheckable(true);
    m_sudoBtn->setAutoRaise(true);
    m_sudoBtn->setToolTip(tr("Run operations as root (sudo) to access protected files"));
    m_sudoBtn->setStyleSheet(QStringLiteral(
        "QToolButton { padding:5px 10px; border-radius:6px; color:#e6e9f2; }"
        "QToolButton:hover { background:#2a2c3a; }"
        "QToolButton:checked { background:#e0af68; color:#101218; font-weight:600; }"));
    connect(m_sudoBtn, &QToolButton::toggled, this, &ExplorerSftpBrowser::toggleSudo);
    row->addWidget(m_sudoBtn);

    row->addStretch();

    // --- Sort menu (排序) ---
    auto *sortBtn = new QToolButton(bar);
    sortBtn->setIcon(lineIcon(Glyph::Sort));
    sortBtn->setIconSize(QSize(18, 18));
    sortBtn->setText(tr("Sort"));
    sortBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    sortBtn->setPopupMode(QToolButton::InstantPopup);
    sortBtn->setAutoRaise(true);
    auto *sortMenu = new QMenu(sortBtn);
    auto *colGroup = new QActionGroup(this);
    const char *cols[] = {"Name", "Date modified", "Type", "Size"};
    for (int i = 0; i < 4; ++i) {
        QAction *a = sortMenu->addAction(tr(cols[i]));
        a->setCheckable(true);
        a->setChecked(i == m_sortColumn);
        colGroup->addAction(a);
        connect(a, &QAction::triggered, this,
                [this, i] { setSort(i, m_sortOrder); });
    }
    sortMenu->addSeparator();
    auto *ordGroup = new QActionGroup(this);
    QAction *ascAct = sortMenu->addAction(tr("Ascending"));
    QAction *descAct = sortMenu->addAction(tr("Descending"));
    for (QAction *a : {ascAct, descAct}) {
        a->setCheckable(true);
        ordGroup->addAction(a);
    }
    ascAct->setChecked(true);
    connect(ascAct, &QAction::triggered, this,
            [this] { setSort(m_sortColumn, Qt::AscendingOrder); });
    connect(descAct, &QAction::triggered, this,
            [this] { setSort(m_sortColumn, Qt::DescendingOrder); });
    sortBtn->setMenu(sortMenu);
    row->addWidget(sortBtn);

    // --- View menu (查看) ---
    auto *viewBtn = new QToolButton(bar);
    viewBtn->setIcon(lineIcon(Glyph::Grid));
    viewBtn->setIconSize(QSize(18, 18));
    viewBtn->setText(tr("View"));
    viewBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    viewBtn->setPopupMode(QToolButton::InstantPopup);
    viewBtn->setAutoRaise(true);
    auto *viewMenu = new QMenu(viewBtn);
    auto *viewGroup = new QActionGroup(this);
    struct { const char *label; int mode; } modes[] = {
        {"Extra large icons", ExtraLargeIcons}, {"Large icons", LargeIcons},
        {"Medium icons", MediumIcons}, {"Small icons", SmallIcons},
        {"List", ListMode}, {"Details", Details}};
    for (const auto &m : modes) {
        QAction *a = viewMenu->addAction(tr(m.label));
        a->setCheckable(true);
        a->setChecked(m.mode == m_viewMode);
        viewGroup->addAction(a);
        const int mode = m.mode;
        connect(a, &QAction::triggered, this, [this, mode] { setViewMode(mode); });
    }
    viewBtn->setMenu(viewMenu);
    row->addWidget(viewBtn);
    return bar;
}

// ---------------------------------------------------------------------------
// Left navigation pane (quick locations)
// ---------------------------------------------------------------------------
QWidget *ExplorerSftpBrowser::buildNavPane()
{
    auto *nav = new NavList(this);
    nav->canAccept = [this] { return !m_dragPaths.isEmpty(); };
    nav->onDropPaths = [this] {
        for (const QString &p : m_dragPaths)
            addBookmark(p);
    };
    nav->setAcceptDrops(true);
    m_navList = nav;
    m_navList->setFrameShape(QFrame::NoFrame);
    m_navList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_navList->setStyleSheet(QStringLiteral(
        "QListWidget { background:#1a1b26; border-right:1px solid #2a2c3a; }"
        "QListWidget::item { padding:7px 10px; border-radius:6px; margin:1px 4px; }"
        "QListWidget::item:hover { background:#23252f; }"
        "QListWidget::item:selected { background:#2dd4bf; color:#101218; }"));
    const QIcon home = lineIcon(Glyph::Home);
    const QIcon drive = lineIcon(Glyph::Drive);
    auto add = [&](const QIcon &ic, const QString &text, const QString &path) {
        auto *it = new QListWidgetItem(ic, text, m_navList);
        it->setData(Qt::UserRole, path);
    };
    add(home, tr("Home"), QStringLiteral("."));
    add(drive, tr("Root"), QStringLiteral("/"));
    loadBookmarks();
    connect(m_navList, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *it) { navigateTo(it->data(Qt::UserRole).toString()); });
    connect(m_navList, &QWidget::customContextMenuRequested, this,
            &ExplorerSftpBrowser::showNavContextMenu);
    return m_navList;
}

// Bookmark items carry the path (UserRole) + a "is-bookmark" flag (UserRole+1)
// so Home/Root are never treated as removable.
static constexpr int kBookmarkRole = Qt::UserRole + 1;

void ExplorerSftpBrowser::addBookmark(const QString &remotePath)
{
    if (remotePath.isEmpty() || remotePath == QStringLiteral(".") ||
        remotePath == QStringLiteral("/"))
        return;
    for (int i = 0; i < m_navList->count(); ++i)
        if (m_navList->item(i)->data(Qt::UserRole).toString() == remotePath)
            return; // already pinned
    const QString label = remotePath.section('/', -1, -1, QString::SectionSkipEmpty);
    auto *it = new QListWidgetItem(
        m_folderIcon.isNull() ? lineIcon(Glyph::Folder) : m_folderIcon,
        label.isEmpty() ? remotePath : label, m_navList);
    it->setData(Qt::UserRole, remotePath);
    it->setData(kBookmarkRole, true);
    it->setToolTip(remotePath);
    persistBookmarks();
    emit statusMessage(tr("Pinned %1").arg(remotePath));
}

void ExplorerSftpBrowser::loadBookmarks()
{
    QSettings settings(QStringLiteral("TermSync"), QStringLiteral("TermSync"));
    const QStringList paths =
        settings.value(QStringLiteral("sftp/bookmarks/") + m_bookmarkHost)
            .toStringList();
    for (const QString &p : paths) {
        const QString label = p.section('/', -1, -1, QString::SectionSkipEmpty);
        auto *it = new QListWidgetItem(lineIcon(Glyph::Folder),
                                       label.isEmpty() ? p : label, m_navList);
        it->setData(Qt::UserRole, p);
        it->setData(kBookmarkRole, true);
        it->setToolTip(p);
    }
}

void ExplorerSftpBrowser::persistBookmarks()
{
    QStringList paths;
    for (int i = 0; i < m_navList->count(); ++i)
        if (m_navList->item(i)->data(kBookmarkRole).toBool())
            paths << m_navList->item(i)->data(Qt::UserRole).toString();
    QSettings settings(QStringLiteral("TermSync"), QStringLiteral("TermSync"));
    settings.setValue(QStringLiteral("sftp/bookmarks/") + m_bookmarkHost, paths);
}

void ExplorerSftpBrowser::showNavContextMenu(const QPoint &pos)
{
    QListWidgetItem *it = m_navList->itemAt(pos);
    if (!it || !it->data(kBookmarkRole).toBool())
        return; // only bookmarks have a menu (not Home/Root)
    QMenu menu(this);
    menu.addAction(tr("Open"), this,
                   [this, it] { navigateTo(it->data(Qt::UserRole).toString()); });
    menu.addAction(tr("Remove bookmark"), this, [this, it] {
        delete it;
        persistBookmarks();
    });
    menu.exec(m_navList->viewport()->mapToGlobal(pos));
}

void ExplorerSftpBrowser::pinSelectionToSidebar()
{
    for (const SftpEntry &e : selectedEntries())
        if (e.isDirectory)
            addBookmark(remoteJoin(m_path, e.name));
}

// ---------------------------------------------------------------------------
// Main details list
// ---------------------------------------------------------------------------
QWidget *ExplorerSftpBrowser::buildFileView()
{
    auto *table = new DragTable(0, 4, this);
    table->onStartDrag = [this] { startDragOut(); };
    // DragOnly: the table starts drags (out to Explorer) but doesn't accept
    // drops, so dropped local files fall through to the browser (upload).
    table->setDragEnabled(true);
    table->setDragDropMode(QAbstractItemView::DragOnly);
    m_table = table;
    m_table->setHorizontalHeaderLabels(
        {tr("Name"), tr("Date modified"), tr("Type"), tr("Size")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->verticalHeader()->hide();
    m_table->setShowGrid(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSortingEnabled(true);
    m_table->setIconSize(QSize(20, 20));
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->setStyleSheet(QStringLiteral(
        "QTableWidget { background:#1a1b26; border:0; }"
        "QTableWidget::item { padding:4px 6px; }"
        "QTableWidget::item:selected { background:#2dd4bf; color:#101218; }"
        "QHeaderView::section { background:#1a1b26; color:#8a92b2; border:0;"
        " border-bottom:1px solid #2a2c3a; padding:5px 6px; }"));
    connect(m_table, &QTableWidget::cellDoubleClicked, this,
            &ExplorerSftpBrowser::onItemActivated);
    connect(m_table, &QTableWidget::itemSelectionChanged, this,
            &ExplorerSftpBrowser::onSelectionChanged);
    connect(m_table, &QWidget::customContextMenuRequested, this,
            &ExplorerSftpBrowser::showContextMenu);
    // Header click drives the shared sort state (kept in sync across views).
    m_table->setSortingEnabled(false);
    m_table->horizontalHeader()->setSectionsClickable(true);
    connect(m_table->horizontalHeader(), &QHeaderView::sectionClicked, this,
            [this](int col) {
                const Qt::SortOrder order =
                    (col == m_sortColumn && m_sortOrder == Qt::AscendingOrder)
                        ? Qt::DescendingOrder
                        : Qt::AscendingOrder;
                setSort(col, order);
            });

    // Icon / list view (large/medium/small icons + list mode).
    auto *iconList = new DragList(this);
    iconList->onStartDrag = [this] { startDragOut(); };
    iconList->setDragEnabled(true);
    iconList->setDragDropMode(QAbstractItemView::DragOnly);
    m_iconView = iconList;
    m_iconView->setFrameShape(QFrame::NoFrame);
    m_iconView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_iconView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_iconView->setResizeMode(QListView::Adjust);
    m_iconView->setMovement(QListView::Static);
    m_iconView->setUniformItemSizes(true);
    m_iconView->setWordWrap(true);
    m_iconView->setStyleSheet(QStringLiteral(
        "QListWidget { background:#1a1b26; border:0; }"
        "QListWidget::item { color:#e6e9f2; border-radius:6px; padding:4px; }"
        "QListWidget::item:hover { background:#23252f; }"
        "QListWidget::item:selected { background:#2dd4bf; color:#101218; }"));
    connect(m_iconView, &QListWidget::itemDoubleClicked, this,
            &ExplorerSftpBrowser::onIconActivated);
    connect(m_iconView, &QListWidget::itemSelectionChanged, this,
            &ExplorerSftpBrowser::onSelectionChanged);
    connect(m_iconView, &QWidget::customContextMenuRequested, this,
            &ExplorerSftpBrowser::showContextMenu);

    m_viewStack = new QStackedWidget(this);
    m_viewStack->addWidget(m_table);    // index 0
    m_viewStack->addWidget(m_iconView); // index 1
    m_viewStack->setCurrentWidget(m_table);
    return m_viewStack;
}

QWidget *ExplorerSftpBrowser::buildStatusStrip()
{
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("explorerBar"));
    bar->setStyleSheet(QStringLiteral(
        "#explorerBar { background:#16171f; border-top:1px solid #2a2c3a; }"));
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(10, 4, 10, 4);
    m_countLabel = new QLabel(tr("Connecting…"), bar);
    m_countLabel->setStyleSheet(QStringLiteral("color:#8a92b2;"));
    row->addWidget(m_countLabel);
    row->addStretch();
    m_xferLabel = new QLabel(bar);
    m_xferLabel->setStyleSheet(QStringLiteral("color:#8a92b2;"));
    row->addWidget(m_xferLabel);
    m_xferBar = new QProgressBar(bar);
    m_xferBar->setFixedWidth(160);
    m_xferBar->setTextVisible(false);
    m_xferBar->setVisible(false);
    row->addWidget(m_xferBar);
    return bar;
}

// ---------------------------------------------------------------------------
// Session events
// ---------------------------------------------------------------------------
void ExplorerSftpBrowser::onConnected()
{
    emit statusMessage(tr("SFTP connected"));
    navigateTo(QStringLiteral("."));
}

void ExplorerSftpBrowser::onDirectoryListed(const QString &path,
                                            const QVector<SftpEntry> &entries)
{
    m_path = path;
    m_entries.clear();
    for (const SftpEntry &e : entries)
        if (e.name != QStringLiteral(".") && e.name != QStringLiteral(".."))
            m_entries.append(e);
    populate();
    rebuildBreadcrumb();
    updateCommandState();
}

void ExplorerSftpBrowser::populate()
{
    const QString filter = m_search ? m_search->text().trimmed() : QString();
    QVector<SftpEntry> rows;
    for (const SftpEntry &e : m_entries)
        if (filter.isEmpty() || e.name.contains(filter, Qt::CaseInsensitive))
            rows.append(e);

    // Folders always group first (like Explorer), then by the chosen column.
    std::sort(rows.begin(), rows.end(), [this](const SftpEntry &a, const SftpEntry &b) {
        if (a.isDirectory != b.isDirectory)
            return a.isDirectory;
        int cmp = 0;
        switch (m_sortColumn) {
        case 1:
            cmp = a.modifiedAt < b.modifiedAt ? -1 : (a.modifiedAt > b.modifiedAt ? 1 : 0);
            break;
        case 2:
            cmp = typeLabel(a).compare(typeLabel(b), Qt::CaseInsensitive);
            break;
        case 3:
            cmp = a.size < b.size ? -1 : (a.size > b.size ? 1 : 0);
            break;
        default:
            cmp = a.name.compare(b.name, Qt::CaseInsensitive);
            break;
        }
        if (cmp == 0)
            cmp = a.name.compare(b.name, Qt::CaseInsensitive);
        return m_sortOrder == Qt::AscendingOrder ? cmp < 0 : cmp > 0;
    });

    // Fill only the visible view (the other is refilled when switched to).
    if (m_viewMode == Details) {
        m_iconView->clear();
        m_table->setRowCount(rows.size());
        for (int r = 0; r < rows.size(); ++r) {
            const SftpEntry &e = rows[r];
            auto *name = new QTableWidgetItem(iconFor(e), e.name);
            name->setData(Qt::UserRole, e.name);
            name->setData(Qt::UserRole + 1, e.isDirectory);
            m_table->setItem(r, 0, name);
            m_table->setItem(r, 1, new QTableWidgetItem(
                                       e.modifiedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
            m_table->setItem(r, 2, new QTableWidgetItem(typeLabel(e)));
            auto *size = new QTableWidgetItem(e.isDirectory ? QString() : humanSize(e.size));
            size->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_table->setItem(r, 3, size);
        }
    } else {
        m_table->setRowCount(0);
        m_iconView->clear();
        for (const SftpEntry &e : rows) {
            auto *it = new QListWidgetItem(iconFor(e), e.name, m_iconView);
            it->setData(Qt::UserRole, e.name);
            it->setData(Qt::UserRole + 1, e.isDirectory);
            it->setTextAlignment(m_viewMode == ListMode
                                     ? Qt::AlignLeft
                                     : Qt::AlignHCenter | Qt::AlignTop);
        }
    }

    const int dirs = std::count_if(rows.begin(), rows.end(),
                                   [](const SftpEntry &e) { return e.isDirectory; });
    m_countLabel->setText(tr("%n item(s)", "", int(rows.size())) +
                          (dirs ? tr("  ·  %n folder(s)", "", dirs) : QString()));
}

void ExplorerSftpBrowser::setSort(int column, Qt::SortOrder order)
{
    m_sortColumn = column;
    m_sortOrder = order;
    if (m_table)
        m_table->horizontalHeader()->setSortIndicator(column, order);
    populate();
}

void ExplorerSftpBrowser::setViewMode(int mode)
{
    m_viewMode = mode;
    if (mode == Details) {
        m_viewStack->setCurrentWidget(m_table);
    } else if (mode == ListMode) {
        m_iconView->setViewMode(QListView::ListMode);
        m_iconView->setIconSize(QSize(20, 20));
        m_iconView->setGridSize(QSize());
        m_iconView->setFlow(QListView::TopToBottom);
        m_iconView->setWrapping(true);
        m_viewStack->setCurrentWidget(m_iconView);
    } else {
        const int sz = mode == ExtraLargeIcons ? 128
                       : mode == LargeIcons     ? 96
                       : mode == MediumIcons    ? 64
                                                : 48;
        m_iconView->setViewMode(QListView::IconMode);
        m_iconView->setIconSize(QSize(sz, sz));
        m_iconView->setGridSize(QSize(sz + 44, sz + 40));
        m_iconView->setFlow(QListView::LeftToRight);
        m_iconView->setWrapping(true);
        m_viewStack->setCurrentWidget(m_iconView);
    }
    populate(); // re-align item text for the new mode
    onSelectionChanged();
}

QIcon ExplorerSftpBrowser::iconFor(const SftpEntry &e) const
{
    // QFileIconProvider::icon() is a synchronous Windows shell lookup
    // (SHGetFileInfo) — expensive. Cache by file-type so a directory of N files
    // costs a handful of lookups, not N, keeping the UI thread responsive.
    if (e.isDirectory) {
        if (m_folderIcon.isNull())
            m_folderIcon = iconProvider().icon(QAbstractFileIconProvider::Folder);
        return m_folderIcon;
    }
    const int dot = e.name.lastIndexOf('.');
    const QString suffix = dot > 0 ? e.name.mid(dot + 1).toLower() : QString();
    const auto cached = m_iconCache.constFind(suffix);
    if (cached != m_iconCache.constEnd())
        return cached.value();
    QIcon ic = iconProvider().icon(
        QFileInfo(suffix.isEmpty() ? QStringLiteral("file")
                                   : QStringLiteral("file.") + suffix));
    if (ic.isNull())
        ic = iconProvider().icon(QAbstractFileIconProvider::File);
    m_iconCache.insert(suffix, ic);
    return ic;
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------
void ExplorerSftpBrowser::requestList(const QString &path)
{
    m_countLabel->setText(tr("Loading…"));
    m_session->listDirectory(path);
}

void ExplorerSftpBrowser::navigateTo(const QString &path, bool pushHistory)
{
    if (pushHistory) {
        // Drop any forward history, then append.
        while (m_history.size() > m_histPos + 1)
            m_history.removeLast();
        m_history.append(path);
        m_histPos = m_history.size() - 1;
    }
    requestList(path);
}

void ExplorerSftpBrowser::goBack()
{
    if (m_histPos > 0)
        requestList(m_history[--m_histPos]);
    updateCommandState();
}

void ExplorerSftpBrowser::goForward()
{
    if (m_histPos + 1 < m_history.size())
        requestList(m_history[++m_histPos]);
    updateCommandState();
}

void ExplorerSftpBrowser::goUp()
{
    const QString parent = parentOf(m_path);
    if (!parent.isEmpty() && parent != m_path)
        navigateTo(parent);
}

void ExplorerSftpBrowser::refresh()
{
    requestList(m_path);
}

void ExplorerSftpBrowser::toggleSudo(bool on)
{
    if (!on) {
        m_session->setSudo(false, QString());
        return;
    }
    bool ok = false;
    const QString pw = QInputDialog::getText(
        this, tr("Sudo password"),
        tr("Enter the sudo password for the remote user:"), QLineEdit::Password,
        QString(), &ok);
    if (!ok) {
        QSignalBlocker block(m_sudoBtn); // user cancelled — revert the toggle
        m_sudoBtn->setChecked(false);
        return;
    }
    emit statusMessage(tr("Authenticating sudo…"));
    m_session->setSudo(true, pw);
}

void ExplorerSftpBrowser::onSudoModeChanged(bool enabled, bool ok,
                                            const QString &message)
{
    {
        QSignalBlocker block(m_sudoBtn);
        m_sudoBtn->setChecked(enabled);
    }
    if (!ok) {
        emit statusMessage(tr("Sudo: %1").arg(message));
        return;
    }
    emit statusMessage(enabled ? tr("Sudo mode on — operating as root")
                               : tr("Sudo mode off"));
    refresh(); // re-list with the new privilege level
}

QString ExplorerSftpBrowser::parentOf(const QString &dir) const
{
    QString p = dir;
    if (p == QStringLiteral(".") || p.isEmpty())
        return QStringLiteral("..");
    if (p.endsWith('/') && p.size() > 1)
        p.chop(1);
    const int slash = p.lastIndexOf('/');
    if (slash > 0)
        return p.left(slash);
    if (slash == 0)
        return QStringLiteral("/");
    return QStringLiteral("..");
}

QString ExplorerSftpBrowser::remoteJoin(const QString &dir, const QString &name) const
{
    if (dir.isEmpty() || dir == QStringLiteral("."))
        return name;
    if (dir.endsWith('/'))
        return dir + name;
    return dir + '/' + name;
}

void ExplorerSftpBrowser::rebuildBreadcrumb()
{
    // Clear existing crumb widgets.
    QLayoutItem *item = nullptr;
    while ((item = m_crumbLayout->takeAt(0)) != nullptr) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    auto addCrumb = [this](const QString &label, const QString &path) {
        auto *b = new QToolButton(m_crumbBar);
        b->setObjectName(QStringLiteral("crumbBtn"));
        b->setText(label);
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, [this, path] { navigateTo(path); });
        m_crumbLayout->addWidget(b);
    };
    auto addSep = [this] {
        auto *s = new QLabel(QStringLiteral("›"), m_crumbBar);
        s->setObjectName(QStringLiteral("crumbSep"));
        m_crumbLayout->addWidget(s);
    };

    if (m_path == QStringLiteral(".") || m_path.isEmpty()) {
        addCrumb(tr("Home"), QStringLiteral("."));
    } else if (m_path.startsWith('/')) {
        addCrumb(tr("Root"), QStringLiteral("/"));
        QString accum;
        const QStringList parts = m_path.split('/', Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            accum += '/' + part;
            addSep();
            addCrumb(part, accum);
        }
    } else {
        // Relative path (from home).
        addCrumb(tr("Home"), QStringLiteral("."));
        QString accum;
        const QStringList parts = m_path.split('/', Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            accum = accum.isEmpty() ? part : accum + '/' + part;
            addSep();
            addCrumb(part, accum);
        }
    }
    m_crumbLayout->addStretch();
    m_addressEdit->setText(m_path);
}

void ExplorerSftpBrowser::updateCommandState()
{
    if (m_back)
        m_back->setEnabled(m_histPos > 0);
    if (m_forward)
        m_forward->setEnabled(m_histPos + 1 < m_history.size());
    onSelectionChanged();
}

// ---------------------------------------------------------------------------
// Selection + activation
// ---------------------------------------------------------------------------
QVector<SftpEntry> ExplorerSftpBrowser::selectedEntries() const
{
    QStringList names;
    if (m_viewMode == Details) {
        const auto rows = m_table->selectionModel()->selectedRows();
        for (const QModelIndex &idx : rows)
            names << m_table->item(idx.row(), 0)->data(Qt::UserRole).toString();
    } else {
        for (QListWidgetItem *it : m_iconView->selectedItems())
            names << it->data(Qt::UserRole).toString();
    }
    QVector<SftpEntry> out;
    for (const QString &name : names)
        for (const SftpEntry &e : m_entries)
            if (e.name == name) {
                out.append(e);
                break;
            }
    return out;
}

void ExplorerSftpBrowser::onSelectionChanged()
{
    const int n = selectedEntries().size();
    if (m_downloadBtn)
        m_downloadBtn->setEnabled(n >= 1); // files and folders are downloadable
    if (m_renameBtn)
        m_renameBtn->setEnabled(n == 1);
    if (m_deleteBtn)
        m_deleteBtn->setEnabled(n >= 1);
}

void ExplorerSftpBrowser::onItemActivated(int row, int)
{
    if (row < 0 || row >= m_table->rowCount())
        return;
    auto *nameItem = m_table->item(row, 0);
    const QString name = nameItem->data(Qt::UserRole).toString();
    const bool isDir = nameItem->data(Qt::UserRole + 1).toBool();
    if (isDir)
        navigateTo(remoteJoin(m_path, name));
    else
        downloadSelected();
}

void ExplorerSftpBrowser::onIconActivated(QListWidgetItem *item)
{
    if (!item)
        return;
    const QString name = item->data(Qt::UserRole).toString();
    const bool isDir = item->data(Qt::UserRole + 1).toBool();
    if (isDir)
        navigateTo(remoteJoin(m_path, name));
    else
        downloadSelected();
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------
void ExplorerSftpBrowser::newFolder()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New Folder"),
                                               tr("Folder name:"), QLineEdit::Normal,
                                               tr("New folder"), &ok);
    if (ok && !name.isEmpty())
        m_session->makeDirectory(remoteJoin(m_path, name));
}

void ExplorerSftpBrowser::uploadFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Upload to %1").arg(m_path));
    for (const QString &local : files) {
        TransferItem item;
        item.direction = TransferItem::Upload;
        item.localPath = local;
        item.displayName = QFileInfo(local).fileName();
        item.remotePath = remoteJoin(m_path, item.displayName);
        item.size = static_cast<quint64>(QFileInfo(local).size());
        m_session->enqueue(item);
    }
}

void ExplorerSftpBrowser::downloadSelected()
{
    const auto entries = selectedEntries();
    if (entries.isEmpty()) {
        emit statusMessage(tr("Select item(s) to download"));
        return;
    }
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Download to"),
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    if (dir.isEmpty())
        return;
    for (const SftpEntry &e : entries) {
        if (e.isDirectory) {
            // A whole folder streams down as one tar bundle (fast for many
            // small files), extracted into the chosen folder.
            m_session->enqueueBulkDownload(remoteJoin(m_path, e.name), dir, e.name);
            continue;
        }
        TransferItem item;
        item.direction = TransferItem::Download;
        item.displayName = e.name;
        item.remotePath = remoteJoin(m_path, e.name);
        item.localPath = QDir(dir).filePath(e.name);
        item.size = e.size;
        m_session->enqueue(item);
    }
}

void ExplorerSftpBrowser::renameSelected()
{
    const auto entries = selectedEntries();
    if (entries.size() != 1)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename"), tr("New name:"),
                                               QLineEdit::Normal, entries[0].name, &ok);
    if (ok && !name.isEmpty() && name != entries[0].name)
        m_session->renameEntry(remoteJoin(m_path, entries[0].name),
                               remoteJoin(m_path, name));
}

void ExplorerSftpBrowser::deleteSelected()
{
    const auto entries = selectedEntries();
    if (entries.isEmpty())
        return;
    const QString what = entries.size() == 1
                             ? tr("Delete “%1”?").arg(entries[0].name)
                             : tr("Delete %n selected item(s)?", "", int(entries.size()));
    if (QMessageBox::question(this, tr("Delete"), what) != QMessageBox::Yes)
        return;
    for (const SftpEntry &e : entries)
        m_session->removeEntry(remoteJoin(m_path, e.name), e.isDirectory);
}

// ---------------------------------------------------------------------------
// Drag-and-drop + clipboard (upload in / download out)
// ---------------------------------------------------------------------------
namespace {
bool hasLocalFiles(const QMimeData *m)
{
    if (!m || !m->hasUrls())
        return false;
    for (const QUrl &u : m->urls())
        if (u.isLocalFile())
            return true;
    return false;
}
} // namespace

void ExplorerSftpBrowser::dragEnterEvent(QDragEnterEvent *event)
{
    if (hasLocalFiles(event->mimeData()))
        event->acceptProposedAction();
}

void ExplorerSftpBrowser::dragMoveEvent(QDragMoveEvent *event)
{
    if (hasLocalFiles(event->mimeData()))
        event->acceptProposedAction();
}

void ExplorerSftpBrowser::dropEvent(QDropEvent *event)
{
    if (!hasLocalFiles(event->mimeData()))
        return;
    event->acceptProposedAction();
    uploadUrls(event->mimeData()->urls());
}

void ExplorerSftpBrowser::uploadUrls(const QList<QUrl> &urls)
{
    int n = 0;
    for (const QUrl &u : urls) {
        if (!u.isLocalFile())
            continue;
        uploadLocalEntry(u.toLocalFile(), m_path);
        ++n;
    }
    if (n)
        emit statusMessage(tr("Uploading %n item(s) to %1", "", n).arg(m_path));
}

void ExplorerSftpBrowser::uploadLocalEntry(const QString &localPath,
                                           const QString &remoteDir)
{
    const QFileInfo fi(localPath);
    if (fi.isDir()) {
        // Bundle the whole folder into one tar stream unpacked on the server —
        // one transfer instead of a mkdir + N per-file round-trips, and no
        // UI-thread directory walk (the worker tars it off-thread).
        m_session->enqueueBulkUpload(localPath, remoteDir, fi.fileName());
    } else if (fi.isFile()) {
        TransferItem item;
        item.direction = TransferItem::Upload;
        item.localPath = localPath;
        item.displayName = fi.fileName();
        item.remotePath = remoteJoin(remoteDir, fi.fileName());
        item.size = static_cast<quint64>(fi.size());
        m_session->enqueue(item);
    }
}

void ExplorerSftpBrowser::pasteFromClipboard()
{
    const QMimeData *m = QApplication::clipboard()->mimeData();
    if (hasLocalFiles(m))
        uploadUrls(m->urls());
    else
        emit statusMessage(tr("Clipboard has no files to upload"));
}

void ExplorerSftpBrowser::downloadSelectedToTemp(
    std::function<void(const QStringList &)> onReady, bool showProgress)
{
    const QVector<SftpEntry> entries = selectedEntries();
    if (entries.isEmpty()) {
        emit statusMessage(tr("Select item(s) first"));
        return;
    }
    // A unique temp folder so repeated copies don't clobber each other.
    const QString base =
        QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(QStringLiteral("termsync-clip-%1")
                          .arg(QRandomGenerator::global()->generate(), 8, 16,
                               QLatin1Char('0')));
    QDir().mkpath(base);

    m_tempBatch.clear();
    m_tempBulkDirs.clear();
    m_tempPaths.clear();
    m_tempBaseDir = base;
    m_tempPendingDirs = 0;
    m_tempBytes = 0;
    m_tempOnReady = std::move(onReady);

    for (const SftpEntry &e : entries) {
        const QString top = QDir(base).filePath(e.name);
        m_tempPaths.append(top); // the dragged/copied top-level item
        if (e.isDirectory) {
            // One tar bundle per folder, extracted into `base` as `base/name`.
            const QString remoteDir = remoteJoin(m_path, e.name);
            const int id = m_session->enqueueBulkDownload(remoteDir, base, e.name);
            m_tempBatch.insert(id);
            m_tempBulkDirs.insert(id, remoteDir);
        } else {
            TransferItem item;
            item.direction = TransferItem::Download;
            item.displayName = e.name;
            item.remotePath = remoteJoin(m_path, e.name);
            item.localPath = top;
            item.size = e.size;
            m_tempBatch.insert(m_session->enqueue(item));
        }
    }

    if (showProgress) {
        delete m_tempProgress;
        m_tempProgress = new QProgressDialog(
            tr("Preparing %n item(s)…", "", int(entries.size())), tr("Cancel"), 0,
            0, this);
        m_tempProgress->setWindowTitle(tr("Copying"));
        m_tempProgress->setWindowModality(Qt::NonModal);
        m_tempProgress->setMinimumDuration(400); // don't flash for quick copies
        connect(m_tempProgress, &QProgressDialog::canceled, this, [this] {
            const QList<int> ids = m_tempBatch.values();
            for (int id : ids)
                m_session->cancel(id);
        });
    }

    emit statusMessage(tr("Preparing %n item(s)…", "", int(entries.size())));
    maybeFinishTemp();
}

void ExplorerSftpBrowser::onSyncListingReady(const QString &root,
                                             const transfer::sync::Listing &listing,
                                             bool ok)
{
    if (m_tempPendingDirs <= 0)
        return; // not part of a pending copy/drag batch
    const QString dirName = root.section('/', -1, -1, QString::SectionSkipEmpty);
    const QString localDir = QDir(m_tempBaseDir).filePath(dirName);
    if (ok) {
        for (auto it = listing.constBegin(); it != listing.constEnd(); ++it) {
            const QString localPath = QDir(localDir).filePath(it.key());
            if (it.value().isDir) {
                QDir().mkpath(localPath);
            } else {
                QDir().mkpath(QFileInfo(localPath).absolutePath());
                TransferItem item;
                item.direction = TransferItem::Download;
                item.displayName = it.key();
                item.remotePath = remoteJoin(root, it.key());
                item.localPath = localPath;
                item.size = it.value().size;
                m_tempBatch.insert(m_session->enqueue(item));
            }
        }
    }
    --m_tempPendingDirs;
    maybeFinishTemp();
}

void ExplorerSftpBrowser::maybeFinishTemp()
{
    if (!m_tempOnReady || m_tempPendingDirs != 0 || !m_tempBatch.isEmpty())
        return;
    if (m_tempProgress) {
        m_tempProgress->close();
        m_tempProgress->deleteLater();
        m_tempProgress = nullptr;
    }
    auto cb = std::move(m_tempOnReady);
    m_tempOnReady = nullptr;
    const QStringList paths = m_tempPaths;
    m_tempPaths.clear();
    cb(paths);
}

void ExplorerSftpBrowser::copySelectionToClipboard()
{
    downloadSelectedToTemp(
        [this](const QStringList &paths) {
            if (paths.isEmpty())
                return; // cancelled
            QList<QUrl> urls;
            for (const QString &p : paths)
                urls.append(QUrl::fromLocalFile(p));
            auto *mime = new QMimeData;
            mime->setUrls(urls);
            QApplication::clipboard()->setMimeData(mime);
            emit statusMessage(tr("Copied %n item(s) — paste into Explorer", "",
                                  int(paths.size())));
        },
        /*showProgress=*/true);
}

// Downloads the current selection to a temp folder and blocks (pumping events,
// with a progress dialog) until the local paths are ready. Safe to call from a
// drag/drop callback. Returns empty on cancel/failure.
QStringList ExplorerSftpBrowser::prepareSelectionToTemp()
{
    QEventLoop loop;
    QStringList ready;
    bool done = false;
    downloadSelectedToTemp(
        [&](const QStringList &paths) {
            ready = paths;
            done = true;
            loop.quit();
        },
        /*showProgress=*/true);
    if (!done)
        loop.exec();
    return ready;
}

void ExplorerSftpBrowser::startDragOut()
{
    const QVector<SftpEntry> sel = selectedEntries();
    if (sel.isEmpty())
        return;

    // Record the dragged folders so a drop on the nav pane can pin them without
    // touching the OS drag payload. Cleared when the drag finishes.
    m_dragPaths.clear();
    for (const SftpEntry &e : sel)
        if (e.isDirectory)
            m_dragPaths << remoteJoin(m_path, e.name);
    struct Clear {
        QStringList *p;
        ~Clear() { p->clear(); }
    } clearer{&m_dragPaths};

#ifdef _WIN32
    // Start the OLE drag *now* so it latches onto the live mouse gesture, and
    // download the files only when Explorer asks for them (i.e. after the drop).
    // Downloading up-front — the old approach — lost the gesture and the drag
    // never left the window.
    startWindowsFileDrag([this]() -> QStringList { return prepareSelectionToTemp(); });
#else
    // Fallback: download first, then a plain Qt drag (works where the drag
    // machinery doesn't require the files up-front).
    const QStringList ready = prepareSelectionToTemp();
    if (ready.isEmpty())
        return;
    QList<QUrl> urls;
    for (const QString &p : ready)
        urls.append(QUrl::fromLocalFile(p));
    auto *mime = new QMimeData;
    mime->setUrls(urls);
    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->exec(Qt::CopyAction);
#endif
}

void ExplorerSftpBrowser::showContextMenu(const QPoint &pos)
{
    const auto entries = selectedEntries();
    QMenu menu(this);
    if (!entries.isEmpty()) {
        if (entries.size() == 1 && entries[0].isDirectory)
            menu.addAction(tr("Open"), this, [this, entries] {
                navigateTo(remoteJoin(m_path, entries[0].name));
            });
        menu.addAction(tr("Download"), this, &ExplorerSftpBrowser::downloadSelected);
        menu.addAction(tr("Copy"), this,
                       &ExplorerSftpBrowser::copySelectionToClipboard);
        const bool anyDir = std::any_of(entries.begin(), entries.end(),
                                        [](const SftpEntry &e) { return e.isDirectory; });
        if (anyDir)
            menu.addAction(tr("Pin to sidebar"), this,
                           &ExplorerSftpBrowser::pinSelectionToSidebar);
        if (entries.size() == 1)
            menu.addAction(tr("Rename"), this, &ExplorerSftpBrowser::renameSelected);
        menu.addAction(tr("Delete"), this, &ExplorerSftpBrowser::deleteSelected);
        menu.addSeparator();
    }
    if (hasLocalFiles(QApplication::clipboard()->mimeData()))
        menu.addAction(tr("Paste"), this, &ExplorerSftpBrowser::pasteFromClipboard);
    menu.addAction(tr("Upload files…"), this, &ExplorerSftpBrowser::uploadFiles);
    menu.addAction(tr("New folder…"), this, &ExplorerSftpBrowser::newFolder);
    menu.addAction(tr("Refresh"), this, &ExplorerSftpBrowser::refresh);
    menu.exec(m_table->viewport()->mapToGlobal(pos));
}

// ---------------------------------------------------------------------------
// Operation + transfer feedback
// ---------------------------------------------------------------------------
void ExplorerSftpBrowser::onOperationFinished(const QString &op, bool ok,
                                              const QString &message)
{
    if (!ok) {
        emit statusMessage(tr("%1 failed: %2").arg(op, message));
        return;
    }
    if (op == QStringLiteral("mkdir") || op == QStringLiteral("remove") ||
        op == QStringLiteral("rename") || op == QStringLiteral("chmod"))
        refresh();
}

void ExplorerSftpBrowser::onTransferQueued(const TransferItem &item)
{
    m_activeXfers.insert(item.id, item);
    m_xferBar->setVisible(true);
    m_xferBar->setRange(0, 100);
    m_xferBar->setValue(0);
    m_xferLabel->setText(item.direction == TransferItem::Upload
                             ? tr("Uploading %1").arg(item.displayName)
                             : tr("Downloading %1").arg(item.displayName));
}

void ExplorerSftpBrowser::onTransferProgress(int id, quint64 done, quint64 total)
{
    if (!m_activeXfers.contains(id))
        return;
    if (total == 0) {
        // Bulk tar streams have no known total — show a busy indicator.
        m_xferBar->setRange(0, 0);
    } else {
        m_xferBar->setRange(0, 100);
        m_xferBar->setValue(int(done * 100 / total));
    }
    if (m_tempProgress && m_tempBatch.contains(id))
        m_tempProgress->setLabelText(tr("Preparing… %1").arg(humanSize(done)));
}

void ExplorerSftpBrowser::onTransferFinished(int id, bool ok, const QString &message)
{
    m_activeXfers.remove(id);

    // "Download to temp" batch for copy-out / drag-out to Windows Explorer.
    if (m_tempBatch.remove(id)) {
        const bool userCancelled = message.contains(QStringLiteral("cancel"),
                                                    Qt::CaseInsensitive);
        if (!ok && !userCancelled && m_tempBulkDirs.contains(id)) {
            // The remote likely lacks `tar`: fall back to a per-file recursive
            // download so the copy still succeeds (slower, but reliable).
            const QString remoteDir = m_tempBulkDirs.take(id);
            QDir().mkpath(QDir(m_tempBaseDir)
                              .filePath(remoteDir.section(
                                  '/', -1, -1, QString::SectionSkipEmpty)));
            ++m_tempPendingDirs;
            m_session->requestSyncListing(remoteDir);
        } else {
            m_tempBulkDirs.remove(id);
            maybeFinishTemp();
        }
        if (m_activeXfers.isEmpty()) {
            m_xferBar->setVisible(false);
            m_xferLabel->clear();
        }
        return; // no remote refresh for a clipboard/temp download
    }

    emit statusMessage(ok ? tr("Transfer complete")
                          : tr("Transfer failed: %1").arg(message));
    if (m_activeXfers.isEmpty()) {
        m_xferBar->setVisible(false);
        m_xferLabel->clear();
    }
    if (ok)
        refresh(); // reflect newly uploaded files
}

} // namespace termsync::ui
