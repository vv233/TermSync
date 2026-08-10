#include "transfer_view/DualPaneBrowser.h"

#include "browse/PathMirror.h"
#include "sync/DirectoryDiffer.h"
#include "sync/SyncEngine.h"
#include "transfer_view/SynchronizeDialog.h"

#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QInputDialog>
#include <QMimeData>
#include <QUrl>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableView>
#include <QTableWidget>
#include <QToolButton>
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
} // namespace

DualPaneBrowser::DualPaneBrowser(const core::SshConnectionParams &params,
                                 const QString &expectedFingerprint,
                                 core::Protocol protocol, QWidget *parent)
    : QWidget(parent)
{
    m_host = params.host;

    // Bookmarks persist in a single JSON file shared across sessions.
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    m_bookmarksPath = dir + QStringLiteral("/bookmarks.json");
    m_bookmarks.load(m_bookmarksPath);

    m_session = new transfer::SftpSession(params, expectedFingerprint, protocol, this);

    connect(m_session, &transfer::SftpSession::connected, this,
            &DualPaneBrowser::onConnected);
    connect(m_session, &transfer::SftpSession::hostKeyFingerprint, this,
            &DualPaneBrowser::hostKeyFingerprintReceived);
    connect(m_session, &transfer::SftpSession::connectionFailed, this,
            [this](const QString &r) {
                emit statusMessage(tr("SFTP connection failed: %1").arg(r));
            });
    connect(m_session, &transfer::SftpSession::directoryListed, this,
            &DualPaneBrowser::onDirectoryListed);
    connect(m_session, &transfer::SftpSession::operationFinished, this,
            &DualPaneBrowser::onOperationFinished);
    connect(m_session, &transfer::SftpSession::transferQueued, this,
            &DualPaneBrowser::onTransferQueued);
    connect(m_session, &transfer::SftpSession::transferProgress, this,
            &DualPaneBrowser::onTransferProgress);
    connect(m_session, &transfer::SftpSession::transferFinished, this,
            &DualPaneBrowser::onTransferFinished);
    connect(m_session, &transfer::SftpSession::syncListingReady, this,
            &DualPaneBrowser::onSyncListingReady);
    connect(m_session, &transfer::SftpSession::sudoModeChanged, this,
            &DualPaneBrowser::onSudoModeChanged);

    auto *panes = new QSplitter(Qt::Horizontal, this);
    panes->addWidget(buildLocalPane());
    panes->addWidget(buildRemotePane());
    panes->setSizes({400, 400});

    auto *outer = new QSplitter(Qt::Vertical, this);
    outer->addWidget(panes);
    outer->addWidget(buildQueuePanel());
    outer->setSizes({500, 150});

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(outer);

    setLocalPath(QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
    m_session->connectToHost();
}

// ---------------------------------------------------------------------------
// Local pane
// ---------------------------------------------------------------------------
QWidget *DualPaneBrowser::buildLocalPane()
{
    auto *pane = new QWidget;
    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(2, 2, 2, 2);

    auto *bar = new QHBoxLayout;
    auto *up = new QToolButton;
    up->setText(tr("Up"));
    connect(up, &QToolButton::clicked, this, &DualPaneBrowser::localGoUp);
    m_localPathEdit = new QLineEdit;
    m_localPathEdit->setReadOnly(true);
    auto *uploadBtn = new QPushButton(tr("Upload →"));
    connect(uploadBtn, &QPushButton::clicked, this, &DualPaneBrowser::uploadSelected);
    bar->addWidget(up);
    bar->addWidget(new QLabel(tr("Local:")));
    bar->addWidget(m_localPathEdit, 1);
    bar->addWidget(uploadBtn);

    m_localModel = new QFileSystemModel(this);
    m_localModel->setRootPath(QDir::rootPath());
    m_localView = new QTableView;
    m_localView->setModel(m_localModel);
    m_localView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_localView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_localView->verticalHeader()->hide();
    m_localView->setColumnHidden(2, true); // type
    connect(m_localView, &QTableView::doubleClicked, this,
            &DualPaneBrowser::onLocalActivated);

    layout->addLayout(bar);
    layout->addWidget(m_localView);
    return pane;
}

void DualPaneBrowser::setLocalPath(const QString &path)
{
    QDir dir(path);
    if (!dir.exists())
        return;
    m_localView->setRootIndex(m_localModel->index(dir.absolutePath()));
    m_localPathEdit->setText(dir.absolutePath());
    mirrorLocalToRemote(dir.absolutePath());
}

void DualPaneBrowser::onLocalActivated(const QModelIndex &index)
{
    const QString path = m_localModel->filePath(index);
    if (QFileInfo(path).isDir())
        setLocalPath(path);
}

void DualPaneBrowser::localGoUp()
{
    QDir dir(m_localPathEdit->text());
    if (dir.cdUp())
        setLocalPath(dir.absolutePath());
}

QStringList DualPaneBrowser::selectedLocalFiles() const
{
    QStringList files;
    const auto rows = m_localView->selectionModel()->selectedRows();
    for (const QModelIndex &idx : rows) {
        const QString path = m_localModel->filePath(idx);
        if (QFileInfo(path).isFile())
            files << path;
    }
    return files;
}

// ---------------------------------------------------------------------------
// Remote pane
// ---------------------------------------------------------------------------
QWidget *DualPaneBrowser::buildRemotePane()
{
    auto *pane = new QWidget;
    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(2, 2, 2, 2);

    auto *bar = new QHBoxLayout;
    auto *up = new QToolButton;
    up->setText(tr("Up"));
    connect(up, &QToolButton::clicked, this, &DualPaneBrowser::remoteGoUp);
    auto *downloadBtn = new QPushButton(tr("← Download"));
    connect(downloadBtn, &QPushButton::clicked, this, &DualPaneBrowser::downloadSelected);

    // Sudo mode: run remote operations as root (parity with the Explorer view).
    m_sudoBtn = new QToolButton;
    m_sudoBtn->setText(tr("Sudo"));
    m_sudoBtn->setCheckable(true);
    m_sudoBtn->setToolTip(
        tr("Run operations as root (sudo) to access protected files"));
    connect(m_sudoBtn, &QToolButton::toggled, this, &DualPaneBrowser::toggleSudo);

    auto *syncBtn = new QPushButton(tr("Synchronize..."));
    connect(syncBtn, &QPushButton::clicked, this, &DualPaneBrowser::onSyncClicked);

    // Synchronized-browsing toggle: mirror navigation across both panes.
    auto *syncBrowse = new QToolButton;
    syncBrowse->setText(tr("Sync Browse"));
    syncBrowse->setCheckable(true);
    syncBrowse->setToolTip(
        tr("Mirror folder navigation between the local and remote panes"));
    connect(syncBrowse, &QToolButton::toggled, this,
            &DualPaneBrowser::setSyncBrowsing);

    m_remotePathEdit = new QLineEdit;
    m_remotePathEdit->setReadOnly(true);
    bar->addWidget(downloadBtn);
    bar->addWidget(m_sudoBtn);
    bar->addWidget(buildBookmarkButton());
    bar->addWidget(syncBrowse);
    bar->addWidget(syncBtn);
    bar->addWidget(new QLabel(tr("Remote:")));
    bar->addWidget(m_remotePathEdit, 1);
    bar->addWidget(up);

    m_remoteTable = new QTableWidget(0, 4);
    m_remoteTable->setObjectName(QStringLiteral("remoteTable"));
    m_remoteTable->setHorizontalHeaderLabels(
        {tr("Name"), tr("Size"), tr("Modified"), tr("Perms")});
    m_remoteTable->horizontalHeader()->setStretchLastSection(true);
    m_remoteTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_remoteTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_remoteTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_remoteTable->verticalHeader()->hide();
    m_remoteTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_remoteTable, &QTableWidget::cellDoubleClicked, this,
            &DualPaneBrowser::onRemoteActivated);
    connect(m_remoteTable, &QWidget::customContextMenuRequested, this,
            &DualPaneBrowser::remoteContextMenu);

    // Accept files dragged in from Explorer (upload). The table isn't a custom
    // subclass, so watch its viewport via an event filter.
    m_remoteTable->setAcceptDrops(true);
    m_remoteTable->viewport()->setAcceptDrops(true);
    m_remoteTable->installEventFilter(this);
    m_remoteTable->viewport()->installEventFilter(this);

    layout->addLayout(bar);
    layout->addWidget(m_remoteTable);
    return pane;
}

