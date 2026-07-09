#include "terminal_view/ConnectingOverlay.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace termsync::ui {

namespace {
QPixmap makeAvatar(const QString &name, int size)
{
    static const QColor palette[] = {
        QColor(0xe9, 0x5b, 0x3a), QColor(0x2d, 0xd4, 0xbf),
        QColor(0x7a, 0xa2, 0xf7), QColor(0xbb, 0x9a, 0xf7),
        QColor(0x9e, 0xce, 0x6a), QColor(0xe0, 0xaf, 0x68)};
    uint h = 0;
    for (const QChar &c : name)
        h = h * 31 + c.unicode();
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(palette[h % (sizeof(palette) / sizeof(palette[0]))]);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(0, 0, size, size, size * 0.28, size * 0.28);
    p.setPen(QColor(0x10, 0x12, 0x18));
    QFont f = p.font();
    f.setPixelSize(int(size * 0.5));
    f.setBold(true);
    p.setFont(f);
    p.drawText(pm.rect(), Qt::AlignCenter,
               name.isEmpty() ? QStringLiteral("?") : name.left(1).toUpper());
    return pm;
}
} // namespace

ConnectingOverlay::ConnectingOverlay(QWidget *parent) : QWidget(parent)
{
    // Opaque background via palette (a type-selector stylesheet would need the
    // namespaced "termsync--ui--ConnectingOverlay" name and is easy to get wrong).
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(0x1a, 0x1b, 0x26));
    setPalette(pal);

    auto *outer = new QVBoxLayout(this);
    outer->addStretch();

    auto *center = new QHBoxLayout;
    center->addStretch();

    m_avatar = new QLabel(this);
    m_avatar->setFixedSize(48, 48);
    center->addWidget(m_avatar);
    center->addSpacing(14);

    auto *textCol = new QVBoxLayout;
    textCol->setSpacing(3);
    m_title = new QLabel(tr("Connecting…"), this);
    m_title->setStyleSheet(QStringLiteral(
        "color:#e6e9f2; font-size:14pt; font-weight:600; background:transparent;"));
    m_subtitle = new QLabel(this);
    m_subtitle->setStyleSheet(QStringLiteral(
        "color:#8a92b2; font-size:10pt; background:transparent;"));
    textCol->addWidget(m_title);
    textCol->addWidget(m_subtitle);
    center->addLayout(textCol);
    center->addSpacing(20);

    auto *showLogs = new QPushButton(tr("Show logs"), this);
    showLogs->setCursor(Qt::PointingHandCursor);
    connect(showLogs, &QPushButton::clicked, this, &ConnectingOverlay::dismissed);
    center->addWidget(showLogs, 0, Qt::AlignVCenter);
    center->addStretch();
    outer->addLayout(center);

    outer->addSpacing(18);
    auto *progressRow = new QHBoxLayout;
    progressRow->addStretch(1);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0); // indeterminate
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(4);
    m_progress->setFixedWidth(420);
    m_progress->setStyleSheet(QStringLiteral(
        "QProgressBar { background:#2a2c3a; border:0; border-radius:2px; }"
        "QProgressBar::chunk { background:#2dd4bf; border-radius:2px; }"));
    progressRow->addWidget(m_progress, 0);
    progressRow->addStretch(1);
    outer->addLayout(progressRow);

    outer->addStretch();

    setAvatar(QString());
}

void ConnectingOverlay::setAvatar(const QString &name)
{
    m_avatar->setPixmap(makeAvatar(name, 48));
}

void ConnectingOverlay::setTitle(const QString &title)
{
    m_title->setText(title.isEmpty() ? tr("Connecting…") : title);
    setAvatar(title);
}

void ConnectingOverlay::setSubtitle(const QString &subtitle)
{
    m_subtitle->setText(subtitle);
}

void ConnectingOverlay::setFailed(const QString &message)
{
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    m_progress->setStyleSheet(QStringLiteral(
        "QProgressBar { background:#2a2c3a; border:0; border-radius:2px; }"
        "QProgressBar::chunk { background:#f7768e; }"));
    m_subtitle->setText(message);
    m_subtitle->setStyleSheet(QStringLiteral(
        "color:#f7768e; font-size:10pt; background:transparent;"));
}

} // namespace termsync::ui
