#pragma once

#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>

#include "common/Icons.h"

namespace termsync::ui {

// A QTabWidget that gives every tab a clean, themed close button (a thin "×"
// that turns red on hover) instead of Fusion's dated boxy default. Tabs whose
// close button is explicitly cleared (e.g. the permanent Hosts tab) stay
// un-closable.
class ChromeTabWidget : public QTabWidget
{
public:
    explicit ChromeTabWidget(QWidget *parent = nullptr) : QTabWidget(parent)
    {
        // No base line under the tabs — it renders as a stray light rule that
        // the selected tab appears to poke through.
        tabBar()->setDrawBase(false);
    }

protected:
    void tabInserted(int index) override
    {
        QTabWidget::tabInserted(index);

        auto *btn = new QToolButton(this);
        btn->setIcon(lineIcon(Glyph::Close, QColor(0x8a, 0x92, 0xb2)));
        btn->setIconSize(QSize(12, 12));
        btn->setAutoRaise(true);
        btn->setCursor(Qt::ArrowCursor);
        btn->setToolTip(tr("Close tab"));
        btn->setStyleSheet(QStringLiteral(
            "QToolButton { border: 0; border-radius: 5px; padding: 3px; }"
            "QToolButton:hover { background: #f04a5a; }"));
        connect(btn, &QToolButton::clicked, this, [this, btn] {
            for (int i = 0; i < count(); ++i) {
                if (tabBar()->tabButton(i, QTabBar::RightSide) == btn) {
                    emit tabCloseRequested(i);
                    return;
                }
            }
        });
        tabBar()->setTabButton(index, QTabBar::RightSide, btn);
    }
};

} // namespace termsync::ui
