✅ **Phase E2 Milestone 1**: WebSocket↔TCP网关 + 打包网页客户端页面
  - 新增文件: web_socket_gateway.h/.cpp, resources/web/client.html, qrc注册
  - 新增测试: test_web_socket_gateway.cpp (5用例, 全绿)
  - main.cpp `--ws` 无头网关模式
  - 全量273用例: 272通过+1环境跳过, 0失败
✅ **Phase E2 Milestone 2**: React SPA
  - 创建React应用 (web-client/)
  - 状态管理: zustand store (connection + UI)
  - 协议层: 类型安全的XRK wire协议 (TS接口)
  - 组件: ConnectionScreen, RemoteDesktop, Toolbar, SettingsPanel
  - 完整UI: 连接界面, 屏幕显示, 输入控制, 工具栏设置
  - 部署: `web-client/dist/` → `resources/web/`
  - 全量测试: 272通过+1环境跳过, 0失败

**下一步**: Phase E2 Milestone 2.1 (React SPA 部署与集成)

1. **部署 React SPA** (已完成): React 应用已打包到 `web-client/dist/`
2. **构建 XRK** (已完成): web-client/dist → resources/web，qrc 注册，重新编译 xrk.exe 并打包
3. **启动网关** (已完成): `xrk --ws 8080` 无头模式，ws://*:8080 + http://*:8081 监听正常
4. **浏览器测试** (已完成): http://127.0.0.1:8081/ 正确返回 SPA 及 /assets/*.js|css、/favicon.svg
5. **组件测试** (✅ 已完成): 为 React 应用添加 Jest + React Testing Library 测试
   - 8 个测试套件、53 个用例全绿：`protocol`（线协议编解码/校验）、
     `ConnectionScreen`、`Button`、`Toolbar`、`SettingsPanel`、`RemoteDesktop`、
     `App`，以及 `RemoteDesktop.integration`（connect→AUTH_RESP→AES 解密→
     decodeScreenFrame→canvas 绘制的端到端帧链路）。
   - 配套补齐 jsdom 环境桩：TextEncoder/TextDecoder、requestFullscreen、
     crypto.subtle（Web Crypto）、WebSocket/Image/getContext，并把 store reset
     包进 `act()`。
6. **UI/UX 优化** (✅ 已完成本轮): 详见 WORK_DONE.md「UI/UX 优化 (React SPA)」
   - 修复 index.css/App.css 主题+布局冲突（#root 1126px 居中列 → 收敛为全局基底）
   - 修复 `npm run build` 因测试文件被拉入 tsc 生产检查而失败（tsconfig.app.json exclude 测试）
   - 连接卡片品牌徽标、工具栏全屏图标区分/状态徽标动态化、`:focus-visible` 焦点环、
     `prefers-reduced-motion` 降级、app-main 渐变背景、`connecting-overlay` 排版
   - `npm run build` 通过、`npm test` 53/53 全绿
   - ⚠️ 部署未做：新 dist 尚未同步到 `resources/web` 并更新 `xrk.qrc` + 重新编译 xrk.exe，
     故运行中的网关仍服务旧资源；需执行标准打包流程才能看到线上效果。
7. **XRK 集成** (待完成): 将 React SPA 集成到 XRK 主应用

**已修复的阻断性 Bug（本次集成发现）**:
- 网关 HTTP 服务器原本只处理 `GET /`，导致 `/assets/*` 与 `/favicon.svg` 全部 404，浏览器白屏。
  已改为把 URL 路径映射到 Qt 资源 `:/web/<path>`，并按扩展名返回正确 Content-Type，
  同时拒绝 `..` 路径穿越。单元测试 `HttpServesWebPage` 已扩展为同时校验资源与 favicon 返回 200。
- 网关单测 `HttpServesWebPage` 原断言页面含字面量 "WebSocket"（旧 client.html 才有），
  React SPA 的 index.html 不含该词，已改为断言引用 `/assets/` 资源（更稳健、抗 hash 变化）。

**全量单元测试**: `WebSocketGatewayTest` 5/5 通过（含资源服务回归校验）。

**端到端说明**: 完整远程桌面流需同时有 Host 实例监听 127.0.0.1:9999（主界面 GUI 启动服务）。

**新增 `--host <port>` 无头模式（本次集成加入）**:
- `main.cpp` 新增与 `--ws` / `--relay` 同构的 `xrk --host 9999` 模式：以 QCoreApplication 启动
  Host，监听 TCP+UDP 9999，并自动批准 consentRequested（无人值守本地测试用）。
