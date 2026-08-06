# keyboard_layout_probe 使用文档

无头（headless）跨键盘布局测量工具。在**命令行**直接复现远程控制中"键值乱了"的根因与修复效果，不需要 GUI、不需要鼠标交互、也不需要真实建立远程连接。

它在 Win32 API 层面演示两件事：

- **旧路径（仅 VK）的实测证据**：同一个 Windows 虚拟键码（`VK_A` 等）在**不同键盘布局**下被 `ToUnicodeEx` 翻译成**不同字符** —— 这就是"控制端按 A、被控机出别的字符"的根因。
- **新路径（Unicode）的修复效果**：直接发送字符的 UTF-16 码元，与布局无关，结果恒定正确。

> 与 `keyboard_layout_test.exe`（交互式 GUI 工具）不同，本工具**不实际注入按键**，只测量"VK 在不同布局下会翻译成什么"，因此可在无桌面环境（CI、控制台、远程服务）运行。

---

## 1. 适用环境

- 命令行即可运行，无需桌面会话或鼠标交互。
- 仅 Windows 有效（依赖 `GetKeyboardLayoutList` / `LoadKeyboardLayout` / `ToUnicodeEx`）。
- 若系统内没有安装对比用的外语布局，工具会自动尝试 `LoadKeyboardLayout`（俄语 `00000419`、希腊语 `00000408`）以制造布局差异。

## 2. 构建

工具随主工程一起构建（已接入 `tools/CMakeLists.txt`）：

```bash
cmake -S . -B build
cmake --build build --target keyboard_layout_probe --config Release
```

产物：`build/tools/Release/keyboard_layout_probe.exe`

## 3. 运行

```bash
build\tools\Release\keyboard_layout_probe.exe
```

输出示例：

```
Layouts compared: 3
  [0] Chinese  (HKL=0x0000000008040804)
  [1] Russian  (HKL=0x0000000004190419)
  [2] Greek    (HKL=0x0000000004080408)

--- OLD path: 同一 VK，在不同布局下被翻译成不同字符（这就是乱码根因）---
  Chinese: VK_A->'a' VK_B->'b' VK_C->'c' VK_1->'1'
  Russian: VK_A->'?' VK_B->'?' VK_C->'?' VK_1->'1'
  Greek:   VK_A->'?' VK_B->'?' VK_C->'?' VK_1->'1'

--- NEW path: 固定 Unicode 码元，与布局无关（修复）---
  直接发 U+0041 U+0042 U+0043 U+0031 ("ABC1")，host 用 KEYEVENTF_UNICODE 注入，
  无论上面哪个布局激活，结果永远是 "ABC1"。

VERDICT: VK_A/B/C/1 在两种布局下产生 DIFFERENT（乱码）字符。
  => OLD(VK) 路径会乱码；NEW(Unicode) 路径始终正确。
```

## 4. 输出解读

- **布局列表**：列出用于对比的所有布局（已安装的 + 自动尝试加载的俄语/希腊语），并显示语言名与 `HKL`。
- **OLD path 段落**：对 `VK_A / VK_B / VK_C / VK_1`，分别用每个布局的 `ToUnicodeEx` 解析出其对应字符。只要不同布局下字符不同（如 `a` vs `?`），即证明"同一 VK 在布局不一致时会被翻译成不同字符"——即乱码根因。
- **NEW path 段落**：说明修复后发送的是固定 UTF-16 码元（`ABC1`），不论哪个布局激活，注入结果恒为 `ABC1`。
- **VERDICT**：程序自动比较两套布局下的翻译结果，给出 `DIFFERENT（scrambled）` 或 `the same` 的结论。

> 注：若沙箱/机器只装了中文输入法资源，临时加载的俄/希布局无法完整翻译，会显示 `?` 而非真实西里尔/希腊字母；这不影响结论——"同一 VK 跨布局结果不同"已经成立。在**真实装了俄语布局**的机器上，这里会直接显示 `ф` / `α` 等真实乱码字符。

## 5. 与 keyboard_layout_test 的区别

| 维度 | keyboard_layout_probe（本工具） | keyboard_layout_test（GUI 工具） |
|------|--------------------------------|----------------------------------|
| 形态 | 命令行程序 | 带窗口的 GUI 程序 |
| 是否实际注入按键 | 否（只测量 VK→字符的翻译） | 是（`SendInput` 真实注入文本框） |
| 运行环境 | 无头即可（CI/控制台/服务） | 必须真实桌面会话 |
| 交互 | 无 | 需鼠标点按钮 |
| 用途 | 快速验证根因（同一 VK 跨布局结果不同） | 端到端演示旧/新路径的落点差异 |

## 6. 错误处理与边界情况

本工具是"测量/演示"程序，**不会因异常而中止**，也**不通过退出码判断成败**（始终返回 `0`）。所有可能的问题都通过 stdout 文本体现，便于在脚本/CI 中直接阅读。下面是各边界情况的实际处理：

### 6.1 没有可用布局（无法对比）

