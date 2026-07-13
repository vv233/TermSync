// Verifies the native drag-out IDataObject builds a well-formed CF_HDROP that
// round-trips the exact file paths Explorer would receive on drop. This exercises
// the hand-rolled COM object and DROPFILES layout without a live drag gesture.
//
// Exit 0 = passed.

#include <QCoreApplication>
#include <cstdio>

#include "transfer_view/WinDragOut.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const QStringList in{QStringLiteral("C:/Users/test/a file.txt"),
                         QStringLiteral("C:/Users/test/folder"),
                         QStringLiteral("D:/x/y/z.bin")};
    const QStringList out = termsync::ui::debugRoundTripHDrop(in);

    // Paths come back with native separators; normalise for comparison.
    QStringList norm;
    for (const QString &p : out)
        norm << QString(p).replace('\\', '/');

    if (norm != in) {
        std::fprintf(stderr, "[FAIL] HDROP round-trip mismatch\n  in : %s\n  out: %s\n",
                     in.join(QStringLiteral(" | ")).toUtf8().constData(),
                     norm.join(QStringLiteral(" | ")).toUtf8().constData());
        return 1;
    }
    std::fprintf(stderr, "[PASS] win_dragout_smoke: %d paths round-tripped via CF_HDROP\n",
                 int(in.size()));
    return 0;
}