// ---------------------------------------------------------------------------
// Bookmarks (M20)
// ---------------------------------------------------------------------------
QToolButton *DualPaneBrowser::buildBookmarkButton()
{
    auto *button = new QToolButton;
    button->setText(tr("Bookmarks"));
    button->setPopupMode(QToolButton::InstantPopup);
    m_bookmarkMenu = new QMenu(button);
    button->setMenu(m_bookmarkMenu);
    // Rebuild on show so the list reflects the current host + saved bookmarks.
    connect(m_bookmarkMenu, &QMenu::aboutToShow, this,
            &DualPaneBrowser::rebuildBookmarkMenu);
    return button;
}

void DualPaneBrowser::rebuildBookmarkMenu()
{
    if (!m_bookmarkMenu)
        return;
    m_bookmarkMenu->clear();

    QAction *addAct = m_bookmarkMenu->addAction(tr("Add Bookmark..."));
    connect(addAct, &QAction::triggered, this, &DualPaneBrowser::addBookmark);

    const QVector<core::Bookmark> list = m_bookmarks.forHost(m_host);
    if (!list.isEmpty())
        m_bookmarkMenu->addSeparator();
    for (const core::Bookmark &b : list) {
        const QString label =
            b.host.isEmpty() ? tr("%1  (global)").arg(b.name) : b.name;
        QAction *act = m_bookmarkMenu->addAction(label);
        act->setToolTip(b.remotePath);
        const core::Bookmark captured = b;
        connect(act, &QAction::triggered, this,
                [this, captured] { navigateToBookmark(captured); });
    }
}

