# Contributing to TermSync

Thanks for your interest! Bug reports, feature ideas, and pull requests are all
welcome.

## Reporting issues

Open a [GitHub issue](https://github.com/vv233/TermSync/issues) and include:

- OS and version (Windows 10/11, or your Linux distro), and how you installed
  TermSync (installer / portable zip / AppImage / built from source).
- The TermSync version (**Help → About**).
- Steps to reproduce, what you expected, and what happened.
- Relevant logs. Enable **File → Log Session** for terminal issues; for crashes,
  any console output helps.

Please don't put credentials, private keys, or host names you consider sensitive
into a public issue.

## Building from source

See [docs/building.md](docs/building.md). In short:

```bash
cmake --preset default -DCMAKE_PREFIX_PATH=/path/to/Qt6
cmake --build --preset default
ctest --preset default
```

Requires a C++17 compiler, CMake ≥ 3.25, Qt 6.5+ (Widgets, Sql, Network, Qml),
and vcpkg for the native dependencies. `VCPKG_ROOT` must be set.

## Pull requests

1. Fork and branch off `main` (`feature/…` or `fix/…`).
2. Keep changes focused; match the surrounding code style (see below).
3. **Add or update tests.** Pure logic (parsers, protocol codecs, stores, the
   sync differ) should have unit tests under `tests/unit/`. `ctest --preset
   default` must pass.
4. Update the docs you touch — `docs/feature-status.md`, `README.md`, etc.
5. Ensure CI (Ubuntu + Windows build & test) is green on your PR.

## Coding conventions

- **Layering:** `core` (no Qt Widgets) ← `terminal` / `transfer` / `script` ←
  `ui` ← `app`. Keep `core` headlessly testable — no Widgets there.
- Wrap third-party libraries (libssh2, libcurl) behind the project's own
  interfaces so they stay swappable and testable.
- Never block the UI thread on network I/O — use the worker-thread + queued
  signal pattern already established for SSH/SFTP.
- Prefer the existing patterns for new protocols: implement
  `AbstractTerminalConnection` (terminal) or the `FileEngine` interface
  (transfer) so the UI stays protocol-agnostic.
- Match the existing formatting (4-space indent, braces, comment density). Add a
  short comment explaining *why* for non-obvious code.

## Commit messages

Use short, imperative summaries with an optional scope, e.g.
`transfer: pipeline SFTP reads for higher throughput`. Reference issues with
`Fixes #123` where applicable.

## License

By contributing you agree that your contributions are licensed under the
project's [MIT License](LICENSE).
