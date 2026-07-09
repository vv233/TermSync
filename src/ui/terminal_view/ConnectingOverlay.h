#pragma once

#include <QWidget>

class QLabel;
class QProgressBar;

namespace termsync::ui {

// A Termius-style "connecting…" overlay shown over a terminal while its session
// establishes: host avatar + name + endpoint, an indeterminate progress bar,
// and a "Show logs" button that dismisses the overlay to reveal the raw terminal
// (where the handshake/auth text is printed). The owning TerminalWidget keeps it
// sized to fill the view and removes it once connected (or on "Show logs").
class ConnectingOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit ConnectingOverlay(QWidget *parent = nullptr);

    void setTitle(const QString &title);
    void setSubtitle(const QString &subtitle);
    void setFailed(const QString &message);

signals:
    void dismissed(); // "Show logs" clicked

private:
    void setAvatar(const QString &name);

    QLabel *m_avatar = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_subtitle = nullptr;
    QProgressBar *m_progress = nullptr;
};

} // namespace termsync::ui
