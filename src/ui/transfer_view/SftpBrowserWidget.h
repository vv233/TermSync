#pragma once

#include <QWidget>
#include <functional>

#include "sftp/SftpFileEngine.h"
#include "ssh/SshConnection.h"

class QLineEdit;
class QTableWidget;

namespace termsync::ui {

class SftpBrowserWidget : public QWidget
{
    Q_OBJECT

public:
    using HostKeyVerifier = std::function<bool(const QString &fingerprint)>;

    explicit SftpBrowserWidget(const core::SshConnectionParams &params,
                               HostKeyVerifier verifier,
                               QWidget *parent = nullptr);

signals:
    void statusMessage(const QString &message);

private:
    void refresh();
    void uploadFile();
    void downloadSelectedFile();
    void showEntries(const QString &path,
                     const QVector<transfer::SftpEntry> &entries);
    bool verifyHostKeyOnGuiThread(const QString &fingerprint);
    QString selectedRemotePath() const;
    QString joinRemote(const QString &dir, const QString &name) const;
    void setBusy(bool busy);

    core::SshConnectionParams m_params;
    HostKeyVerifier m_verifier;
    QLineEdit *m_path = nullptr;
    QTableWidget *m_table = nullptr;
};

} // namespace termsync::ui
