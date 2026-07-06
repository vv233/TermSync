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
- [x] **M6** Dual-pane browser + transfer queue  ✅ *DualPaneBrowser (local QFileSystemModel | remote SFTP) + worker-threaded SftpSession + transfer-queue panel with progress/cancel + remote mkdir/delete/rename/chmod; verified live against Rebex (download read-path; writes code-complete, need writable server)*
- [x] **M7** Sync engine (one-way + two-way) + **Synchronize dialog**  ✅ *DirectoryDiffer + SyncEngine, 13 deterministic tests. SecureFX-style Synchronize dialog now wired into the dual-pane browser: recursive remote enumeration (FileEngine::listRecursive) → direction pick → dry-run Preview (upload/download/delete/conflict counts) → Start enqueues transfers*
- [x] **M8** FTP/FTPS  ✅ *shared FileEngine interface; FtpFileEngine (libcurl) verified live against Rebex FTP (connect+list); protocol picked per-profile in the session/browser + Quick Connect protocol dropdown. FTPS wired (flag) — data-channel TLS interop tuning is a follow-up*
- [x] **M9** Public-key/agent/keyboard-interactive auth  ✅ *auth dispatch in both SSH & SFTP engines (publickey-fromfile w/ passphrase, ssh-agent identities, keyboard-interactive); Quick Connect auth-method + key-file UI; password paths regression-verified live. Public-key positive test needs a controllable server; encrypted non-keychain vault still a follow-up*
- [x] **M10** Packaging (first releasable MVP)  ✅ *About dialog, THIRD_PARTY_NOTICES, windeployqt integration + CPack (NSIS;ZIP); verified self-contained launch with Qt off PATH and produced a 35 MB distributable ZIP (NSIS .exe needs NSIS installed on the build host)*
- [x] **M11** Port forwarding (local + dynamic SOCKS)  ✅ *PortForwarder (own SSH session + QTcpServer + libssh2 direct-tcpip piping); SOCKS5 parser 9 unit tests. Live forward needs a reachable target through the server; remote-forward + Session Options panel are follow-ups*
- [x] **M12** SCP + X/Y/ZMODEM primitives  ✅ *SCP download/upload on the SSH session (libssh2_scp); X/Y/ZMODEM framing — CRC-16/XMODEM, CRC-32, 128/1024 block encode/parse, ZDLE escaping — 7 unit tests. Full interactive ZMODEM rz/sz terminal session is a follow-up*
- [x] **M13** Telnet  ✅ *AbstractTerminalConnection base lets TerminalWidget drive any protocol; TelnetConnection (IAC negotiation, NAWS, terminal-type) verified live against telehack.com; SSH regression-verified. rlogin is a thin follow-up variant*
- [x] **M14** Serial  ✅ *SerialConnection via the native OS serial API (Win32 CreateFile/ReadFile + POSIX termios) on a worker thread — no Qt SerialPort dep; wired into Quick Connect (Serial protocol, baud) and MainWindow. Code-complete; live test needs a real/virtual serial device the sandbox lacks*
- [x] **M15** Scripting engine  ✅ *QJSEngine-based JavaScript automation with a SecureCRT-style crt.Screen/crt.Session object model (Send/SendLine/WaitForString/Get/Sleep), 7 unit tests; Script→Run executes .js against the active terminal via TerminalScriptContext. Python + record/map-to-button are follow-ups*
- [x] **M16a** TN3270 host emulation  ✅ *TN3270 Telnet negotiation + EOR records; basic 3270 screen/field model (Write/EraseWrite/SBA/SF/RA/EUA), editable unprotected fields, Enter modified-field submit, Quick Connect protocol option; 2 stream parser unit tests. TN5250 and full 3270 attributes/keys remain M16b follow-ups*
- [x] **M16b** TN5250 (first pass) + full 3270 attributes/keyboard  ✅ *3270: full AID keyset (PF1-24/PA1-3/Clear, PA short-read), field navigation (Tab/Backtab/Home), extended field attributes (SFE/SA color/highlight/intensity) — 6 stream tests; TerminalWidget now sends F1-F12/Backtab. TN5250: Telnet negotiation (IBM-3179-2) + read-only Write-to-Display render (SBA/RA/text) — 2 tests. TN5250 field input/full datastream remains a follow-up*
- [x] **M17** Firewall/proxy (SOCKS5 + HTTP CONNECT)  ✅ *ProxyConfig on SshConnectionParams; proxy client handshake (SOCKS5 greeting/user-pass/CONNECT, HTTP CONNECT + Basic auth) — 6 unit tests; wired into the SSH connect path (connect to proxy → tunnel to host). Live proxy-connect needs a proxy the sandbox lacks. Advanced auth (Kerberos/GSSAPI/X.509) rides on the M9 dispatch and is a build-dependent follow-up*
- [~] **M18** Advanced transfer — **M18a parallel + M18b throughput tuning done**  ✅ *parallel multi-connection SFTP (M18a); M18b throughput levers matching fast clients (WindTerm-class): libssh2 SFTP outstanding request pipeline (read-ahead + write ACK queue), 32 KB SSH channel packet default, 1 MB upload/download buffers by default, optional nonblocking SFTP pump (`TERMSYNC_SFTP_NONBLOCK=1`), parallel connections scaled by file size (5 GB tuned: upload up to 8 lanes, download up to 6), 16 MB parallel threshold, TCP_NODELAY + 4 MB socket buffers; env-tunable (`TERMSYNC_SFTP_{DOWNLOAD,UPLOAD,BUFFER}_KB` / `_PARALLEL` / `_THRESHOLD_MB`), measurable with `sftp_bench`. Throttle/resume/relentless/bookmarks remain M18c*
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
- [x] TN3270  → M16a
- [ ] TN5250 / TVI910/925 / Wyse 50/60  → M16b/M20
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
- [ ] High-speed SFTP pipeline (async read, nonblocking I/O pump, packet/window tuning, benchmark target)  → M18
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
