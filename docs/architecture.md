# Architecture

TermSync is a layered Qt application with reusable libraries behind two entry
points: the desktop application and the headless CLI.

```text
src/app       desktop bootstrap
src/cli       transfer, sync, scheduling, and execution CLI
src/ui        main window, tabs, dialogs, terminal and transfer views
src/script    JavaScript automation bridge
src/terminal  VT parser, screen buffer, colors, highlighting, hex formatting
src/transfer  file engines, queues, synchronization, scheduler, tar archives
src/core      profiles, credentials, transports, protocols, logging, proxies
```

## Dependency direction

`core` is the shared foundation. `terminal`, `transfer`, and `script` build on
that foundation. `ui` composes those libraries, and `app` remains a thin
bootstrap. The CLI links directly to `core` and `transfer` without Qt Widgets.

## Connections and threading

A saved profile is the common configuration for terminal and transfer views.
Each active view owns the protocol connection needed for its work. Network and
transfer loops run outside the GUI thread and communicate through queued Qt
signals. Transfer queues can use multiple worker connections while preserving
per-item progress, cancellation, pause, and retry state.

## Terminal path

Incoming bytes pass through the selected protocol connection into the VT parser
and screen buffer. `TerminalWidget` paints the grid with QPainter and translates
keyboard and mouse input back into terminal sequences. Logging, highlighting,
hex view, and script hooks observe this same data path.

## Transfer path

`FileEngine` provides the common remote-file contract. SFTP and FTP backends
implement listing and file operations; `SftpSession` owns the worker thread and
queue. Directory synchronization compares structured local and remote listings.
Bulk directory operations can stream tar archives over an SSH command channel,
with per-file SFTP as the compatibility fallback.

## Persistence

Profiles and non-secret settings are stored in SQLite or QSettings. Secrets are
stored through the operating-system credential provider. Import/export excludes
secrets by design.

## Release boundary

Development output stays under `build/`. The only distributable output is
`release/`, produced by `scripts/make-release.ps1`.
