#pragma once

#include <QHash>
#include <QIcon>
#include <QSet>
#include <QStringList>
#include <QWidget>

#include "model/ConnectionProfile.h"
#include "queue/SftpSession.h"
#include "ssh/SshConnection.h"

class QLabel;
class QLineEdit;
class QProgressBar;
class QProgressDialog;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QToolButton;
class QListWidget;
class QListWidgetItem;
class QHBoxLayout;

namespace termsync::ui {

// A single-pane remote file browser styled after the Windows 11 File Explorer:
// a navigation bar (back/forward/up/refresh + clickable breadcrumb + search), a
// command bar (New folder / Upload / Download / Rename / Delete / View), a left
// navigation pane, and a details list (Name / Date modified / Type / Size).
// Shares the same transfer::SftpSession backend as DualPaneBrowser and exposes
// the same signals, so MainWindow can use either interchangeably. The classic
// dual-pane view remains available via the SFTP browser-style setting.
class ExplorerSftpBrowser : public QWidget
{
    Q_OBJECT

public:
    ExplorerSftpBrowser(const core::SshConnectionParams &params,
                        const QString &expectedFingerprint,
                        core::Protocol protocol = core::Protocol::SSH2,
                        QWidget *parent = nullptr);

signals:
    void statusMessage(const QString &message);
    void hostKeyFingerprintReceived(const QString &fingerprint);
    // Detected remote OS id (for the host icon), forwarded from the session.
    void osDetected(const QString &osId);

private slots:
    void onConnected();
    void onDirectoryListed(const QString &path,
                           const QVector<transfer::SftpEntry> &entries);
    void onOperationFinished(const QString &op, bool ok, const QString &message);
    void onTransferQueued(const transfer::TransferItem &item);
    void onTransferProgress(int id, quint64 done, quint64 total);
    void onTransferFinished(int id, bool ok, const QString &message);
    void onSyncListingReady(const QString &root,
                            const transfer::sync::Listing &listing, bool ok);
    // Privilege escalation: toggle prompts for the sudo password; the result
    // updates the button and refreshes the listing.
    void toggleSudo(bool on);
    void onSudoModeChanged(bool enabled, bool ok, const QString &message);

    void onItemActivated(int row, int column);
    void onIconActivated(QListWidgetItem *item);
    void onSelectionChanged();
    void showContextMenu(const QPoint &pos);

    void goBack();
    void goForward();
    void goUp();
    void refresh();
    void setViewMode(int mode);
    void setSort(int column, Qt::SortOrder order);
    void newFolder();
    void uploadFiles();
    void downloadSelected();
    void renameSelected();
    void deleteSelected();
    void copySelectionToClipboard();
    void pasteFromClipboard();
    // Drag the selected remote files out to Windows Explorer: download them to a
    // temp folder (with a progress dialog), then start an OS drag of real files.
    void startDragOut();

protected:
    // Drag local files in (upload). Remote-out drag is started by the table.
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    QWidget *buildNavBar();
    QWidget *buildCommandBar();
    QWidget *buildNavPane();
    QWidget *buildFileView();
    QWidget *buildStatusStrip();

    void navigateTo(const QString &path, bool pushHistory = true);
    void requestList(const QString &path);
    void populate();
    void rebuildBreadcrumb();
    void updateCommandState();
    QString remoteJoin(const QString &dir, const QString &name) const;
    QString parentOf(const QString &dir) const;
    QVector<transfer::SftpEntry> selectedEntries() const;
    QIcon iconFor(const transfer::SftpEntry &e) const;

    // Upload every local URL (files uploaded; folders created + recursed) into
    // the current remote directory.
    void uploadUrls(const QList<QUrl> &urls);
    void uploadLocalEntry(const QString &localPath, const QString &remoteDir);
    // Downloads the selected remote items into a fresh temp folder; `onReady` is
    // called with the local paths once everything has arrived (for clipboard /
    // drag-out to Windows Explorer, which needs real files). Folders come down as
    // a single tar stream. When `showProgress` is set a modeless progress dialog
    // is shown (used by copy; drag-out drives its own blocking dialog).
    void downloadSelectedToTemp(std::function<void(const QStringList &)> onReady,
                                bool showProgress = false);
    // Blocking variant used by drag-out: fetches the selection to temp (pumping
    // events) and returns the local paths, or empty on cancel.
    QStringList prepareSelectionToTemp();
    void maybeFinishTemp();

    transfer::SftpSession *m_session = nullptr;
    QString m_path = QStringLiteral(".");
    QVector<transfer::SftpEntry> m_entries;

    // Navigation history (back/forward).
    QStringList m_history;
    int m_histPos = -1;

    // Nav bar / breadcrumb.
    QToolButton *m_back = nullptr;
    QToolButton *m_forward = nullptr;
    QToolButton *m_up = nullptr;
    QWidget *m_crumbBar = nullptr;
    QHBoxLayout *m_crumbLayout = nullptr;
    QLineEdit *m_addressEdit = nullptr;
    QStackedWidget *m_crumbStack = nullptr;
    QLineEdit *m_search = nullptr;

    // Command bar buttons that depend on the selection.
    QToolButton *m_downloadBtn = nullptr;
    QToolButton *m_renameBtn = nullptr;
    QToolButton *m_deleteBtn = nullptr;
    QToolButton *m_sudoBtn = nullptr; // checkable: route ops through sudo

    // View modes (match the Windows 11 Explorer "View" menu).
    enum ViewMode {
        ExtraLargeIcons,
        LargeIcons,
        MediumIcons,
        SmallIcons,
        ListMode,
        Details,
    };

    // Views: a details table and an icon/list view, switched via a stack.
    QStackedWidget *m_viewStack = nullptr;
    QTableWidget *m_table = nullptr;
    QListWidget *m_iconView = nullptr;
    QListWidget *m_navList = nullptr;
    int m_viewMode = Details;

    // Sort state (column 0=Name,1=Date,2=Type,3=Size), folders always first.
    int m_sortColumn = 0;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;

    // Status strip.
    QLabel *m_countLabel = nullptr;
    QLabel *m_xferLabel = nullptr;
    QProgressBar *m_xferBar = nullptr;

    QHash<int, transfer::TransferItem> m_activeXfers;

    // Cached shell icons (by file-type suffix) — QFileIconProvider is slow.
    mutable QHash<QString, QIcon> m_iconCache;
    mutable QIcon m_folderIcon;

    // Pending "download to temp" batch (for copy-out / drag-out to Explorer).
    // m_tempPaths holds the top-level temp paths (the selected files/folders);
    // m_tempBatch tracks the in-flight transfers (per-file downloads + whole-dir
    // tar bundles) and m_tempPendingDirs the recursive listings still awaited by
    // the per-file fallback used when the remote lacks tar.
    QSet<int> m_tempBatch;
    QHash<int, QString> m_tempBulkDirs; // bulk-dir transfer id -> remote dir
    QStringList m_tempPaths;
    QString m_tempBaseDir;
    int m_tempPendingDirs = 0;
    std::function<void(const QStringList &)> m_tempOnReady;
    QProgressDialog *m_tempProgress = nullptr;
    quint64 m_tempBytes = 0; // bytes fetched so far (for the progress label)
};

} // namespace termsync::ui
