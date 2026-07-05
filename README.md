# TermSync

An open-source, cross-platform **SSH terminal + SFTP/FTP file-transfer client** — combining
the core capabilities of a SecureCRT-style terminal emulator and a SecureFX-style file-transfer
tool into a single application, built with **C++ / Qt 6**.

> ⚠️ **Early development.** This repository currently contains the **M1 scaffold** only:
> a build system and an empty Qt 6 main window with the menu shell. No SSH/SFTP functionality
> is implemented yet. See [the plan](#roadmap) below.

## Why one app?

A single **connection profile** backs both a terminal session and a file-transfer session,
reusing one authenticated SSH transport (one auth prompt, one host-key trust decision) —
mirroring how SecureCRT/SecureFX share a session engine.

## Features (planned)

- SSH2 terminal emulation (VT100/xterm) with tabbed multi-session support
- Session manager with saved profiles and OS-keychain credential storage
- SFTP / FTP / FTPS dual-pane file browser with a transfer queue
- One-way and two-way directory synchronization
- Later: Telnet / Serial / rlogin, SCP, X/Y/ZMODEM, port forwarding, scripting, TN3270/5250

- **Full roadmap / design plan**: [`docs/plan.md`](docs/plan.md) — architecture, tech choices, data model, and all 20 milestones
- **Feature-parity checklist**: [`docs/ui-parity.md`](docs/ui-parity.md) — every SecureCRT/SecureFX feature mapped to a milestone

## Building

You need a C++17 compiler, **CMake ≥ 3.25**, **Qt 6**, and (for non-Qt dependencies) **vcpkg**.
Step-by-step per-OS instructions are in [`docs/building.md`](docs/building.md).

```bash
cmake --preset default
cmake --build --preset default
```

## Tech stack

| Area | Library | License |
|---|---|---|
| SSH2/SFTP | libssh2 | BSD-3 |
| FTP/FTPS | libcurl | curl (≈MIT) |
| Terminal emulation | custom VT parser + QPainter | (this project) |
| Credential storage | QtKeychain | MIT |
| Config persistence | SQLite + nlohmann-json | Public domain / MIT |
| UI | Qt 6 Widgets | LGPL |

## License

MIT — see [`LICENSE`](LICENSE). Third-party components retain their own licenses.

*Not affiliated with or endorsed by VanDyke Software. "SecureCRT" and "SecureFX" are
trademarks of VanDyke Software, Inc., referenced here only to describe compatibility goals.*
