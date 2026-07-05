# Third-Party Notices

TermSync is distributed under the MIT License (see `LICENSE`). It links against
the following third-party components, each under its own license:

| Component | Purpose | License |
|---|---|---|
| [Qt 6](https://www.qt.io/) | GUI framework (Widgets, Core, Sql) | LGPL-3.0 |
| [libssh2](https://libssh2.org/) | SSH2 transport, SFTP, agent | BSD-3-Clause |
| [libcurl](https://curl.se/) | FTP/FTPS transfer | curl (MIT-like) |
| [OpenSSL](https://www.openssl.org/) | Crypto backend for libssh2/libcurl | Apache-2.0 |
| [zlib](https://zlib.net/) | Compression (transitive) | zlib |
| [SQLite](https://sqlite.org/) | Profile store (via Qt Sql QSQLITE) | Public domain |
| [nlohmann/json](https://github.com/nlohmann/json) | JSON (profile import/export) | MIT |
| [GoogleTest](https://github.com/google/googletest) | Unit tests (not shipped) | BSD-3-Clause |

## Qt / LGPL note

Qt is used under the LGPL-3.0. TermSync links Qt dynamically; the Qt libraries
are shipped as separate shared libraries (deployed via `windeployqt` on
Windows), so users can replace them, as the LGPL requires. Qt source is
available from https://www.qt.io/.

## Trademarks

"SecureCRT" and "SecureFX" are trademarks of VanDyke Software, Inc. TermSync is
an independent project, not affiliated with or endorsed by VanDyke Software;
those names are referenced only to describe feature-compatibility goals.

Full license texts for each component are available at the URLs above and are
included in the binary distribution's `licenses/` folder.
