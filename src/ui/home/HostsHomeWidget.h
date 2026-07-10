#pragma once

#include <QFrame>
#include <QVector>
#include <QWidget>

#include "model/ConnectionProfile.h"

class QLabel;
class QLineEdit;
class QVBoxLayout;

namespace termsync::ui {

// A single clickable host "card" (icon avatar + name + "protocol, user").
class HostCard : public QFrame
{
    Q_OBJECT

public:
    explicit HostCard(const core::ConnectionProfile &profile,
                      QWidget *parent = nullptr);

signals:
    void activated(const QString &profileId);
    void sftpRequested(const QString &profileId);
    void editRequested(const QString &profileId);
    void deleteRequested(const QString &profileId);

protected:
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    QString m_id;
    core::Protocol m_protocol;
};

// Termius-style home page: a prominent connect bar, quick-action buttons, and a
// scrollable list of saved-host cards. Emits intents; MainWindow wires them to
// the actual connect/new-host actions.
class HostsHomeWidget : public QWidget
{
    Q_OBJECT

public:
    explicit HostsHomeWidget(QWidget *parent = nullptr);

    void setProfiles(const QVector<core::ConnectionProfile> &profiles);

signals:
    void newHostRequested();
    void localShellRequested();
    void quickConnectRequested(const QString &text);
    void hostActivated(const QString &profileId);
    void hostSftpRequested(const QString &profileId);
    void hostEditRequested(const QString &profileId);
    void hostDeleteRequested(const QString &profileId);

private:
    void rebuildCards();

    QLineEdit *m_connectEdit = nullptr;
    QWidget *m_cardsContainer = nullptr;
    QVBoxLayout *m_cardsLayout = nullptr;
    QLabel *m_emptyHint = nullptr;
    QLabel *m_countLabel = nullptr;
    QVector<core::ConnectionProfile> m_profiles;
};

} // namespace termsync::ui
