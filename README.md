# TermSync

TermSync is an open-source, cross-platform remote operations client built with
C++17 and Qt 6. It combines tabbed terminal sessions, connection profiles,
file transfer, synchronization, automation, and a headless CLI in one project.

> **Pre-release:** `0.1.0-pre.3` is intended for validation. Configuration and
> behavior may still change before the first stable release.

## Current capabilities

- SSH2, Telnet, serial, local shell, TN3270, and first-pass TN5250 sessions
- VT/xterm terminal rendering, color schemes, logging, keyword highlighting,
  hex view, scripting, port forwarding, X11 forwarding, and proxy support
- SecureCRT-style terminal clipboard behavior: selecting text copies it
  immediately, right-click pastes, and Shift+right-click opens the context menu
- SFTP, FTP, FTPS, and SCP transfer through Explorer-style and dual-pane views
- Transfer queues, pause/resume, bandwidth limits, reconnect, synchronization,
  bookmarks, native Windows drag-out, tar-stream directory transfer, and sudo mode
- Saved profiles, OS credential storage, host-key trust, import/export, and host
  operating-system icons
- `termsync-cli` for transfer, synchronization, scheduling, remote execution,
  local execution, and remote file editing

See [feature-status.md](docs/feature-status.md) for verified coverage and known
gaps, and [architecture.md](docs/architecture.md) for the component layout.

## Build and test

Prerequisites and platform setup are documented in [building.md](docs/building.md).

```bash
cmake --preset default
cmake --build --preset default
ctest --preset default
```

The current suite contains 136 automated tests.

## Pre-release packaging

`release/` is the only distribution output. On the configured Windows build
machine, refresh the runnable bundle and its versioned ZIP with:

```powershell
powershell -File scripts/make-release.ps1
```

Do not distribute executables from `build/`; those are development intermediates.

The Windows installer offers VcXsrv as a default optional component. It is
installed from the verified WinGet package when selected. When X11 forwarding
is enabled, TermSync starts VcXsrv on demand with a generated Xauthority cookie;
it does not disable X11 access control with `-ac`.

## Technology

| Area | Library |
|---|---|
| UI | Qt 6 Widgets |
| SSH2/SFTP | libssh2 |
| FTP/FTPS | libcurl |
| Configuration | SQLite and nlohmann/json |
| Terminal | Project VT parser, screen buffer, and QPainter renderer |

## License

TermSync is licensed under MIT. Third-party components retain their own licenses;
see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