void DualPaneBrowser::addBookmark()
{
    bool ok = false;
    const QString suggested = m_remotePath.section('/', -1, -1,
                                                   QString::SectionSkipEmpty);
    const QString name = QInputDialog::getText(
        this, tr("Add Bookmark"), tr("Bookmark name:"), QLineEdit::Normal,
        suggested.isEmpty() ? m_remotePath : suggested, &ok);
    if (!ok || name.isEmpty())
        return;

    core::Bookmark b;
    b.id = core::BookmarkStore::newId();
    b.name = name;
    b.host = m_host;
    b.remotePath = m_remotePath;
    b.localPath = m_localPathEdit ? m_localPathEdit->text() : QString();
    m_bookmarks.add(b);
    if (!m_bookmarks.save(m_bookmarksPath))
        emit statusMessage(tr("Could not save bookmark"));
    else
        emit statusMessage(tr("Bookmarked '%1'").arg(name));
}

void DualPaneBrowser::navigateToBookmark(const core::Bookmark &b)
{
    if (!b.localPath.isEmpty())
        setLocalPath(b.localPath);
    if (!b.remotePath.isEmpty())
        requestRemoteList(b.remotePath);
    emit statusMessage(tr("Jumped to bookmark '%1'").arg(b.name));
}

// ---------------------------------------------------------------------------
// Synchronized browsing (M20)
// ---------------------------------------------------------------------------
void DualPaneBrowser::setSyncBrowsing(bool on)
{
    m_syncBrowsing = on;
    if (on) {
        // Capture the current locations as the mirror roots.
        m_syncLocalRoot = m_localPathEdit ? m_localPathEdit->text() : QString();
        m_syncRemoteRoot = m_remotePath;
        emit statusMessage(tr("Synchronized browsing on"));
    } else {
        emit statusMessage(tr("Synchronized browsing off"));
    }
}

void DualPaneBrowser::mirrorLocalToRemote(const QString &newLocal)
{
    if (!m_syncBrowsing)
        return;
    const QString target =
        core::mirrorPath(m_syncLocalRoot, newLocal, m_syncRemoteRoot);
    if (!target.isEmpty() && target != m_remotePath)
        requestRemoteList(target);
}

void DualPaneBrowser::mirrorRemoteToLocal(const QString &newRemote)
{
    if (!m_syncBrowsing || !m_localPathEdit)
        return;
    const QString target =
        core::mirrorPath(m_syncRemoteRoot, newRemote, m_syncLocalRoot);
    if (!target.isEmpty() && target != m_localPathEdit->text())
        setLocalPath(target);
}

QWidget *DualPaneBrowser::buildQueuePanel()
{
    m_queueTable = new QTableWidget(0, 4);
    m_queueTable->setHorizontalHeaderLabels(
        {tr("File"), tr("Direction"), tr("Progress"), tr("Status")});
    m_queueTable->horizontalHeader()->setStretchLastSection(true);
    m_queueTable->verticalHeader()->hide();
    m_queueTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    return m_queueTable;
}

void DualPaneBrowser::onConnected()
{
    emit statusMessage(tr("SFTP connected"));
    requestRemoteList(m_remotePath);
}

