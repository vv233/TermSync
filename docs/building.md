# Building TermSync

## Prerequisites

| Tool | Requirement |
|---|---|
| Compiler | C++17; MSVC 2022+, GCC 11+, Clang 14+, or Apple Clang |
| CMake | 3.25 or newer |
| Ninja | Recent version |
| Qt | 6.5 or newer with Widgets, Sql, Network, and Qml |
| vcpkg | libssh2, curl, OpenSSL, zlib, SQLite, JSON, and GoogleTest |

Set `VCPKG_ROOT` and make Qt discoverable through `CMAKE_PREFIX_PATH` or
`Qt6_DIR`.

## Development build

```bash
cmake --preset default -DCMAKE_PREFIX_PATH=/path/to/Qt6
cmake --build --preset default
ctest --preset default
```

Development executables are written beneath `build/default/`. The
`no-vcpkg` configure preset is available when dependencies are supplied by the
system.

## Windows pre-release

On the configured Windows machine, run:

```powershell
powershell -File scripts/make-release.ps1
```

The script performs a Release build, deploys Qt and third-party runtime files,
copies notices, and creates `TermSync-0.1.0-pre.1-win64.zip`. All distributable
files are written to `release/`; no other build directory is a release source.

Machine-specific Visual Studio and Qt locations can be overridden:

```powershell
powershell -File scripts/make-release.ps1 -VsDir C:\Path\To\VS -QtDir C:\Path\To\Qt
```
