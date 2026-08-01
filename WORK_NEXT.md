# XRK 路线图：补齐与向日葵的功能差距

> 本文档记录本软件与向日葵（Sunflower）远程控制软件的功能差距，以及逐项补齐的计划。
> 已完成的功能汇总见 `WORK_DONE.md`。差距结论来自对 `src/` 实际代码的核查（非凭文件名判断）。

## 核查结论：真实差距清单

| # | 功能 | 现状 | 证据 | 计划 |
|---|------|------|------|------|
| 1 | 剪贴板双向同步 | **部分**：仅控制器→被控端单向；无自动粘贴 | `clipboard_manager.cpp:64-209`；host 仅 `host.cpp:1020-1034` 写本机，无 host 监听 | 补齐 host 监听 + 双向 + 自动粘贴 |
| 2 | 隐私屏屏蔽本地输入 | **部分**：黑屏+防截屏已做，但 `WindowTransparentForInput` 让输入穿透（方向反） | `privacy_screen.cpp:61` | 改为拦截/屏蔽本地物理输入 |
| 3 | 游戏/低延迟模式开关 | **部分**：zerolatency 编码已有，但控制器侧无画质档位选择 | `h264_encoder.cpp:63`；仅 `remote_desktop_widget.cpp:412-450` 显示 | 加画质/延迟档位开关 |
| 4 | 文件实时同步 | **缺失**：仅一次性传输，无 watch/sync | `file_transfer_manager.cpp` 无监控逻辑 | 目录监控 + 增量同步 |
| 5 | 企业级/服务器侧设备管理 | **部分**：仅本地 JSON 地址簿 | `address_book.cpp`；中继仅桥接 | 强化地址簿/评估服务端 |
| 6 | 远程打印 | **缺失**：全库无 printer | grep 无命中 | 评估（见下） |
| 7 | 虚拟网卡/VPN | **缺失**：无 tap/tun/vpn | grep 无命中 | 评估（见下） |
| 8 | 移动端(Android/iOS) | **缺失**：纯 Windows | glob 无命中 | 长期路线，本次不实现 |

## 已具备可比性（无需再做）
远程音频双向回放、远程电源管理+WoL、AVI 录制、聊天、终端、NAT/中继/P2P、
H.264 编码、AES 加密、多显示器、断线重连、跨平台采集/输入抽象。

## 执行计划（按优先级逐个实现 + 提交）

### 阶段 A：修复半成品（成本低、收益高，先做）
- [x] **Task 24** 剪贴板双向同步 + 自动粘贴 — 已提交 `052fd6d`
- [x] **Task 25** 隐私屏屏蔽本地物理输入 — 已提交 `99a9060`（移除 WindowTransparentForInput，改用 Windows BlockInput(TRUE/FALSE) 在显示/隐藏时屏蔽本机物理键鼠输入；保留 WDA_EXCLUDEFROMCAPTURE 防被远端截屏；保留不抢焦以不影响远控）
- [x] **Task 26** 游戏/低延迟模式开关（控制器侧画质档位）
- [x] **Task 27** 文件实时同步（本地→远程 + 反向远程→本地）

### 阶段 B：新增中等功能
- [x] **Task 27** 文件实时同步（目录 watch + 增量传输）
- [x] **Task 28** 企业级/服务器侧设备管理（强化地址簿或轻量服务端）

### 阶段 C：架构重、需评估（先评估再落地）
- [ ] **Task 29** 远程打印 —— 评估结论：标注长期（见下方评估）
- [ ] **Task 30** 虚拟网卡/VPN —— 评估结论：标注长期（见下方评估）
- [ ] **Task 31** 移动端客户端 —— 记录为长期路线图（本次不实现）

