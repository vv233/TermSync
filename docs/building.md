# Building TermSync

## Prerequisites

| Tool | Version | Notes |
|---|---|---|
| C++ compiler | C++17 | MSVC 2022 (Windows), GCC 11+ / Clang 14+ (Linux), Apple Clang (macOS) |
| CMake | ≥ 3.25 | Uses `CMakePresets.json` |
| Ninja | any recent | Generator used by the presets |
| Qt | 6.5+ | Widgets module; install via the official Qt online installer or a system package |
| vcpkg | latest | Provides libssh2, curl, sqlite3, nlohmann-json, gtest |

> **None of these are installed in the current dev sandbox** — they must be installed
> locally before the project can be configured or built. Only `git` and `python` are
> present out of the box.

## One-time setup

1. **Install vcpkg** and set `VCPKG_ROOT`:
   ```bash
   git clone https://github.com/microsoft/vcpkg
   ./vcpkg/bootstrap-vcpkg.sh        # or bootstrap-vcpkg.bat on Windows
   export VCPKG_ROOT=$PWD/vcpkg      # setx VCPKG_ROOT on Windows
   ```
   The manifest (`vcpkg.json`) is installed automatically during CMake configure.

2. **Install Qt 6** and tell CMake where it is, via one of:
   - `CMAKE_PREFIX_PATH`, e.g. `-DCMAKE_PREFIX_PATH="C:/Qt/6.7.2/msvc2019_64"`
   - or set the `Qt6_DIR` environment variable.

## Configure & build

```bash
# Debug (default)
cmake --preset default -DCMAKE_PREFIX_PATH=/path/to/Qt6
cmake --build --preset default

# Release
cmake --preset release -DCMAKE_PREFIX_PATH=/path/to/Qt6
cmake --build --preset release
```

The `termsync` executable lands in `build/<preset>/bin/`.

If all dependencies come from the system (no vcpkg), use the `no-vcpkg` preset.

## Run tests

```bash
ctest --preset default
```

## Milestone status

Currently at **M1 (scaffold)**: an empty Qt 6 main window with the menu shell.
Configuring and building should produce a runnable window; no SSH/SFTP yet.
