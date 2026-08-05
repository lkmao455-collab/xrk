# xrk 测试断言说明（tests/）

本目录下的 GoogleTest 用例覆盖了协议编解码、加解密、主机/控制端生命周期、设备/会话管理、文件传输、剪贴板、各类消息编解码等。

- **键盘修复相关用例**（`test_input_control.cpp` 的 `InputControlKeyTest.*`、`test_protocol.cpp` 的 `ProtocolTest.EncodeKeyEvent*`）的逐条断言见 **`tests/KEYBOARD_TESTS.md`**（本文不再重复）。
- **鼠标注入相关用例**（`test_input_control.cpp` 的 `InputControlMouseTest.*`）的逐条断言见 **`tests/MOUSE_TESTS.md`**（本文不再重复）。
- 其余测试文件（共 42 个）的断言说明如下，按文件名排序。每条为一行的行为描述（其 `EXPECT/ASSERT` 所校验的内容）。

---

### tests/test_address_book.cpp
Suite(s): AddressBook
- `JsonRoundTripKeepsTagsAndLastSeen`: 条目序列化 JSON 再反序列化，所有字段（id/name/ip/group/favorite/tags/lastSeen）保持不变。
- `CsvRoundTripPreservesEntries`: 含逗号/引号/换行的两条目导出 CSV 再导入，字段与真实换行/引号均保留。
- `GroupRenameAndRemove`: 重命名分组后条目随之移动、旧分组消失，其余分组不受影响。
- `RecentSortsByLastConnected`: `recent(2)` 按 lastConnected 降序返回（最近优先）。
- `ImportMergeByIpUpdatesExisting`: 按 ip 合并导入时以 id 更新已有条目，不重复创建。

### tests/test_arp_resolver.cpp
Suite(s): ArpResolverTest
- `EmptyIp`: `resolveMac("")` 返回空串（不崩溃）。
- `InvalidIp`: 非法/垃圾 IP 返回空而非伪造 MAC。
- `UnreachableIpReturnsEmpty`: 不可达 IP 返回空或合法 MAC 格式。
- `LoopbackEitherEmptyOrValidFormat`: 回环地址解析返回空或格式正确的 MAC。

### tests/test_audit_logger.cpp
Suite(s): AuditLoggerTest
- `ConstructorCreatesLogFile`: 构造函数在临时目录创建 `audit_*.log` 日志文件。
- `LogAllTypes`: 记录连接/认证/会话/操作/错误等类型，校验数量、顺序与分型字段。
- `RecentEntriesLimitNewestFirst`: `recentEntries(2)` 仅返回 2 条且最新在前。
- `EntriesSinceFilter`: `entriesSince` 返回过去时间点之后的条目，未来时间点返回空。
- `EntryAddedSignal`: `entryAdded` 信号每次记录触发一次且载荷正确。
- `SetLogDirectory`: 切换日志目录后新条目写入新目录、不移动旧日志。
- `ClearOldLogs`: `clearOldLogs` 删除带日期的旧日志、保留当前日志。

### tests/test_camera_basic.cpp
Suite(s): CameraTest
- `EnumerateDevices`: 视频输入枚举返回非负数量且不崩溃。
- `CreateCamera`: 有摄像头时 `QCamera` 可用。
- `StartStopCamera`: 启动/停止摄像头会话不崩溃。
- `CameraWithVideoOutput`: 绑定视频控件输出的摄像头启停正常。
- `CameraErrorSignal`: 连接错误处理器后启停，错误信号路径可用。
- `MultipleSessions`: 单个摄像头会话启停可重复。
- `DeviceProperties`: 每个枚举到的摄像头有非空 id 与描述。
- `SupportedFormats`: 有摄像头时支持的格式 API 可访问。

### tests/test_camera_capture.cpp
Suite(s): CameraCaptureTest
- `CameraCountIsNonNegative`: `cameraCount()` >= 0。
- `DefaultIndexIsZero`: 默认摄像头索引为 0。
- `UninitializedReturnsNoFrame`: 未初始化时 `isInitialized()` 为假且返回空帧。
- `ShutdownWhenNotInitializedIsSafe`: 未初始化时 `shutdown()` 为空操作（不崩溃/不抛）。
- `InitializeAndShutdown`: 初始化后 `isInitialized()` 为真，`shutdown()` 清除状态。
- `ReinitializeDoesNotHang`: 重复初始化不挂起/不崩溃。
- `SetCameraIndex`: 合法索引被接受、越界索引被拒绝且不改变已存索引。
- `SetCameraIndexWhenUninitialized`: 未初始化前设置索引记录为待选。
- `CameraName`: 合法索引返回非空名；越界返回空。
- `TeardownWithoutExplicitShutdownDoesNotHang`: 回归——初始化后析构（未显式 shutdown）不得阻塞（无头析构路径）。

