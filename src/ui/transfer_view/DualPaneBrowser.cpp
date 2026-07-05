#include "transfer_view/DualPaneBrowser.h"

#include "sync/DirectoryDiffer.h"
#include "sync/SyncEngine.h"
#include "transfer_view/SynchronizeDialog.h"

#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
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
    auto *syncBtn = new QPushButton(tr("Synchronize..."));
    connect(syncBtn, &QPushButton::clicked, this, &DualPaneBrowser::onSyncClicked);
    m_remotePathEdit = new QLineEdit;
    m_remotePathEdit->setReadOnly(true);
    bar->addWidget(downloadBtn);
    bar->addWidget(syncBtn);
    bar->addWidget(new QLabel(tr("Remote:")));
    bar->addWidget(m_remotePathEdit, 1);
    bar->addWidget(up);

    m_remoteTable = new QTableWidget(0, 4);
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

    layout->addLayout(bar);
    layout->addWidget(m_remoteTable);
    return pane;
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
    for (const QString &local : files) {
        TransferItem item;
        item.direction = TransferItem::Upload;
        item.localPath = local;
        item.displayName = QFileInfo(local).fileName();
        item.remotePath = remoteJoin(m_remotePath, item.displayName);
        item.size = static_cast<quint64>(QFileInfo(local).size());
        m_session->enqueue(item);
    }
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