## 实现约定
- 每个 Task 完成后：构建通过（`build.bat` / `cmake --build`）、相关单测通过（headless）、本地提交。
- 仓库无 remote，仅本地提交（与历史一致），不推送。
- 调试/构建需 Qt 在 PATH：`export PATH="/d/Qt/6.10.0/msvc2022_64/bin:$PATH"`，headless 运行加 `QT_QPA_PLATFORM=offscreen QT_PLUGIN_PATH=D:/Qt/6.10.0/msvc2022_64/plugins`。
- 有 GUI 事件（拖拽/输入）的部分用合成事件单测覆盖逻辑，肉眼验证交给桌面端。

## 进度（完成一项更新一项）
- 已完成：Task 24, Task 25, Task 26, Task 27, Task 28（+ 本地锁屏按钮 / 信任IP免确认 / 移除Esc断连 三项追加）
- 已评估：Task 29（远程打印，标注长期）、Task 30（虚拟网卡/VPN，标注长期）
- 待办：31

## Task 26 记录（画质/延迟档位）
- 新增 `MessageType::SET_QUALITY` + `QualityRequest{level,gameMode}` 结构体及
  `encode/decodeQualityRequest`（protocol_manager）。
- 控制器 `RemoteController::sendQualityLevel(QualityLevel,bool)` 发送档位。
- 主机 `Host::setQualityLevel`：AUTO/ADAPTIVE 恢复带宽自适应；其余档位锁定
  jpegQuality/captureFps（流畅45/24 · 标准65/30 · 高清82/30 · 游戏92/60），
  游戏档尝试切 H264 低延迟编码器（无 libx264 自动回退 JPEG）。`onQualityTimer`
  在 `m_autoAdapt=false` 时仅上报不再自适应。
- UI：工具栏新增「画质」下拉（自动/流畅/标准/高清/游戏），切换即下发；
  不抢焦点；新会话重置为「自动」。
- 单测：协议往返 + 主机档位映射（headless，无需 Host::start）。

## Task 27 记录（文件实时同步，含正向与反向）
### 正向（本地→远程）
- 新增 `FileSyncManager`（src/app）：`QFileSystemWatcher` 递归监控本地目录，
  目录变化时按相对路径映射上传到被控端目标目录；400ms 防抖合并多次事件；
  支持多配对（本地↔远程）、开始/停止、连接断开时暂停上传。
- 纯函数 `relativePath` / `remoteTargetPath` 做本地相对路径→远程绝对路径映射
  （保留子目录结构）。
- `FileTransferManager::uploadFileTo(filePath, remotePath)` 新增，上传到指定
  远程路径（而非临时目录）。
- 主机 `handleFileRequest` 上传分支：若 `request.path` 非空则写入该路径并自动
  创建父目录，否则回落到临时目录（向后兼容）。
- UI：文件传输面板新增「实时同步（本地→远程）」分组，可添加/移除同步目录对、
  开始/停止，状态栏显示最近同步动作。上传经 FileTransferManager 在传输队列可见。
- 单测：路径映射 + 初始/增量/修改上传决策 + 断开时不上传（headless，4 用例全绿）。

### 反向（远程→本地）
- 新增 `SYNC_ADD`/`SYNC_REMOVE`/`SYNC_NOTIFY` 消息与 `SyncPair`/`SyncNotify` 编解码。
- 主机 `Host::addReverseSync`：每对 (clientId, hostDir) 起一个 `FileSyncManager`
  监控被控端目录，文件变化时其上传回调改为发送 `SYNC_NOTIFY` 给该客户端
  （含 hostDir/hostFilePath/localDir/size/mtime）；客户端断开时
  `removeReverseSyncForClient` 清理监控。
- 控制器 `RemoteController::sendSyncAdd/sendSyncRemove`；`processMessage` 解码
  `SYNC_NOTIFY` 并 emit `syncNotifyReceived(SyncNotify)`。
- UI：文件传输面板新增「反向同步（远程→本地）」分组（添加/移除目录对），
  收到 `SYNC_NOTIFY` 按相对路径下载到本地目录（自动建子目录，复用 downloadFile）；
  重连时自动重新下发反向同步请求。