### tests/test_clipboard.cpp
Suite(s): ClipboardManagerTest
- `ApplyRemoteDoesNotBroadcast`: 写入远端剪贴板内容不得触发广播回调（防回环）且更新本地文本。
- `LocalChangeBroadcasts`: 本地剪贴板变更被检测并广播（校验 mimeType `text/plain`）。

### tests/test_clipboard_history.cpp
Suite(s): ClipboardHistoryTest
- `AddEntry`: 添加文本条目校验数量、生成 id、mimeType、data、preview、favorite。
- `AddEmptyData`: 空数据被拒（空 id、数量不变）。
- `AddEntryDedup`: 相同数据重复添加去重为单条且 id 相同。
- `PreviewTruncation`: 长文本 preview 截断到 203 字符并以 "..." 结尾。
- `PreviewImage`: 图片条目预览为 `[Image N bytes]`。
- `PreviewUriList`: 单/多 URI 列表预览渲染为 URI 或 `[N files/items]`。
- `Search`: 按内容、mimeType 搜索匹配；无匹配返回空。
- `EntryById`: 按 id 获取可用；未知 id 返回空条目。
- `RemoveEntry`: 删除条目移除之；未知 id 删除安全。
- `ToggleFavorite`: 切换 favorite 双向翻转。
- `Clear`: 清空历史。
- `PruneNonFavorite`: 上限 10 时溢出淘汰最旧、保持恰好 10 条。
- `FavoritesSurvivePrune`: 被收藏的最旧条目在淘汰中保留、非收藏被逐出。
- `SetMaxEntriesClamped`: maxEntries < 1 被夹到 10。
- `ToJsonFromJsonRoundtrip`: ClipboardEntry 序列化/反序列化保留全部字段。
- `SaveLoadRoundtrip`: 条目持久化存储后可正确重载。

### tests/test_controller_screen.cpp
Suite(s): ControllerScreen
- `EncryptedScreenFrameDecrypts`: 回归——驱动 host 式 AUTH_RESP（50 字节 key+IV）与加密 SCREEN_FRAME 经真实控制端槽函数，断言发出的帧等于解密后负载且解码为非黑图像。
- `DecodeSelfHealRequestsJpeg`: 回归——连续 30 次 H264 解码失败后控制端请求切回 JPEG（一次性闩锁），成功后仍保持置位。

### tests/test_database_manager.cpp
Suite(s): DatabaseManagerTest
- `InitializeEnablesWalAndCreatesSchema`: 校验 WAL 日志模式与全部期望表存在。
- `SaveAndLoadMessageRoundTrip`: 保存消息并重载，sender/content/timestamp 一致。
- `LoadMessagesPagination`: 校验按时间升序、limit 取最新 N、beforeTimestamp 仅过滤更旧。
- `LoadMessagesGroupVsDirectAreSeparate`: 私聊与群消息分别存储/查询。
- `DeleteMessageMarksRecalled`: 撤回消息标记为已撤回且内容变为 `[已撤回]`。
- `MarkMessageReadUpdatesUnreadCount`: 标记已读后未读数递减。
- `DeviceCrudRoundTrip`: 设备 保存/更新 lastSeen/删除 往返正确。
- `FriendLifecycle`: 加/删好友切换 `isFriend` 与好友列表。
- `GroupRoundTrip`: 群 保存/更新/删除（含成员）往返。
- `SettingsRoundTripWithDefault`: 设置存取；缺键返回所给默认值或空。
- `RecentChatsOrderedByTimestamp`: 最近会话按时间倒序且 isGroup 正确。
- `SaveMessageCreatesRecentChatEntry`: 保存消息生成对应最近会话条目。
- `TransferRecordRoundTripAndCompletion`: 传输记录存取；进度更新至完成设置状态、已传大小、结束时间。
- `OfflineMessageQueueLifecycle`: 待发离线消息按最旧优先；重试计数与送达移除生效。
- `OfflineMessageExpiredIsExcludedAndCleaned`: 过期离线消息从待发排除并被清理。
- `OfflineMessagesAreDeviceScoped`: 离线消息按目标设备 id 过滤。
- `DataPersistsAcrossReopen`: 设备/消息/设置/好友在数据库关闭重开后仍保留。
- `BuildSyncSnapshotRoundTripsAllTypes`: 同步快照捕获设备、群、设置、消息。
- `BuildSyncSnapshotLimitsMessages`: 快照限制消息为最新 N 条。
- `ApplySyncSnapshotSettingLwwNewerWins`: 较新远端设置覆盖本地、较旧远端被忽略（LWW）。
- `ApplySyncSnapshotDeviceMerge`: 设备合并应用较新远端；较新本地胜出。
- `ApplySyncSnapshotMessageDedupById`: 同一消息快照应用两次幂等（不重复）。
- `ApplySyncSnapshotGroupMerge`: 群合并应用较新远端成员、拒绝较旧远端。

