#include "home/HostsHomeWidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace termsync::ui {

namespace {

QString protocolName(core::Protocol p)
{
    switch (p) {
    case core::Protocol::SSH2: return QStringLiteral("ssh");
    case core::Protocol::SFTP_ONLY: return QStringLiteral("sftp");
    case core::Protocol::FTP: return QStringLiteral("ftp");
    case core::Protocol::FTPS: return QStringLiteral("ftps");
    case core::Protocol::TELNET: return QStringLiteral("telnet");
    case core::Protocol::RLOGIN: return QStringLiteral("rlogin");
    case core::Protocol::SERIAL: return QStringLiteral("serial");
    case core::Protocol::TN3270: return QStringLiteral("tn3270");
    case core::Protocol::TN5250: return QStringLiteral("tn5250");
    }
    return QStringLiteral("ssh");
}

// A rounded square "avatar" with the host's initial, tinted by a hash of the
// name so each host is visually distinct (like Termius's per-host icons).
QPixmap avatar(const QString &name, int size)
{
    static const QColor palette[] = {
        QColor(0xe9, 0x5b, 0x3a), QColor(0x2d, 0xd4, 0xbf),
        QColor(0x7a, 0xa2, 0xf7), QColor(0xbb, 0x9a, 0xf7),
        QColor(0x9e, 0xce, 0x6a), QColor(0xe0, 0xaf, 0x68),
        QColor(0xf7, 0x76, 0x8e), QColor(0x7d, 0xcf, 0xff)};
    uint h = 0;
    for (const QChar &c : name)
        h = h * 31 + c.unicode();
    const QColor c = palette[h % (sizeof(palette) / sizeof(palette[0]))];

    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(c);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(0, 0, size, size, size * 0.28, size * 0.28);

    p.setPen(QColor(0x10, 0x12, 0x18));
    QFont f = p.font();
    f.setPixelSize(int(size * 0.5));
    f.setBold(true);
    p.setFont(f);
    const QString initial =
        name.isEmpty() ? QStringLiteral("?") : name.left(1).toUpper();
    p.drawText(pm.rect(), Qt::AlignCenter, initial);
    return pm;
}

} // namespace

// ---------------------------------------------------------------------------
// HostCard
// ---------------------------------------------------------------------------
HostCard::HostCard(const core::ConnectionProfile &profile, QWidget *parent)
    : QFrame(parent), m_id(profile.id), m_protocol(profile.protocol)
{
    setObjectName(QStringLiteral("hostCard"));
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral(
        "#hostCard { background: #1f2130; border: 1px solid #2a2c3a;"
        " border-radius: 10px; }"
        "#hostCard:hover { background: #262a3b; border: 1px solid #2dd4bf; }"));

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(12, 10, 12, 10);
    row->setSpacing(12);

    auto *icon = new QLabel(this);
    icon->setPixmap(avatar(profile.name, 40));
    icon->setFixedSize(40, 40);
    row->addWidget(icon);

    auto *text = new QVBoxLayout;
    text->setSpacing(2);
    auto *name = new QLabel(profile.name.isEmpty() ? profile.host : profile.name,
                            this);
    name->setStyleSheet(QStringLiteral("color:#e6e9f2; font-size:11pt;"
                                       " font-weight:600; background:transparent;"));
    const QString sub = QStringLiteral("%1, %2").arg(
        protocolName(profile.protocol),
        profile.username.isEmpty() ? profile.host : profile.username);
    auto *subtitle = new QLabel(sub, this);
    subtitle->setStyleSheet(QStringLiteral(
        "color:#8a92b2; font-size:9pt; background:transparent;"));
    text->addWidget(name);
    text->addWidget(subtitle);
    row->addLayout(text);
    row->addStretch();
}

void HostCard::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->pos()))
        emit activated(m_id);
    QFrame::mouseReleaseEvent(event);
}

void HostCard::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit activated(m_id);
    QFrame::mouseDoubleClickEvent(event);
}