- 范围：反向为快照式拉取（变化时下载），非持续差异比对；双向把同一目录配对
  可能形成回环（用户应避免）。
- 单测：SyncPair/SyncNotify 协议往返 + 主机反向配对注册/清理（headless，全绿）。
- 注意：本机 headless 下 `Host::start()` 会卡在屏幕采集初始化，主机写路径逻辑
  靠运行态 app 验证。

## 追加功能记录（非路线图原项，按用户临时要求）

### 本地锁屏按钮（用户临时要求）
- `PrivacyScreen::showLocal(seconds)`：显示全屏遮罩 + `BlockInput(TRUE)` 屏蔽本地键鼠，
  内部 1s 倒计时，到时自动 `hide()` 释放输入；避免 BlockInput 造成的永久锁死
  （本地输入被屏蔽时无法输入解锁手势）。
- `Host::lockScreenLocal / unlockScreenLocal / isLocalLockActive`：独立于远控会话的
  本机锁屏（用单独的 `m_localLock` 实例，不与远控隐私屏 `m_privacyScreen` 冲突）；
  `stop()` 与析构会释放本地锁。
- UI：菜单「文件 ▸ 锁定本机屏幕」+ 托盘右键菜单「锁定本机屏幕」；点击后弹窗输入
  锁定时长（默认 60s，5–3600s），到时自动解锁，远程端也可提前解锁。
- 说明：因 BlockInput 同时屏蔽本地解锁手势，故设计为定时自动解锁，绝不锁死用户。

### 同 IP 免确认（连接授权记住信任 IP）
- `Host` 信任 IP 名单（`m_trustedIps`，经 QSettings 持久化）：
  `addTrustedIp / isTrustedIp / trustedIps / removeTrustedIp / clearTrustedIps`。
- `requestConsent`：若对端 IP 在信任名单，直接 `grantConsent` 跳过授权弹窗。
- UI：授权弹窗新增复选框「记住此 IP（…），下次自动允许」；勾选并在允许时写入信任名单。
- 单测：`HostTeardown.TrustedIpStore`（headless，全绿）。
- 说明：经中继连接时 peer 为中继 IP，仅直连（LAN）能精确匹配控制端真实 IP。

### 移除控制端 Esc 断连
- 删除 `RemoteDesktopWidget` 中 `Qt::Key_Escape` → `stopRemote` 的 `QShortcut`
  （易误触导致断连）。断连仍可通过界面「断开」按钮进行。

## Task 28 记录（企业级设备管理）

### 设计目标
- 扩展中继服务器，从纯转发升级为"带状态的中继"，维护设备注册表
- 支持设备注册、查询、更新、删除等管理操作
- 设备状态持久化到JSON文件，支持多控制端共享设备目录
- 保持向后兼容，不破坏现有中继功能

### 实现方案

#### 1. 新增 DeviceRegistry 类（`src/app/device_registry.h/.cpp`）
- `RegisteredDevice` 结构体：设备ID、名称、IP、端口、版本、分组、MAC、备注、标签、在线状态、最后心跳时间
- 持久化：JSON文件存储，支持增量更新
- 线程安全：QMutex保护并发访问
- 自动清理：定时器检测超时设备（5分钟无心跳标记为离线）
- 信号：deviceRegistered/deviceUpdated/deviceRemoved/deviceOnlineStatusChanged

#### 2. 扩展 RelayServer 协议
新增命令：
- `DEVICE_REGISTER <deviceId> [name] [ip] [port] [version] [group] [mac] [notes] [tags]`
  - 设备连接时上报详细信息，自动注册到设备注册表
- `DEVICE_LIST [online|group <name>|search <keyword>]`
  - 返回设备列表（JSON格式，Base64编码）
