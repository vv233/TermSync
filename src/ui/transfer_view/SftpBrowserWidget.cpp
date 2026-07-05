#include "transfer_view/SftpBrowserWidget.h"

#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLineEdit>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QThread>
#include <QVBoxLayout>

namespace termsync::ui {

namespace {
constexpr int kNameRole = Qt::UserRole + 1;
constexpr int kIsDirRole = Qt::UserRole + 2;
}

SftpBrowserWidget::SftpBrowserWidget(const core::SshConnectionParams &params,
                                     HostKeyVerifier verifier,
                                     QWidget *parent)
    : QWidget(parent)
    , m_params(params)
    , m_verifier(std::move(verifier))
{
    auto *layout = new QVBoxLayout(this);
    auto *bar = new QHBoxLayout;

    m_path = new QLineEdit(QStringLiteral("."), this);
    auto *refreshButton = new QPushButton(tr("Refresh"), this);
    auto *uploadButton = new QPushButton(tr("Upload"), this);
    auto *downloadButton = new QPushButton(tr("Download"), this);

    bar->addWidget(m_path, 1);
    bar->addWidget(refreshButton);
    bar->addWidget(uploadButton);
    bar->addWidget(downloadButton);
    layout->addLayout(bar);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels(
        {tr("Name"), tr("Size"), tr("Modified"), tr("Attributes")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_table, 1);

    connect(refreshButton, &QPushButton::clicked, this, &SftpBrowserWidget::refresh);
    connect(uploadButton, &QPushButton::clicked, this, &SftpBrowserWidget::uploadFile);
    connect(downloadButton, &QPushButton::clicked, this, &SftpBrowserWidget::downloadSelectedFile);
    connect(m_path, &QLineEdit::returnPressed, this, &SftpBrowserWidget::refresh);
    connect(m_table, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int) {
                auto *item = m_table->item(row, 0);
                if (!item || !item->data(kIsDirRole).toBool())
                    return;
                const QString name = item->data(kNameRole).toString();
                if (name == ".")
                    return;
                if (name == "..") {
                    const QString path = m_path->text();
                    const int slash = path.lastIndexOf('/');
                    m_path->setText(slash > 0 ? path.left(slash) : QStringLiteral("."));
                } else {
                    m_path->setText(joinRemote(m_path->text(), name));
                }
                refresh();
            });

    refresh();
}

void SftpBrowserWidget::refresh()
{
    const QString path = m_path->text().trimmed().isEmpty()
                             ? QStringLiteral(".")
                             : m_path->text().trimmed();
    setBusy(true);
    emit statusMessage(tr("Listing %1...").arg(path));

    QPointer<SftpBrowserWidget> guard(this);
    QThread *thread = QThread::create([guard, params = m_params, path] {
        transfer::SftpFileEngine engine;
        QVector<transfer::SftpEntry> entries;
        QString error;
        bool ok = engine.connectToHost(params, [guard](const QString &fp) {
            return guard ? guard->verifyHostKeyOnGuiThread(fp) : false;
        });
        if (ok)
            ok = engine.listDirectory(path, &entries);
        if (!ok)
            error = engine.lastError();

        if (guard) {
            QMetaObject::invokeMethod(guard, [guard, path, entries, error] {
                if (!guard)
                    return;
                guard->setBusy(false);
                if (!error.isEmpty()) {
                    emit guard->statusMessage(error);
                    return;
                }
                guard->showEntries(path, entries);
            }, Qt::QueuedConnection);
        }
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void SftpBrowserWidget::uploadFile()
{
    const QString local = QFileDialog::getOpenFileName(this, tr("Upload File"));
    if (local.isEmpty())
        return;

    const QString remote = joinRemote(m_path->text(), QFileInfo(local).fileName());
    setBusy(true);
    emit statusMessage(tr("Uploading %1...").arg(remote));

    QPointer<SftpBrowserWidget> guard(this);
    QThread *thread = QThread::create([guard, params = m_params, local, remote] {
        transfer::SftpFileEngine engine;
        QString error;
        bool ok = engine.connectToHost(params, [guard](const QString &fp) {
            return guard ? guard->verifyHostKeyOnGuiThread(fp) : false;
        });
        if (ok)
            ok = engine.uploadFile(local, remote);
        if (!ok)
            error = engine.lastError();

        if (guard) {
            QMetaObject::invokeMethod(guard, [guard, remote, error] {
                if (!guard)
                    return;
                guard->setBusy(false);
                emit guard->statusMessage(error.isEmpty()
                                              ? tr("Uploaded %1").arg(remote)
                                              : error);
                if (error.isEmpty())
                    guard->refresh();
            }, Qt::QueuedConnection);
        }
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void SftpBrowserWidget::downloadSelectedFile()
{
    const QString remote = selectedRemotePath();
    if (remote.isEmpty())
        return;

    const QString local = QFileDialog::getSaveFileName(
        this, tr("Download File"), QFileInfo(remote).fileName());
    if (local.isEmpty())
        return;

    setBusy(true);
    emit statusMessage(tr("Downloading %1...").arg(remote));

    QPointer<SftpBrowserWidget> guard(this);
    QThread *thread = QThread::create([guard, params = m_params, remote, local] {
        transfer::SftpFileEngine engine;
        QString error;
        bool ok = engine.connectToHost(params, [guard](const QString &fp) {
            return guard ? guard->verifyHostKeyOnGuiThread(fp) : false;
        });
        if (ok)
            ok = engine.downloadFile(remote, local);
        if (!ok)
            error = engine.lastError();

        if (guard) {
            QMetaObject::invokeMethod(guard, [guard, remote, error] {
                if (!guard)
                    return;
                guard->setBusy(false);
                emit guard->statusMessage(error.isEmpty()
                                              ? tr("Downloaded %1").arg(remote)
                                              : error);
            }, Qt::QueuedConnection);
        }
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void SftpBrowserWidget::showEntries(const QString &path,
                                    const QVector<transfer::SftpEntry> &entries)
{
    m_path->setText(path);
    m_table->setRowCount(entries.size());
    for (int row = 0; row < entries.size(); ++row) {
        const auto &e = entries[row];
        auto *name = new QTableWidgetItem((e.isDirectory ? QStringLiteral("[") + e.name + ']' : e.name));
        name->setData(kNameRole, e.name);
        name->setData(kIsDirRole, e.isDirectory);
        m_table->setItem(row, 0, name);
        m_table->setItem(row, 1, new QTableWidgetItem(e.isDirectory ? QString() : QString::number(e.size)));
        m_table->setItem(row, 2, new QTableWidgetItem(e.modifiedAt.isValid()
                                                          ? e.modifiedAt.toString(Qt::ISODate)
                                                          : QString()));
        m_table->setItem(row, 3, new QTableWidgetItem(QString::number(e.permissions, 8)));
    }
    m_table->resizeColumnsToContents();
    emit statusMessage(tr("Listed %1 (%2 entries)").arg(path).arg(entries.size()));
}

bool SftpBrowserWidget::verifyHostKeyOnGuiThread(const QString &fingerprint)
{
    bool accepted = false;
    QMetaObject::invokeMethod(this, [this, fingerprint, &accepted] {
        accepted = m_verifier ? m_verifier(fingerprint) : true;
    }, Qt::BlockingQueuedConnection);
    return accepted;
}

QString SftpBrowserWidget::selectedRemotePath() const
{
    const auto items = m_table->selectedItems();
    if (items.isEmpty())
        return {};
    auto *nameItem = m_table->item(items.first()->row(), 0);
    if (!nameItem || nameItem->data(kIsDirRole).toBool())
        return {};
    return joinRemote(m_path->text(), nameItem->data(kNameRole).toString());
}

QString SftpBrowserWidget::joinRemote(const QString &dir, const QString &name) const
{
    if (dir.isEmpty() || dir == ".")
        return name;
    if (dir.endsWith('/'))
        return dir + name;
    return dir + '/' + name;
}

void SftpBrowserWidget::setBusy(bool busy)
{
    setEnabled(!busy);
}

} // namespace termsync::ui