void DualPaneBrowser::requestRemoteList(const QString &path)
{
    m_session->listDirectory(path);
}

void DualPaneBrowser::onDirectoryListed(const QString &path,
                                        const QVector<SftpEntry> &entries)
{
    m_remotePath = path;
    m_remotePathEdit->setText(path);
    mirrorRemoteToLocal(path);

    // Sort: directories first, then by name.
    m_remoteEntries = entries;
    std::sort(m_remoteEntries.begin(), m_remoteEntries.end(),
              [](const SftpEntry &a, const SftpEntry &b) {
                  if (a.isDirectory != b.isDirectory)
                      return a.isDirectory;
                  return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
              });

    m_remoteTable->setRowCount(m_remoteEntries.size());
    for (int r = 0; r < m_remoteEntries.size(); ++r) {
        const SftpEntry &e = m_remoteEntries[r];
        auto *nameItem = new QTableWidgetItem(
            (e.isDirectory ? QStringLiteral("📁 ") : QString()) + e.name);
        m_remoteTable->setItem(r, 0, nameItem);
        m_remoteTable->setItem(r, 1, new QTableWidgetItem(
                                          e.isDirectory ? QString() : humanSize(e.size)));
        m_remoteTable->setItem(r, 2, new QTableWidgetItem(
                                          e.modifiedAt.toString("yyyy-MM-dd hh:mm")));
        m_remoteTable->setItem(r, 3, new QTableWidgetItem(
                                          QString::number(e.permissions & 0777, 8)));
    }
}

void DualPaneBrowser::onRemoteActivated(int row, int)
{
    if (row < 0 || row >= m_remoteEntries.size())
        return;
    const SftpEntry &e = m_remoteEntries[row];
    if (e.isDirectory) {
        if (e.name == ".")
            return;
        if (e.name == "..") {
            remoteGoUp();
            return;
        }
        requestRemoteList(remoteJoin(m_remotePath, e.name));
    }
}

void DualPaneBrowser::remoteGoUp()
{
    QString p = m_remotePath;
    if (p.endsWith('/') && p.size() > 1)
        p.chop(1);
    const int slash = p.lastIndexOf('/');
    if (slash > 0)
        requestRemoteList(p.left(slash));
    else if (slash == 0)
        requestRemoteList(QStringLiteral("/"));
    else
        requestRemoteList(QStringLiteral(".."));
}

QString DualPaneBrowser::remoteJoin(const QString &dir, const QString &name) const
{
    if (dir.isEmpty() || dir == ".")
        return name;
    if (dir.endsWith('/'))
        return dir + name;
    return dir + '/' + name;
}

// ---------------------------------------------------------------------------
// Transfers
// ---------------------------------------------------------------------------
void DualPaneBrowser::uploadSelected()
{
    const QStringList files = selectedLocalFiles();
    if (files.isEmpty()) {
        emit statusMessage(tr("Select local file(s) to upload"));
        return;
    }
    uploadPaths(files);
}

void DualPaneBrowser::uploadDroppedUrls(const QList<QUrl> &urls)
{
    QStringList paths;
    for (const QUrl &u : urls)
        if (u.isLocalFile())
            paths << u.toLocalFile();
    if (!paths.isEmpty())
        uploadPaths(paths);
}

void DualPaneBrowser::uploadPaths(const QStringList &files)
{
    int n = 0;
    for (const QString &local : files) {
        const QFileInfo fi(local);
        TransferItem item;
        item.direction = TransferItem::Upload;
        if (fi.isDir()) {
            item.kind = TransferItem::BulkDir;
            item.localPath = local;
            item.remotePath = m_remotePath; // parent dir the tree unpacks into
            item.displayName = fi.fileName();
            m_session->enqueueBulkUpload(local, m_remotePath, fi.fileName());
        } else {
            item.localPath = local;
            item.displayName = fi.fileName();
            item.remotePath = remoteJoin(m_remotePath, item.displayName);
            item.size = static_cast<quint64>(fi.size());
            m_session->enqueue(item);
        }
        ++n;
    }
    if (n)
        emit statusMessage(tr("Uploading %n item(s) to %1", "", n).arg(m_remotePath));
}

