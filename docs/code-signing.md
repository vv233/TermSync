# Code Signing

Unsigned Windows binaries trigger a **SmartScreen "unrecognized app"** warning,
which scares off most users. Signing the app `.exe` and the NSIS installer with
an Authenticode certificate removes that warning (EV certificates clear
SmartScreen immediately; OV certificates clear it after enough downloads build
reputation).

Signing runs in CI via [`scripts/sign-windows.ps1`](../scripts/sign-windows.ps1).
**The private key never leaves your GitHub Actions secrets** — it is not in the
repo and is never seen by anyone. If no certificate is configured, the script
skips signing and the build still succeeds (so forks/PRs are unaffected).

---

## 1. Get a certificate

Pick one:

| Option | Cost | SmartScreen | Notes |
|---|---|---|---|
| **Azure Trusted Signing** | ~$10/mo | Good (OV) | Modern, cloud key (no .pfx to manage). Needs a verified org/individual. Recommended. |
| **OV code-signing cert** (Sectigo, DigiCert, SSL.com) | ~$150–300/yr | Reputation-based | Delivered as a `.pfx`; works with the script below. |
| **EV code-signing cert** | ~$300–500/yr | Instant | Usually on a hardware token / cloud HSM — can't export a `.pfx`, so CI needs the CA's cloud-signing action instead. |
| **Self-signed** | Free | ❌ (testing only) | Verifies the pipeline end-to-end without buying a cert. See below. |

### Self-signed certificate (to test the pipeline)

```powershell
# Create a self-signed code-signing cert (valid 3 years).
$cert = New-SelfSignedCertificate -Type CodeSigningCert `
    -Subject "CN=TermSync Test" -CertStoreLocation Cert:\CurrentUser\My `
    -KeyExportPolicy Exportable -NotAfter (Get-Date).AddYears(3)

# Export to a password-protected .pfx.
$pw = ConvertTo-SecureString "test-password" -AsPlainText -Force
Export-PfxCertificate -Cert $cert -FilePath termsync-test.pfx -Password $pw

# Base64-encode it for the GitHub secret.
[Convert]::ToBase64String([IO.File]::ReadAllBytes("termsync-test.pfx")) | Set-Clipboard
```

A self-signed cert lets you confirm the signature is applied and timestamped;
Windows will still warn because the cert isn't from a trusted CA.

---

## 2. Add the GitHub secrets

Repo → **Settings → Secrets and variables → Actions → New repository secret**:

| Secret name | Value |
|---|---|
| `WINDOWS_CERTIFICATE` | Base64 of the `.pfx` (the clipboard string above) |
| `WINDOWS_CERTIFICATE_PASSWORD` | The `.pfx` password |

---

## 3. Wire it into `release.yml`

**This is already wired up.** `release.yml` has a **Sign application** step
(after `windeployqt`, before packaging) and a **Sign installer** step (after
NSIS). Both read `WINDOWS_CERTIFICATE` / `WINDOWS_CERTIFICATE_PASSWORD` from
secrets and no-op if they're empty — so once you add the two secrets above, the
next tagged release is signed automatically. Nothing else to change.

The steps look like this (for reference):

```yaml
      - name: Sign application executable
        shell: pwsh
        env:
          WINDOWS_CERTIFICATE: ${{ secrets.WINDOWS_CERTIFICATE }}
          WINDOWS_CERTIFICATE_PASSWORD: ${{ secrets.WINDOWS_CERTIFICATE_PASSWORD }}
        run: pwsh scripts/sign-windows.ps1 -Path "build/release/bin/termsync.exe"

      # ... run CPack / NSIS here to produce TermSync-<ver>-Setup.exe ...

      - name: Sign installer
        shell: pwsh
        env:
          WINDOWS_CERTIFICATE: ${{ secrets.WINDOWS_CERTIFICATE }}
          WINDOWS_CERTIFICATE_PASSWORD: ${{ secrets.WINDOWS_CERTIFICATE_PASSWORD }}
        run: pwsh scripts/sign-windows.ps1 -Path "build/**/TermSync-*-Setup.exe"
```

Adjust the paths to match where your workflow puts the exe and installer. Sign
the exe **before** packaging so the copy inside the zip/installer is signed too,
then sign the installer itself.

### Azure Trusted Signing instead of a .pfx

If you use Azure Trusted Signing, replace the two `sign-windows.ps1` steps with
the official action (no `.pfx`, no password secret):

```yaml
      - uses: azure/trusted-signing-action@v0
        with:
          azure-tenant-id: ${{ secrets.AZURE_TENANT_ID }}
          azure-client-id: ${{ secrets.AZURE_CLIENT_ID }}
          azure-client-secret: ${{ secrets.AZURE_CLIENT_SECRET }}
          endpoint: https://wcus.codesigning.azure.net/
          trusted-signing-account-name: <your-account>
          certificate-profile-name: <your-profile>
          files-folder: build/release/bin
          files-folder-filter: exe
```

---

## 4. Local signing (optional)

```powershell
pwsh scripts/sign-windows.ps1 `
    -Path build/release/bin/termsync.exe `
    -PfxPath C:\certs\termsync.pfx `
    -Password (Read-Host "PFX password")
```

Verify any signed binary:

```powershell
signtool verify /pa /v build/release/bin/termsync.exe
```

---

## 5. macOS (for when a mac build is added)

macOS needs both **codesign** (with a "Developer ID Application" certificate)
and **notarization** by Apple, or Gatekeeper blocks the app:

```bash
# Sign the .app (deep, hardened runtime).
codesign --deep --force --options runtime --timestamp \
  --sign "Developer ID Application: Your Name (TEAMID)" TermSync.app

# Notarize the .dmg and staple the ticket.
xcrun notarytool submit TermSync.dmg --apple-id "$APPLE_ID" \
  --team-id "$TEAM_ID" --password "$APP_SPECIFIC_PASSWORD" --wait
xcrun stapler staple TermSync.dmg
```

Store `APPLE_ID`, `TEAM_ID`, and an app-specific password as GitHub secrets and
run these in the macOS release job. (macOS CI is currently disabled pending the
Qt 6.8 / AGL build fix — see the CI notes.)