- 触发：`GetKeyboardLayoutList` 返回 `0`，且 `LoadKeyboardLayout("00000419"/"00000408")` 也都失败（如系统缺失对应键盘 DLL，俄语 `kbdru.dll` / 希腊语 `kbdgre.dll` 不存在）。
- 表现：打印 `Layouts compared: 0`，随后没有任何对比行，也不输出 `VERDICT`（该段落被 `layouts.size() >= 2` 守卫跳过）。
- 含义：当前机器无法构造"布局不一致"，演示无法进行。需在系统"设置 → 语言 → 键盘"中**安装并启用至少一种与当前不同的布局**后重跑。

### 6.2 只有一种布局 / 加载外语布局失败

- 触发：系统仅安装了一种布局，且临时加载俄/希布局返回 `NULL`。
- 表现：只列出那一种布局并翻译，但 `VERDICT` 段被跳过（需要 `>= 2` 种才能比较），不会给出"乱码/正确"结论。
- 含义：不是工具出错，而是缺乏对照样本。安装第二种布局即可。

### 6.3 `ToUnicodeEx` 返回空（显示 `□`）

- 触发：某个 `VK` 在某布局下 `ToUnicodeEx` 返回 `0`（无映射）或 `< 0`（死键，dead key）。
- 表现：`vkToChar` 对该键返回占位符 `□`（U+25A1）。
- 含义：该物理键在此布局下不产生可见字符（例如死键、或组合键）。属正常现象，不代表工具故障；对照结论只看"不同布局间字符是否不同"，个别 `□` 不影响整体判断。

### 6.4 `ToUnicodeEx` 返回 `?`（显示 `?`）

- 触发：布局 DLL 仅部分可用（沙箱/服务器常出现），`ToUnicodeEx` 把该键显式映射为 `?`（未定义键）。
- 表现：对应位置打印 `?`，如本机实测里俄语/希腊语下的 `VK_A->'?'`。
- 含义：不是乱码字符本身，而是"该布局在此环境下未能完整解析"。这**仍然证明了结论**——同一 `VK_A` 在中文布局下是 `a`、在俄/希下是 `?`，即"跨布局结果不同"。在**真实装好俄语布局**的机器上，这里会直接显示 `ф` / `α` 之类的真实西里尔/希腊字母。

### 6.5 布局激活失败（被忽略）

- `ActivateKeyboardLayout` 的返回值未被检查（best-effort）。即使激活失败，每次探测都**显式把 `hkl` 传给 `ToUnicodeEx`**，因此翻译结果始终按对应布局精确计算，不受激活成败影响。

### 6.6 语言名显示

- 语言名由 `HKL` 低 16 位（LANGID）查小表得到（支持 English/Chinese/Russian/Greek/German/French/Japanese）。若遇到表中未列的语言，显示为 `lang#<id>`，仅为标识，不影响测量。

### 6.7 退出码

- 始终为 `0`。该工具定位是"演示根因与修复"，不是 pass/fail 测试门槛；如需在 CI 中做断言，请改用 `tests/` 下的 gtest（`InputControlKeyTest.*`、`ProtocolTest.EncodeKeyEvent*`），它们会返回真实的测试成败与非零退出码。

## 7. 相关代码

- 注入修复逻辑：`src/hw/input_control.cpp` 的 `InputControl::keyEvent()`（有字符走 `KEYEVENTF_UNICODE`，否则走 `wVk` 并补 `KEYEVENTF_EXTENDEDKEY`）
- 控制端发送字符：`src/ui/remote_desktop_widget.cpp` 的 `keyPressEvent` / `sendKeyEventToRemote`（发送 `QKeyEvent::text()`）
- 协议字段：`src/core/types.h` 的 `KeyEvent::text`，`src/core/protocol_manager.cpp` 的 `encodeKeyEvent` / `decodeKeyEvent`

## 8. 配套 gtest 断言说明（tests/ 下）

本工具只做"演示"，不做 pass/fail 判定（见 §6.7）。若要在 CI 或命令行获得**可断言的成败结果**（非零退出码），请运行 `tests/` 下的 gtest。键盘修复相关的逐条断言（集中在 `test_input_control.cpp` 的 `InputControlKeyTest.*` 与 `test_protocol.cpp` 的 `ProtocolTest.EncodeKeyEvent*`）已整理到独立文档 **`tests/KEYBOARD_TESTS.md`**（含构建命令、断言表、与修复点的对应关系），是这部分断言的权威出处。

一键运行：

```bash
cmake --build build --target xrk_tests --config Release
./build/tests/Release/xrk_tests.exe \
    --gtest_filter='InputControlKeyTest.*:ProtocolTest.EncodeKeyEvent*'
```

> 非 Windows 平台下 `test_input_control.cpp` 整体被 `#ifdef _WIN32` 跳过（其断言依赖 Windows 的 `INPUT` / `KEYEVENTF_UNICODE` 语义）；`ProtocolTest.EncodeKeyEvent*` 跨平台均生效。

