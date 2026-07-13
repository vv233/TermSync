# TermSync Development Plan

TermSync is now beyond the initial milestone implementation phase. Development
is organized around release readiness and measurable behavior rather than the
original numbered prototype milestones.

## Pre-release scope

- Stabilize the custom title bar, Hosts home, tabs, and quick-command workflow.
- Validate SSH terminal and Explorer-style SFTP behavior on supported Windows
  versions and common display scales.
- Exercise sudo operations and tar-stream directory transfer against multiple
  Linux distributions, including fallback behavior when remote tools are absent.
- Keep the full automated suite green and add tests for every fixed regression.
- Produce one reproducible, self-contained package under `release/`.

## Stable release criteria

- No known data-loss issue in upload, download, synchronization, delete, rename,
  resume, or sudo workflows.
- Host-key, credential, and proxy failures have actionable user-facing errors.
- Terminal rendering is verified at common DPI, font, resize, and color settings.
- The packaged application launches without a development environment or Qt on
  `PATH`.
- Documentation matches the shipped UI and CLI.
- Version, release notes, executable metadata, and archive name agree.

## Post-release priorities

- Complete interactive TN5250 input and broaden terminal compatibility.
- Add native PTY support for local shells on every platform.
- Improve accessibility, keyboard-only navigation, and high-contrast behavior.
- Add signed installers and update delivery after the portable package is stable.
- Expand live integration coverage for FTPS, proxies, serial devices, X11, and
  unreliable-network transfer recovery.

## Engineering rules

- Shared protocol and transfer behavior belongs in library modules, not widgets.
- GUI networking must stay off the UI thread.
- Structured formats use structured parsers; secrets never enter exported
  profiles, logs, or release artifacts.
- `build/` contains development intermediates. `release/` is the only publishing
  boundary.
