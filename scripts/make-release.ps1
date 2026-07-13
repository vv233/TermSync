# Builds the TermSync pre-release directly into <repo>\release.
# Run after any change you want packaged:  powershell -File scripts\make-release.ps1
#
# /release is the project's only distribution output. The script refreshes the
# runnable bundle and creates one versioned ZIP beside it.
param(
    [string]$Repo  = (Split-Path -Parent $PSScriptRoot),
    [string]$VsDir = "C:\Program Files\Microsoft Visual Studio\18\Community",
    [string]$QtDir = "C:\Qt\6.8.3\msvc2022_64"
)
$ErrorActionPreference = "Stop"

$cmake  = "$VsDir\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninja  = "$VsDir\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
$vcvars = "$VsDir\VC\Auxiliary\Build\vcvars64.bat"
$build  = "$Repo\build\release-work"
$rel    = "$Repo\release"
$prefix = ("$QtDir;$Repo\vcpkg_installed\x64-windows") -replace '\\','/'
$srcFwd = $Repo -replace '\\','/'
$relFwd = $rel -replace '\\','/'

foreach ($p in @($cmake, $ninja, $vcvars, $QtDir)) {
    if (-not (Test-Path $p)) { throw "Not found: $p (override -VsDir/-QtDir)" }
}

# Stop bundled executables before replacing the release directory.
Get-Process termsync, termsync-cli -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -and $_.Path.StartsWith($rel) } |
    Stop-Process -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $rel | Out-Null
Get-ChildItem -LiteralPath $rel -Force | Remove-Item -Recurse -Force

# (Re)configure if the build dir is missing or was configured for another source tree.
$needConfigure = $true
if (Test-Path "$build\CMakeCache.txt") {
    $cache = Get-Content "$build\CMakeCache.txt" -Raw
    if ($cache.Contains("TermSync_SOURCE_DIR:STATIC=$srcFwd") -and
        $cache.Contains("CMAKE_RUNTIME_OUTPUT_DIRECTORY:UNINITIALIZED=$relFwd")) {
        $needConfigure = $false
    }
}
if ($needConfigure -and (Test-Path $build)) {
    Write-Host "Reconfiguring (stale/missing cache)..."
    Remove-Item -Recurse -Force $build
}

$env:PATH = (Split-Path $cmake) + ";" + (Split-Path $ninja) + ";" + $env:PATH

if ($needConfigure) {
    $cfg = "`"$vcvars`" >nul && `"$cmake`" -S `"$Repo`" -B `"$build`" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=`"$relFwd`" -DCMAKE_MAKE_PROGRAM=`"$ninja`" -DCMAKE_PREFIX_PATH=`"$prefix`" -DTERMSYNC_BUILD_TESTS=OFF"
    cmd /c $cfg
    if ($LASTEXITCODE) { throw "configure failed" }
}

Write-Host "Building Release..."
$bld = "`"$vcvars`" >nul && `"$cmake`" --build `"$build`" --target termsync termsync_cli"
cmd /c $bld
if ($LASTEXITCODE) { throw "build failed" }
foreach ($exeName in @("termsync.exe", "termsync-cli.exe")) {
    if (-not (Test-Path "$rel\$exeName")) {
        throw "Release build did not produce $rel\$exeName"
    }
}

# Qt runtime and plugins next to the executable. The VC++ runtime is copied
# explicitly below because windeployqt may look for a different toolset version.
$deploy = "`"$vcvars`" >nul && `"$QtDir\bin\windeployqt.exe`" --release --no-translations --no-compiler-runtime `"$rel\termsync.exe`""
cmd /c $deploy | Out-Null

# Copy the VC++ runtime explicitly (System32 matches the active toolset; fall
# back to the newest VC14x.CRT redist).
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
Copy-Item "$Repo\LICENSE", "$Repo\README.md", "$Repo\THIRD_PARTY_NOTICES.md" $rel -Force
Copy-Item "$Repo\docs\release-notes.md" "$rel\RELEASE_NOTES.md" -Force

# Build the archive outside /release so it cannot include itself, then move the
# completed artifact into the single release output directory.
$version = (Get-Content "$build\termsync-version.txt" -Raw).Trim()
$zipName = "TermSync-$version-win64.zip"
$zipTemp = Join-Path ([IO.Path]::GetTempPath()) $zipName
Remove-Item -LiteralPath $zipTemp -Force -ErrorAction SilentlyContinue
Compress-Archive -Path "$rel\*" -DestinationPath $zipTemp -CompressionLevel Optimal
Move-Item -LiteralPath $zipTemp -Destination "$rel\$zipName" -Force

# A stale configuration may have left old executables under the intermediate
# build tree. They are never distribution artifacts.
Remove-Item -LiteralPath "$build\bin" -Recurse -Force -ErrorAction SilentlyContinue

$exe = Get-Item "$rel\termsync.exe"
Write-Host ("Done. release\$zipName  " + $exe.LastWriteTime.ToString("yyyy-MM-dd HH:mm"))
