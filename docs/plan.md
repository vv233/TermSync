# 计划：SecureCRT + SecureFX 合体软件（开源，Qt6/C++）

> 📌 **这是本项目已批准的总体设计与路线图**（TermSync）。当前进度：**M1 ✅ · M2 SSH2 连接 ✅ · M3 VT100/xterm 渲染 ✅ · M4 会话管理+凭据存储 ✅** 均已构建并验证（25 项单测全绿；终端渲染连真实 SSH 出图；凭据存储 Windows Credential Manager 往返验证）。
> 功能落地进度见 [`ui-parity.md`](ui-parity.md)。本文件是长期参照，随重大决策更新。
>
> **实现中的两处务实偏离（相对原计划）**：① 配置持久化用 **Qt6::Sql 内置的 QSQLITE 驱动**（而非 vcpkg sqlite3），零额外依赖、已验证；② 凭据存储 M4 先用 **Windows 凭据管理器原生 API**（而非 QtKeychain，避免 vcpkg-Qt 与官方 Qt 冲突），接口 `CredentialStore` 已抽象，M9 再补 QtKeychain/libsecret 后端跨平台。

## Context（背景 / 为什么做）

从零构建一个开源的跨平台桌面客户端，把 **SecureCRT**（SSH2 终端仿真 + 会话管理）和 **SecureFX**（SFTP/FTP 文件传输 + 目录同步）的核心功能合并到**同一个应用**里。核心价值点是二者共享同一套连接/认证逻辑：一个"连接配置"既能开终端标签，也能开文件浏览标签，复用同一条已认证的 SSH 连接（这正是真实 SecureCRT/SecureFX 共享会话引擎的做法）。

已确认的决策（不再重新讨论）：
- **技术栈**：跨平台原生 C++ + **Qt 6**（Windows / Linux / macOS）
- **MVP 范围（第一批交付）**：① SSH2 终端仿真 + 基本会话管理；② SFTP/FTP 文件传输 + 双栏浏览器 + 目录同步
- **用途**：开源软件（用自己的原创名字，避免直接叫 SecureCRT/SecureFX 触碰 VanDyke 商标；本计划里用占位名 **"TermSync"**）
- **完整功能目标（纳入规划，非 MVP 但都要做）**：Telnet / Serial(串口) / rlogin 协议、脚本引擎（Python + VBScript/JScript 自动化）、完整 ZMODEM（以及 XMODEM/YMODEM）、TN3270/TN5250 主机仿真、SCP、端口转发（本地/远程/动态 SOCKS）UI
- **UI/操作流程要求**：**照抄 SecureCRT 和 SecureFX** 的界面布局与操作流程（菜单结构、工具栏、会话管理器树、标签/分屏、连接栏、快捷键、对话框流程、双栏文件浏览器交互），做到老用户零学习成本迁移（视觉资产用原创图标/配色，避免直接复制受版权保护的图标位图）

---

## 1. 架构总览（分层，库优先）

```
src/app       —  main()、应用启动、依赖装配
src/ui        —  MainWindow、标签管理、会话对话框、双栏浏览器、同步对话框
src/terminal  —  VT 解析器 + 屏幕缓冲 + QPainter 渲染的终端控件
src/transfer  —  SFTP/FTP 文件引擎、同步引擎、传输队列
src/core      —  共享骨干：
                 · ConnectionProfile（数据模型）
                 · ProfileStore（SQLite 持久化）
                 · CredentialStore（QtKeychain / 加密本地保险库）
                 · SshConnection（libssh2 封装：传输+认证+多通道）
                 · FtpConnection（libcurl 封装）
                 · Session（持有 1 条已认证 SSH 传输，可派生 1 个 shell 通道 + N 个 sftp 通道）
third_party / vcpkg.json  —  libssh2、curl、qtkeychain、sqlite3、nlohmann-json、gtest
```

**终端与文件传输如何共享连接/认证（架构核心）**：一个 `ConnectionProfile` → 一条已认证传输 → 多个逻辑通道。`core::Session::fromProfile()` 持有一个 `SshConnection`（libssh2 的 `LIBSSH2_SESSION*`，单 socket），只认证一次；`openShellChannel()` 给终端标签，`openSftpChannel()` 给文件浏览器。libssh2 原生支持一条传输多路复用多个通道，因此"同一主机的终端标签 + 文件标签"复用同一次认证、同一次 known_hosts 信任决策。FTP/FTPS 配置没有共享终端（`FtpConnection` 独立），UI 里对 FTP 配置不提供"打开终端"入口。

**线程模型**：UI 线程绝不阻塞在网络 I/O 上。每个 `Session` 在独立 `QThread` 上跑 libssh2 事件循环（libssh2 会话非线程安全，同一会话的所有通道必须在同一线程上泵）。数据通过 `Qt::QueuedConnection` 信号跨线程传给 UI。传输队列另起 worker 线程，避免大文件上传卡住键盘回显。

**分层约束**：`src/core` 不依赖 Qt Widgets（可依赖 Qt Core/Network），可无头测试；`src/terminal` 与 `src/transfer` 都依赖 `core` 但彼此不依赖；只有 `src/ui` 允许依赖全部。