### tests/test_device_discovery.cpp
Suite(s): DeviceDiscoveryTest
- `StartStop`: 启停发现不崩溃。
- `GetDevicesEmpty`: 新建发现无设备。
- `HasDeviceFalse`: 未知 id 的 `hasDevice` 为假。
- `AddDevice`: 注入发现消息后以正确名称注册设备。
- `GetDevices`: 多条注入消息产生期望设备数量。

### tests/test_device_manager.cpp
Suite(s): DeviceManagerTest
- `AddDevice`: 添加设备后列表增长并存储 id。
- `RemoveDevice`: 删除设备后从列表清除。
- `UpdateDevice`: 更新设备改变其存储字段。
- `GetDevice`: 按 id 获取返回匹配设备。
- `GetDeviceNotFound`: 未知 id 返回空设备。
- `MultipleDevices`: 添加 5 个设备列表为 5。
- `ClearDevices`: 删除全部后列表清空。

### tests/test_device_registry.cpp
Suite(s): DeviceRegistryTest
- `RegisterDevice`: 注册存储设备字段与 online 标志。
- `UpdateDevice`: 更新设备名称/分组。
- `RemoveDevice`: 删除清除设备与计数。
- `DeviceGroups`: 按分组查询设备并列出分组名。
- `OnlineDevices`: `onlineDevices` 排除离线设备、计数一致。
- `SearchDevices`: 按名称/IP/MAC/分组搜索匹配。
- `Persistence`: 注册设备经保存+重载保留。
- `GroupManagement`: 重命名分组移动成员；删除分组仅清空其成员的分组。
- `OnlineStatusUpdates`: online 标志切换；心跳更新保持在线。
- `EmptyDeviceId`: 空 id 设备不被注册。
- `NonExistentDevice`: 未知设备的查找/更新/删除安全（不崩溃）。
- `Statistics`: 计数与按分组计数正确、在线计数一致。

### tests/test_e2e_black_screen.cpp
Suite(s): E2E
- `BlackScreenPipeline`: 真实 localhost TCP 端到端——真实 Host + RemoteController + widget 在 5s 内至少交付一帧屏幕帧（复现黑屏 bug）。

### tests/test_encryption.cpp
Suite(s): EncryptionTest
- `RoundTripASCII`: 加解密还原原始 ASCII 明文且密文不同于明文。
- `RoundTripBinaryWith0x80`: 含 0x80 的 256 字节缓冲往返（旧 sentinel-padding bug）。
- `RoundTripAllZeros`: 全零负载往返。
- `RoundTripSingleByte`: 单字节 0x80 往返。
- `RoundTripExactBlockSize`: 32 字节（整块）负载往返。
- `RoundTripLargeFrame`: ~50KB 二进制帧往返；密文大于明文。
- `EncryptProducesValidCiphertext`: 密文大小为 16 的倍数且大于明文。
- `DecryptRejectsInvalidSize`: 15 字节（非整块）密文解密为空。
- `DecryptRejectsEmpty`: 空密文解密为空。
- `DifferentKeysProduceDifferentOutput`: 同明文不同密钥密文不同。