- `DEVICE_UPDATE <deviceId> <jsonBase64>`
  - 更新设备信息（名称、分组等）
- `DEVICE_REMOVE <deviceId>`
  - 从注册表中移除设备
- `DEVICE_QUERY <deviceId>`
  - 查询单个设备详细信息
- `HEARTBEAT <deviceId>`
  - 设备心跳，更新在线状态

#### 3. 集成到现有架构
- `RelayServer::setDeviceRegistry(DeviceRegistry*)` 注入设备注册表
- 设备注册时自动调用 `DeviceRegistry::registerDevice()`
- 设备断开时自动标记为离线 `DeviceRegistry::setDeviceOnline(false)`
- 心跳包更新设备在线状态

### 协议格式示例
```
# 设备注册
DEVICE_REGISTER dev-001 "Server-01" 192.168.1.100 9999 1.0.0 "servers" "AA:BB:CC:DD:EE:FF" "生产服务器" "linux,production"

# 查询设备列表
DEVICE_LIST
DEVICE_LIST online
DEVICE_LIST group servers
DEVICE_LIST search server

# 更新设备
DEVICE_UPDATE dev-01 eyJkZXZpY2VOYW1lIjoiU2VydmVyLTAyIn0=

# 移除设备
DEVICE_REMOVE dev-001

# 心跳
HEARTBEAT dev-001
```

### 文件变更
- 新增：`src/app/device_registry.h` - 设备注册表头文件
- 新增：`src/app/device_registry.cpp` - 设备注册表实现
- 修改：`src/app/relay_server.h` - 添加设备注册表支持
- 修改：`src/app/relay_server.cpp` - 实现设备管理协议
- 修改：`src/app/CMakeLists.txt` - 添加新源文件

## Task 29 评估记录（远程打印）

### 评估结论：标注长期，本次不实现

### 评估分析

#### 1. 需求分析
- **核心需求**：将本地文档通过远程打印机打印
- **使用场景**：局域网内远程办公、IT运维、文档分发
- **用户价值**：中等（非核心功能，但有特定用户群体）

#### 2. 方案评估

##### 方案A：打印到PDF再传输（推荐的轻量方案）
- **原理**：本地将文档打印为PDF → 文件传输到远程端 → 远程端调用系统打印
- **优点**：
  - 实现简单，复用现有文件传输模块
  - 无需直接控制远程打印机
  - 跨平台兼容性好
- **缺点**：
  - 需要两步操作（打印PDF + 传输）
  - PDF可能不保留所有打印格式（如特殊纸张大小、双面打印设置）
  - 需要远程端安装PDF阅读器或打印支持

##### 方案B：直接远程打印控制
- **原理**：控制端发送打印命令，被控端调用Windows API（`StartDocPrinter`/`WritePrinter`）执行打印
- **优点**：
  - 一步操作，用户体验更好
  - 可控制打印机设置（纸张、双面、质量等）
- **缺点**：
  - 实现复杂，需要处理打印机枚举、驱动兼容性
  - Windows平台特定（需跨平台适配）
  - 安全性考虑（恶意打印风险）

#### 3. 技术可行性
- **Qt打印支持**：Qt提供`QPrinter`/`QPrintDialog`，但主要用于本地打印
- **Windows API**：`winspool.drv`提供打印机管理API
- **文件传输**：已有`FileTransferManager`可复用
- **协议扩展**：需新增`PRINT_REQ`/`PRINT_RESP`消息类型

#### 4. 实现复杂度
- **方案A**：低（2-3天开发）
- **方案B**：高（5-7天开发，含跨平台适配）

#### 5. 建议
- **短期**：标注为长期功能，不实现
- **中期**：如用户需求强烈，可实现方案A（打印到PDF再传输）
- **长期**：如企业用户增多，可考虑方案B（直接远程打印）

