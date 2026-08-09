# 贡献指南

感谢您对 XRK 项目的关注！本文档将帮助您了解如何参与贡献。

## 开发环境

- **操作系统**：Windows 10/11（64位）
- **编译器**：MSVC 2019+ / GCC 9+ / Clang 10+
- **构建系统**：CMake 3.20+
- **Qt 版本**：Qt 6.2+（含 Network / Widgets / Gui）
- **测试框架**：Google Test

## 代码规范

### C++ 风格
- 遵循 C++17 标准
- 使用 `xrk` 命名空间
- 类名使用 PascalCase（如 `ScreenCapture`）
- 方法名使用 camelCase（如 `captureFrame`）
- 成员变量使用 `m_` 前缀（如 `m_scanning`）
- 常量使用全大写 + 下划线（如 `DEFAULT_PORT`）

### Qt 规范
- 使用 Qt 的信号槽机制进行组件通信
- UI 组件继承自 `QWidget`，使用 `Q_OBJECT` 宏
- 在构造函数中调用 `setupUI()` 初始化界面
- 使用 `QVBoxLayout` / `QHBoxLayout` 等进行布局

### 文件组织
- 头文件放在 `src/` 对应目录下
- 实现文件与头文件同名，扩展名为 `.cpp`
- 测试文件放在 `tests/` 目录下，前缀为 `test_`
- 文档放在 `docs/` 目录下

## 分支策略

- `main`：稳定版本，只接受 PR 合并
- `develop`：开发分支，新功能基于此分支开发
- `feature/*`：功能分支，命名格式为 `feature/简短描述`
- `fix/*`：修复分支，命名格式为 `fix/简短描述`

## 提交信息格式

```
类型(范围): 简短描述

详细说明 (可选)
```

### 类型
- **feat**: 新功能
- **fix**: 修复 bug
- **docs**: 文档更新
- **style**: 代码格式调整（不影响逻辑）
- **refactor**: 重构（不新增功能也不修复 bug）
- **test**: 测试相关
- **chore**: 构建/工具相关

### 示例
```
feat(ui): 添加表情选择器组件

- 实现 7 个表情分类
- 支持搜索过滤
- 支持最近使用记录
```

## 测试要求

### 单元测试
- 新功能必须附带单元测试
- 测试文件放在 `tests/` 目录下
- 使用 Google Test 框架
- 测试用例命名清晰，描述测试场景

### 运行测试
```bash
# 构建测试
cmake --build build --config Release --target xrk_tests

# 运行所有测试
export QT_QPA_PLATFORM=offscreen
./build/tests/Release/xrk_tests.exe

# 运行特定测试
./build/tests/Release/xrk_tests.exe --gtest_filter="ClassName.TestName"
```

### 测试覆盖率
- 目标：核心逻辑测试覆盖率 > 80%
- UI 组件测试可适当放宽
- 关键路径（网络、编码、协议）必须有测试

## 提交 PR 流程

1. **Fork 项目**到您的 GitHub 账户
2. **创建功能分支**：`git checkout -b feature/your-feature`
3. **编写代码**并确保通过测试
4. **更新文档**（如需要）
5. **提交代码**：遵循提交信息格式
6. **推送分支**：`git push origin feature/your-feature`
7. **创建 Pull Request**，填写模板中的所有必填项

### PR 检查清单
- [ ] 代码编译通过
- [ ] 所有测试通过
- [ ] 新功能附带测试
- [ ] 文档已更新
- [ ] 代码符合项目规范
- [ ] 没有引入新的编译警告
- [ ] 提交信息清晰描述了变更内容

## Issue 规范

### Bug 报告
- 描述问题的详细信息
- 提供复现步骤
- 附上截图或日志（如有）
- 说明运行环境（OS、Qt 版本等）

### 功能请求
- 描述功能需求
- 说明使用场景
- 提供设计建议（如有）

## 代码审查

所有 PR 需要至少一位维护者审查通过才能合并。审查重点：
- 代码质量和可读性
- 功能正确性
- 测试覆盖
- 性能影响
- 安全性

## 许可证

贡献的代码将按照项目的双授权模式（GPL v3 / 商业授权）发布。

## 联系方式

- GitHub Issues: https://github.com/lkmao455-collab/xrk/issues
- Email: lkmao455-collab@users.noreply.github.com
