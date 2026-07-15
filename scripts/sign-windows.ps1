<#
.SYNOPSIS
    Authenticode-signs Windows binaries (the app exe and the NSIS installer).

.DESCRIPTION
    Works both locally and in CI. The signing certificate is supplied either as
    a base64-encoded .pfx (env var / secret — used in CI) or as a path to a .pfx
    file (used locally). The private key never has to be committed or pasted
    anywhere except a GitHub Actions secret.

    If no certificate is available the script prints a warning and exits 0, so
    unsigned developer/PR builds still succeed.

.PARAMETER Path
    One or more files (or globs) to sign, e.g. build\bin\termsync.exe.

.PARAMETER CertBase64
    Base64 of the .pfx. Defaults to $env:WINDOWS_CERTIFICATE (the CI secret).

.PARAMETER PfxPath
    Path to a .pfx file (alternative to CertBase64, for local signing).

.PARAMETER Password
    The .pfx password. Defaults to $env:WINDOWS_CERTIFICATE_PASSWORD.

.PARAMETER TimestampUrl
    RFC-3161 timestamp server (default: DigiCert). Timestamping keeps the
    signature valid after the certificate expires.

.PARAMETER Description
    Signature description shown in the UAC / properties dialog.

.EXAMPLE
    # Local, using a .pfx on disk:
    pwsh scripts/sign-windows.ps1 -Path build/bin/termsync.exe `
        -PfxPath C:\certs\termsync.pfx -Password (Read-Host -AsSecureString)

.EXAMPLE
    # CI: WINDOWS_CERTIFICATE + WINDOWS_CERTIFICATE_PASSWORD come from secrets.
    pwsh scripts/sign-windows.ps1 -Path "dist/**/*.exe"
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string[]]$Path,
    [string]$CertBase64 = $env:WINDOWS_CERTIFICATE,
    [string]$PfxPath,
    [string]$Password = $env:WINDOWS_CERTIFICATE_PASSWORD,
    [string]$TimestampUrl = 'http://timestamp.digicert.com',
    [string]$Description = 'TermSync'
)

$ErrorActionPreference = 'Stop'

function Find-SignTool {
    $cmd = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    # Search the Windows 10/11 SDK, newest version first, prefer x64.
    $roots = @("${env:ProgramFiles(x86)}\Windows Kits\10\bin",
               "${env:ProgramFiles}\Windows Kits\10\bin")
    foreach ($root in $roots) {
        if (-not (Test-Path $root)) { continue }
        $found = Get-ChildItem $root -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\x64\\' } |
            Sort-Object FullName -Descending | Select-Object -First 1
        if ($found) { return $found.FullName }
    }
    return $null
}

# --- Resolve the certificate ------------------------------------------------
$tempPfx = $null
try {
    if (-not $PfxPath -and $CertBase64) {
        $tempPfx = Join-Path ([System.IO.Path]::GetTempPath()) "termsync-sign-$([guid]::NewGuid()).pfx"
        [IO.File]::WriteAllBytes($tempPfx, [Convert]::FromBase64String($CertBase64))
        $PfxPath = $tempPfx
    }

    if (-not $PfxPath -or -not (Test-Path $PfxPath)) {
        Write-Warning "No signing certificate provided (set WINDOWS_CERTIFICATE or -PfxPath). Skipping signing; binaries will be UNSIGNED."
        exit 0
    }

    $signtool = Find-SignTool
    if (-not $signtool) {
        Write-Error "signtool.exe not found. Install the Windows SDK (or add it to PATH)."
        exit 1
    }
    Write-Host "Using signtool: $signtool"

    # --- Expand the file list -----------------------------------------------
    $files = @()
    foreach ($p in $Path) {
        $matches = Get-ChildItem -Path $p -Recurse -ErrorAction SilentlyContinue -File
        if ($matches) { $files += $matches.FullName } else { $files += $p }
    }
    $files = $files | Where-Object { Test-Path $_ } | Select-Object -Unique
    if (-not $files) { Write-Warning "No files matched $Path"; exit 0 }

    # --- Sign + verify each file --------------------------------------------
    foreach ($file in $files) {
        Write-Host "Signing $file"
        & $signtool sign `
            /f $PfxPath /p $Password `
            /fd SHA256 /tr $TimestampUrl /td SHA256 `
            /d $Description $file
        if ($LASTEXITCODE -ne 0) { throw "signtool sign failed for $file (exit $LASTEXITCODE)" }

        # Verify is a sanity check. It fails for self-signed/untrusted roots
        # (expected during pipeline testing), so it only warns — the sign step
        # above already hard-fails if signing itself did not work.
        & $signtool verify /pa /v $file
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "signtool verify reported the chain is not trusted for $file. This is expected for a self-signed test cert; a real CA cert should verify cleanly."
        }
    }
    Write-Host "Signed $($files.Count) file(s) successfully."
}
finally {
    if ($tempPfx -and (Test-Path $tempPfx)) { Remove-Item $tempPfx -Force }
}
