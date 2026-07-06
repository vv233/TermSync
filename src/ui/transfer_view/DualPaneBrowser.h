#pragma once

#include <QHash>
#include <QWidget>

#include "model/ConnectionProfile.h"
#include "queue/SftpSession.h"
#include "ssh/SshConnection.h"
#include "store/BookmarkStore.h"

class QFileSystemModel;
class QLineEdit;
class QMenu;
class QTableView;
class QTableWidget;
class QModelIndex;
class QToolButton;

namespace termsync::ui {

// SecureFX-style dual-pane file browser: local files on the left, remote SFTP
// on the right, with a transfer-queue panel below. Transfers run off the UI
// thread via transfer::SftpSession.
class DualPaneBrowser : public QWidget
{
    Q_OBJECT

public:
    DualPaneBrowser(const core::SshConnectionParams &params,
                    const QString &expectedFingerprint,
                    core::Protocol protocol = core::Protocol::SSH2,
                    QWidget *parent = nullptr);

signals:
    void statusMessage(const QString &message);
    // Reports the server fingerprint so the owner can persist it (TOFU).
    void hostKeyFingerprintReceived(const QString &fingerprint);

private slots:
    void onConnected();
    void onDirectoryListed(const QString &path,
                           const QVector<transfer::SftpEntry> &entries);
    void onOperationFinished(const QString &op, bool ok, const QString &message);
    void onTransferQueued(const transfer::TransferItem &item);
    void onTransferProgress(int id, quint64 done, quint64 total);
    void onTransferFinished(int id, bool ok, const QString &message);
    void onSyncClicked();
    void onSyncListingReady(const QString &root,
                            const transfer::sync::Listing &listing, bool ok);

    void onLocalActivated(const QModelIndex &index);
    void onRemoteActivated(int row, int column);
    void uploadSelected();
    void downloadSelected();
    void remoteContextMenu(const QPoint &pos);
    void localGoUp();
    void remoteGoUp();

    // Bookmarks (M20) + synchronized browsing.
    void rebuildBookmarkMenu();
    void addBookmark();
    void navigateToBookmark(const core::Bookmark &b);
    void setSyncBrowsing(bool on);

private:
    QWidget *buildLocalPane();
    QWidget *buildRemotePane();
    QWidget *buildQueuePanel();
    QToolButton *buildBookmarkButton();
    void setLocalPath(const QString &path);
    void requestRemoteList(const QString &path);
    // When synchronized browsing is on, mirror the other pane to `newLocal` /
    // `newRemote` (no-op if the mirrored path is unchanged or out of subtree).
    void mirrorRemoteToLocal(const QString &newRemote);
    void mirrorLocalToRemote(const QString &newLocal);
    QString remoteJoin(const QString &dir, const QString &name) const;
    QStringList selectedLocalFiles() const;

    transfer::SftpSession *m_session = nullptr;
    QString m_remotePath = QStringLiteral(".");

    QFileSystemModel *m_localModel = nullptr;
    QTableView *m_localView = nullptr;
    QLineEdit *m_localPathEdit = nullptr;

    QTableWidget *m_remoteTable = nullptr;
    QLineEdit *m_remotePathEdit = nullptr;
    QVector<transfer::SftpEntry> m_remoteEntries;

    QTableWidget *m_queueTable = nullptr;
    QHash<int, int> m_taskRow; // transfer id -> queue table row

    bool m_syncPending = false; // awaiting a recursive listing for Synchronize

    // Bookmarks (M20).
    QString m_host;             // owning host, for host-scoped bookmarks
    core::BookmarkStore m_bookmarks;
    QString m_bookmarksPath;    // JSON file backing the store
    QMenu *m_bookmarkMenu = nullptr;

    // Synchronized browsing (M20): mirror navigation between the two panes,
    // relative to the roots captured when it was enabled.
    bool m_syncBrowsing = false;
    QString m_syncLocalRoot;
    QString m_syncRemoteRoot;
};

} // namespace termsync::ui
