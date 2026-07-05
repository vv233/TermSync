# Architecture

Layered, library-first. See the approved plan for full detail.

```
src/app       main(), bootstrap, dependency wiring
src/ui        MainWindow, tabs, dialogs, dual-pane browser, sync dialog
src/terminal  VT parser + screen buffer + QPainter terminal widget
src/transfer  SFTP/FTP file engines, sync engine, transfer queue
src/core      shared backbone:
              - model/       ConnectionProfile, SyncPairDefinition
              - store/       ProfileStore (SQLite), JSON import/export
              - credential/  CredentialStore, QtKeychainStore
              - ssh/         SshConnection, SshChannel (libssh2)
              - ftp/         FtpConnection (libcurl)
              - session/     Session (one transport, many channels)
```

## Key principle: one profile → one transport → many channels

`core::Session::fromProfile()` owns a single authenticated `SshConnection`
(libssh2 `LIBSSH2_SESSION*`). A terminal tab opens a shell channel; a file
browser opens an SFTP-subsystem channel — both multiplexed over the same
authenticated transport, so the user authenticates and trusts the host key once.

## Dependency direction

`core` (no Qt Widgets) ← `terminal` / `transfer` ← `ui` ← `app`.
`terminal` and `transfer` never touch sockets directly — they consume
`core::Session` channel handles, which keeps them independently testable.

## Threading

One `QThread` per `Session` runs the libssh2 event loop (libssh2 sessions are
not thread-safe across threads). Bytes cross to the UI thread via queued
signals. The transfer queue uses its own worker thread(s).

## Current status

**M1** — only `src/ui` (MainWindow shell) and `src/app` are wired into the
build. The remaining modules are added by their milestones.