### tests/test_file_sync.cpp
Suite(s): FileSyncTest
- `RelativePathAndRemoteTarget`: 相对路径计算（及回退到 basename）与远端目标路径拼接（容忍尾部斜杠）。
- `AddPairDedupeAndRemove`: 添加配对成功；重复本地被拒；移除清除。
- `InitialAndIncrementalUpload`: 初次扫描上传预置文件到正确远端路径；重扫仅上传新增/修改文件。
- `NoUploadWhenDisconnected`: `canUpload=false` 时不尝试上传。

### tests/test_file_transfer_drag.cpp
Suite(s): FileTransferDragTest
- `DragEnterActivatesHighlight`: 匹配 MIME 的拖入设置 dropping 属性并触发 active 信号。
- `DragLeaveClearsHighlight`: 拖离清除高亮并翻转 active 信号。
- `DropEmitsRemoteDroppedAndClearsHighlight`: 放下触发 `remoteDropped` 并清除高亮。
- `NonMatchingMimeIsIgnored`: 不匹配 MIME 的拖拽被忽略（无高亮、无信号）。

### tests/test_file_transfer_manager.cpp
Suite(s): FileTransferManagerTest
- `UploadFileNotFound`: 上传不存在文件返回空 id。
- `UploadFileSuccess`: 上传真实临时文件返回有效 id 与活跃传输。
- `CancelTransfer`: 取消使传输失效。
- `PauseResumeTransfer`: 暂停/恢复保持传输活跃。
- `GetActiveTransfers`: 两次上传产生两条活跃传输。
- `IsTransferActive`: 传输活跃直至取消。
- `DownloadFile`: 下载注册活跃传输并清理。
- `GetTransferOffset`: 新上传传输 offset 为 0。
- `GetTransferChecksumEmptyForActive`: 传输活跃时 checksum 为空。
- `CanResumeTransfer`: 新上传不可在传输中续传。
- `ResumeTransferWithOffset`: 带 offset 续传返回同一文件 id。
- `ProtocolFileChecksumRoundTrip`: 文件 checksum+id 编解码往返。
- `TransferRecordWithChecksumAndOffset`: 上传、暂停、带 offset 续传正确记录。

### tests/test_frame_queue.cpp
Suite(s): FrameQueueTest
- `FifoOrder`: 入队/出队保持 FIFO 顺序与容量跟踪。
- `EnqueueNonBlockingFull`: 满时非阻塞入队返回 false；出队后成功。
- `DequeueTimeoutOnEmpty`: 空队出队超时（~30ms）返回 0。
- `Clear`: 清空队列并接受新项。
- `BlockingEnqueueWhenFull`: 阻塞入队等待消费者腾出空间。
- `DefaultCapacity`: 默认队列容量 3 且为空。

### tests/test_host_authkey.cpp
Suite(s): HostAuth
- `SendsKeyOnAutoAuth`: 回归——无密码的真实 Host 必须在 auto-auth 时发送含 50 字节会话密钥（"OK"+32 key+16 IV）的 AUTH_RESP。

### tests/test_host_encoder_default.cpp
Suite(s): HostEncoder
- `DefaultEncoderIsJpeg`: 回归——启动的 Host 默认 JPEG 编码器（非 H264）。
- `NonGameGearIsJpeg`: 非游戏档保持 JPEG 编码器。
- `GameGearUsesH264OrFallsBackToJpeg`: 游戏/ultra 档优先 H264，H264 不可用时回退 JPEG。

### tests/test_host_run.cpp
Suite(s): HostRun
- `StartsAndRunsWithoutCrash`: 回归——启动 Host、应用 FPS、运行 worker ~1.2s 不崩溃；stop 置为非运行。

### tests/test_host_teardown.cpp
Suite(s): HostTeardown
- `StartsStopsAndDestroys`: 启动/停止 Host 并析构不挂起（无头析构）。
- `SetQualityLevelAppliesGear`: 质量档映射到期望 jpeg 质量/fps（HIGH/LOW/ULTRA/AUTO）且不启动。
- `ReverseSyncPairRegistration`: `addReverseSync` 注册观察者；`removeReverseSyncForClient` 拆除；重复添加幂等。
- `TrustedIpStore`: 可信 IP 增/删/清与重复添加幂等正确。

