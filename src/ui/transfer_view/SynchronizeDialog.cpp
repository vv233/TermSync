#include "transfer_view/SynchronizeDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace termsync::ui {

using transfer::sync::ActionType;
using transfer::sync::Direction;
using transfer::sync::SyncAction;

SynchronizeDialog::SynchronizeDialog(const QString &localPath,
                                     const QString &remotePath, ComputeFn compute,
                                     ExecuteFn execute, QWidget *parent)
    : QDialog(parent)
    , m_compute(std::move(compute))
    , m_execute(std::move(execute))
{
    setWindowTitle(tr("Synchronize"));
    setModal(true);
    resize(640, 460);

    auto *form = new QFormLayout;
    form->addRow(tr("Local:"), new QLabel(localPath, this));
    form->addRow(tr("Remote:"), new QLabel(remotePath, this));

    m_direction = new QComboBox(this);
    m_direction->addItem(tr("Upload (local → remote)"),
                         static_cast<int>(Direction::LocalToRemote));
    m_direction->addItem(tr("Download (remote → local)"),
                         static_cast<int>(Direction::RemoteToLocal));
    m_direction->addItem(tr("Mirror (two-way)"),
                         static_cast<int>(Direction::TwoWay));
    form->addRow(tr("Direction:"), m_direction);

    m_table = new QTableWidget(0, 3, this);
    m_table->setHorizontalHeaderLabels({tr("Action"), tr("Path"), tr("Reason")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);

    m_summary = new QLabel(tr("Click Preview to compute changes."), this);

    auto *previewBtn = new QPushButton(tr("Preview"), this);
    connect(previewBtn, &QPushButton::clicked, this, &SynchronizeDialog::preview);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto *startBtn = buttons->addButton(tr("Start"), QDialogButtonBox::AcceptRole);
    startBtn->setEnabled(false);
    connect(buttons, &QDialogButtonBox::accepted, this, &SynchronizeDialog::start);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    // Re-preview whenever the direction changes.
    connect(m_direction, &QComboBox::currentIndexChanged, this,
            [this, startBtn] { startBtn->setEnabled(false); m_actions.clear(); });
    // Enable Start after a successful preview.
    connect(this, &QDialog::accepted, this, [] {});
    m_startButton = startBtn;

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(previewBtn);
    layout->addWidget(m_table, 1);
    layout->addWidget(m_summary);
    layout->addWidget(buttons);
}

void SynchronizeDialog::preview()
{
    const auto dir = static_cast<Direction>(m_direction->currentData().toInt());
    m_actions = m_compute(dir);

    int uploads = 0, downloads = 0, deletes = 0, conflicts = 0, changes = 0;
    m_table->setRowCount(0);
    for (const SyncAction &a : m_actions) {
        if (a.type == ActionType::Skip)
            continue;
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(
                                     QString::fromLatin1(transfer::sync::actionName(a.type))));
        m_table->setItem(row, 1, new QTableWidgetItem(a.relativePath));
        m_table->setItem(row, 2, new QTableWidgetItem(a.reason));
        switch (a.type) {
        case ActionType::Upload: ++uploads; ++changes; break;
        case ActionType::Download: ++downloads; ++changes; break;
        case ActionType::DeleteLocal:
        case ActionType::DeleteRemote: ++deletes; ++changes; break;
        case ActionType::Conflict: ++conflicts; break;
        default: break;
        }
    }
    m_summary->setText(tr("%1 upload(s), %2 download(s), %3 delete(s), %4 conflict(s)")
                           .arg(uploads).arg(downloads).arg(deletes).arg(conflicts));
    if (m_startButton)
        m_startButton->setEnabled(changes > 0);
}

void SynchronizeDialog::start()
{
    if (!m_actions.isEmpty())
        m_execute(m_actions);
    accept();
}

} // namespace termsync::ui