// ---------------------------------------------------------------------------
// HostsHomeWidget
// ---------------------------------------------------------------------------
HostsHomeWidget::HostsHomeWidget(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(28, 24, 28, 24);
    outer->setSpacing(16);

    // --- Connect bar ---
    auto *connectRow = new QHBoxLayout;
    connectRow->setSpacing(10);
    m_connectEdit = new QLineEdit(this);
    m_connectEdit->setPlaceholderText(
        tr("Find a host or  ssh user@hostname…"));
    m_connectEdit->setClearButtonEnabled(true);
    m_connectEdit->setMinimumHeight(40);
    auto *connectBtn = new QPushButton(tr("Connect"), this);
    connectBtn->setMinimumHeight(40);
    connectBtn->setMinimumWidth(110);
    connectBtn->setDefault(true);
    connectBtn->setCursor(Qt::PointingHandCursor);
    connectRow->addWidget(m_connectEdit, 1);
    connectRow->addWidget(connectBtn);
    outer->addLayout(connectRow);

    auto emitConnect = [this] {
        const QString t = m_connectEdit->text().trimmed();
        if (!t.isEmpty())
            emit quickConnectRequested(t);
    };
    connect(m_connectEdit, &QLineEdit::returnPressed, this, emitConnect);
    connect(connectBtn, &QPushButton::clicked, this, emitConnect);

    // --- Quick actions ---
    auto *actions = new QHBoxLayout;
    actions->setSpacing(10);
    auto *newHost = new QPushButton(tr("＋  New Host"), this);
    auto *terminal = new QPushButton(tr("Local Terminal"), this);
    for (QPushButton *b : {newHost, terminal}) {
        b->setMinimumHeight(34);
        b->setCursor(Qt::PointingHandCursor);
    }
    connect(newHost, &QPushButton::clicked, this,
            &HostsHomeWidget::newHostRequested);
    connect(terminal, &QPushButton::clicked, this,
            &HostsHomeWidget::localShellRequested);
    actions->addWidget(newHost);
    actions->addWidget(terminal);
    actions->addStretch();
    outer->addLayout(actions);

    // --- Hosts section header ---
    m_countLabel = new QLabel(tr("HOSTS"), this);
    m_countLabel->setStyleSheet(QStringLiteral(
        "color:#8a92b2; font-size:9pt; font-weight:700;"
        " letter-spacing:1px; background:transparent;"));
    outer->addWidget(m_countLabel);

    // --- Cards list ---
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(QStringLiteral("background:transparent;"));
    m_cardsContainer = new QWidget;
    m_cardsContainer->setStyleSheet(QStringLiteral("background:transparent;"));
    m_cardsLayout = new QVBoxLayout(m_cardsContainer);
    m_cardsLayout->setContentsMargins(0, 0, 0, 0);
    m_cardsLayout->setSpacing(8);
    m_cardsLayout->addStretch();
    scroll->setWidget(m_cardsContainer);
    outer->addWidget(scroll, 1);

    m_emptyHint = new QLabel(
        tr("No saved hosts yet.\nUse the connect bar above or “New Host”."), this);
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setStyleSheet(QStringLiteral(
        "color:#5a6078; font-size:11pt; background:transparent;"));
    m_cardsLayout->insertWidget(0, m_emptyHint);
}

void HostsHomeWidget::setProfiles(
    const QVector<core::ConnectionProfile> &profiles)
{
    m_profiles = profiles;
    rebuildCards();
}

void HostsHomeWidget::rebuildCards()
{
    // Remove existing cards (everything except the trailing stretch + hint).
    QLayoutItem *item = nullptr;
    while (m_cardsLayout->count() > 0) {
        item = m_cardsLayout->takeAt(0);
        if (QWidget *w = item->widget())
            if (w != m_emptyHint)
                w->deleteLater();
        delete item;
    }

    m_emptyHint->setVisible(m_profiles.isEmpty());
    m_cardsLayout->addWidget(m_emptyHint);

    for (const core::ConnectionProfile &p : m_profiles) {
        auto *card = new HostCard(p, m_cardsContainer);
        connect(card, &HostCard::activated, this,
                &HostsHomeWidget::hostActivated);
        connect(card, &HostCard::sftpRequested, this,
                &HostsHomeWidget::hostSftpRequested);
        m_cardsLayout->addWidget(card);
    }
    m_cardsLayout->addStretch();

    m_countLabel->setText(m_profiles.isEmpty()
                              ? tr("HOSTS")
                              : tr("HOSTS · %1").arg(m_profiles.size()));
}

} // namespace termsync::ui
