#pragma once

#include <QHash>
#include <QIcon>
#include <QStringList>
#include <QWidget>

#include "model/ConnectionProfile.h"
#include "queue/SftpSession.h"
#include "ssh/SshConnection.h"

class QLabel;
class QLineEdit;
class QProgressBar;
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

private slots:
    void onConnected();
    void onDirectoryListed(const QString &path,
                           const QVector<transfer::SftpEntry> &entries);
    void onOperationFinished(const QString &op, bool ok, const QString &message);
    void onTransferQueued(const transfer::TransferItem &item);
    void onTransferProgress(int id, quint64 done, quint64 total);
    void onTransferFinished(int id, bool ok, const QString &message);

    void onItemActivated(int row, int column);
    void onSelectionChanged();
    void showContextMenu(const QPoint &pos);

    void goBack();
    void goForward();
    void goUp();
    void refresh();
    void newFolder();
    void uploadFiles();
    void downloadSelected();
    void renameSelected();
    void deleteSelected();

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

    // Views.
    QTableWidget *m_table = nullptr;
    QListWidget *m_navList = nullptr;

    // Status strip.
    QLabel *m_countLabel = nullptr;
    QLabel *m_xferLabel = nullptr;
    QProgressBar *m_xferBar = nullptr;

    QHash<int, transfer::TransferItem> m_activeXfers;
};

} // namespace termsync::ui