---

## 2. 第三方库选型（含许可证理由）

| 领域 | 选择 | 许可证 | 理由 / 备选 |
|---|---|---|---|
| SSH2/SFTP | **libssh2** | BSD-3 | 宽松许可，纯客户端（我们不做服务端），多通道/非阻塞模型契合线程设计。备选 libssh（LGPL）留作接口后的可替换回退。拒绝 wolfSSH（GPLv3/商业双授权）。 |
| 终端仿真 | **自研 VT 解析器 + QPainter 渲染** | 我们自己（可 MIT/Apache-2.0） | 两个理由：① qtermwidget 是 GPLv2+，链接会污染整个应用许可；② qtermwidget 的 `Pty` 抽象绑定本地子进程，用远程 SSH 字节流驱动它很别扭。自研约 2000–4000 行（CSI/OSG/ESC 状态机 + Cell 屏幕缓冲 + 回滚环 + paintEvent），可完全掌控选区/回滚/reflow。备选 qtermwidget 仅在愿意接受 GPLv2+ 时作快速原型。 |
| FTP/FTPS | **libcurl** | curl(≈MIT) | 事实标准，FTP+FTPS+断点续传开箱即用；FTPS 只是 `CURLOPT_USE_SSL` 一个开关，几乎零成本可顺带做。 |
| 凭据存储 | **QtKeychain** | MIT | 对接 Windows 凭据管理器 / macOS Keychain / Linux Secret Service。无 keychain 环境回退到加密本地保险库（AES-256-GCM + Argon2id，列为 v1.1 硬化项）。 |
| 配置持久化 | **SQLite** | 公有领域 | 关系型（profile ↔ N 个 sync_pair）、事务写入、`user_version` 迁移。单会话导出格式用 JSON（`nlohmann-json`）便于分享。 |
| 构建/依赖 | **CMake ≥3.25 + CMakePresets + vcpkg（manifest 模式）** | — | 非 Qt 依赖走 vcpkg 固定版本；Qt6 本体让贡献者用官方安装器，`find_package(Qt6)` 定位。 |
| 打包 | windeployqt+WiX/NSIS、linuxdeployqt+AppImage/deb/rpm、macdeployqt+dmg | — | 标准 Qt 打包；商店分发延后。 |
| 测试 | GoogleTest | BSD-3 | 见第 7 节。 |
| 串口(M14) | **Qt SerialPort** | LGPL(Qt) | Qt 官方模块，跨平台串口。 |
| 脚本(M15) | **pybind11 + CPython** 嵌入；VBScript/JScript 兼容层 | BSD/PSF | 暴露 SecureCRT 风格对象模型；VBScript 在 Windows 用 Active Scripting，跨平台 JScript 子集用 QuickJS(MIT)。 |
| ZMODEM(M12) | 自研 X/Y/ZMODEM 帧编解码（参考 lrzsz 协议规格） | 我们自己 | 与终端字节流集成，避免 GPL 的 lrzsz 代码污染。 |

---

## 3. 仓库目录结构

```
/  (repo root)
├── CMakeLists.txt, CMakePresets.json
├── vcpkg.json, vcpkg-configuration.json
├── LICENSE (MIT 或 Apache-2.0), README.md
├── docs/            architecture.md, building.md, data-model.md
├── src/
│   ├── app/         main.cpp, 启动装配
│   ├── core/
│   │   ├── model/       ConnectionProfile, SyncPairDefinition, 枚举
│   │   ├── store/       ProfileStore(SQLite), JSON 导入导出
│   │   ├── credential/  CredentialStore 接口, QtKeychainStore, 保险库回退
│   │   ├── ssh/         SshConnection, SshChannel (libssh2)
│   │   ├── ftp/         FtpConnection (libcurl)
│   │   └── session/     Session (串联 model+ssh/ftp+credential)
│   ├── terminal/    vt/(VtParser 状态机)  screen/(ScreenBuffer,Cell,回滚)  widget/(TerminalWidget 渲染+输入)
│   ├── transfer/    sftp/  ftp/  sync/(SyncEngine,Differ,ConflictResolver)  queue/(TransferQueue,ProgressReporter)
│   ├── ui/          mainwindow/  session_dialogs/  terminal_view/  transfer_view/  common/
│   └── platform/    OS 特定薄封装
├── third_party/     (理想为空，尽量走 vcpkg)
├── resources/       图标, .qrc, 默认主题/键位 JSON
├── packaging/       windows/  linux/  macos/
├── tests/           unit/  integration/(docker-compose: OpenSSH + vsftpd)
└── scripts/         dev-setup.ps1/.sh, run-integration-tests.sh
```

---

## 3b. UI / 操作流程（照抄 SecureCRT + SecureFX）

目标：老用户从 SecureCRT/SecureFX 迁移**零学习成本**。逐项对齐两款软件的界面与交互（自绘原创图标/配色，不复制原版位图资产）。

