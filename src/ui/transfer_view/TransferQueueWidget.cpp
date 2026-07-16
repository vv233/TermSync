#include "transfer_view/TransferQueueWidget.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QList>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace termsync::ui {

using transfer::SftpSession;
using transfer::TransferItem;

namespace {
constexpr int kColName = 0;
constexpr int kColSize = 1;
constexpr int kColProgress = 2;
constexpr int kColSpeed = 3;
constexpr int kColEta = 4;
constexpr int kColStatus = 5;
} // namespace

TransferQueueWidget::TransferQueueWidget(QWidget *parent) : QWidget(parent)
{
    m_table = new QTableWidget(0, 6, this);
    m_table->setHorizontalHeaderLabels({tr("Name"), tr("Size"), tr("Progress"),
                                        tr("Speed"), tr("Time left"),
                                        tr("Status")});
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(kColName, 240);
    m_table->setColumnWidth(kColProgress, 160);

    m_summary = new QLabel(tr("No transfers"), this);
    auto *clearBtn = new QPushButton(tr("Clear finished"), this);
    connect(clearBtn, &QPushButton::clicked, this,
            &TransferQueueWidget::clearFinished);

    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(6, 4, 6, 4);
    bar->addWidget(m_summary, 1);
    bar->addWidget(clearBtn);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(m_table, 1);
    root->addLayout(bar);
}

void TransferQueueWidget::attachSession(SftpSession *session, const QString &label)
{
    if (!session)
        return;
    connect(session, &SftpSession::transferQueued, this,
            [this, session, label](const TransferItem &item) {
                onQueued(session, label, item);
            });
    connect(session, &SftpSession::transferProgress, this,
            [this, session](int id, quint64 done, quint64 total) {
                onProgress(session, id, done, total);
            });
    connect(session, &SftpSession::transferFinished, this,
            [this, session](int id, bool ok, const QString &msg) {
                onFinished(session, id, ok, msg);
            });
    // Drop live rows if the session goes away (its tab was closed), so a reused
    // pointer can't alias an old transfer.
    connect(session, &QObject::destroyed, this,
            [this, session] { forgetSession(session); });
}

void TransferQueueWidget::onQueued(SftpSession *s, const QString &label,
                                   const TransferItem &item)
{
    const int r = m_table->rowCount();
    m_table->insertRow(r);

    const QChar arrow = item.direction == TransferItem::Upload ? u'↑' : u'↓';
    const QString name = QStringLiteral("%1 %2").arg(arrow).arg(item.displayName);
    m_table->setItem(r, kColName, new QTableWidgetItem(name));
    auto *nameItem = m_table->item(r, kColName);
    nameItem->setToolTip(label.isEmpty() ? item.displayName : label);

    m_table->setItem(r, kColSize, new QTableWidgetItem(humanBytes(item.size)));

    auto *pbar = new QProgressBar(m_table);
    pbar->setRange(0, 100);
    pbar->setValue(0);
    pbar->setTextVisible(true);
    m_table->setCellWidget(r, kColProgress, pbar);

    m_table->setItem(r, kColSpeed, new QTableWidgetItem(QString()));
    m_table->setItem(r, kColEta, new QTableWidgetItem(QString()));
    m_table->setItem(r, kColStatus, new QTableWidgetItem(tr("Queued")));

    Row row;
    row.tableRow = r;
    row.total = item.size;
    m_rows.insert(qMakePair(s, item.id), row);
    updateSummary();
    emit transferActivity();
}

