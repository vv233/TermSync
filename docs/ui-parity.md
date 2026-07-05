# Feature-parity checklist (SecureCRT + SecureFX)

This is the living checklist that tracks TermSync against the two reference products.
Every official feature maps to a milestone. Tick a box (`[x]`) when it lands and is
verified. Items marked **(backlog)** are explicitly deferred.

Sources:
- SecureCRT features: https://www.vandyke.com/products/securecrt/features.html
- SecureFX features: https://www.vandyke.com/products/securefx/features.html
- SecureCRT docs (menu/dialog reference): https://documentation.help/SecureCRT/

Legend: `→ Mn` = target milestone.

---

## Milestones overview

- [x] **M1** Scaffold: CMake + Qt6 empty window + menu shell  ✅ *built & verified*
- [x] **M2** SSH2 connect + raw shell passthrough  ✅ *verified against a live SSH server (handshake + password auth + shell + raw read)*
- [x] **M3** VT100/xterm terminal rendering  ✅ *M3a parser+buffer (19 unit tests) & M3b QPainter renderer verified against live SSH output; full-screen app (vim/htop/tmux) check pending a shell-capable test server*
- [x] **M4** Session profile manager + tabbed UI + credential store  ✅ *SQLite ProfileStore (6 tests) · Windows Credential Manager round-trip verified · host-key TOFU · session tree; integrated double-click GUI flow wired (constituent parts verified)*
- [x] **M5** SFTP listing + basic transfer  ✅ *SftpFileEngine + SftpBrowserWidget; connect/auth/list verified live against Rebex; upload/download implemented (round-trip pending a writable test server)*
- [ ] **M6** Dual-pane browser + transfer queue
- [ ] **M7** Sync engine (one-way, then two-way)
- [ ] **M8** FTP/FTPS
- [ ] **M9** Credential vault + public-key/agent auth
- [ ] **M10** Packaging (first releasable MVP)
- [ ] **M11** Port forwarding (local/remote/dynamic SOCKS)
- [ ] **M12** SCP + X/Y/ZMODEM
- [ ] **M13** Telnet / rlogin
- [ ] **M14** Serial
- [ ] **M15** Scripting engine
- [ ] **M16** TN3270 / TN5250 host emulation
- [ ] **M17** Firewall/proxy + advanced auth (Kerberos/GSSAPI/X.509)
- [ ] **M18** Advanced transfer (parallel, throttle, resume, relentless, bookmarks)
- [ ] **M19** Automation/scheduler/CLI tools + remote file editing
- [ ] **M20** Terminal power features + application-level polish

---

## SecureCRT menu structure  → M4 unless noted

### File
- [ ] Connect… / Quick Connect… / Connect in Tab-Tile…
- [ ] Reconnect / Reconnect All / Disconnect / Disconnect All
- [ ] Connect SFTP Session (→ M5)
- [ ] Clone Session / Lock Session (Lock → M20)
- [ ] Save Session As…
- [ ] Print (Screen / Selection / Setup) (→ M20)
- [ ] Log Session / Raw Log Session (→ M20)
- [ ] Trace Options
- [ ] Import/Export Settings Wizard (→ M20)
- [x] Exit

### Edit  → M3/M4
- [ ] Copy / Paste / Copy and Paste / Paste (upload)
- [ ] Select All / Column (Block) Select
- [ ] Clear Screen / Clear Scrollback / Clear Screen and Scrollback / Reset
- [ ] Find…
- [ ] Copy on Select / Auto Copy

### View  → M3/M4/M20
- [ ] Menu Bar / Toolbar / Connect Bar / Command Window / Chat Window
- [ ] Button Bar (→ M20) / Status Bar / Tab Bar
- [ ] New Horizontal/Vertical Tab Group / Full Screen / Always on Top / Zoom

### Options
- [ ] Session Options… / Global Options… / Edit Default Session
- [ ] Save Settings Now / Auto Save Options / Keymap Editor…

### Transfer  → M12
- [ ] Send/Receive ASCII / Send Binary
- [ ] Send/Receive Xmodem / Ymodem / Kermit
- [ ] Zmodem Upload List / Start Zmodem Upload / Cancel

