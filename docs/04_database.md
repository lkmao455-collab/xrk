# XRK 局域网远程控制软件 - 数据设计文档

## 1. 数据存储策略

XRK采用本地文件存储方式，不依赖外部数据库。

## 2. 配置文件

### 2.1 应用配置 (config.json)
```json
{
  "device": {
    "id": "uuid",
    "name": "Device Name",
    "port": 9999
  },
  "network": {
    "auto_discovery": true,
    "broadcast_interval": 5000,
    "connection_timeout": 10000
  },
  "security": {
    "encryption_enabled": false,
    "session_expiry": 3600000
  },
  "capture": {
    "target_fps": 30,
    "quality": 80,
    "format": "jpeg"
  }
}
```

### 2.2 设备列表 (devices.json)
```json
{
  "devices": [
    {
      "id": "uuid",
      "name": "Device Name",
      "ip": "192.168.1.100",
      "port": 9999,
      "last_seen": "2026-01-01T00:00:00Z"
    }
  ]
}
```

### 2.3 会话记录 (sessions.json)
```json
{
  "sessions": [
    {
      "id": "session-uuid",
      "device_id": "device-uuid",
      "start_time": "2026-01-01T00:00:00Z",
      "end_time": "2026-01-01T01:00:00Z",
      "duration": 3600
    }
  ]
}
```

## 3. 内存数据结构

### 3.1 设备缓存
```cpp
QHash<QString, DeviceInfo> m_devices;
// Key: Device ID
// Value: Device Info
```

### 3.2 会话缓存
```cpp
QHash<QString, SessionInfo> m_sessions;
// Key: Session ID
// Value: Session Info
```

### 3.3 连接池
```cpp
QHash<QString, std::shared_ptr<TcpConnection>> m_connections;
// Key: Device ID
// Value: TCP Connection
```

### 3.4 帧缓冲队列
```cpp
std::deque<Frame> m_frameQueue;
// 线程安全的帧缓冲
// 最大容量: 30帧 (1秒@30fps)
```

## 4. 数据序列化

### 4.1 JSON序列化
- 使用Qt QJsonDocument
- 用于配置文件读写
- 用于调试信息输出

### 4.2 二进制序列化
- 自定义协议格式
- 用于网络传输
- 高效紧凑

## 5. 数据安全

### 5.1 设备ID生成
- 使用QUuid生成唯一ID
- 首次运行时生成并持久化

### 5.2 会话Token
- 随机字符串生成
- 有效期控制
- 内存中存储，不持久化

### 5.3 加密预留
- AES-256加密接口
- 可选启用
- 配置文件控制