### tests/test_ipmsg_crypto.cpp
Suite(s): IpmsgCryptoTest
- `GenerateKeyPairProducesDistinctKeys`: ECDH 密钥对 32 字节且每次不同。
- `PublicKeyDerivedFromPrivate`: 公钥等于 SHA256(私钥)。
- `SharedSecretIsSymmetric`: Alice 与 Bob 由各自密钥对算出相同 32 字节共享密钥。
- `AesGcmRoundTrip`: AES-GCM 加解密往返二进制（含 0x80/0xFF）正确。
- `AesGcmRejectsTamperedCiphertext`: 篡改密文主体或 tag 解密为空。
- `EncryptDecryptMessageRoundTripAcrossPeers`: Alice 加密消息在 Bob 用共享密钥解密成功。
- `EncryptWithoutSessionReturnsPlaintext`: 无会话加密返回原输入。
- `DecryptWithoutSessionReturnsAsIs`: 无会话解密返回原输入。
- `HasEstablishedSessionRequiresSharedSecret`: 仅当共享密钥非空才存在会话。
- `CalculateFileMd5MatchesReference`: 文件 MD5 匹配参考哈希；缺失文件返回空。
- `CalculateFileMd5QIODevice`: 经 QIODevice 的 MD5 匹配参考。
- `ChunkMd5sMatchManualSegments`: 分块 MD5（1MB 分块、末块部分）匹配手算。
- `ChunkMd5sSmallFileSingleChunk`: 极小文件恰产生一个 16 字节分块 MD5。
- `SnapshotSerializeDeserializeRoundTrip`: 同步快照（设备/群/设置/消息）序列化/反序列化保留。
- `SnapshotRoundTripSurvivesJsonStringify`: 含 UTF-8/emoji/撤回字段的快照经 JSON 字符串化往返保留。

### tests/test_jpeg_encoder.cpp
Suite(s): JpegEncoderTest
- `Initialize`: 初始化/关闭切换 `isInitialized`。
- `Type`: 编码器类型报告为 JPEG。
- `DefaultQuality`: 默认质量 70。
- `SetQualityClamped`: 质量夹到 [1,100]。
- `EncodeNullImage`: 编码空图像得到空数据、码率 0。
- `EncodeValidImage`: 编码产生 JPEG（FFD8 头）且码率非零。
- `EncodeQualityAffectsSize`: 低质量输出小于高质量。
- `SetBitrateNoop`: 设码率对 JPEG 为空操作（保持 0）。
- `FactoryCreate`: `VideoEncoder::create(JPEG)` 返回未初始化的 JPEG 编码器。

### tests/test_logger.cpp
Suite(s): LoggerTest
- `Singleton`: `Logger::instance()` 返回同一对象。
- `LogLevels`: 设置/获取各级别日志级别有效。
- `SetLogFile`: 设置日志文件路径被反映。
- `LogMessages`: 各级别记录不崩溃。
- `LogWithCategory`: 分类记录不崩溃。
- `SetLogFileAndWrite`: 记录消息出现在写入的日志文件中。
- `ClearLogFile`: 清空日志文件使其为空。

### tests/test_loopback.cpp
Suite(s): LoopbackConnectionTest
- `ConnectAndAuthenticate`: 真实控制端连接 stub host 完成认证并建立传输。
- `WrongPasswordRejected`: 错误密码被拒（authFailed，无 authSuccess）。
- `SystemInfoRoundTrip`: 认证后 SYSINFO_REQ 得到真实 SYSINFO_RESP（双向通道可用）。

### tests/test_message_codec_edge.cpp
Suite(s): MessageCodecEdgeTest
- `DecodeTooShort`: 过短/空缓冲解码失败。
- `DecodeInvalidMagic`: 破坏 magic 字节解码失败。
- `ParseHeaderWrongVersion`: 破坏版本字节头部解析失败。
- `ParseHeaderSessionIdTooLong`: 超长 sessionId 长度头部解析失败。
- `DecodeDeclaredLengthExceedsData`: 声明负载长度超出缓冲解码失败。
- `VerifyChecksumTooShort`: 空/2 字节输入校验和验证失败。
- `VerifyChecksumValidMessage`: 合法消息通过校验和。
- `VerifyChecksumCorruptedPayload`: 破坏负载使校验和失效。
- `ChecksumDeterministic`: 同输入同校验和、异输入异校验和。
- `CreateHeaderRoundTrip`: 头部 type/length/sessionId 往返。
- `UnicodeSessionIdRoundTrip`: Unicode session id 编解码往返。
- `EmptyPayloadRoundTrip`: 空负载编解码往返。

