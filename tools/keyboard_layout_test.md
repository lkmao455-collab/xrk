# keyboard_layout_test 使用文档

跨键盘布局按键实测工具。用于在本机**直接验证**远程控制中"键值乱了"的根因与修复效果，无需两台机器、也无需真实建立远程连接。

它在本进程内模拟"被控机"：把当前线程强制切换到与你物理键盘**不一致**的键盘布局，然后用两种方式把同一串按键注入到一个文本框里，肉眼对比结果。

- **旧路径（仅 VK）**：复现修复前的 bug —— 只用 Windows 虚拟键码（`wVk`）注入，结果由目标布局翻译，布局不一致时乱码。
- **新路径（Unicode）**：复现修复后的行为 —— 用 `KEYEVENTF_UNICODE` 直接注入字符的 UTF-16 码元，结果与布局无关。

---

## 1. 适用环境

- **必须**在真实桌面会话中运行（需要交互式窗口 + 鼠标点击）。
- 控制台 / 无头（offscreen、CI、远程服务）环境无法使用：没有可聚焦的前台窗口，`SendInput` 也无处投递。
- 仅 Windows 有效（依赖 `SendInput` / `ActivateKeyboardLayout`）。

## 2. 构建

工具随主工程一起构建（已接入 `tools/CMakeLists.txt`）。在已配置好的构建目录下：

```bash
# 配置（首次需要；之后可跳过）
cmake -S . -B build

# 构建工具
cmake --build build --target keyboard_layout_test --config Release
```

产物：`build/tools/Release/keyboard_layout_test.exe`

> 构建依赖 Qt6（本机 `D:/Qt/6.10.0/msvc2022_64`）与 `xrk_hw` 静态库，无需额外操作。

## 3. 运行

双击 `keyboard_layout_test.exe`，或在命令行：

```bash
build\tools\Release\keyboard_layout_test.exe
```

窗口标题：**跨布局按键实测 (xrk)**。

## 4. 操作步骤

1. **聚焦文本框**：用鼠标点一下窗口中间的文本框（占位提示"注入结果显示在这里..."），让它成为前台焦点。注入的按键会落到这个框里。
2. **选择"被控机布局"**：在下拉框里选一个与你物理键盘**不同**的已安装布局（如 `俄语 / Greek`）。切换后当前线程即被强制使用该布局，模拟"被控机布局不一致"。
3. **旧路径注入**：点击 **「旧路径: 仅 VK 注入 "Test"」**。观察文本框出现的内容 —— 通常会变成乱码（如俄语布局下类似 `Теьу`）。
4. **清空**：点击 **「清空」**。
5. **新路径注入**：点击 **「新路径: Unicode 注入 "Test"」**。观察文本框 —— 始终显示 `Test`。

## 5. 预期结果

| 路径 | 注入方式 | 布局与物理键一致 | 布局与物理键不一致 |
|------|----------|------------------|--------------------|
| 旧路径 | `wVk`（VK） | `Test` | **乱码**（如 `Теьу`） |
| 新路径 | `KEYEVENTF_UNICODE` | `Test` | `Test` |

结论：只要两端布局不同，旧路径必然乱码；新路径恒定正确。这正是 `src/hw/input_control.cpp` 中 `keyEvent()` 改为"有字符走 Unicode、否则走 VK"后所解决的问题。

## 6. 让对比更直观的建议

- 在系统"设置 → 时间和语言 → 语言和区域 → 键盘"里**预先安装**一个不同布局（推荐俄语 `ru-RU` 或希腊语 `el-GR`）。装好后下拉框里会出现对应项，旧路径会显示真实的非拉丁字符，对比最明显。
- 若下拉框只有当前一种布局，仍可切换，但旧路径可能只是大小写/符号的细微差异，不如跨语种直观。

## 7. 常见问题

- **点了按钮文本框没变化？** 多半是文本框没有获得焦点（注入目标不是前台窗口）。先点一下文本框再点按钮；若仍无效，确认是否在真实桌面会话中运行（见第 1 节）。
- **中文/IME 下旧路径出的是汉字而非字母？** 这正是 IME 介入的表现，属于 bug 的一部分；新路径发送 Unicode 会绕过 IME，输出仍为 `Test`。
- **与其他测试的区别**：`keyboard_layout_probe.exe` 是无头探针，用 `ToUnicodeEx` 在命令行直接打印"同一 VK 在不同布局下被翻译成不同字符"，适合没有桌面环境的机器做快速验证；本工具则是可交互的端到端演示。

## 8. 相关代码

- 注入逻辑：`src/hw/input_control.cpp` 的 `InputControl::keyEvent()` / `simulateText()`
- 控制端发送字符：`src/ui/remote_desktop_widget.cpp` 的 `keyPressEvent` / `sendKeyEventToRemote`（发送 `QKeyEvent::text()`）
- 协议字段：`src/core/types.h` 的 `KeyEvent::text`，`src/core/protocol_manager.cpp` 的 `encodeKeyEvent` / `decodeKeyEvent`