**主窗口整体（对齐 SecureCRT/SecureFX 外壳）**：
- 顶部 **菜单栏**：File / Edit / View / Options / Transfer / Script / Tools / Window / Help（与 SecureCRT 一致的分组与项）。
- **工具栏**：连接、快速连接、断开、会话管理器切换、日志、打印、剪贴板、查找等按钮，位置/顺序照抄。
- **Session Manager 侧栏（Connect 对话框 + 停靠树）**：左侧可停靠的会话树（文件夹分层组织 profile），双击连接；顶部 Connect 对话框列出会话，与 SecureCRT `Connect` 窗口一致（列：Name/Host/Port/Protocol/Description，右键菜单 Connect/Connect in Tab/Properties/Rename/Delete）。
- **标签 + 分屏（Tabbed/Tiled）**：终端多标签，支持水平/垂直分屏（tile），标签右键菜单（Clone Session、Disconnect、Rename Tab…）照抄 SecureCRT。
- **底部状态栏**：连接状态、行列尺寸、大小写/滚动锁、传输进度、日志指示。
- **Command Window / Chat 栏**（SecureCRT 底部命令输入，可同时发送到多会话）：对齐"Send commands to all sessions / all tabs"功能。

**连接/会话对话框流程（照抄 SecureCRT Quick Connect + Session Options）**：
- **Quick Connect** 对话框：Protocol 下拉(SSH2/SSH1/Telnet/Serial/rlogin/TAPI)、Hostname、Port、Firewall、Username、认证方式勾选列表（Password/PublicKey/Keyboard Interactive/GSSAPI）、"Save session""Open in a tab"复选框——逐字段对齐。
- **Session Options** 属性页（左侧分类树 + 右侧面板）：Connection / Logon Actions / SSH2 / Port Forwarding / Terminal / Emulation / Modes / Emacs / Mapped Keys / Advanced / Appearance / Window / Log File / X/Y/Zmodem / File Transfer ——分类与 SecureCRT 一致。

**SecureFX 文件传输 UI（照抄双栏 + 传输队列）**：
- **双栏浏览器**：左=本地，右=远程（或可切换单栏），地址栏 + 路径下拉 + 上一级/刷新按钮 + 过滤框；列表列 Name/Size/Type/Modified/Attributes/Owner，可排序，与 SecureFX 一致。
- **传输方式**：拖拽、双击下载、右键 Upload/Download、工具栏箭头按钮；**Transfer Queue** 面板（底部停靠）显示队列项、进度条、速率、剩余时间、暂停/恢复/取消，照抄 SecureFX 队列窗口。
- **同步对话框（Synchronize）**：照抄 SecureFX `Synchronize` 窗口——Local/Remote 路径、方向单选（Upload/Download/Keep Newer/Mirror）、过滤、Preview（dry-run 列表）后 Start。
- **地址栏协议切换**：SFTP/FTP/FTPS/SCP 会话在同一浏览器内。

**实现方式**：UI 用 Qt Widgets（QMainWindow + QDockWidget 停靠、QMdiArea/自定义标签+分屏、QTreeView 会话树、QTableView 文件列表），QSS 样式表模拟 SecureCRT/SecureFX 视觉。所有对话框流程/菜单项/快捷键做成与原版一一对应的清单，在 `docs/ui-parity.md` 里逐条勾对。

---

## 4. 数据模型

`ConnectionProfile`（同时支撑终端与文件传输的共享实体）：
- `id`(UUID), `name`, `folder_path`(树形组织)
- `protocol` ∈ {SSH2, SFTP_ONLY, FTP, FTPS}（SSH2 = 终端+SFTP 都可用）
- `host`, `port`(默认 22/21)
- `auth_method` ∈ {PASSWORD, PUBLIC_KEY, KEYBOARD_INTERACTIVE, AGENT}
- `username`, `credential_ref`(指向 CredentialStore 的不透明句柄；null=每次询问)
- `private_key_path?`, `host_key_fingerprint?`(首次连接信任后固定)
- `terminal_settings`(term_type, 编码, 配色), `ftp_settings?`(被动模式, 显式 TLS)
- `startup_directory{local?, remote?}`, `sync_pairs[]`(0..N)

`SyncPairDefinition`：`local_path`, `remote_path`, `direction`∈{LOCAL_TO_REMOTE, REMOTE_TO_LOCAL, TWO_WAY}, `compare_strategy`∈{MTIME_SIZE, CHECKSUM}, `conflict_policy`∈{NEWER_WINS, PROMPT, SKIP, KEEP_BOTH}, `include/exclude_patterns[]`, `delete_orphans`, `last_run_at?`

**关键安全约束**：SQLite 只存 `credential_ref`（查找键），**绝不**存明文密钥；真实密钥存 OS keychain（QtKeychain）。SQLite 表：`profiles`、`sync_pairs`(FK)、`connection_history`(MRU)、`known_hosts_overrides`。

---

## 5. 同步引擎设计

