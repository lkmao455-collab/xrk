# XRK 工具使用文档说明

本目录（`tools/`）包含 XRK 的辅助工具：键盘布局诊断、跨布局按键实测，以及 Web 控制台无头冒烟测试。每个工具有独立的详细文档（下表「详细文档」列），本文档为**总览与快速上手**。

## 1. 工具一览

| 工具 | 类型 | 用途 | 详细文档 |
|------|------|------|----------|
| `keyboard_layout_test` | GUI (Qt) | 交互式演示跨键盘布局下字符注入效果，验证「Unicode 注入不乱码」修复 | [keyboard_layout_test.md](keyboard_layout_test.md) |
| `keyboard_layout_probe` | 无头 (C++) | 测量同一虚拟键码在不同布局下翻译成的字符，证明布局无关性 | [keyboard_layout_probe.md](keyboard_layout_probe.md) |
| `web_console_smoke.py` | 无头 (Python) | 验证 Web 控制台数据通道：HTTP 托管 SPA + WS↔Host 桥接 + AUTH 握手 + 帧捕获 | [web_console_smoke.md](web_console_smoke.md) |

## 2. 构建

C++ 工具通过 CMake 构建（与主线同一构建树）：
```bash
cmake --build build --target keyboard_layout_test --config Release
cmake --build build --target keyboard_layout_probe  --config Release
```
产物位于 `build/tools/Release/`：
- `keyboard_layout_test.exe`
- `keyboard_layout_probe.exe`

`web_console_smoke.py` 是脚本，无需编译，但依赖：
- Python 3
- `websocket-client`：`pip install websocket-client`
- 被测 `xrk.exe`（`build/src/Release/xrk.exe`）及 Qt 运行时在 PATH（见 `web_console_smoke.md` 第 1 节）

## 3. 快速使用

### 3.1 跨布局键盘修复验证
- **交互演示**（需桌面环境）：
  ```bash
  ./build/tools/Release/keyboard_layout_test.exe
  ```
  在界面切换输入法/布局，输入字符观察是否按预期注入。详见 [keyboard_layout_test.md](keyboard_layout_test.md)。
- **无头测量**（沙箱/CI 可跑）：
  ```bash
  ./build/tools/Release/keyboard_layout_probe.exe
  ```
  输出同一 VK 在不同布局下的字符映射，证明注入与布局无关。详见 [keyboard_layout_probe.md](keyboard_layout_probe.md)。

### 3.2 Web 控制台冒烟测试
```bash
# 方式 A：Host 与网关已在运行
python3 tools/web_console_smoke.py

# 方式 B：由脚本自动拉起并清理（Qt 运行时需在 PATH）
export PATH="/d/Qt/6.10.0/msvc2022_64/bin:$PATH"
python3 tools/web_console_smoke.py --xrk build/src/Release/xrk.exe --save-frame /tmp/cap.jpg
```
输出 HTTP 托管 / WS AUTH 握手 / 帧捕获三项结果；`--save-frame` 可把捕获到的桌面帧存为 JPEG 供目检。详见 [web_console_smoke.md](web_console_smoke.md)。

## 4. 如何选择工具

- 想**亲眼在 GUI 看**跨布局输入效果 → `keyboard_layout_test`
- 想在无头/CI **量化证明** VK→字符与布局无关 → `keyboard_layout_probe`
- 想验证 **Web 控制台（浏览器 SPA）数据通道**是否通畅、能否抓到桌面帧 → `web_console_smoke.py`
- 想**真实看到远程桌面渲染画面** → 见 `docs/08_desktop_render_checklist.md`（需在真实交互式桌面会话目检，无头环境无法渲染）

## 5. 关联文档

- 键盘单元测试断言：`tests/KEYBOARD_TESTS.md`
- 鼠标单元测试断言：`tests/MOUSE_TESTS.md`
- 测试总索引：`tests/TESTS.md`
- 桌面渲染实测清单：`docs/08_desktop_render_checklist.md`
- Web 控制台部署/集成记录：`WORK_NEXT.md`