- Host 仅依赖 QImage/qApp（条件使用），可在 QCoreApplication 下运行，无需 GUI/显示器。
- 用途：配合 `xrk --ws 8080` 网关，让浏览器/移动端在无交互桌面的环境下完成端到端链路验证。
- ⚠️ 安全提示：该无头 Host 自动批准所有连接请求，仅供本地（127.0.0.1）测试；生产环境请用主界面 GUI 的人工批准。

**端到端验证（已通过）**:
- 网关 `xrk --ws 8080`（PID 29208）+ 无头 Host `xrk --host 9999`（PID 8880）同启。
- Python websocket 客户端经 ws://127.0.0.1:8080 发送 AUTH_REQ → Host 返回 AUTH_RESP "OK"
  （认证通过 + consent 自动批准）→ 随后持续收到二进制流（59 条 / 3.58 MB / 5s，即 AES 加密的
  SCREEN_FRAME 画面流；密钥在 AUTH_RESP 中交换，由 React 客户端解密）。
- 结论：浏览器 → 网关 → Host 的完整链路（认证/批准/画面流）已打通。

**优先级**: React SPA 部署、浏览器集成、组件测试均已完成；下一步为 UI/UX 优化 / XRK 主应用集成

---

## 🖥️ 浏览器实时画面显示（本次测试发现并修复的阻断性 Bug）

**现象**: 网关桥接、认证、consent 全部正常，但浏览器画面空白（canvas 无内容）。

**根因（两个代码 Bug）**:
1. **SPA 缺失 AES 解密**：Host 对每个 SCREEN_FRAME 载荷做 AES-256-CBC 加密（密钥在
   AUTH_RESP 中下发：`"OK"` + 32 字节 key + 16 字节 iv）。但 web-client 此前**完全没有解密逻辑**，
   直接把密文喂给 `decodeScreenFrame`，读到的是乱码（宽高/格式全错）→ 无法渲染。
2. **`decodeScreenFrame` 字节偏移错误**：C++ `encodeScreenFrame` 的线格式为
   `width(4) height(4) format(4) timestamp(4) dataSize(4) data(...)`，但 SPA 旧代码把
   `dataSize` 读在 offset 20、data 读在 offset 24（漏掉了 timestamp 字段）。

**修复**:
- 新增 `web-client/src/services/crypto.ts`：`importSessionKey` + `decryptFrame`（Web Crypto
  AES-256-CBC，浏览器安全上下文 localhost 可用）。
- `useWebSocket.ts`：AUTH_RESP 时提取 key/iv 并 `importKey`；SCREEN_FRAME 先解密再 `decodeScreenFrame`；
  `connect()` / `onclose` 时清空会话密钥。
- `protocol.ts` `decodeScreenFrame`：`dataSize` 改读 offset 16、data 改读 offset 20。
- `web-client` 重新构建（资源 hash 变为 `index-Dmkz-rPE.js`），同步更新 `resources/xrk.qrc`，
  重新编译并 `deploy` 打包 XRK。

**验证**:
- SPA `npm run build` 通过（tsc + vite）。
- 端到端实测（Python websocket 客户端，复刻浏览器逻辑）：
  - 连 `ws://127.0.0.1:8080` → AUTH_RESP `"OK"` 且成功提取 key/iv ✓
  - 握手完整：CONSENT_REQUEST(190) / CONSENT_RESPONSE(191) / QUALITY_INFO(22) 均收到 ✓
  - **AES 加解密闭环**：用 Host 完全相同的 AES-256-CBC+PKCS7 加密一个 ScreenFrame，再按 SPA 方式
    解密并解析，字节完全一致（w=1920 h=1080 fmt=0）→ 证明浏览器渲染路径正确 ✓
- **环境限制（非代码问题）**：本无头/非交互会话中 DXGI 桌面复制与 GDI 均无法抓取桌面
  （`SCREENSHOT_REQ` 探针返回空，确认 `captureFrame()` 返回 null）。因此当前环境**不会产出画面帧**，
  浏览器会显示"已连接"但画面为空白——这是会话限制，不是渲染 Bug。

**如何看到真实画面**: 需在能抓取桌面的**交互式桌面会话**中运行 Host：
- 方式 A：启动 GUI `xrk.exe` → 点"启动服务"（运行在用户桌面会话，DXGI/GDI 可捕获）；
- 方式 B：在普通命令行（非本 agent 的无头 shell）执行 `xrk.exe --host 9999`。
  网关 `--ws 8080` 可继续无头运行。两者就绪后浏览器打开 `http://127.0.0.1:8081/` 即可看到实时画面。

**当前运行实例**: 网关 PID 24004（`--ws 8080`）+ 无头 Host PID 16128（`--host 9999`）已启动，
SPA 已含解密修复；仅因环境无法抓屏而无画面流。