每个 `SyncPairDefinition` 走三段流水线：
1. **枚举**：本地 `QDirIterator` + 远程 `SftpFileEngine::listRecursive` → 两张 `相对路径 → FileMeta{size,mtime,isDir}` 映射，枚举时即应用 glob 过滤。
2. **Diff** → `SyncAction` 列表：仅本地有=上传/删除；仅远程有=镜像；两侧都有按 `compare_strategy` 比较（MTIME_SIZE 默认，含 2s 时钟偏差容忍；CHECKSUM 用 xxHash64，只为变更检测非安全）。两侧都变=冲突，按 `conflict_policy` 处理。**双向同步**需 `last_sync_state` 快照表（sync_pair_id+相对路径→上次同步时两侧 size/mtime），做三方比较才能区分"本地删除"与"从未存在"。
3. **执行**：Diff 结果作为 `TransferTask` 喂给共享 `TransferQueue`（与拖拽传输同一条队列，无独立代码路径），每 profile 2–4 并发（用 SFTP 请求流水线而非开 N 个通道），信号上报进度，失败收集为运行后报告而非首错即停。

**Dry-run**：Diff 输出天然就是预览（"12 上传 / 3 删除 / 1 冲突"），执行前弹确认框，也是集成测试的断言对象。建议**先做单向同步**（无状态、简单、已很有用），双向 + 状态表作为同一里程碑内的快速跟进。

---

## 6. 分阶段构建顺序（每个里程碑可独立演示）

- **M1 脚手架**：CMake+Presets+vcpkg；空 Qt6 MainWindow + 标签壳；CI 三平台构建。*出口：三系统能构建出可运行的空窗口。*
- **M2 SSH2 连接 + 原始透传**：`SshConnection`(libssh2) 连接/认证/开 shell 通道/读写原始字节，先丢进 `QPlainTextEdit`。*出口：连真实服务器跑 `ls`，看到（带转义的）原始输出并能执行命令。*
- **M3 VT100/xterm 渲染**：`VtParser` 状态机 + `ScreenBuffer` + `TerminalWidget`(QPainter)。光标/颜色(16/256/truecolor)/备用屏幕缓冲/resize/复制粘贴/回滚。**最大里程碑**，拆 M3a(解析器+缓冲+无头单测) / M3b(渲染+输入+resize)。*出口：`vim`/`htop`/`tmux` 正常渲染可用。*
- **M4 会话管理 + 标签 UI**：`ConnectionProfile`+SQLite `ProfileStore`；新建会话对话框；会话树侧栏；多会话标签；接入 QtKeychain 存/取密码；host-key TOFU 提示 + 持久化。*出口：存带密码的 profile→重启→双击免密连接开新标签；同时开两个不同主机标签。*
- **M5 SFTP 列目录 + 基本传输**：`SftpFileEngine`(list/stat/mkdir/get/put)；单栏远程列表；单文件上传下载 + 进度框。复用 M4 的已认证 `Session`（同连接开第二通道）。*出口：同一连接上同时开"终端"和"文件"标签，浏览远程目录，上传下载文件。*
- **M6 双栏浏览器**：本地栏(`QFileSystemModel`)+远程栏并排；拖拽/右键双向传输；`TransferQueue` 正式化（多文件、可取消、并发、单文件+聚合进度）；远程重命名/删除/chmod。*出口：拖 5 文件+子目录到远程，看队列进度全部到达；传输中干净取消。*
- **M7 同步引擎**：`DirectoryDiffer` + 单向同步(双方向) + dry-run 预览，接入 profile 编辑器的 `SyncPairDefinition` CRUD；`last_sync_state` 表 + 双向同步 + 冲突策略 UI 作为下半程。*出口：定义同步对，连跑两次单向(第二次 0 变更)；两侧各改一文件跑双向，确认冲突检测与按策略解决。*
- **M8 FTP/FTPS**：`FtpConnection`(libcurl)+`FtpFileEngine` 实现与 `SftpFileEngine` 相同接口（使 transfer/UI 协议无关）；新建会话对话框加 FTP/FTPS 类型（无终端标签）。*出口：双栏与同步对 FTP 与 SFTP 行为一致，UI 除类型选择外无改动。*
- **M9 凭据硬化 + 公钥/agent 认证**：SSH 公钥认证(文件+带口令私钥)、SSH-agent；无 keychain 环境的加密保险库回退；键盘交互(2FA/OTP)流程。*出口：用带口令私钥（口令取自 keychain）连接；对要求 OTP 的服务器在握手中被正确提示。*
- **M10 首个可发布版打包**：三平台部署工具 + 安装器(WiX/AppImage/dmg) via CPack；图标/关于框/崩溃安全设置；用户文档；许可证审计（LICENSE + 第三方声明）。*出口：非开发者下载单个安装包即可运行 MVP（SSH 终端 + SFTP/FTP + 同步），无需单独装 Qt/依赖。*

**—— 以下为完整功能里程碑（MVP 之后继续，全部纳入规划）——**

