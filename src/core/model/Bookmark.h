#pragma once

#include <QString>

namespace termsync::core {

// A saved location in the file browser. `host` scopes
// it to a connection (empty = global / any host); `remotePath` is the folder it
// jumps to, with an optional paired `localPath` for the local pane.
struct Bookmark
{
    QString id;         // UUID
    QString name;       // display name
    QString host;       // owning host, or empty for global
    QString remotePath;
    QString localPath;  // optional

    bool isValid() const { return !name.isEmpty() && !remotePath.isEmpty(); }
};

} // namespace termsync::core
