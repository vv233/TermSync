# Known Limitations

TermSync is **pre-release** software. This page lists the current rough edges
that affect users, with workarounds where they exist. For the developer-facing
per-feature status, see [feature-status.md](feature-status.md).

## Platforms

- **macOS is not built yet.** The Qt 6.8 build references the removed `AGL`
  framework, so macOS binaries aren't produced. Windows and Linux are supported.
- **Windows builds are not code-signed yet**, so SmartScreen shows an
  "unrecognized app" prompt — choose *More info → Run anyway*. Signing is wired
  up and turns on once a certificate is configured
  ([code-signing.md](code-signing.md)).
- **Linux AppImage** needs FUSE (`sudo apt install libfuse2`) or
  `--appimage-extract-and-run`.

## Protocols & terminals

- **TN5250** is a first pass: it renders Write-to-Display output but **field
  input / AID submission is not implemented** yet. TN3270 input works.
- Legacy terminal emulations beyond VT/xterm and basic 3270/5250 are incomplete.
- **Serial** requires a real or virtual device; it can't be exercised without one.
- **rlogin** and interactive **ZMODEM** (rz/sz auto-detect) are not shipped; the
  X/Y/ZMODEM framing primitives exist but aren't wired into a live session.

## Authentication & security

- **FTPS**, **proxy chains**, **X11 forwarding**, and **auto-reconnect** work but
  have had limited live-environment coverage — treat them as beta.
- On platforms without an OS credential vault, saved passwords fall back to a
  non-persistent, in-process store (you'll be prompted again next run). An
  encrypted local vault is planned.
- Kerberos/GSSAPI and X.509 client certificates are not yet fully wired.

## Transfers

- Very high-throughput SFTP is tuned (parallel connections + deep read-ahead)
  but **absolute speed depends on your server and network**; tune with the
  `TERMSYNC_SFTP_BUFFER_KB` / `TERMSYNC_SFTP_PARALLEL` / `TERMSYNC_SFTP_THRESHOLD_MB`
  environment variables and measure with `sftp_bench`.
- Some servers reject many parallel connections (`MaxSessions`); lower
  `TERMSYNC_SFTP_PARALLEL` if transfers fail to start.

## Application

- **Local shell** uses a pipe-backed process rather than a native PTY on some
  systems, so full-screen TUIs (vim/htop) may render imperfectly in a local
  shell tab (remote SSH shells are unaffected).
- **Not yet shipped:** automatic updates, session locking, host-based printing,
  script recording, and full accessibility validation.
- Some advanced menu entries are intentionally **disabled** until their complete
  workflow is available.

---

Found something not listed here? Please
[open an issue](https://github.com/vv233/TermSync/issues) — see
[CONTRIBUTING.md](../CONTRIBUTING.md).