- **M11 端口转发 UI（本地/远程/动态 SOCKS）**：libssh2 已有转发原语，此里程碑做 Session Options 里的 Port Forwarding 面板 + 动态 SOCKS 代理，交互照抄 SecureCRT。*出口：配置本地端口转发访问远程内网服务；开动态 SOCKS 代理供浏览器用。*
- **M12 SCP + X/Y/ZMODEM 文件传输**：SCP 走 `libssh2_scp_*` 复用 `SshConnection`；在终端内实现 ZMODEM（及 XMODEM/YMODEM）自动侦测 `rz/sz` 触发的传输（SecureCRT 招牌功能），对齐 Session Options 的 X/Y/Zmodem 面板与传输进度框。*出口：终端里执行 `sz file` 自动弹出下载；`rz` 自动上传。*
- **M13 Telnet / rlogin 协议**：新增 `TelnetConnection`/`RloginConnection`（Telnet 选项协商 IAC、NAWS 窗口大小、终端类型），复用现有 `TerminalWidget` 渲染与会话/标签体系；Quick Connect 协议下拉加入。*出口：连 Telnet/rlogin 服务器正常交互。*
- **M14 Serial(串口)**：`SerialConnection` 用 Qt SerialPort，Session Options 加串口面板（波特率/数据位/停止位/校验/流控），复用终端渲染。*出口：连本地串口设备（或虚拟串口对）收发数据。*
- **M15 脚本引擎（自动化）**：嵌入脚本运行时 + 暴露自动化对象模型（对齐 SecureCRT 的 `crt.Screen` / `crt.Session` / `crt.Dialog` API 语义）。首选嵌入 **Python**（pybind11 暴露对象模型），并提供 VBScript/JScript 兼容层（Windows 用 Active Scripting，跨平台可用 QuickJS 跑 JScript 子集）；Script 菜单的 Run/Record/Map to button。*出口：录制一段登录+发命令脚本并回放；脚本读取屏幕文本并据此分支。*
- **M16 主机仿真 TN3270 / TN5250**：IBM 主机仿真（3270/5250 数据流解析 + 字段属性 + 键盘映射 + 屏幕渲染），作为独立仿真模式接入终端标签体系。*出口：连 TN3270 主机看到正确的字段化屏幕并可提交。*

- **M17 防火墙/代理 + 高级认证**：命名防火墙、SOCKS4/5、HTTP 代理、TIS/WinGate、本地代理命令、Dependent Session（依赖会话/跳板）；Kerberos v5 / GSSAPI 密钥交换、X.509 证书认证、SSH-agent 转发、OpenSSH 证书、多种密钥格式(RSA/Ed25519/ECDSA/DSA/PuTTY PPK)。*出口：经 SOCKS5 代理连内网主机；用 Ed25519 密钥 + agent 转发连跳板后到目标。*
- **M18 传输能力增强（SecureFX 高级）**：多连接/并行传输、带宽限速、暂停/恢复、"Relentless"断线自动重连续传、覆盖控制、移动文件、忙站重试、保活、同步浏览(两栏联动)、书签/书签管理器、SFTP ASCII 模式、上传权限/权限保留、符号链接解析、文件名大小写转换。*出口：限速下并行传大目录，中途断网自动重连续传，暂停/恢复正常。*
- **M19 自动化/调度/命令行工具**：任务调度器(定时同步/传输)、命令行自动化工具(对齐 SecureFX 的 SFXCL 与 SecureCRT 命令行选项)、脚本编辑器标签页 + 脚本录制器、ActiveX 多语言(Windows) + Python 全平台、远程文件编辑(下载→本地编辑器→回传)、Execute Local Shell Command、Quote/Raw 命令。*出口：命令行工具无 GUI 完成一次 SFTP 同步；调度任务每天定时跑；远程文件双击本地编辑保存自动回传。*
- **M20 终端强力功能 + 应用级**：实时关键字高亮、Hex View、Scratchpad 标签、本地 Shell 会话、命令窗口(多会话广播)、Active Sessions Manager、按钮栏/Command Manager、日志(参数替换/轮转/命令行)、主机打印(host-based printing)、深色模式、字体缩放/Alpha 透明、URL/Google 搜索、会话锁定、TFTP 服务器、Unicode/80-132 列/NRCS、凭据管理器(全局凭据集)、Personal Data Folder、导入/导出配置、自动更新、MSI 安装器、FIPS 140-2、IPv6、Section 508 无障碍。*出口：官方功能页逐项在 `docs/ui-parity.md` 勾对完成。*

**再往后（暂不排期，列为 backlog）**：RDP 支持、TAPI/拨号、HTTPS/MVS/VMS 服务器互操作、PGP 兼容、主题市场、插件系统、云会话同步。

---

## 7. 各里程碑验证方式

