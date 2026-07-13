#include "common/Icons.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QtMath>

namespace termsync::ui {

namespace {

// All glyphs are authored on a 24x24 grid and stroked with round caps/joins for
// a soft, modern look, then rendered into `px`x`px`.
void drawGlyph(QPainter &p, Glyph g)
{
    auto line = [&](qreal x1, qreal y1, qreal x2, qreal y2) {
        p.drawLine(QPointF(x1, y1), QPointF(x2, y2));
    };
    auto poly = [&](std::initializer_list<QPointF> pts) {
        p.drawPolyline(QPolygonF(QVector<QPointF>(pts)));
    };

    switch (g) {
    case Glyph::Back:
        poly({{15, 5}, {8, 12}, {15, 19}});
        break;
    case Glyph::Forward:
        poly({{9, 5}, {16, 12}, {9, 19}});
        break;
    case Glyph::Up:
        poly({{5, 14}, {12, 7}, {19, 14}});
        line(12, 7, 12, 19);
        break;
    case Glyph::Refresh: {
        QPainterPath arc;
        // ~300° open ring with an arrowhead at the top-right opening.
        arc.arcMoveTo(4, 4, 16, 16, 70);
        arc.arcTo(4, 4, 16, 16, 70, 300);
        p.drawPath(arc);
        const QPointF tip(12 + 8 * qCos(qDegreesToRadians(70.0)),
                          12 - 8 * qSin(qDegreesToRadians(70.0)));
        poly({{tip.x() - 4, tip.y() - 1}, {tip.x(), tip.y()}, {tip.x() + 1, tip.y() - 4}});
        break;
    }
    case Glyph::NewFolder:
        poly({{3, 8}, {9, 8}, {11, 10}, {21, 10}, {21, 19}, {3, 19}, {3, 8}});
        // plus
        line(16, 12, 16, 17);
        line(13.5, 14.5, 18.5, 14.5);
        break;
    case Glyph::Upload:
        poly({{8, 8}, {12, 4}, {16, 8}});
        line(12, 4, 12, 15);
        poly({{5, 15}, {5, 20}, {19, 20}, {19, 15}});
        break;
    case Glyph::Download:
        poly({{8, 11}, {12, 15}, {16, 11}});
        line(12, 4, 12, 15);
        poly({{5, 15}, {5, 20}, {19, 20}, {19, 15}});
        break;
    case Glyph::Rename: {
        // Pencil across the tile.
        poly({{5, 19}, {5, 15.5}, {15.5, 5}, {19, 8.5}, {8.5, 19}, {5, 19}});
        line(13, 7.5, 16.5, 11);
        break;
    }
    case Glyph::Trash:
        line(4, 7, 20, 7);
        poly({{9, 7}, {9, 4.5}, {15, 4.5}, {15, 7}});
        poly({{6, 7}, {7, 20}, {17, 20}, {18, 7}});
        line(10, 10, 10.4, 17);
        line(14, 10, 13.6, 17);
        break;
    case Glyph::Sort:
        line(5, 7, 14, 7);
        line(5, 12, 11, 12);
        line(5, 17, 8, 17);
        poly({{16, 8}, {19, 5}, {22, 8}}); // small up marker on the right stack
        line(19, 5, 19, 19);
        poly({{16, 16}, {19, 19}, {22, 16}});
        break;
    case Glyph::Grid: {
        const qreal s = 7.5, g0 = 3.5, g1 = 13;
        p.drawRoundedRect(QRectF(g0, g0, s, s), 1.5, 1.5);
        p.drawRoundedRect(QRectF(g1, g0, s, s), 1.5, 1.5);
        p.drawRoundedRect(QRectF(g0, g1, s, s), 1.5, 1.5);
        p.drawRoundedRect(QRectF(g1, g1, s, s), 1.5, 1.5);
        break;
    }
    case Glyph::Home:
        poly({{4, 12}, {12, 4.5}, {20, 12}});
        poly({{6, 11}, {6, 19.5}, {18, 19.5}, {18, 11}});
        p.drawRoundedRect(QRectF(10, 14, 4, 5.5), 1, 1);
        break;
    case Glyph::Drive:
        p.drawRoundedRect(QRectF(3.5, 8, 17, 8), 2, 2);
        // status LED + slot
        p.save();
        p.setBrush(p.pen().color());
        p.drawEllipse(QPointF(7, 12), 1.1, 1.1);
        p.restore();
        line(11, 12, 17, 12);
        break;
    case Glyph::Folder:
        poly({{3, 8}, {9, 8}, {11, 10}, {21, 10}, {21, 19}, {3, 19}, {3, 8}});
        break;
    case Glyph::File:
        poly({{6, 3.5}, {14, 3.5}, {19, 8.5}, {19, 20.5}, {6, 20.5}, {6, 3.5}});
        poly({{14, 3.5}, {14, 8.5}, {19, 8.5}});
        break;
    case Glyph::Close:
        line(7, 7, 17, 17);
        line(17, 7, 7, 17);
        break;
    case Glyph::WinMinimize:
        line(7, 12, 17, 12);
        break;
    case Glyph::WinMaximize:
        p.drawRect(QRectF(7, 7, 10, 10));
        break;
    case Glyph::WinRestore:
        p.drawRect(QRectF(7, 9, 8, 8));            // front
        poly({{9, 9}, {9, 7}, {17, 7}, {17, 15}, {15, 15}}); // back
        break;
    }
}

} // namespace

QIcon lineIcon(Glyph glyph, const QColor &color)
{
    const int px = 64;
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(px / 24.0, px / 24.0);
    QPen pen(color, 1.7);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    drawGlyph(p, glyph);
    p.end();
    return QIcon(pm);
}

QIcon appIcon()
{
    QIcon icon;
    for (int size : {16, 24, 32, 48, 64, 128, 256}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        const qreal s = size / 24.0;
        p.scale(s, s);

        // Rounded teal tile with a soft vertical gradient.
        QLinearGradient grad(0, 2, 0, 22);
        grad.setColorAt(0.0, QColor(0x3d, 0xe6, 0xd0));
        grad.setColorAt(1.0, QColor(0x17, 0xb3, 0xa3));
        p.setPen(Qt::NoPen);
        p.setBrush(grad);
        p.drawRoundedRect(QRectF(2, 2, 20, 20), 6, 6);

        // Terminal prompt: a chevron and an underscore caret, in deep ink.
        QPen ink(QColor(0x0c, 0x1a, 0x1c), 2.1);
        ink.setCapStyle(Qt::RoundCap);
        ink.setJoinStyle(Qt::RoundJoin);
        p.setPen(ink);
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(QPolygonF(QVector<QPointF>{{7.5, 8.5}, {11, 12}, {7.5, 15.5}}));
        p.drawLine(QPointF(13, 15.5), QPointF(17, 15.5));
        p.end();

        icon.addPixmap(pm);
    }
    return icon;
}

} // namespace termsync::ui
