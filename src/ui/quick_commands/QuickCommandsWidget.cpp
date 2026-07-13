#include "quick_commands/QuickCommandsWidget.h"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

namespace termsync::ui {

namespace {
constexpr int kCommandRole = Qt::UserRole + 1;
QString settingsKey() { return QStringLiteral("quickcommands/list"); }
} // namespace

QuickCommandsWidget::QuickCommandsWidget(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(8);

    auto *hint = new QLabel(tr("Double-click to run in the active terminal."), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#8a92b2; font-size:9pt;"));
    outer->addWidget(hint);

    m_list = new QListWidget(this);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setStyleSheet(QStringLiteral(
        "QListWidget { background:#14151d; border:1px solid #2a2c3a;"
        " border-radius:8px; }"
        "QListWidget::item { padding:8px 10px; border-radius:6px; margin:2px 4px;"
        " color:#e6e9f2; font-family:'Consolas','Cascadia Mono',monospace; }"
        "QListWidget::item:hover { background:#23252f; }"
        "QListWidget::item:selected { background:#2dd4bf; color:#101218; }"));
    connect(m_list, &QListWidget::itemActivated, this,
            &QuickCommandsWidget::onItemActivated);
    connect(m_list, &QListWidget::itemDoubleClicked, this,
            &QuickCommandsWidget::onItemActivated);
    connect(m_list, &QWidget::customContextMenuRequested, this,
            &QuickCommandsWidget::showContextMenu);
    outer->addWidget(m_list, 1);

    // Add row: type a command and press Enter / the + button.
    auto *addRow = new QHBoxLayout;
    addRow->setSpacing(6);
    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(tr("Add a command…"));
    m_input->setClearButtonEnabled(true);
    connect(m_input, &QLineEdit::returnPressed, this,
            &QuickCommandsWidget::addFromInput);
    auto *addBtn = new QToolButton(this);
    addBtn->setText(QStringLiteral("＋"));
    addBtn->setToolTip(tr("Add command"));
    addBtn->setStyleSheet(QStringLiteral(
        "QToolButton { padding:4px 12px; border-radius:6px; background:#262a3b;"
        " color:#e6e9f2; font-size:13pt; }"
        "QToolButton:hover { background:#2dd4bf; color:#101218; }"));
    connect(addBtn, &QToolButton::clicked, this, &QuickCommandsWidget::addFromInput);
    addRow->addWidget(m_input, 1);
    addRow->addWidget(addBtn);
    outer->addLayout(addRow);

    load();
}

void QuickCommandsWidget::addFromInput()
{
    const QString cmd = m_input->text().trimmed();
    if (cmd.isEmpty())
        return;
    addCommand(cmd);
    m_input->clear();
}

void QuickCommandsWidget::addCommand(const QString &command, bool persist)
{
    auto *item = new QListWidgetItem(command, m_list);
    item->setData(kCommandRole, command);
    item->setToolTip(command);
    if (persist)
        save();
}

void QuickCommandsWidget::onItemActivated(QListWidgetItem *item)
{
    if (item)
        emit runCommand(item->data(kCommandRole).toString(), /*execute=*/true);
}

void QuickCommandsWidget::showContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_list->itemAt(pos);
    QMenu menu(this);
    if (item) {
        const QString cmd = item->data(kCommandRole).toString();
        menu.addAction(tr("Run"), this,
                       [this, cmd] { emit runCommand(cmd, true); });
        menu.addAction(tr("Type (no Enter)"), this,
                       [this, cmd] { emit runCommand(cmd, false); });
        menu.addSeparator();
        menu.addAction(tr("Edit…"), this, [this, item] {
            bool ok = false;
            const QString edited = QInputDialog::getText(
                this, tr("Edit command"), tr("Command:"), QLineEdit::Normal,
                item->data(kCommandRole).toString(), &ok);
            if (ok && !edited.trimmed().isEmpty()) {
                const QString c = edited.trimmed();
                item->setText(c);
                item->setData(kCommandRole, c);
                item->setToolTip(c);
                save();
            }
        });
        menu.addAction(tr("Delete"), this, [this, item] {
            delete item;
            save();
        });
    } else {
        menu.addAction(tr("Add command…"), this, [this] { m_input->setFocus(); });
    }
    menu.exec(m_list->viewport()->mapToGlobal(pos));
}

void QuickCommandsWidget::load()
{
    QSettings settings(QStringLiteral("TermSync"), QStringLiteral("TermSync"));
    QStringList cmds = settings.value(settingsKey()).toStringList();
    if (cmds.isEmpty() && !settings.contains(settingsKey())) {
        // Seed a few useful examples on first run.
        cmds = {QStringLiteral("ls -la"), QStringLiteral("df -h"),
                QStringLiteral("free -h"), QStringLiteral("tail -f /var/log/syslog")};
    }
    for (const QString &c : cmds)
        addCommand(c, /*persist=*/false);
}

void QuickCommandsWidget::save()
{
    QStringList cmds;
    for (int i = 0; i < m_list->count(); ++i)
        cmds << m_list->item(i)->data(kCommandRole).toString();
    QSettings settings(QStringLiteral("TermSync"), QStringLiteral("TermSync"));
    settings.setValue(settingsKey(), cmds);
}

} // namespace termsync::ui