- **M1**：CI 三平台绿；Linux 用 `-platform offscreen` 冒烟测试进程退出 0。
- **M2**：Docker 本地 OpenSSH（`tests/integration/docker-compose.yml`），自动连接跑 `echo hello` 断言字节流含 hello；CI 作为 service 容器。
- **M3**：`VtParser` 纯无头单测（喂已知字节序列断言 `ScreenBuffer` 网格），golden-file 对比 `vttest`/真实 tmux/vim 转录。**项目最高价值测试套件**，重点投入 fixture 覆盖。
- **M4**：`ProfileStore`(SQLite CRUD+迁移) 与 `CredentialStore`(CI 用 mock 后端) 单测；真实 OS keychain 集成列为每次发布的手动 QA。
- **M5/M6**：对同一 Docker OpenSSH（自带 sftp-subsystem）做往返测试（put→get 断言字节一致）+ 列目录测试。
- **M7**：同步引擎最适合 dry-run 确定性测试——用 `QTemporaryDir` 造两棵目录树 + 本地 fake `RemoteFileEngine` 测试替身（实现相同接口，无需网络），断言各方向/冲突策略下 `[SyncAction]` 精确匹配；再少量真实 Docker SFTP 端到端。
- **M8**：Docker 加 vsftpd，对 FTP profile 重跑 M5/M6/M7 集成套件，验证协议无关接口成立。
- **M9**：测试用一次性密钥对（`tests/integration/fixtures/`，明确标注非真实凭据）对 Docker OpenSSH 测公钥认证；keychain 相关路径手动 QA。
- **M10**：每次发布在干净 VM/容器手动装/卸验证；CI 至少确认安装包能构建成功。
- **M11**：对 Docker OpenSSH 配转发，断言本地端口能触达容器内另一服务；SOCKS 代理用 curl `--socks5` 验证。
- **M12**：Docker 容器装 `lrzsz`，自动化 `sz`/`rz` 触发，断言 ZMODEM 传输字节一致；X/Y MODEM 用回环 fixture 单测帧编解码。
- **M13**：Docker telnet 服务（`inetutils` / `telnetd`）自动登录跑命令断言输出；rlogin 同理。
- **M14**：用虚拟串口对（Linux `socat pty`、Windows com0com）回环收发断言。
- **M15**：脚本引擎单测——对象模型 mock 屏幕缓冲，断言 `Screen.WaitForString`/`Send` 等 API 行为；端到端跑一段脚本对 Docker SSH 登录。
- **M16**：TN3270 数据流解析用捕获的 3270 会话字节做 golden-file 单测断言字段化屏幕结构。

---

## 附录 A：SecureCRT 功能照抄清单（逐菜单 / 对话框，映射到里程碑）

> 说明：documentation.help 全站对自动抓取返回 403、随附 PDF 用子集化字体无法程序化取文，故以下清单依据 SecureCRT 实际产品结构整理（菜单分组已用搜索确认）。**实现前需对着 https://documentation.help/SecureCRT/ 逐条核对叶子项名称并在 `docs/ui-parity.md` 勾对**；如你能连上 Chrome 扩展或粘贴文档目录，我会把措辞对齐到与官方完全一致。

**菜单栏（照抄分组与项）**
- **File**：Connect… / Quick Connect… / Connect in Tab-Tile… / Reconnect / Reconnect All / Connect SFTP Session / Disconnect / Disconnect All / Clone Session / Lock Session / Save Session As… / Print(Screen/Selection/Setup) / Log Session · Raw Log Session / Trace Options / Import-Export Settings Wizard / Exit  → M4·M5·M10
- **Edit**：Copy / Paste / Copy and Paste / Paste(upload) / Select All / Clear Screen / Clear Scrollback / Clear Screen and Scrollback / Reset / Find… / Column(Block) Select / Copy on Select · Auto Copy  → M3·M4
- **View**：Menu Bar / Toolbar / Connect Bar / Command Window / Chat Window / Button Bar / Status Bar / Tab Bar / New Horizontal-Vertical Tab Group / Full Screen / Always on Top / Zoom·Font  → M3·M4·M15(Button Bar 跑脚本)
- **Options**：Session Options… / Global Options… / Edit Default Session / Save Settings Now / Auto Save Options / Keymap Editor…  → M4·M9
- **Transfer**：Send ASCII / Receive ASCII / Send Binary / Send Xmodem / Receive Xmodem / Send Ymodem / Receive Ymodem / Zmodem Upload List / Start Zmodem Upload / Cancel / Send-Receive Kermit  → M12
- **Script**：Run… / Cancel / Start Recording Script / Stop Recording Script / 最近脚本列表 / Map Selected Script to Button-Key  → M15
- **Tools**：Create Public Key… / Public-Key Assistant / Convert Private Key to… / Manage Agent Keys / Start-Stop SSH Agent / Keyboard(Keymap)… / 打开 SecureFX(本项目=切到文件视图)  → M9·M15
- **Window**：Cascade / Tile Horizontally-Vertically / Arrange / 会话窗口列表  → M4
- **Help**：Help Topics / Update Now / About  → M10

**Session Options 属性树（左类别 + 右面板，逐类别照抄）**
- **Connection**：协议、主机、端口、用户名、Firewall  → M4
  - **Logon Actions**：登录提示自动化、Expect/Send 序列  → M15(自动化引擎强化)
  - **SSH2**：KEX/Cipher/MAC/主机密钥算法、认证方式(Password/PublicKey/Keyboard-Interactive/GSSAPI)、压缩  → M2·M9
  - **Port Forwarding** → **Remote/X11**：本地/远程转发条目、X11 转发  → M11
  - **SFTP Session**：初始本地/远程目录  → M5
  - **Advanced**  → M4
