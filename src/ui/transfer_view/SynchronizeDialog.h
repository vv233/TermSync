#pragma once

#include <QDialog>
#include <QVector>
#include <functional>

#include "sync/SyncTypes.h"

class QComboBox;
class QLabel;
class QTableWidget;

namespace termsync::ui {

// SecureFX-style Synchronize window: pick a direction, Preview shows the exact
// dry-run action list (uploads/downloads/deletes), then Start executes it.
// The heavy lifting (diff + execution) is supplied by the owner as callbacks so
// the dialog stays UI-only.
class SynchronizeDialog : public QDialog
{
    Q_OBJECT

public:
    using ComputeFn =
        std::function<QVector<transfer::sync::SyncAction>(transfer::sync::Direction)>;
    using ExecuteFn =
        std::function<void(const QVector<transfer::sync::SyncAction> &)>;

    SynchronizeDialog(const QString &localPath, const QString &remotePath,
                      ComputeFn compute, ExecuteFn execute,
                      QWidget *parent = nullptr);

private slots:
    void preview();
    void start();

private:
    ComputeFn m_compute;
    ExecuteFn m_execute;

    QComboBox *m_direction = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_summary = nullptr;
    class QPushButton *m_startButton = nullptr;
    QVector<transfer::sync::SyncAction> m_actions;
};

} // namespace termsync::ui
