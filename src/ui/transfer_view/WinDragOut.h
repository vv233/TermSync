#pragma once

#include <QStringList>
#include <functional>

namespace termsync::ui {

// Starts a native Windows drag-out of remote files to Explorer.
//
// Unlike QDrag, this begins the OLE drag *immediately* (so it latches onto the
// live mouse gesture) and defers materialising the files: `provideFiles` is
// invoked only when the drop target asks for the data — i.e. after the user has
// dropped — and must download the items and return their real local paths (top
// level; folders allowed). Returns true if a copy actually happened.
//
// Windows-only; the header is includable everywhere but the implementation is
// compiled behind _WIN32.
bool startWindowsFileDrag(std::function<QStringList()> provideFiles);

// Test hook: builds the CF_HDROP the drag would hand Explorer for `paths` and
// parses it straight back, so the IDataObject / DROPFILES plumbing can be
// verified without a live drag gesture. Empty on non-Windows.
QStringList debugRoundTripHDrop(const QStringList &paths);

} // namespace termsync::ui