void TransferQueueWidget::onProgress(SftpSession *s, int id, quint64 done,
                                     quint64 total)
{
    auto it = m_rows.find(qMakePair(s, id));
    if (it == m_rows.end())
        return;
    Row &row = it.value();
    if (total > 0)
        row.total = total;

    // Instantaneous rate, exponentially smoothed to steady the display.
    if (!row.started) {
        row.started = true;
        row.clock.start();
        row.lastBytes = done;
    } else {
        const double dt = row.clock.restart() / 1000.0;
        if (dt > 0.0) {
            const double inst =
                double(done > row.lastBytes ? done - row.lastBytes : 0) / dt;
            row.emaSpeed = row.emaSpeed > 0.0 ? 0.35 * inst + 0.65 * row.emaSpeed
                                              : inst;
            row.lastBytes = done;
        }
    }

    if (auto *pbar = qobject_cast<QProgressBar *>(
            m_table->cellWidget(row.tableRow, kColProgress))) {
        const int pct =
            row.total > 0 ? int(done * 100 / row.total) : 0;
        pbar->setValue(qBound(0, pct, 100));
    }
    if (auto *speed = m_table->item(row.tableRow, kColSpeed))
        speed->setText(row.emaSpeed > 1.0
                           ? tr("%1/s").arg(humanBytes(row.emaSpeed))
                           : QString());
    if (auto *eta = m_table->item(row.tableRow, kColEta)) {
        if (row.emaSpeed > 1.0 && row.total > done) {
            const double secs = double(row.total - done) / row.emaSpeed;
            const int s = int(std::ceil(secs));
            eta->setText(s >= 60 ? tr("%1m %2s").arg(s / 60).arg(s % 60)
                                 : tr("%1s").arg(s));
        } else {
            eta->setText(QString());
        }
    }
    if (auto *status = m_table->item(row.tableRow, kColStatus))
        status->setText(tr("Transferring"));
}

void TransferQueueWidget::onFinished(SftpSession *s, int id, bool ok,
                                     const QString &msg)
{
    auto it = m_rows.find(qMakePair(s, id));
    if (it == m_rows.end())
        return;
    const Row row = it.value();
    if (auto *pbar = qobject_cast<QProgressBar *>(
            m_table->cellWidget(row.tableRow, kColProgress))) {
        if (ok)
            pbar->setValue(100);
    }
    if (auto *speed = m_table->item(row.tableRow, kColSpeed))
        speed->setText(QString());
    if (auto *eta = m_table->item(row.tableRow, kColEta))
        eta->setText(QString());
    if (auto *status = m_table->item(row.tableRow, kColStatus))
        status->setText(ok ? tr("Done")
                           : (msg.isEmpty() ? tr("Failed")
                                            : tr("Failed: %1").arg(msg)));
    m_rows.erase(it);
    updateSummary();
}

void TransferQueueWidget::forgetSession(SftpSession *s)
{
    for (auto it = m_rows.begin(); it != m_rows.end();) {
        if (it.key().first == s) {
            if (auto *status = m_table->item(it.value().tableRow, kColStatus))
                status->setText(tr("Disconnected"));
            it = m_rows.erase(it);
        } else {
            ++it;
        }
    }
    updateSummary();
}

void TransferQueueWidget::clearFinished()
{
    // Live rows (still tracked in m_rows) stay; everything else is finished or
    // disconnected and gets removed. Removing shifts indices, so recompute each
    // survivor's row afterwards from the remaining table order.
    QSet<int> liveRows;
    for (auto it = m_rows.cbegin(); it != m_rows.cend(); ++it)
        liveRows.insert(it.value().tableRow);

    for (int r = m_table->rowCount() - 1; r >= 0; --r) {
        if (!liveRows.contains(r))
            m_table->removeRow(r);
    }

    // Survivors keep their relative order; reassign compacted indices 0,1,2,…
    QList<QPair<SftpSession *, int>> keys(m_rows.keyBegin(), m_rows.keyEnd());
    std::sort(keys.begin(), keys.end(),
              [this](const auto &a, const auto &b) {
                  return m_rows[a].tableRow < m_rows[b].tableRow;
              });
    int newRow = 0;
    for (const auto &key : keys)
        m_rows[key].tableRow = newRow++;
    updateSummary();
}

void TransferQueueWidget::updateSummary()
{
    const int active = m_rows.size();
    const int total = m_table->rowCount();
    if (total == 0)
        m_summary->setText(tr("No transfers"));
    else
        m_summary->setText(tr("%1 active / %2 shown").arg(active).arg(total));
}

QString TransferQueueWidget::humanBytes(double bytes)
{
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int u = 0;
    while (bytes >= 1024.0 && u < 4) {
        bytes /= 1024.0;
        ++u;
    }
    return QStringLiteral("%1 %2")
        .arg(bytes, 0, 'f', (u == 0 ? 0 : 1))
        .arg(QLatin1String(units[u]));
}

} // namespace termsync::ui