- **Terminal**：反空闲(NO-OP/发送字符串)、滚动、警铃  → M3·M4
  - **Emulation**：终端类型(xterm/VT100/VT220/Linux/ANSI/…)、仿真选项  → M3
    - **Modes**：初始键盘/光标/换行模式  → M3
    - **Emacs**：Meta 键映射  → M3
    - **Mapped Keys**：自定义按键映射  → M3
    - **Advanced**  → M3
  - **Appearance**：字体、ANSI 配色方案、光标样式、粗体颜色  → M3
    - **Window**：滚回行数、标签标题  → M3·M4
  - **Log File**：日志文件名/滚动/时间戳  → M4
  - **X/Y/Zmodem**：上传下载目录、ZMODEM 选项  → M12
- **说明**：3270/5250 主机仿真作为 Emulation 下的独立仿真类型接入  → M16

**连接相关对话框**：Connect(会话树) / Quick Connect / Connect in Tab / Activator(托盘) / Trace Options — 逐控件照抄  → M4

## 附录 B：SecureFX 功能照抄清单（映射里程碑）

- **菜单**：File(Connect/Quick Connect/New Session/Reconnect/Disconnect/Import-Export/Print/Exit)、Edit(Cut/Copy/Paste/Select All/Invert Selection/Find)、View(工具栏/本地·远程栏切换/Log 页/Queue 页/Refresh/Filter/Show Hidden Files/图标·详情·列表视图)、Transfer(Upload/Download/**Synchronize**/Resume/传输模式 ASCII·Binary·Auto/Queue: Start·Stop·Clear)、Tools(切到终端视图/Options/Global Options)  → M5·M6·M7·M8
- **双栏浏览器**：地址栏+路径下拉+上级/刷新、过滤框、列(Name/Size/Type/Modified/Attributes/Owner)可排序、拖拽/双击/右键传输  → M6
- **Transfer Queue 面板**：队列项、进度条、速率、剩余时间、暂停/恢复/取消、失败重试  → M6
- **Synchronize 对话框**：Local/Remote 路径、方向(Upload/Download/Keep Newer/Mirror=双向镜像)、过滤、Preview(dry-run) → Start  → M7
- **SecureFX Session Options**：Connection、File Transfer(默认上传/下载目录、传输模式、ASCII 扩展名列表、文件名大小写、权限保留)、Directory Listing、Filters  → M5·M6·M8
- **协议**：SFTP / FTP / FTPS(显式+隐式 TLS) / SCP，同一浏览器内切换  → M5·M8·M12

## 附录 C：官方功能页逐项对照（vandyke.com features → 里程碑）

