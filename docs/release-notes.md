# TermSync 0.1.0-pre.1

This is a validation build for the first public TermSync release line.

## Highlights

- New Hosts home, custom tabbed title bar, application icons, and Quick Commands
- Explorer-style SFTP browser with native Windows drag, drop, and clipboard flows
- Faster directory transfer through streamed tar archives with automatic fallback
- Optional sudo-backed listing, transfer, create, delete, and rename operations
- Remote operating-system detection for saved-host icons
- Terminal font normalization and corrected monospace cell measurement
- SFTP connection and file-icon performance improvements

## Validation focus

- Exercise terminal rendering at different DPI and font settings.
- Test file and directory transfers in both directions, including cancellation.
- Verify sudo mode on Ubuntu, Debian, and other supported Linux hosts.
- Confirm the portable ZIP launches on a machine without Qt installed.

This pre-release may change configuration and behavior before the stable version.
