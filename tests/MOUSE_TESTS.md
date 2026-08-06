# 鼠标测试断言说明（tests/）

本文档说明鼠标注入相关的 GoogleTest 断言。这些用例锁定 `InputControl` 鼠标注入逻辑的正确性（按键标志映射、绝对坐标归一化、滚轮标志/数据），是 `tests/test_input_control.cpp` 中 `InputControlMouseTest.*` 用例的逐条说明。

涉及文件：
- `tests/test_input_control.cpp` —— `InputControlMouseTest`（鼠标注入逻辑辅助方法）

> 与键盘文档的关系：键盘修复重点是"跨布局不乱码"，对应 `tests/KEYBOARD_TESTS.md`；鼠标无需处理布局/IME，本文只关注"注入标志与坐标是否正确"——这是鼠标事件能否被系统正确识别的关键。

---

## 1. 背景：为什么鼠标也要可断言

`InputControl` 的鼠标方法（`mouseMoveEvent` / `mouseButtonEvent` / `mouseScrollEvent`）最终通过 Windows `SendInput` 把这三类事件投给系统。投错标志位或坐标偏移量，会导致：
- 左键事件被当成中键 / 右键；
- 移动坐标未归一化到 `0..65535`，系统把指针甩到错误位置；
- 滚轮 `dwFlags` / `mouseData` 不对，滚动方向或步长错误。

为让上述逻辑**可单测、可 CI**，我们把"决定 `INPUT[]` 内容"的逻辑抽到一组 `static` 辅助方法（`mouseButtonFlags` / `mouseNormalizedPos` / `mouseScrollFlags` / `mouseScrollData`），并在实现侧用 `buildMouse*Inputs` 静态自由函数组装 `INPUT[]`。测试直接断言这些辅助方法，即等价于断言 `SendInput` 实际收到的内容。

| 被测点 | 锁定它的断言 |
|--------|--------------|
| 按键→`MOUSEEVENTF_*` 标志映射 | `InputControlMouseTest.ButtonFlagsMapCorrectly` |
| 屏幕坐标→绝对坐标归一化（比例/有界） | `InputControlMouseTest.NormalizedPosIsProportionalAndBounded` |
| 滚轮标志与滚动增量透传 | `InputControlMouseTest.ScrollFlagsAndData` |

---

## 2. 构建与运行

```bash
cmake --build build --target xrk_tests --config Release

# 只跑鼠标相关断言
./build/tests/Release/xrk_tests.exe --gtest_filter='InputControlMouseTest.*'
```

- 这些用例为**无头可跑**，已在沙箱内验证 3 项全过。
- 与键盘用例一样，`test_input_control.cpp` 整体被 `#ifdef _WIN32` 包围：非 Windows 平台下整套跳过，因为其断言依赖 Windows 的 `INPUT` / `MOUSEEVENTF_*` 语义。
- `ASSERT_EQ` 失败会立即终止该用例；`EXPECT_EQ` 失败仅记错并继续；两者都会让汇总出现 `FAILED` 且进程返回非零。

---

## 3. `tests/test_input_control.cpp` —— `InputControlMouseTest`

这些用例直接验证 `InputControl` 的鼠标注入辅助方法，即各鼠标方法最终交给 `SendInput` 的 `INPUT[]` 的决定因素。

| 用例 | 断言 | 锁定的行为 |
|------|------|------------|
| `ButtonFlagsMapCorrectly` | `mouseButtonFlags(LEFT, true) == 0x0002 (MOUSEEVENTF_LEFTDOWN)`<br>`mouseButtonFlags(LEFT, false) == 0x0004 (LEFTUP)`<br>`mouseButtonFlags(RIGHT, true) == 0x0008 (RIGHTDOWN)`<br>`mouseButtonFlags(RIGHT, false) == 0x0010 (RIGHTUP)`<br>`mouseButtonFlags(MIDDLE, true) == 0x0020`<br>`mouseButtonFlags(MIDDLE, false) == 0x0040` | 左/右/中键的按下与松开都映射到正确的 `MOUSEEVENTF_*` 标志位。中键使用非左/右的专用码（0x0020/0x0040），避免被误判为左键。 |
| `NormalizedPosIsProportionalAndBounded` | `mouseNormalizedPos(0,0) == (0,0)`<br>且 `(100,100)` 与 `(200,200)` 均 `> 0`、`(200,200) > (100,100)`、约 2 倍关系（`EXPECT_NEAR(..., 1.0)`）、两者 `x/y <= 65535` | 绝对坐标按屏幕分辨率线性归一化到 `0..65535`，保持比例与单调，且不超过 `SendInput` 绝对坐标上限。用例**不依赖 `GetSystemMetrics` 的返回值**，只用比例关系，因此跨分辨率环境均稳定。 |
| `ScrollFlagsAndData` | `mouseScrollFlags() == 0x0800 (MOUSEEVENTF_WHEEL)`<br>`mouseScrollData(120) == 120`<br>`mouseScrollData(-120) == -120` | 滚轮事件使用正确的 `MOUSEEVENTF_WHEEL` 标志，且滚动增量（含正负方向，正负代表上下滚）原样透传，不丢符号、不变量。 |

> 说明：`mouseButtonFlags` 决定 `buildMouseButtonInputs` 里 `mi.dwFlags`；`mouseNormalizedPos` 决定移动事件 `mi.dx/mi.dy`（配合 `MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE`）；`mouseScrollFlags` / `mouseScrollData` 决定滚轮事件 `mi.dwFlags` / `mi.mouseData`。测这四个辅助方法即等价于测鼠标 `SendInput` 实际发出的内容。

---

## 4. 一套辅助方法的测试边界

以下行为**目前未**通过 `InputControlMouseTest` 断言（属于实现侧而非逻辑辅助层，需真实注入/真实屏幕才能验证）：
- 连续点击/`simulateMouseDoubleClick` 的时序（4 次按下/松开序列）——目前通过 `mouseButtonEvent` 调用链间接覆盖，无独立断言。
- 滚轮在 Linux（XTest 按 120 折算步数）与 macOS（CoreGraphics 按行数）的差异实现——属平台分支，Windows 断言无法覆盖，靠各平台手动验证。

如需把它们纳入回归，可新增补充用例（例如给定 `delta` 断言 `buildMouseScrollInputs` 产出的 `INPUT[].mi.mouseData` 符号与量级），并将该静态自由函数提升为带前向声明的可测单元。

---

## 5. 一键回归检查

要在本地确认鼠标注入逻辑未被破坏，运行：

```bash
./build/tests/Release/xrk_tests.exe --gtest_filter='InputControlMouseTest.*'
```

期望输出 `PASSED`（3 项）。任意一项失败都意味着鼠标标志位映射或坐标归一化被破坏，需先修复再合入。

与键盘回归一起跑：

```bash
./build/tests/Release/xrk_tests.exe --gtest_filter='InputControl*'
```

期望 `PASSED`（8 项：5 键盘 + 3 鼠标）。
