#include "transfer_view/ExplorerSftpBrowser.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
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
#include <QPushButton>
#include <QRandomGenerator>
#include <QShortcut>
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

} // namespace

ExplorerSftpBrowser::ExplorerSftpBrowser(const core::SshConnectionParams &params,
                                         const QString &expectedFingerprint,
                                         core::Protocol protocol, QWidget *parent)
    : QWidget(parent)
{
    m_session = new transfer::SftpSession(params, expectedFingerprint, protocol, this);
    connect(m_session, &transfer::SftpSession::connected, this,
            &ExplorerSftpBrowser::onConnected);
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

    auto navButton = [&](QStyle::StandardPixmap sp, const QString &tip) {
        auto *b = new QToolButton(bar);
        b->setIcon(style()->standardIcon(sp));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        return b;
    };
    m_back = navButton(QStyle::SP_ArrowBack, tr("Back"));
    m_forward = navButton(QStyle::SP_ArrowForward, tr("Forward"));
    m_up = navButton(QStyle::SP_ArrowUp, tr("Up"));
    auto *refreshBtn = navButton(QStyle::SP_BrowserReload, tr("Refresh"));
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

    auto cmd = [&](QStyle::StandardPixmap sp, const QString &text) {
        auto *b = new QToolButton(bar);
        b->setIcon(style()->standardIcon(sp));
        b->setText(text);
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setAutoRaise(true);
        return b;
    };
    auto *newFolderBtn = cmd(QStyle::SP_FileDialogNewFolder, tr("New folder"));
    auto *uploadBtn = cmd(QStyle::SP_ArrowUp, tr("Upload"));
    m_downloadBtn = cmd(QStyle::SP_ArrowDown, tr("Download"));
    m_renameBtn = cmd(QStyle::SP_FileDialogDetailedView, tr("Rename"));
    m_deleteBtn = cmd(QStyle::SP_TrashIcon, tr("Delete"));
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
    row->addStretch();
    return bar;
}

// ---------------------------------------------------------------------------
// Left navigation pane (quick locations)
// ---------------------------------------------------------------------------
QWidget *ExplorerSftpBrowser::buildNavPane()
{
    m_navList = new QListWidget(this);
    m_navList->setFrameShape(QFrame::NoFrame);
    m_navList->setStyleSheet(QStringLiteral(
        "QListWidget { background:#1a1b26; border-right:1px solid #2a2c3a; }"
        "QListWidget::item { padding:7px 10px; border-radius:6px; margin:1px 4px; }"
        "QListWidget::item:hover { background:#23252f; }"
        "QListWidget::item:selected { background:#2dd4bf; color:#101218; }"));
    const QIcon home = style()->standardIcon(QStyle::SP_DirHomeIcon);
    const QIcon drive = style()->standardIcon(QStyle::SP_DriveHDIcon);
    auto add = [&](const QIcon &ic, const QString &text, const QString &path) {
        auto *it = new QListWidgetItem(ic, text, m_navList);
        it->setData(Qt::UserRole, path);
    };
    add(home, tr("Home"), QStringLiteral("."));
    add(drive, tr("Root"), QStringLiteral("/"));
    connect(m_navList, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *it) { navigateTo(it->data(Qt::UserRole).toString()); });
    return m_navList;
}

// ---------------------------------------------------------------------------
// Main details list
// ---------------------------------------------------------------------------
QWidget *ExplorerSftpBrowser::buildFileView()
{
    m_table = new QTableWidget(0, 4, this);
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
    return m_table;
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
    std::sort(rows.begin(), rows.end(), [](const SftpEntry &a, const SftpEntry &b) {
        if (a.isDirectory != b.isDirectory)
            return a.isDirectory;
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });

    m_table->setSortingEnabled(false);
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
        size->setData(Qt::UserRole, static_cast<qulonglong>(e.size));
        m_table->setItem(r, 3, size);
    }
    m_table->setSortingEnabled(true);

    const int dirs = std::count_if(rows.begin(), rows.end(),
                                   [](const SftpEntry &e) { return e.isDirectory; });
    m_countLabel->setText(tr("%n item(s)", "", int(rows.size())) +
                          (dirs ? tr("  ·  %n folder(s)", "", dirs) : QString()));
}

