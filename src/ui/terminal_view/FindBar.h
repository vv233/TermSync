#pragma once

#include <QWidget>

class QLineEdit;
class QCheckBox;
class QLabel;

namespace termsync::ui {

// A slim search bar (M20 polish) shown above the terminal for Edit -> Find.
// It owns no terminal; MainWindow routes searchRequested() to the active
// TerminalWidget and calls setNotFound() with the result.
class FindBar : public QWidget
{
    Q_OBJECT

public:
    explicit FindBar(QWidget *parent = nullptr);

    // Reveal the bar, focus the field, and select any existing text.
    void activate();
    QString text() const;
    bool caseSensitive() const;

signals:
    // forward=false requests a backward (previous) match. Emitted for Enter and
    // the ▲/▼ buttons — it advances from the current match.
    void searchRequested(const QString &needle, bool forward, bool caseSensitive);
    // Emitted as the user types: search from the top so the match tracks the
    // query rather than jumping ahead on each keystroke.
    void incrementalSearch(const QString &needle, bool caseSensitive);
    void closed();

public slots:
    void setNotFound(bool notFound);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void emitSearch(bool forward);

    QLineEdit *m_edit = nullptr;
    QCheckBox *m_caseBox = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace termsync::ui