bool DualPaneBrowser::eventFilter(QObject *obj, QEvent *event)
{
    if (m_remoteTable &&
        (obj == m_remoteTable || obj == m_remoteTable->viewport())) {
        const QEvent::Type t = event->type();
        if (t == QEvent::DragEnter || t == QEvent::DragMove || t == QEvent::Drop) {
            auto *de = static_cast<QDropEvent *>(event);
            const QMimeData *m = de->mimeData();
            bool hasFiles = false;
            if (m && m->hasUrls())
                for (const QUrl &u : m->urls())
                    if (u.isLocalFile()) { hasFiles = true; break; }
            if (hasFiles) {
                de->acceptProposedAction();
                if (t == QEvent::Drop) {
                    QStringList paths;
                    for (const QUrl &u : m->urls())
                        if (u.isLocalFile())
                            paths << u.toLocalFile();
                    uploadPaths(paths);
                }
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

void DualPaneBrowser::toggleSudo(bool on)
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
        QSignalBlocker block(m_sudoBtn);
        m_sudoBtn->setChecked(false);
        return;
    }
    emit statusMessage(tr("Authenticating sudo…"));
    m_session->setSudo(true, pw);
}

void DualPaneBrowser::onSudoModeChanged(bool enabled, bool ok, const QString &message)
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
    requestRemoteList(m_remotePath); // re-list at the new privilege level
}

void DualPaneBrowser::downloadSelected()
{
    const auto rows = m_remoteTable->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        emit statusMessage(tr("Select remote file(s) to download"));
        return;
    }
    const QString localDir = m_localPathEdit->text();
    for (const QModelIndex &idx : rows) {
        const int r = idx.row();
        if (r < 0 || r >= m_remoteEntries.size())
            continue;
        const SftpEntry &e = m_remoteEntries[r];
        if (e.isDirectory)
            continue; // recursive dir download deferred to M7 sync
        TransferItem item;
        item.direction = TransferItem::Download;
        item.displayName = e.name;
        item.remotePath = remoteJoin(m_remotePath, e.name);
        item.localPath = QDir(localDir).filePath(e.name);
        item.size = e.size;
        m_session->enqueue(item);
    }
}

void DualPaneBrowser::remoteContextMenu(const QPoint &pos)
{
    QMenu menu(this);
    QAction *dl = menu.addAction(tr("Download"));
    QAction *ren = menu.addAction(tr("Rename..."));
    QAction *del = menu.addAction(tr("Delete"));
    menu.addSeparator();
    QAction *mkdir = menu.addAction(tr("New Folder..."));
    QAction *refresh = menu.addAction(tr("Refresh"));

    QAction *chosen = menu.exec(m_remoteTable->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == refresh) {
        requestRemoteList(m_remotePath);
        return;
    }
    if (chosen == mkdir) {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("New Folder"),
                                                   tr("Folder name:"),
                                                   QLineEdit::Normal, QString(), &ok);
        if (ok && !name.isEmpty())
            m_session->makeDirectory(remoteJoin(m_remotePath, name));
        return;
    }

    const int r = m_remoteTable->currentRow();
    if (r < 0 || r >= m_remoteEntries.size())
        return;
    const SftpEntry &e = m_remoteEntries[r];

    if (chosen == dl) {
        downloadSelected();
    } else if (chosen == del) {
        if (QMessageBox::question(this, tr("Delete"),
                                  tr("Delete '%1'?").arg(e.name)) == QMessageBox::Yes)
            m_session->removeEntry(remoteJoin(m_remotePath, e.name), e.isDirectory);
    } else if (chosen == ren) {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Rename"),
                                                   tr("New name:"), QLineEdit::Normal,
                                                   e.name, &ok);
        if (ok && !name.isEmpty() && name != e.name)
            m_session->renameEntry(remoteJoin(m_remotePath, e.name),
                                   remoteJoin(m_remotePath, name));
    }
}

void DualPaneBrowser::onSyncClicked()
{
    emit statusMessage(tr("Scanning remote tree for synchronization..."));
    m_syncPending = true;
    m_session->requestSyncListing(m_remotePath);
}