### Script  → M15
- [ ] Run… / Cancel / Start-Stop Recording Script / Map Script to Button-Key

### Tools  → M9/M15
- [ ] Create Public Key… / Public-Key Assistant / Convert Private Key
- [ ] Manage Agent Keys / Start-Stop SSH Agent / Keyboard (Keymap)…

### Window  → M4
- [ ] Cascade / Tile Horizontally-Vertically / Arrange / session list

### Help  → M10
- [ ] Help Topics / Update Now / About

---

## SecureCRT Session Options tree

- [ ] Connection (host/port/user/firewall)  → M4
  - [ ] Logon Actions  → M15
  - [ ] SSH2 (KEX/cipher/MAC/hostkey algs, auth methods, compression)  → M2/M9
  - [ ] Port Forwarding → Remote/X11  → M11
  - [ ] SFTP Session (initial dirs)  → M5
  - [ ] Advanced  → M4
- [ ] Terminal (anti-idle, scroll, bell)  → M3/M4
  - [ ] Emulation (terminal type)  → M3
    - [ ] Modes / Emacs / Mapped Keys / Advanced  → M3
  - [ ] Appearance (font, ANSI colors, cursor)  → M3
    - [ ] Window (scrollback, tab title)  → M3/M4
  - [ ] Log File  → M4
  - [ ] X/Y/Zmodem  → M12

---

## SecureCRT official feature list (vandyke.com)

### Secure Shell
- [ ] SSH1/SSH2  → M2
- [ ] Auth: password/publickey/Kerberos5/keyboard-interactive/RSA/Ed25519/ECDSA/DSA/PuTTY PPK/OpenSSH cert/X.509  → M9/M17
- [ ] Credentials management  → M20
- [ ] Public-Key Assistant  → M9
- [ ] GSSAPI key exchange  → M17
- [ ] Strong ciphers (ChaCha20-Poly1305/AES-GCM/AES/Twofish/3DES)  → M2
- [ ] Password/passphrase caching  → M4
- [ ] Port forwarding / dynamic port forwarding  → M11
- [ ] X.509 support  → M17
- [ ] OpenSSH key format / agent forwarding / SSH agent  → M9/M17
- [ ] Host key management  → M4
- [ ] X11 forwarding  → M11
- [ ] Data compression  → M2

### Emulation
- [ ] VT100/102/220/320, ANSI, SCO ANSI, Xterm, Linux console  → M3
- [ ] TN3270 / TVI910/925 / Wyse 50/60  → M16
- [ ] Xterm extensions / char attributes / Unicode / 80-132 col / configurable rows-cols / NAWS  → M3
- [ ] National Replacement Character Set  → M20
- [ ] Raw protocol mode  → M2

### Keyboard / Session / Ease of Use
- [ ] Keyboard mappings + graphical keymap editor  → M3
- [ ] Personal data folder  → M20
- [ ] Named firewalls  → M17
- [ ] Color schemes  → M3
- [ ] 128,000-line scrollback  → M3
- [ ] Emacs mode  → M3
- [ ] Command window  → M20
- [ ] Env vars in paths  → M19
- [ ] Multiple session editing  → M4
- [ ] Import/export config  → M20
- [ ] Tabbed sessions / tab groups / tiling  → M4
- [ ] Session Manager / Active Sessions Manager  → M4/M20
- [ ] Button bar / Command Manager  → M20
- [ ] Session status info  → M4
- [ ] URL and Google search support  → M20
- [ ] Simple automated logons  → M15
- [ ] Quick Connect / Connect bar  → M4
- [ ] Clipboard copy-paste / multi-line paste dialog  → M3
- [ ] Customize toolbar and menu  → M20
- [ ] Anti-idle  → M4

### Firewall
- [ ] SOCKS v4/v5 / TIS-Wingate proxy / HTTP proxy / local proxy command / dependent session  → M17

### File Transfer
- [ ] Zmodem/Xmodem/Ymodem/Kermit  → M12
- [ ] Send/receive ASCII / Send Binary  → M6/M12
- [ ] SFTP in a tab  → M5
- [ ] Drag-and-drop file transfer  → M6
- [ ] Built-in TFTP server  → M20

