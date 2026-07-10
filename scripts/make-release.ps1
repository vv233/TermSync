# Builds TermSync in Release and refreshes the self-contained bundle in <repo>\release.
# Run after any change you want packaged:  powershell -File scripts\make-release.ps1
#
# Produces a double-clickable release\termsync.exe (+ termsync-cli.exe) with all Qt
# and third-party runtime DLLs/plugins alongside it. Machine paths (VS 2026, Qt 6.8.3)
# match this dev box; override with -VsDir / -QtDir if they differ.
param(
    [string]$Repo  = (Split-Path -Parent $PSScriptRoot),
    [string]$VsDir = "C:\Program Files\Microsoft Visual Studio\18\Community",
    [string]$QtDir = "C:\Qt\6.8.3\msvc2022_64"
)
$ErrorActionPreference = "Stop"

$cmake  = "$VsDir\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninja  = "$VsDir\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
$vcvars = "$VsDir\VC\Auxiliary\Build\vcvars64.bat"
$build  = "$Repo\build\release"
$rel    = "$Repo\release"
$prefix = ("$QtDir;$Repo\vcpkg_installed\x64-windows") -replace '\\','/'
$srcFwd = $Repo -replace '\\','/'

foreach ($p in @($cmake, $ninja, $vcvars, $QtDir)) {
    if (-not (Test-Path $p)) { throw "Not found: $p (override -VsDir/-QtDir)" }
}

# (Re)configure if the build dir is missing or was configured for another source tree.
$needConfigure = $true
if (Test-Path "$build\CMakeCache.txt") {
    $cache = Get-Content "$build\CMakeCache.txt" -Raw
    if ($cache.Contains("TermSync_SOURCE_DIR:STATIC=$srcFwd")) { $needConfigure = $false }
}
if ($needConfigure -and (Test-Path $build)) {
    Write-Host "Reconfiguring (stale/missing cache)..."
    Remove-Item -Recurse -Force $build
}

$env:PATH = (Split-Path $cmake) + ";" + (Split-Path $ninja) + ";" + $env:PATH

if ($needConfigure) {
    $cfg = "`"$vcvars`" >nul && `"$cmake`" -S `"$Repo`" -B `"$build`" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM=`"$ninja`" -DCMAKE_PREFIX_PATH=`"$prefix`" -DTERMSYNC_BUILD_TESTS=OFF"
    cmd /c $cfg
    if ($LASTEXITCODE) { throw "configure failed" }
}

Write-Host "Building Release..."
$bld = "`"$vcvars`" >nul && `"$cmake`" --build `"$build`" --target termsync termsync_cli"
cmd /c $bld
if ($LASTEXITCODE) { throw "build failed" }

# --- Deploy into release\ --------------------------------------------------
New-Item -ItemType Directory -Force $rel | Out-Null
Copy-Item "$build\bin\termsync.exe", "$build\bin\termsync-cli.exe" $rel -Force

# Qt runtime + plugins + the VC++ redistributable runtime, next to the exe.
# Run windeployqt inside the VS env so VCINSTALLDIR is set and --compiler-runtime
# can copy vcruntime140*.dll / msvcp140.dll for a fully portable bundle.
$deploy = "`"$vcvars`" >nul && `"$QtDir\bin\windeployqt.exe`" --release --no-translations --compiler-runtime `"$rel\termsync.exe`""
cmd /c $deploy | Out-Null

# windeployqt's --compiler-runtime can miss the CRT if the exact redist version
# isn't installed. Copy the VC++ runtime explicitly (System32 matches the toolset;
# fall back to the newest VC14x.CRT redist).
$crtRedist = Get-ChildItem "$VsDir\VC\Redist\MSVC" -Directory -ErrorAction SilentlyContinue |
    ForEach-Object { Get-ChildItem "$($_.FullName)\x64" -Directory -Filter "Microsoft.VC14*.CRT" -ErrorAction SilentlyContinue } |
    Sort-Object Name | Select-Object -Last 1
foreach ($d in @("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll", "concrt140.dll")) {
    $src = if (Test-Path "$env:WINDIR\System32\$d") { "$env:WINDIR\System32\$d" }
           elseif ($crtRedist -and (Test-Path "$($crtRedist.FullName)\$d")) { "$($crtRedist.FullName)\$d" }
           else { $null }
    if ($src) { Copy-Item $src $rel -Force }
}

# Third-party runtime DLLs (release variants) from the vendored vcpkg tree.
$vbin = "$Repo\vcpkg_installed\x64-windows\bin"
foreach ($dll in @("libssh2.dll", "libcrypto-3-x64.dll", "libssl-3-x64.dll", "libcurl.dll", "legacy.dll", "z.dll", "sqlite3.dll")) {
    if (Test-Path "$vbin\$dll") { Copy-Item "$vbin\$dll" $rel -Force }
}
Copy-Item "$Repo\LICENSE", "$Repo\README.md", "$Repo\THIRD_PARTY_NOTICES.md" $rel -Force -ErrorAction SilentlyContinue

$exe = Get-Item "$rel\termsync.exe"
Write-Host ("Done. release\termsync.exe  " + $exe.LastWriteTime.ToString("yyyy-MM-dd HH:mm") + "  " + $exe.Length + " bytes")
