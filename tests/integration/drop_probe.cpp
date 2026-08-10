// Minimal real-OLE drop probe: a top-level window with setAcceptDrops(true).
// Drag a file onto it from Explorer; it logs whether the drag is accepted and
// what was dropped. Verifies OLE drop registration works in this environment.

#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QLabel>
#include <QMimeData>
#include <QUrl>
#include <QWidget>
#include <cstdio>

class Probe : public QWidget
{
public:
    Probe()
    {
        setAcceptDrops(true);
        setWindowTitle(QStringLiteral("DropProbe"));
        resize(520, 420);
        m_lbl = new QLabel(QStringLiteral("DROP A FILE HERE"), this);
        m_lbl->setAlignment(Qt::AlignCenter);
        m_lbl->setStyleSheet(QStringLiteral("font-size:20px;color:#e6e9f2;"
                                            "background:#1a1b26;"));
    }

protected:
    void resizeEvent(QResizeEvent *) override { m_lbl->resize(size()); }
    void dragEnterEvent(QDragEnterEvent *e) override
    {
        std::fprintf(stderr, "[dragEnter] hasUrls=%d\n", e->mimeData()->hasUrls());
        if (e->mimeData()->hasUrls()) {
            e->acceptProposedAction();
            m_lbl->setText(QStringLiteral("DRAG ACCEPTED (no X)"));
        }
    }
    void dragMoveEvent(QDragMoveEvent *e) override
    {
        if (e->mimeData()->hasUrls())
            e->acceptProposedAction();
    }
    void dropEvent(QDropEvent *e) override
    {
        QString s;
        for (const QUrl &u : e->mimeData()->urls())
            s += u.toLocalFile() + QLatin1Char('\n');
        e->acceptProposedAction();
        std::fprintf(stderr, "[DROPPED]\n%s", s.toUtf8().constData());
        m_lbl->setText(QStringLiteral("DROPPED:\n") + s);
    }

private:
    QLabel *m_lbl = nullptr;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    Probe p;
    p.show();
    return app.exec();
}