### Scripting
- [ ] ActiveX scripting languages / Python  → M15
- [ ] Scripting functions / script recorder / script editor tab  → M15/M19

### Logging / Printing
- [ ] Session log (parameter substitutions, rotation, command-line)  → M20
- [ ] Host-based printing / basic printing / session-or-global print settings  → M20

### Other / Application
- [ ] Real-time keyword highlighting  → M20
- [ ] RDP support  → **(backlog)**
- [ ] IPv6  → M20
- [ ] Screen font scaling / dark mode / alpha transparency  → M20
- [ ] Call from web browser  → M20
- [ ] Launch remote command on connect  → M15
- [ ] Local shell session  → M20
- [ ] Execute local shell command  → M19
- [ ] Serial device support  → M14
- [ ] Hex view / Scratchpad tab  → M20
- [ ] TAPI support  → **(backlog)**
- [ ] Delay options  → M15
- [ ] FIPS 140-2  → M20
- [ ] Session locking  → M20
- [ ] File-based configuration  → M4
- [ ] Multi-platform / auto-update / MSI / command-line utilities / integration with file view  → M10/M19/M20

---

## SecureFX official feature list (vandyke.com)

### Security
- [ ] Auth password/publickey/Kerberos5/keyboard-interactive  → M9/M17
- [ ] Credentials management (global credential sets)  → M20
- [ ] Public-Key Assistant  → M9
- [ ] Encryption ciphers  → M2
- [ ] Data integrity verification  → M5
- [ ] Host key management  → M4
- [ ] SSH Agent support  → M9
- [ ] X.509 certificate auth / GSSAPI  → M17
- [ ] PGP compatibility  → **(backlog)**

### File Transfer
- [ ] Multi-protocol SSH2/SFTP/FTPS/SCP/FTP  → M5/M8/M12
- [ ] HTTPS transfer  → **(backlog)**
- [ ] Site synchronization (upload/download/mirror)  → M7
- [ ] Synchronized file browsing  → M18
- [ ] Multiple simultaneous connections and transfers  → M18
- [ ] Complete overwrite control  → M18
- [ ] "Relentless" file transfer (auto-reconnect)  → M18
- [ ] Pause and resume transfers  → M18
- [ ] Task scheduler  → M19
- [ ] Throttle transfer bandwidth  → M18
- [ ] Parallel transfer specification  → M18
- [ ] Move files  → M18
- [ ] Transfer queue  → M6
- [ ] File transfer server support (Win/Linux/macOS, MVS/VMS → partial backlog)  → M8
- [ ] SFTP ASCII transfer option  → M18

### Ease of Use
- [ ] Tabbed sessions  → M6
- [ ] Drag-and-drop (Explorer)  → M6
- [ ] Address bar with path history  → M6
- [ ] Connect bar with autocomplete  → M4
- [ ] Bookmarks and Bookmark Manager  → M18
- [ ] Filter View with wildcards  → M6
- [ ] Quick Connect / New Session wizard  → M4
- [ ] Sound notifications  → M20
- [ ] Toolbar and menu customization  → M20
- [ ] Dark mode  → M20

### Application / Advanced
- [ ] Multi-platform / auto-update / MSI installers  → M10/M20
- [ ] Integration with terminal view  → M5
- [ ] Firewall support (SOCKS4/5, CSM, WinGate, proxies)  → M17
- [ ] Dependent session option  → M17
- [ ] Session Manager (dockable, filterable) / site organization  → M4
- [ ] Personal data folder / import-export configuration  → M20
- [ ] Busy site retry / keep-alive / quote commands / paste URL  → M6/M18/M19
- [ ] OpenSSH key format support  → M9
- [ ] Remote file editing  → M19
- [ ] Command-line automation (SFXCL)  → M19
- [ ] Change/upload/default file permissions on server  → M6/M18
- [ ] Environment variables in paths / execute local shell command  → M19
- [ ] SCP sudo command  → M12
- [ ] Resolve symbolic links / autohide dot files / filename conversion on upload / time zone config  → M18
- [ ] IPv6 / FIPS 140-2  → M20
- [ ] Section 508 compliance  → M20