void DualPaneBrowser::onSyncListingReady(const QString &,
                                         const transfer::sync::Listing &remote,
                                         bool ok)
{
    if (!m_syncPending)
        return;
    m_syncPending = false;
    if (!ok) {
        emit statusMessage(tr("Synchronize: could not scan the remote tree"));
        return;
    }

    const QString localDir = m_localPathEdit->text();
    const QString remoteDir = m_remotePath;
    const transfer::sync::Listing local =
        transfer::sync::enumerateLocalTree(localDir);
    // Capture the two listings so the dialog can recompute per direction.
    const transfer::sync::Listing remoteCopy = remote;

    auto compute = [local, remoteCopy](transfer::sync::Direction dir) {
        transfer::sync::DirectoryDiffer differ(
            dir, transfer::sync::CompareStrategy::MtimeSize,
            transfer::sync::ConflictPolicy::NewerWins, /*deleteOrphans=*/false);
        return differ.diff(local, remoteCopy);
    };

    auto execute = [this, localDir, remoteDir](
                       const QVector<transfer::sync::SyncAction> &actions) {
        using transfer::sync::ActionType;
        int queued = 0;
        for (const auto &a : actions) {
            const QString localPath = QDir(localDir).filePath(a.relativePath);
            const QString remotePath = remoteJoin(remoteDir, a.relativePath);
            switch (a.type) {
            case ActionType::Upload: {
                transfer::TransferItem item;
                item.direction = transfer::TransferItem::Upload;
                item.localPath = localPath;
                item.remotePath = remotePath;
                item.displayName = a.relativePath;
                m_session->enqueue(item);
                ++queued;
                break;
            }
            case ActionType::Download: {
                QDir().mkpath(QFileInfo(localPath).absolutePath());
                transfer::TransferItem item;
                item.direction = transfer::TransferItem::Download;
                item.localPath = localPath;
                item.remotePath = remotePath;
                item.displayName = a.relativePath;
                m_session->enqueue(item);
                ++queued;
                break;
            }
            case ActionType::MakeRemoteDir:
                m_session->makeDirectory(remotePath);
                break;
            case ActionType::MakeLocalDir:
                QDir().mkpath(localPath);
                break;
            case ActionType::DeleteRemote:
                m_session->removeEntry(remotePath, /*isDir=*/false);
                break;
            case ActionType::DeleteLocal:
                QFile::remove(localPath);
                break;
            default:
                break;
            }
        }
        emit statusMessage(tr("Synchronize: queued %1 transfer(s)").arg(queued));
    };

    SynchronizeDialog dialog(localDir, remoteDir, compute, execute, this);
    dialog.exec();
}

void DualPaneBrowser::onOperationFinished(const QString &op, bool ok,
                                          const QString &message)
{
    if (!ok) {
        emit statusMessage(tr("%1 failed: %2").arg(op, message));
        return;
    }
    // Refresh the remote listing after a mutating operation.
    if (op == "mkdir" || op == "remove" || op == "rename" || op == "chmod")
        requestRemoteList(m_remotePath);
}

// ---------------------------------------------------------------------------
// Transfer queue panel
// ---------------------------------------------------------------------------
void DualPaneBrowser::onTransferQueued(const TransferItem &item)
{
    const int row = m_queueTable->rowCount();
    m_queueTable->insertRow(row);
    m_taskRow.insert(item.id, row);
    m_queueTable->setItem(row, 0, new QTableWidgetItem(item.displayName));
    m_queueTable->setItem(row, 1, new QTableWidgetItem(
                                      item.direction == TransferItem::Upload
                                          ? tr("Upload")
                                          : tr("Download")));
    auto *progress = new QProgressBar;
    progress->setRange(0, 100);
    progress->setValue(0);
    m_queueTable->setCellWidget(row, 2, progress);
    m_queueTable->setItem(row, 3, new QTableWidgetItem(tr("Queued")));
}

void DualPaneBrowser::onTransferProgress(int id, quint64 done, quint64 total)
{
    if (!m_taskRow.contains(id))
        return;
    const int row = m_taskRow.value(id);
    if (auto *bar = qobject_cast<QProgressBar *>(m_queueTable->cellWidget(row, 2)))
        bar->setValue(total ? static_cast<int>(done * 100 / total) : 0);
    if (auto *item = m_queueTable->item(row, 3))
        item->setText(tr("Transferring"));
}

void DualPaneBrowser::onTransferFinished(int id, bool ok, const QString &message)
{
    if (m_taskRow.contains(id)) {
        const int row = m_taskRow.value(id);
        if (auto *bar = qobject_cast<QProgressBar *>(m_queueTable->cellWidget(row, 2)))
            if (ok)
                bar->setValue(100);
        if (auto *item = m_queueTable->item(row, 3))
            item->setText(ok ? tr("Done") : tr("Failed: %1").arg(message));
    }
    emit statusMessage(ok ? tr("Transfer complete") : tr("Transfer failed: %1").arg(message));
    // Refresh remote to show newly uploaded files.
    if (ok)
        requestRemoteList(m_remotePath);
}

} // namespace termsync::ui