来源：[SecureCRT features](https://www.vandyke.com/products/securecrt/features.html) 与 [SecureFX features](https://www.vandyke.com/products/securefx/features.html)。目标是官方页上**每一项都要实现或明确排期**。

**SecureCRT · Secure Shell**：SSH1/SSH2✓M2 · 用户认证(password/publickey/Kerberos5/keyboard-interactive/RSA/Ed25519/ECDSA/DSA/PuTTY PPK/OpenSSH 证书/X.509)✓M9·M17 · 凭据管理✓M20 · Public-Key Assistant✓M9 · GSSAPI 密钥交换✓M17 · 强加密(ChaCha20-Poly1305/AES-GCM/AES/Twofish/3DES)✓M2 · 密码/口令缓存✓M4 · 端口转发✓M11 · 动态端口转发✓M11 · X.509✓M17 · OpenSSH 密钥格式✓M9 · OpenSSH agent 转发✓M17 · SSH-agent✓M9 · 主机密钥管理✓M4 · X11 转发✓M11 · 数据压缩✓M2

**SecureCRT · Emulation**：VT100/102/220/320、ANSI、SCO ANSI、TN3270、TVI910/925、Wyse50/60、Xterm、Linux console✓M3·M16 · Xterm 扩展✓M3 · 字符属性✓M3 · Unicode✓M3 · 80/132 列✓M3 · NRCS✓M20 · 可配置行列✓M3 · Raw 协议模式✓M2 · 窗口尺寸变化(NAWS)✓M3

**SecureCRT · Keyboard/Session/Ease**：键盘映射 + 图形化 Keymap 编辑器✓M3 · Personal Data Folder✓M20 · 命名防火墙✓M17 · 配色方案✓M3 · 128,000 行回滚✓M3 · Emacs 模式✓M3 · 命令窗口✓M20 · 路径环境变量✓M19 · 多会话编辑✓M4 · 导入/导出配置✓M20 · 标签会话/标签组/平铺✓M4 · Session Manager / Active Sessions Manager✓M4·M20 · Button Bar / Command Manager✓M20 · 会话状态✓M4 · URL/Google 搜索✓M20 · 自动登录✓M15 · Quick Connect / Connect Bar✓M4 · 剪贴板/多行粘贴✓M3 · 工具栏菜单自定义✓M20 · Anti-idle✓M4

**SecureCRT · Firewall/FileTransfer/Scripting/Logging/Printing**：SOCKS4/5、TIS/WinGate、HTTP 代理、本地代理命令、Dependent Session✓M17 · Zmodem/Xmodem/Ymodem/Kermit、Send/Receive ASCII、SFTP-in-a-tab、拖拽传输、Send Binary、内置 TFTP 服务器✓M12·M5·M6·M20 · ActiveX 多语言脚本 + Python + 脚本函数 + 录制器 + 脚本编辑器标签✓M15·M19 · 日志(参数替换/轮转/命令行)✓M20 · 主机打印/基本打印/会话或全局打印设置✓M20

**SecureCRT · Other/Application**：关键字实时高亮✓M20 · RDP(backlog) · IPv6✓M20 · 字体缩放✓M20 · 深色模式✓M20 · 浏览器调起✓M20 · 连接时执行远程命令✓M15 · 本地 Shell 会话✓M20 · Execute Local Shell Command✓M19 · 串口设备✓M14 · Hex View✓M20 · Scratchpad 标签✓M20 · TAPI(backlog) · Alpha 透明✓M20 · 延迟选项✓M15 · FIPS 140-2✓M20 · 会话锁定✓M20 · 文件式配置✓M4 · 多平台/自动更新/MSI/命令行工具/与文件视图集成✓M10·M19·M20

**SecureFX · Security**：认证(password/publickey/Kerberos5/keyboard-interactive)✓M9·M17 · 凭据管理(全局凭据集)✓M20 · Public-Key Assistant✓M9 · 加密(ChaCha20-Poly1305/AES-GCM/AES/Twofish/3DES)✓M2 · 数据完整性校验✓M5 · 主机密钥管理✓M4 · SSH-agent✓M9 · X.509 认证✓M17 · GSSAPI✓M17 · PGP 兼容(backlog)

**SecureFX · File Transfer**：多协议(SSH2/SFTP/FTPS/HTTPS/SCP/FTP)✓M5·M8·M12(HTTPS→backlog) · 站点同步(上传/下载/镜像)✓M7 · 同步浏览✓M18 · 多连接并行传输✓M18 · 覆盖控制✓M18 · Relentless 自动重连✓M18 · 暂停/恢复✓M18 · 任务调度器✓M19 · 带宽限速✓M18 · 并行数指定✓M18 · 移动文件✓M18 · 传输队列✓M6 · 多平台服务器支持(Win/Linux/macOS/MVS/VMS→部分 backlog)✓M8 · SFTP ASCII 传输✓M18

**SecureFX · Ease/Application/Advanced**：标签会话✓M6 · 拖拽(资源管理器)✓M6 · 地址栏路径历史✓M6 · Connect Bar 自动完成✓M4 · 书签/书签管理器✓M18 · 通配符 Filter View✓M6 · Quick Connect / New Session 向导✓M4 · 声音通知✓M20 · 工具栏菜单自定义✓M20 · 深色模式✓M20 · 多平台✓M10 · 自动更新✓M20 · 与终端视图集成✓M5 · 防火墙(SOCKS4/5/CSM/WinGate/代理)✓M17 · Dependent Session✓M17 · Session Manager(可停靠/可过滤)✓M4 · 站点组织✓M4 · Personal Data Folder✓M20 · 导入/导出✓M20 · 忙站重试✓M18 · Quote 命令✓M19 · 保活✓M18 · Paste URL✓M6 · OpenSSH 密钥格式✓M9 · 远程文件编辑✓M19 · 命令行自动化(SFXCL)✓M19 · 服务器端改权限/上传权限/默认上传权限✓M6·M18 · 路径环境变量✓M19 · Execute Local Shell Command✓M19 · SCP sudo✓M12 · 解析符号链接✓M18 · MSI 安装器✓M10 · 自动隐藏 dot 文件✓M18 · 时区配置✓M18 · 上传文件名转换✓M18 · IPv6✓M20 · FIPS 140-2✓M20 · Section 508✓M20

> **执行原则**：`docs/ui-parity.md` 建成一张覆盖上述每一项的勾选表，每完成一个里程碑就把对应行打勾；backlog 项显式标注"暂不实现/未来"，做到"官方功能页逐项可追溯"。

---

## 起步关键文件（从空仓库最先创建、锚定后续一切）

- `CMakeLists.txt` + `vcpkg.json`（根）— 构建与依赖清单
- `src/core/ssh/SshConnection.{h,cpp}` — libssh2 封装，终端与 SFTP 通道都建于其上
- `src/core/session/Session.{h,cpp}` — "一个 profile 同时支撑终端和文件传输"的架构支点
- `src/terminal/vt/VtParser.{h,cpp}` — 最高复杂度/最高测试价值，定义 `ScreenBuffer` 契约
- `src/core/model/ConnectionProfile.h` — 所有模块共同 key 的数据模型

---

## 首次执行建议（本轮批准后从这里开始）

批准计划后，第一轮实现聚焦 **M1 脚手架**：初始化 git 仓库、写 `CMakeLists.txt` / `CMakePresets.json` / `vcpkg.json`、搭出空的 Qt6 `MainWindow` + 标签壳、`.gitignore`、`README.md` 与 `LICENSE`，确保 `cmake --preset default && cmake --build` 能产出可运行空窗口。M2 起再进入 SSH 连接。
