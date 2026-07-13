# Feature Status

This document tracks TermSync's own implemented behavior and remaining gaps.

## Verified foundations

- SSH2 connection, password and key authentication, host-key trust, agent and
  keyboard-interactive authentication
- VT/xterm parsing and painting, Unicode, resize handling, terminal colors,
  logging, highlighting, and hex view
- Profile storage, operating-system credential storage, import/export, and tabs
- SFTP and FTP listing and transfer, queueing, synchronization, resume, rate
  limits, pause, reconnect, permissions, symbolic links, and ASCII conversion
- Local and dynamic forwarding, HTTP and SOCKS proxies, X11 forwarding
- Telnet, serial, local shell, TN3270, first-pass TN5250, TFTP, and transfer
  protocol primitives
- JavaScript automation and a headless CLI with scheduling and execution

## Current pre-release additions

- Hosts-first home screen and custom tabbed title bar
- Explorer-style SFTP browser with classic dual-pane compatibility mode
- Native Windows drag-in, drag-out, clipboard transfer, view, and sorting
- Quick Commands dock
- Remote operating-system detection and host icons
- sudo-backed remote file operations
- tar-stream bulk directory upload and download with SFTP fallback

## Known gaps

- TN5250 input and wider legacy terminal emulation remain incomplete.
- Local shell uses a pipe-backed process instead of a native PTY on some systems.
- FTPS, proxy, serial, X11, and reconnect paths need broader live environment
  coverage.
- Signed installers, automatic updates, full accessibility validation, session
  locking, host printing, and script recording are not yet shipped.
- Some advanced menu entries are placeholders and should remain disabled until
  their complete workflows are available.

## Verification

The repository currently contains 136 automated tests spanning unit, integration,
render-smoke, protocol, CLI, and transfer scenarios. Tests requiring external
servers or devices are opt-in and document their environment prerequisites.
