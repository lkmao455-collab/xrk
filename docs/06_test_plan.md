# XRK 局域网远程控制软件 - 测试方案

## 1. 测试策略

### 1.1 测试层次
- **单元测试**: 模块内部功能测试
- **集成测试**: 模块间交互测试
- **系统测试**: 完整功能测试
- **性能测试**: 压力和性能验证

### 1.2 测试框架
- **Google Test (GTest)**: 单元测试框架
- **Qt Test**: Qt专用测试工具
- **手动测试**: UI交互测试

## 2. 单元测试

### 2.1 Core Layer 测试

#### NetworkManagerTest
```cpp
TEST(NetworkManagerTest, Initialize) {
    NetworkManager manager;
    EXPECT_TRUE(manager.initialize(9999));
    EXPECT_TRUE(manager.isRunning());
    manager.shutdown();
}

TEST(NetworkManagerTest, BroadcastDiscovery) {
    NetworkManager manager;
    manager.initialize(9999);
    // 验证广播发送
    manager.broadcastDiscovery();
}
```

#### ProtocolManagerTest
```cpp
TEST(ProtocolManagerTest, EncodeDecode) {
    QByteArray payload = "test data";
    QByteArray encoded = ProtocolManager::encode(
        MessageType::HEARTBEAT, payload);
    
    MessageType type;
    QByteArray decoded;
    EXPECT_TRUE(ProtocolManager::decode(encoded, type, decoded));
    EXPECT_EQ(type, MessageType::HEARTBEAT);
    EXPECT_EQ(decoded, payload);
}
```

#### DeviceDiscoveryTest
```cpp
TEST(DeviceDiscoveryTest, StartStop) {
    NetworkManager network;
    network.initialize(9999);
    
    DeviceDiscovery discovery(&network);
    discovery.startDiscovery();
    // 验证发现机制
    discovery.stopDiscovery();
}
```

### 2.2 Application Layer 测试

#### DeviceManagerTest
```cpp
TEST(DeviceManagerTest, AddRemoveDevice) {
    DeviceManager manager(nullptr);
    
    DeviceInfo info;
    info.deviceId = "test-123";
    info.deviceName = "Test Device";
    
    manager.addDevice(info);
    EXPECT_TRUE(manager.hasDevice("test-123"));
    EXPECT_EQ(manager.deviceCount(), 1);
    
    manager.removeDevice("test-123");
    EXPECT_FALSE(manager.hasDevice("test-123"));
}
```

#### SessionManagerTest
```cpp
TEST(SessionManagerTest, CreateSession) {
    SessionManager manager;
    QString sessionId = manager.createSession("device-123");
    
    EXPECT_FALSE(sessionId.isEmpty());
    EXPECT_TRUE(manager.isSessionValid(sessionId));
    
    manager.closeSession(sessionId);
    EXPECT_FALSE(manager.isSessionValid(sessionId));
}
```

### 2.3 Hardware Layer 测试

#### InputControlTest
```cpp
TEST(InputControlTest, Initialize) {
    InputControl control;
    EXPECT_TRUE(control.initialize());
    
    // 模拟鼠标移动
    control.simulateMouseMove(100, 100);
    
    control.shutdown();
}
```

## 3. 集成测试

### 3.1 网络通信测试
```cpp
TEST(IntegrationTest, TcpCommunication) {
    NetworkManager server;
    NetworkManager client;
    
    server.initialize(9999);
    client.initialize(9998);
    
    // 服务器监听
    // 客户端连接
    // 发送消息
    // 验证接收
    
    server.shutdown();
    client.shutdown();
}
```

### 3.2 远程桌面测试
```cpp
TEST(IntegrationTest, RemoteDesktop) {
    // 启动屏幕采集
    // 启动网络服务
    // 模拟远程连接
    // 验证帧传输
}
```

## 4. 性能测试

### 4.1 屏幕采集性能
- 测试目标: 1920x1080 @ 30fps
- CPU占用 < 30%
- 内存占用 < 200MB

### 4.2 网络传输性能
- 帧延迟 < 100ms
- 带宽占用 < 10Mbps (JPEG压缩)
- 丢包率 < 1%

### 4.3 并发连接测试
- 支持同时10个客户端连接
- 连接建立时间 < 1s

## 5. 测试用例

### 5.1 功能测试用例
| ID | 测试项 | 预期结果 |
|----|--------|----------|
| TC01 | 设备发现 | 能发现局域网内设备 |
| TC02 | 建立连接 | TCP连接成功建立 |
| TC03 | 屏幕共享 | 远端桌面正常显示 |
| TC04 | 鼠标控制 | 远端鼠标响应正确 |
| TC05 | 键盘控制 | 远端键盘响应正确 |
| TC06 | 文件传输 | 文件上传下载成功 |
| TC07 | 断线重连 | 网络中断后自动重连 |

### 5.2 异常测试用例
| ID | 测试项 | 预期结果 |
|----|--------|----------|
| EX01 | 网络断开 | 提示连接中断 |
| EX02 | 服务端崩溃 | 客户端超时处理 |
| EX03 | 数据包损坏 | 校验失败丢弃 |
| EX04 | 内存不足 | 优雅降级 |

## 6. 测试执行

```powershell
# 编译测试
cmake --build build --target xrk_tests

# 运行所有测试
cd build
ctest --output-on-failure

# 运行特定测试
.\bin\Release\xrk_tests.exe --gtest_filter=NetworkManagerTest.*
```

## 7. 测试报告

测试完成后生成报告:
- 通过率
- 失败用例详情
- 代码覆盖率
- 性能指标