QIcon ExplorerSftpBrowser::iconFor(const SftpEntry &e) const
{
    if (e.isDirectory)
        return iconProvider().icon(QAbstractFileIconProvider::Folder);
    // Type-specific icon by extension via a (non-existent) sample name.
    const QIcon byType = iconProvider().icon(QFileInfo(e.name));
    return byType.isNull() ? iconProvider().icon(QAbstractFileIconProvider::File)
                           : byType;
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
    QVector<SftpEntry> out;
    const auto rows = m_table->selectionModel()->selectedRows();
    for (const QModelIndex &idx : rows) {
        const QString name = m_table->item(idx.row(), 0)->data(Qt::UserRole).toString();
        for (const SftpEntry &e : m_entries)
            if (e.name == name) { out.append(e); break; }
    }
    return out;
}

void ExplorerSftpBrowser::onSelectionChanged()
{
    const int n = m_table->selectionModel()
                      ? m_table->selectionModel()->selectedRows().size()
                      : 0;
    const bool hasFiles = std::any_of(
        selectedEntries().begin(), selectedEntries().end(),
        [](const SftpEntry &e) { return !e.isDirectory; });
    if (m_downloadBtn)
        m_downloadBtn->setEnabled(hasFiles);
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
    QVector<SftpEntry> files;
    for (const SftpEntry &e : entries)
        if (!e.isDirectory)
            files.append(e);
    if (files.isEmpty()) {
        emit statusMessage(tr("Select file(s) to download"));
        return;
    }
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Download to"),
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    if (dir.isEmpty())
        return;
    for (const SftpEntry &e : files) {
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
        // Create the remote folder, then recurse (SftpSession serialises the
        // mkdir before the child transfers, so ordering is preserved).
        const QString remoteSub = remoteJoin(remoteDir, fi.fileName());
        m_session->makeDirectory(remoteSub);
        const QFileInfoList children =
            QDir(localPath).entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
        for (const QFileInfo &child : children)
            uploadLocalEntry(child.absoluteFilePath(), remoteSub);
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
    std::function<void(const QStringList &)> onReady)
{
    QVector<SftpEntry> files;
    for (const SftpEntry &e : selectedEntries())
        if (!e.isDirectory)
            files.append(e);
    if (files.isEmpty()) {
        emit statusMessage(tr("Select file(s) first"));
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
    m_tempPaths.clear();
    m_tempOnReady = std::move(onReady);
    for (const SftpEntry &e : files) {
        TransferItem item;
        item.direction = TransferItem::Download;
        item.displayName = e.name;
        item.remotePath = remoteJoin(m_path, e.name);
        item.localPath = QDir(base).filePath(e.name);
        item.size = e.size;
        m_tempBatch.insert(m_session->enqueue(item));
    }
    emit statusMessage(tr("Preparing %n file(s)…", "", int(files.size())));
}

void ExplorerSftpBrowser::copySelectionToClipboard()
{
    downloadSelectedToTemp([this](const QStringList &paths) {
        QList<QUrl> urls;
        for (const QString &p : paths)
            urls.append(QUrl::fromLocalFile(p));
        auto *mime = new QMimeData;
        mime->setUrls(urls);
        QApplication::clipboard()->setMimeData(mime);
        emit statusMessage(
            tr("Copied %n file(s) — paste into Explorer", "", int(paths.size())));
    });
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
    m_xferBar->setValue(total ? int(done * 100 / total) : 0);
}

void ExplorerSftpBrowser::onTransferFinished(int id, bool ok, const QString &message)
{
    QString localPath;
    if (m_activeXfers.contains(id))
        localPath = m_activeXfers.value(id).localPath;
    m_activeXfers.remove(id);

    // "Download to temp" batch for copy-out / drag-out to Windows Explorer.
    if (m_tempBatch.remove(id)) {
        if (ok && !localPath.isEmpty())
            m_tempPaths.append(localPath);
        if (m_tempBatch.isEmpty() && m_tempOnReady) {
            auto cb = std::move(m_tempOnReady);
            m_tempOnReady = nullptr;
            const QStringList paths = m_tempPaths;
            m_tempPaths.clear();
            if (!paths.isEmpty())
                cb(paths);
            else
                emit statusMessage(tr("Copy failed: %1").arg(message));
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