### 决策理由
1. **非核心功能**：局域网远程控制的核心是屏幕共享和输入控制，打印是辅助功能
2. **实现成本**：方案B实现复杂，且Windows平台特定
3. **用户需求**：当前用户群体以IT运维和远程办公为主，打印需求不突出
4. **替代方案**：用户可通过文件传输手动打印，或使用系统自带的远程桌面打印功能

## Task 30 评估记录（虚拟网卡/VPN）

### 评估结论：标注长期，本次不实现

### 评估分析

#### 1. 需求分析
- **核心需求**：创建安全的远程访问通道，让远程设备像在本地网络一样访问资源
- **使用场景**：
  - 远程办公：访问公司内网资源（文件共享、内部系统）
  - IT运维：远程管理服务器和网络设备
  - 安全连接：加密的远程访问隧道
- **用户价值**：高（对企业和IT专业用户），但非远程控制核心功能

#### 2. 技术方案评估

##### 方案A：基于WFP（Windows Filtering Platform）
- **原理**：使用Windows内核驱动拦截网络流量，创建虚拟网卡
- **优点**：
  - 性能高，内核级处理
  - 可精细控制网络流量
  - Windows原生支持
- **缺点**：
  - 需要开发内核驱动（高风险、高复杂度）
  - 需要驱动签名（Windows 10+强制要求）
  - 调试困难，蓝屏风险
  - 需要管理员权限安装

##### 方案B：基于TAP-Windows（OpenVPN虚拟网卡）
- **原理**：使用现有的TAP-Windows驱动创建虚拟网卡
- **优点**：
  - 无需开发内核驱动
  - 已有成熟实现（OpenVPN社区版）
  - 跨平台兼容性好
- **缺点**：
  - 需要安装第三方驱动（用户接受度低）
  - 驱动可能与系统不兼容
  - 性能开销较大

##### 方案C：用户态VPN（WireGuard/WireGuard-NT）
- **原理**：使用用户态VPN库（如WireGuard-NT）实现虚拟网卡
- **优点**：
  - 性能接近内核态
  - 无需开发内核驱动
  - 现代加密协议
- **缺点**：
  - 需要安装WireGuard-NT驱动
  - 相对较新，文档和社区支持较少

#### 3. 技术可行性
- **Windows API**：需使用`DeviceIoControl`与虚拟网卡驱动通信
- **网络配置**：需设置IP地址、路由表、DNS
- **加密**：需集成加密库（如libsodium、OpenSSL）
- **协议扩展**：需新增VPN隧道相关的消息类型

#### 4. 实现复杂度
- **方案A**：极高（需内核驱动开发，2-3个月）
- **方案B**：高（需集成TAP驱动，2-3周）
- **方案C**：中高（需集成WireGuard-NT，1-2周）

#### 5. 建议
- **短期**：标注为长期功能，不实现
- **中期**：如企业用户需求强烈，可考虑方案C（WireGuard-NT）
- **长期**：如成为核心需求，可评估自研虚拟网卡驱动

### 决策理由
1. **非核心功能**：虚拟网卡/VPN是网络层功能，与远程控制核心功能（屏幕共享、输入控制）关系不大
2. **实现成本高**：无论哪种方案都需要深入的网络知识和驱动开发
3. **安全风险**：虚拟网卡/VPN涉及系统底层，实现不当可能导致安全漏洞
4. **用户接受度**：安装虚拟网卡驱动需要管理员权限，用户可能抵触
5. **替代方案**：用户可使用现有的VPN解决方案（如OpenVPN、WireGuard客户端）配合远程控制软件
6. **架构复杂性**：引入虚拟网卡会显著增加软件架构复杂性，影响维护和稳定性

### 替代建议
- **推荐方案**：用户使用现有的商业VPN解决方案（如Cisco AnyConnect、FortiClient）建立VPN连接，然后使用XRK进行远程控制
- **未来考虑**：如用户需求明确，可集成WireGuard客户端作为可选组件