### tests/test_microphone_basic.cpp
Suite(s): MicrophoneTest
- `EnumerateDevices`: 音频输入枚举返回非负数量。
- `EnumerateOutputDevices`: 音频输出枚举返回非负数量。
- `CreateAudioInput`: 有设备时可创建音频输入。
- `CreateAudioOutput`: 可创建音频输出并报告所设音量。
- `AudioFormat`: 音频格式字段（采样率/声道/采样格式）正确。
- `AudioInputWithSession`: 音频输入+输出可接入采集会话。
- `VolumeLevels`: 音量 0/0.5/1.0 精确回读。
- `DeviceProperties`: 每个输入设备有非空 id/描述。
- `OutputDeviceProperties`: 每个输出设备有非空 id/描述。
- `MuteFunctionality`: 静音切换 `isMuted`。
- `AudioSinkCreation`: 可按格式创建音频 sink。
- `MultipleSessions`: 第二个采集会话启动不崩溃。
- `DeviceState`: 枚举到的输入设备非空。

### tests/test_network.cpp
Suite(s): NetworkTest
- `Initialize`: 初始化设置运行状态并报告端口。
- `InitializeTwice`: 重复初始化仍运行。
- `Shutdown`: 关闭清除运行状态。
- `BroadcastDiscovery`: 广播发现后网络保持运行。
- `ConnectToNonexistent`: 连接不存在主机返回空连接。
- `DisconnectAll`: 全断开后网络仍运行。

### tests/test_p2p_relay.cpp
Suite(s): RelayServerTest, P2PManagerTest
- `TokenAuthRejectsBadToken`: 错误中继 token 得到 `ERROR AUTH_FAILED`。
- `TokenAuthAcceptsGoodToken`: 正确 token 得到 `REGISTERED`。
- `PunchExchangesPeerAddr`: 两个已注册对等方打洞交换 PEER_ADDR（回环 IP 与期望 P2P 端口）。
- `ConnectsToListeningPeer`: P2P manager 与监听对等方建立直连。
- `FailsToClosedPort`: 打洞到关闭端口最终触发 `punchFailed`。

### tests/test_remote_terminal.cpp
Suite(s): RemoteTerminalTest
- `StartStop`: 启动终端置运行；停止清除。
- `EchoOutput`: 写入 `echo` 命令在输出中产生标记且 `exit` 关闭终端。

### tests/test_screen_recorder.cpp
Suite(s): ScreenRecorderTest
- `StartStopBasic`: 录制启动、报告路径、创建 AVI 文件并停止。
- `StartInvalidPath`: 写到非法路径失败且保持未录制。
- `AddFrameWhenNotRecording`: 录制前加帧不产生文件。
- `AddEmptyFrameNoop`: 加空帧被忽略；文件为合法单帧 AVI。
- `AviStructure`: 录制 AVI 含 RIFF/AVI /hdrl/movi/idx1/00dc、正确帧数与 RIFF 大小。
- `FpsClamped`: fps 0 夹到 1、fps 100 夹到 60（写入 AVI 头）。
- `RestartRecording`: 重新录制正确切换活动文件路径。

### tests/test_security_manager.cpp
Suite(s): SecurityManagerTest
- `GenerateDeviceId`: 设备 id 非空。
- `DeviceIdUnique`: 两个设备 id 不同。
- `GenerateToken`: 会话 token 非空。
- `TokenUnique`: 两个 token 不同。
- `ValidateToken`: 生成的 token 校验通过。
- `ValidateTokenInvalid`: 非法 token 校验失败。
- `EncryptDecrypt`: 开启加密时 encrypt→decrypt 往返且密文异于明文。
- `EncryptEmpty`: 加密空数据得空。
- `HashPassword`: 密码哈希非空且异于原文。
- `VerifyPassword`: 正确密码校验通过。
- `VerifyPasswordWrong`: 错误密码校验失败。
- `GenerateECDHKeyPair`: ECDH 私钥 32 字节且非零。
- `DeriveKeyFromPassword`: 密钥派生对 (password,salt) 确定性且依赖 salt。
- `CreateE2EESession`: 创建 E2EE 会话，32 字节密钥、AES-256-GCM、12 字节 nonce 正确。
- `E2EESessionUniqueIds`: 两个会话 id 不同。
- `RemoveE2EESession`: 移除已有会话成功；未知返回 false。
- `GetAllE2EESessions`: 三个会话可列出且全部可移除。
- `UpdateSessionActivity`: 更新活动把 `lastActivity` 前移。
- `GetSessionId`: 会话 id 可取且在移除后清空。
- `GetNonExistentE2EESession`: 未知设备查找安全（空）。

