# 键盘测试断言说明（tests/）

本文档说明键盘修复相关的 GoogleTest 断言。这些用例锁定"跨布局按键乱码"修复的正确性，是 `tools/keyboard_layout_probe.md`（无头运行时演示）与 `tools/keyboard_layout_test.exe`（GUI 交互演示）的**可断言对应物**——在 CI / 命令行给出 pass/fail 与非零退出码。

涉及文件：
- `tests/test_input_control.cpp` —— `InputControlKeyTest`（注入逻辑辅助方法）
- `tests/test_protocol.cpp` —— `ProtocolTest::EncodeKeyEvent` / `EncodeKeyEventUnicode`（键盘消息字段往返）

---

## 1. 背景：修复与断言的对应关系

根因：控制端发送 Windows 虚拟键码（`wVk`），被控端 `SendInput` 注入时经**被控机当前键盘布局/输入法**翻译，布局不一致即乱码。

修复：控制端额外发送 `QKeyEvent::text()`（真实字符）；被控端 `InputControl::keyEvent()` 改为"有字符走 `KEYEVENTF_UNICODE`，否则走 `wVk` 并补 `KEYEVENTF_EXTENDEDKEY`"。

三组断言分别锁住修复的三个关键点，防止回归：

| 修复点 | 锁定它的断言 |
|--------|--------------|
| 字符按 UTF-16 码元注入，与布局/IME 无关 | `InputControlKeyTest.UnicodeUnits*`（CJK、代理对、畸形 UTF-16） |
| 中文/多字符文本跨网络不丢不乱 | `ProtocolTest.EncodeKeyEvent*` |
| 非字符键（方向键等）位置正确 | `InputControlKeyTest.ExtendedFlagForArrowsButNotLetters` |

---

## 2. 构建与运行

```bash
cmake --build build --target xrk_tests --config Release

# 只跑键盘相关断言
./build/tests/Release/xrk_tests.exe \
    --gtest_filter='InputControlKeyTest.*:ProtocolTest.EncodeKeyEvent*'
```

- 这些用例为**无头可跑**，已在沙箱内验证 7 项全过。
- `test_input_control.cpp` 整体被 `#ifdef _WIN32` 包围：非 Windows 平台（Linux/macOS）下整套跳过，因为其断言依赖 Windows 的 `INPUT` / `KEYEVENTF_UNICODE` 语义；`ProtocolTest.EncodeKeyEvent*` 跨平台均生效。
- `ASSERT_EQ` 失败会立即终止该用例；`EXPECT_EQ` 失败仅记错并继续；两者都会让汇总出现 `FAILED` 且进程返回非零。

---

## 3. `tests/test_input_control.cpp` —— `InputControlKeyTest`

这些用例直接验证 `InputControl` 的注入逻辑辅助方法（`unicodeUnits` / `isExtendedKey`），即 `keyEvent()` 最终发给 `SendInput` 的 `INPUT[]` 的决定因素。

| 用例 | 断言 | 锁定的修复点 |
|------|------|--------------|
| `UnicodeUnitsSingleChar` | `unicodeUnits("A")` → 长度 1，且 `[0] == 0x0041` | 单字符被分解为正确的 UTF-16 码元（Unicode 注入的数据源） |
| `UnicodeUnitsCJKIsLayoutIndependent` | `unicodeUnits("你")` → `[0] == 0x4F60` (U+4F60) | 中文字符按码元注入，与主机布局/IME 无关（修复核心） |
| `UnicodeUnitsSurrogatePair` | `unicodeUnits("😀")` → `[0] == 0xD83D, [1] == 0xDE00` | 代理对（emoji 等）正确分组，Unicode 注入不丢字符 |
| `UnicodeUnitsStrayLowSurrogateSkipped` | 孤立低代理项 + `'A'` → 仅 `[0] == 0x0041` | 畸形 UTF-16（单独低代理项）被安全跳过，不会注入乱码 |
| `ExtendedFlagForArrowsButNotLetters` | `isExtendedKey(0x25/0xA3/0x5B)` 为真；`isExtendedKey(0x0D/0x41/0x31)` 为假 | 方向键 / 右Ctrl / Win 等正确打 `KEYEVENTF_EXTENDEDKEY`，而字母 / 数字 / 主 Enter 不打（避免扩展键错位） |

> 说明：`unicodeUnits` 负责把"字符"变成 `KEYEVENTF_UNICODE` 要注入的 `wScan` 码元；`isExtendedKey` 负责 VK 路径下扩展键标志。二者完全决定 `keyEvent()` 生成的 `INPUT[]`，因此测它们就等价于测修复的注入正确性。

---

## 4. `tests/test_protocol.cpp` —— `ProtocolTest`（键盘字段往返）

| 用例 | 断言 | 锁定的修复点 |
|------|------|--------------|
| `EncodeKeyEvent` | 编码后再解码，`keyCode` / `pressed` / `modifiers` / `text("A")` 四字段全部相等 | 新增的 `KeyEvent::text` 字段在网络上**不丢、不乱**（控制端发的字符能原样到达被控端） |
| `EncodeKeyEventUnicode` | `text("你好")` 编码解码后完全相等 | 中文 / 多字符文本跨网络往返一致，为 Unicode 注入提供正确数据源 |

> 说明：`KeyEvent::text` 是本次修复新增的协议字段（`src/core/types.h`）。若序列化/反序列化漏掉它，上述用例的 `EXPECT_EQ(decoded.text, event.text)` 会失败，从而拦截"字符到被控端丢失"的回归。

---

## 5. 一键回归检查

要在本地确认键盘修复未被破坏，运行：

```bash
./build/tests/Release/xrk_tests.exe \
    --gtest_filter='InputControlKeyTest.*:ProtocolTest.EncodeKeyEvent*'
```

期望输出 `PASSED`（7 项）。任意一项失败都意味着 Unicode 注入或协议字段往返被破坏，需先修复再合入。
