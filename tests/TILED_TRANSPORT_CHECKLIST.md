# 弱网分块传输（Tiled Transport）手动验证清单

本清单用于在两台真实机器（或同一机器的两个进程）上验证弱网分块屏幕传输
（Phases A–F）。自动化测试见 `test_tile_encoder.cpp`（编码器/协议）与
`test_tiled_transport.cpp`（真实 socket 回环 + 模拟丢包）。

## 0. 前置条件

- 两端均使用 JPEG 编码器（默认）。H264 与分块互斥：只要有一个客户端不是
  tile-capable，或当前编码器为 H264，主机就会回退到整帧 `SCREEN_FRAME`。
- 两个实例：一个做 Host（被控/主控端），一个做 Controller（控制端）。
- 运行方式：`build\src\app\Release\xrk.exe`（或对应 target 的可执行文件）。
- 日志实时输出到标准输出/日志窗口。本文档第 4 节列出需要观察的关键日志。

## 1. 能力握手（Phase C — 启用分块）

1. Controller 成功 AUTH 后，会自动发送 `SCREEN_KEYFRAME`（声明支持分块）。
2. Host 收到后日志出现：
   `Host: tiled screen transport enabled`
3. 如果看到 `disabled`，检查：是否所有已认证客户端都支持分块、当前编码器是否为 JPEG。

## 2. 脏区检测（Phase A）

分块仅对**变化区域**编码，静态屏幕不重复发送：

- 屏幕静止时：主机几乎不发送 `SCREEN_TILE`，日志
  `EncodeWorker(tiled): N frames ... 0 deferred` 且 tiles 数很小或为空。
- 移动一个窗口 / 拖动滑块：只应发送覆盖该区域的少量 tile。
  （可用 `test_tiled_transport.cpp::OnlyChangedTileIsStreamed` 自动验证：
  只改一个 tile 时主机恰好发出 1 个 tile。）
- DXGI 元数据不可用时（如设备重置）走 `hasFrameChanged` 兜底：整帧无变化则
  不发送；整帧有变化才强制全屏。这避免了“元数据丢失就每帧重发整屏”。

## 3. 逐 tile 自适应编码（Phase D）

- 纯色 / 少量颜色区域 → RLE（无损）。
- 照片 / 视频区域 → JPEG（按自适应质量）。
- 切换不同内容区域，观察传输字节量随内容类型变化。
  （`test_tile_encoder.cpp` 中 `FlatColorRoundTripIsLosslessRLE` /
  `PhotographicTileEncodesJpeg` 已自动验证分类与往返正确性。）

## 4. 弱网与修复（Phases E / F）— 关键观察项

 induce 弱网（任选其一）：
 - 跨网段/跨 VPN 制造高延迟、抖动、丢包；
 - 或在主机端用网络限速工具限制上行带宽。

观察日志：

| 现象 | Host 日志 | 含义 |
|------|-----------|------|
| 周期性全屏修复 | `Host: periodic keyframe requested (interval elapsed)` | 每 `KEYFRAME_INTERVAL_MS`(10s) 强制一次全屏关键帧，收敛残留错块 |
| 关键帧发出 | `EncodeWorker(tiled): keyframe sent — <n> tiles, <kB>KB` | 客户端请求/周期触发了全屏重发 |
| 链路变差 | `Host: tile budget <old> -> <new> (worst rtt=..ms loss=..% buffer=..)` | AIMD：拥塞时预算减半，健康时逐步加回 |
| 客户端报告弱链 | `Host: client <id> weak link — rtt=..ms loss=..% buffer=..` | 客户端 ack 反馈 RTT/丢包/缓冲过高（每 5s 最多一条）|
| 客户端丢块修复 | `RemoteController: ack — received=.. lost=.. rtt=..ms buffer=..%` | 客户端每 1s 回报；`lost>0` 表示有丢块 |
| tile 校验失败 | `RemoteController: tile MD5 mismatch (corrupt on wire) — requesting tile resend` | 单 tile 被篡改/损坏，客户端丢弃并发送**精准 NACK**（只请求该 tile，而非整屏关键帧）|
| 精准重传发出 | `RemoteController: NACK — requested resend of N tile(s) (frame WxH)` | 客户端把 ≤100ms 内累积的坏 tile 合并成一条 `SCREEN_TILE_REQUEST` 发给主机 |
| 主机精准重传 | `EncodeWorker(tiled): NACK resend — N tile(s)` | 主机从最近一帧缓存 `m_lastRawFrame` 重新编码并单发被请求的 tile |
| 队列溢出 | `RemoteController: tile queue full — dropped tile, requesting keyframe` | 解码侧来不及消费，触发全屏修复 |

验证点：
- 制造丢包后，画面短暂出现残块 → 在下一个关键帧（≤10s 或客户端主动请求）后
  **自动恢复**（关键帧强制全屏重绘，绕过 hash 跳过）。
- **精准 NACK（Phase F 逐 tile 重传）**：单个 tile 被篡改/损坏时，客户端应只请求
  重传这一个 tile（日志 `NACK — requested resend of 1 tile(s)`），而非整屏关键帧；
  主机 `NACK resend` 后该 tile 修复、画面收敛，其余区域不受影响。比关键帧修复更省带宽。
- 带宽紧张时，`tile budget` 日志应出现下降；宽松后应回升。
- `test_tiled_transport.cpp::LostTilesRepairedByKeyframe` 已自动模拟丢包并断言
  客户端最终收敛到与主机一致的画面。
- `test_tiled_transport.cpp::NackRepairsSpecificTile` 已自动模拟单 tile 损坏，并断言
  主机精准重传后该区域被修复（精确修复路径，无需关键帧）。

## 5. 向后兼容

- 旧版 Controller（从不发送 `SCREEN_KEYFRAME`）连接时，主机保持整帧
  `SCREEN_FRAME` 路径，分块不会被启用。确认旧客户端画面正常、无黑屏。

## 6. 快速回归（自动化）

```
export PATH="/d/Qt/6.10.0/msvc2022_64/bin:$PATH"
export QT_QPA_PLATFORM=offscreen
./build/tests/Release/xrk_tests.exe --gtest_filter='TileEncoder.*:TiledTransportTest.*'
```

预期：14 个测试全部 PASSED（9 个编码器/协议 + 5 个回环传输）。

## 7. 光标优先级（鼠标所在 tile 优先）

- 主机 `EncodeWorker` 在发送分块前用 `std::stable_partition` 把指针覆盖的 tile
  排到最前（`m_mousePos` 由 `processMouseEvent` 经 `setMousePosition` 喂入）。
- 弱网/限带宽时，用户正在操作的区域会先于屏幕其余部分重绘。
- `test_tiled_transport.cpp::CursorTileSentFirst` 已自动验证：在只发送第一个（最高
  优先级）tile、丢弃其余的限带宽场景下，主机确实先发了光标所在 tile，且客户端先
  收到并应用了该 tile（光标区域已更新，而其他已改动区域仍为旧基线）。

> 注：客户端 `applyTile` 在解码线程上运行，其内的 NACK 计时器通过
> `QMetaObject::invokeMethod(m_nackTimer, "start", QueuedConnection)` 切回主线程启动，
> 避免“跨线程启动定时器”错误；`m_pendingNack` 用 `QMutex` 保护。