### tests/test_session_manager.cpp
Suite(s): SessionManagerTest
- `CreateSession`: 创建会话返回有效 id。
- `CloseSession`: 关闭使会话失效。
- `GetSession`: 获取会话返回匹配 id/device 与 active 标志。
- `GetActiveSessions`: 两个已建会话出现在活跃列表。
- `CleanupExpiredSessions`: 会话在显式关闭前保持有效（无公开过期）。
- `MultipleSessions`: 五个会话都得到有效 id。
- `CloseAllSessions`: 全部关闭后活跃列表清空。

### tests/test_svg_icons.cpp
Suite(s): SvgIconTest
- `AllNavIconsLoadAndRender`: 每个导航 SVG 图标（及窗口 PNG）从资源加载并光栅化为至少一个可见（非透明）像素。

### tests/test_system_info.cpp
Suite(s): SystemInfoTest
- `CollectBasicFields`: 收集的 SysInfo 含 Windows OS、非空版本、正的内存/磁盘/运行时长/进程数。
- `CollectUsageRanges`: CPU/内存/磁盘使用率在 [0,100]。

### tests/test_theme_manager.cpp
Suite(s): ThemeManagerTest
- `AvailableThemes`: 恰好 8 个主题，各含合法颜色，含 cute_pink 与 otaku。
- `CurrentTheme`: 当前主题 id/name 非空且一致。
- `ApplyInvalidThemeNoop`: 应用未知主题不改变当前主题。
- `ApplyThemeRoundTrip`: 应用 otaku/student 更新当前主题 id。
- `GenerateQSSContainsColors`: 生成 QSS 非空、含主题色与 QPushButton/QMenuBar 选择器。
- `GenerateQSSPerTheme`: cute_pink 与 otaku 的 QSS 不同。

### tests/test_translation_manager.cpp
Suite(s): TranslationManagerTest
- `AvailableLanguages`: 恰好 10 种语言，含 en_US、zh_CN、zh_TW、ja_JP、ko_KR。
- `SystemLanguageSupported`: 检测到的系统语言在可用语言中。
- `DefaultLanguageName`: 默认当前语言为空、其名为 "English"。

### tests/test_video_roundtrip.cpp
Suite(s): VideoRoundTrip
- `H264EncodeDecode`: 有可用 H264 编码器时编码→解码得到非空、非黑帧（缺 libx264 时跳过）。

