#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QWidget>

#include "queue/SftpSession.h"

class QTableWidget;
class QLabel;

namespace termsync::ui {

// A global, SecureFX-style Transfer Queue panel. It watches one or more
// transfer::SftpSession backends (attached as SFTP sessions open) and shows one
// row per transfer with a live progress bar, transfer rate, and ETA. Rows are
// keyed by (session, id) so ids from different sessions never collide.
class TransferQueueWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TransferQueueWidget(QWidget *parent = nullptr);

    // Start tracking a session's transfers. `label` names the session (host) in
    // the panel. Safe to call once per opened SFTP browser.
    void attachSession(transfer::SftpSession *session, const QString &label);

signals:
    // Emitted when a new transfer is queued, so the host can reveal the panel.
    void transferActivity();

private:
    struct Row
    {
        int tableRow = 0;
        quint64 total = 0;
        quint64 lastBytes = 0;
        double emaSpeed = 0.0; // bytes/sec, exponentially smoothed
        bool started = false;
        QElapsedTimer clock;
    };

    void onQueued(transfer::SftpSession *s, const QString &label,
                  const transfer::TransferItem &item);
    void onProgress(transfer::SftpSession *s, int id, quint64 done, quint64 total);
    void onFinished(transfer::SftpSession *s, int id, bool ok, const QString &msg);
    void forgetSession(transfer::SftpSession *s);
    void clearFinished();
    void updateSummary();

    static QString humanBytes(double bytes);

    QTableWidget *m_table = nullptr;
    QLabel *m_summary = nullptr;

    // Key = session pointer combined with the per-session transfer id.
    QHash<QPair<transfer::SftpSession *, int>, Row> m_rows;
};

} // namespace termsync::ui