### tests/test_voice_messages.cpp
Suite(s): VoiceMessageTest / VideoMessageTest / LocationMessageTest / CardMessageTest / MergeForwardMessageTest / CallInviteTest / CallAcceptTest / CallRejectTest / CallEndTest / IceCandidateTest / VideoCallStartTest / VideoCallStopTest / VideoCallFrameTest / ScreenShareStartTest / ScreenShareStopTest / ScreenShareFrameTest / GroupAnnouncementTest / GroupMentionTest / GroupVoteTest / GroupFileTest / GroupAlbumTest / GroupTodoTest
（以下每条覆盖对应消息类型的 编解码往返 / 空数据 / 协议消息往返 / 类型常量存在）
- `VoiceMessage*`: 语音消息字段保留；空语音数据保留空与已读；VOICE_MSG=143、VOICE_ACK=144。
- `VideoMessage*`: 视频消息字段（含尺寸）保留；空视频数据零尺寸；VIDEO_MSG=145、VIDEO_ACK=146。
- `LocationMessage*`: 位置消息 lat/long/name 保留；空位置归零；LOCATION_MSG=147、LOCATION_ACK=148。
- `CardMessage*`: 名片（vCard）数据保留；空 vCard 保留空；CARD_MSG=149、CARD_ACK=150。
- `MergeForwardMessage*`: 合并转发保留嵌套转发消息（ids/content/msgType）；空列表保留空；MERGE_FORWARD=151、MERGE_FORWARD_ACK=152。
- `CallInvite*`: 呼叫邀请保留 callId/caller/sdp；视频呼叫 callType 正确；CALL_INVITE=153。
- `CallAccept*`: 呼叫接受保留 callId/callee/sdp；CALL_ACCEPT=154。
- `CallReject*`: 呼叫拒绝保留 callId/callee/reason；CALL_REJECT=155。
- `CallEnd*`: 呼叫结束保留 callId/peer；CALL_END=156。
- `IceCandidate*`: ICE 候选保留 callId/candidate；ICE_CANDIDATE=157。
- `VideoCallStart*`: 视频通话开始保留尺寸/fps；VIDEO_CALL_START=250。
- `VideoCallStop*`: 视频通话停止保留 callId/peer；VIDEO_CALL_STOP=251。
- `VideoCallFrame*`: 视频通话帧保留帧数据/关键帧/序号；非关键帧 isKeyFrame 为假；VIDEO_CALL_FRAME=252、VIDEO_CALL_ACK=253。
- `ScreenShareStart*`: 屏幕共享开始保留尺寸/fps；SCREEN_SHARE_START=254。
- `ScreenShareStop*`: 屏幕共享停止保留 sessionId/peer；SCREEN_SHARE_STOP=255。
- `ScreenShareFrame*`: 屏幕共享帧保留帧数据/关键帧/序号；非关键帧 isKeyFrame 为假；SCREEN_SHARE_FRAME=256、SCREEN_SHARE_ACK=257。
- `GroupAnnouncement*`: 群公告保留 group/announcer；GROUP_ANNOUNCEMENT=258。
- `GroupMention*`: 群@保留 message/被@成员；GROUP_MENTION=259。
- `GroupVote*`: 群投票保留 title/options/creator；GROUP_VOTE=260。
- `GroupFile*`: 群文件保留 fileId/name/size/md5/uploader；GROUP_FILE=261、GROUP_FILE_ACK=262。
- `GroupAlbum*`: 群相册保留 album/file 列表/creator；GROUP_ALBUM=263、GROUP_ALBUM_ACK=264。
- `GroupTodo*`: 群待办保留 title/status/priority/assignee/日期；GROUP_TODO=265、GROUP_TODO_ACK=266、GROUP_TODO_UPDATE=267。

### tests/test_wake_on_lan.cpp
Suite(s): WakeOnLanTest
- `ValidMacFormats`: 接受冒号/短横/纯十六进制及带空格填充的 MAC 格式。
- `InvalidMacFormats`: 拒绝错误长度、13 字节、非十六进制字符、缩写形式。
- `NormalizeMacAddress`: 所有接受格式归一化为纯大写 12 位十六进制串。
- `SendMagicPacketInvalidMac`: 空/非法 MAC 发送返回 false。
- `SendMagicPacketLoopback`: 向回环（127.0.0.1:9）发送魔法包返回 true。

### tests/test_web_socket_gateway.cpp
Suite(s): WebSocketGatewayTest
- `EchoRoundTrip`: WS 客户端消息经网关回显到后端 host；会话计数更新。
- `MultipleClients`: 两个 WS 客户端各得各自回显；会话计数为 2。
- `ClientClosePropagatesToTarget`: 关闭 WS 客户端把断开传播到目标 host 并清除会话。
- `HttpServesWebPage`: 网关提供 SPA 首页（200、含 "XRK"/assets），提供引用 /assets/* 与 /favicon.svg（200），未知路径 404。
- `StopWithActiveClients`: 有活跃客户端时停止网关干净拆除（非运行、零会话）。
- `ReassemblesMultipleCoalescedMessages`: 后端一次性写入两条完整 XRK 消息（回环可能合并为单段），网关须将其作为【两条】独立 WS 二进制帧转发（不合并、不残缺）。
- `ReassemblesFragmentedSingleMessage`: 回归——后端把一条大消息拆成两段 TCP 分片先后送达，网关须将其重组为【恰好一条】完整 WS 二进制帧（TCP→WS 分片缺陷曾导致浏览器收到残缺帧）。校验解出 SCREENSHOT_RESP 且负载字节完整。

---

## 运行方式

```bash
cmake --build build --target xrk_tests --config Release
./build/tests/Release/xrk_tests.exe --gtest_filter='<SuiteName>.*'
```

例如只跑键盘相关断言（详见 `tools/keyboard_layout_probe.md` 第 8 节）：

```bash
./build/tests/Release/xrk_tests.exe --gtest_filter='InputControlKeyTest.*:ProtocolTest.EncodeKeyEvent*'
```
