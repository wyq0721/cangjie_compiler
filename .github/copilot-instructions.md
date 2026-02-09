# 仓颉编译器（Cangjie Compiler）深层记忆 — AI 辅助开发上下文

> 本文档为团队开发仓颉编译器时与 AI 协作的固有知识基底。
> AI 助手在回答任何关于本项目的问题时，应始终以此为前提假设。
> 最后更新：2026-02-09

---

## 1. 项目概览

- **项目名称**：仓颉编程语言编译器（Cangjie Compiler）
- **编程语言**：C++ (98.3%)，辅以 CMake、Python、Shell 构建脚本
- **开源协议**：Apache-2.0 with Runtime Library Exception
- **C++ 标准**：C++11（参见 `.clang-format` 中 `Standard: Cpp11`）
- **核心定位**：面向全场景应用开发的仓颉编程语言的编译器前端 + LLVM 后端修改
- **核心命名空间**：`Cangjie`，所有代码都位于 `namespace Cangjie` 下，子模块有 `Cangjie::AST`、`Cangjie::CHIR`、`Cangjie::CodeGen` 等

---

## 2. 系统架构（编译流水线）

编译器采用**多阶段流水线**架构，由 `CompilerInstance` 类驱动，阶段定义在 `CompileStage` ��举中，按顺序执行：

```
源码 → Lex(词法分析) → Parse(语法分析/AST构建) → ConditionalCompilation(条件编译)
     → ImportPackage(包导入) → MacroExpand(宏展开) → AST_DIFF(增量编译分析)
     → Sema(语义分析/类型检查) → DesugarAfterSema(脱糖) → GenericInstantiation(泛型实例化)
     → OverflowStrategy(溢出策略) → Mangling(符号改名) → CHIR(中间层IR生成与优化)
     → CodeGen(LLVM IR生成) → opt/llc/ld(LLVM后端优化、编译、链接)
```

**关键类**：
- `CompilerInstance`（`include/cangjie/Frontend/CompilerInstance.h`）：编译器主实例，持有全生命周期数据
- `DefaultCompilerInstance`（`include/cangjie/FrontendTool/DefaultCompilerInstance.h`）：默认编译流程实现
- `IncrementalCompilerInstance`：增量编译流程实现
- `CompileStrategy`（`include/cangjie/Frontend/CompileStrategy.h`）：策略模式，区分全量/增量编译
- `CompilerInvocation`：编译器调用参数

---

## 3. 目录结构与模块职责

| 目录 | 职责 | 关键头文件路径 |
|------|------|--------------|
| `src/Lex` | 词法分析，将源码分解为 Token | `include/cangjie/Lex/` |
| `src/Parse` | 语法分析，构建 AST；含 ASTChecker | `include/cangjie/Parse/Parser.h` |
| `src/AST` | AST 节点定义、Walker（遍历器）、Clone、类型验证 | `include/cangjie/AST/` |
| `src/Sema` | 语义分析：类型检查、类型推断、作用域分析、脱糖 | `include/cangjie/Sema/TypeChecker.h` |
| `src/CHIR` | Cangjie High-Level IR：AST→CHIR 翻译与优化 | `include/cangjie/CHIR/` |
| `src/CodeGen` | CHIR→LLVM IR 代码生成 | `src/CodeGen/CGModule.h` |
| `src/Macro` | 宏展开处理 | `include/cangjie/Macro/` |
| `src/Mangle` | 符号名称修饰（Mangling） | `include/cangjie/Mangle/` |
| `src/Modules` | 包管理，模块加载，依赖管理（依赖 FlatBuffers） | `include/cangjie/Modules/PackageManager.h` |
| `src/Driver` | 编译器流程驱动，启动前端并调用后端命令 | `include/cangjie/Driver/` |
| `src/Frontend` | 编译器实例类，组织完整编译流程 | `include/cangjie/Frontend/CompilerInstance.h` |
| `src/FrontendTool` | 面向外部工具的编译器实例 | `include/cangjie/FrontendTool/` |
| `src/Basic` | 基础组件：诊断引擎、源码管理、工具函数 | `include/cangjie/Basic/` |
| `src/Option` | 编译器选项解析与控制 | `include/cangjie/Option/` |
| `src/ConditionalCompilation` | 条件编译处理 | `include/cangjie/ConditionalCompilation/` |
| `src/IncrementalCompilation` | 增量编译 | `include/cangjie/IncrementalCompilation/` |
| `src/MetaTransformation` | 元编程编译器插件 | `include/cangjie/MetaTransformation/` |
| `src/Utils` | 公共工具函数 | `include/cangjie/Utils/` |
| `demangler` | 符号反向解析工具 | `demangler/` |
| `schema` | FlatBuffers Schema 文件 | `schema/` |
| `unittests` | 单元测试 | `unittests/` |

---

## 4. 核心设计模式

### 4.1 AST Walker（访问者模式变体）
- **核心类**：`WalkerT<NodeT>`（`include/cangjie/AST/Walker.h`）
- 接受 `VisitPre` 和 `VisitPost` 回调函数
- 返回值 `VisitAction`：`WALK_CHILDREN`（继续遍历子节点）、`SKIP_CHILDREN`（跳过子节点）、`STOP_NOW`（立即停止）、`KEEP_DECISION`（保持之前决定）
- 使用 `visitedByWalkerID` 防止重复访问
- 类型别名：`Walker = WalkerT<Node>`、`ConstWalker = WalkerT<const Node>`

### 4.2 AST→CHIR 翻译器
- **核心类**：`Translator`（`include/cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h`）
- 采用 `Visit()` 重载模式，针对每种 AST 节点类型有对应的 `Visit` 方法
- 使用 `ASTKind.inc` 宏展开枚举所有 AST 种类

### 4.3 诊断引擎（DiagnosticEngine）
- **核心类**：`DiagnosticEngine`（`include/cangjie/Basic/DiagnosticEngine.h`）
- 采用 **Handler 模式**：`DiagnosticHandler`（抽象基类）→ `CompilerDiagnosticHandler`（编译器）/ `LSPHandler`（LSP）
- `Diagnostic` 类封装完整诊断信息：severity（ERROR/WARNING/NOTE）、位置、消息、高亮、修复建议
- `DiagnosticBuilder` 辅助类：链式调用添加诊断附加信息
- 支持 JSON 格式输出、按类别存储/发射、错误数量限制
- **阶段隔离策略**：如果前一阶段有错误，后续阶段的诊断可能不被发射（`CanBeEmitted`）
- 新旧诊断格式兼容：`ConvertOldDiagToNew`
- Pimpl 模式：`DiagnosticEngineImpl`

### 4.4 编译策略（Strategy Pattern）
- `CompileStrategy` 基类 → `FullCompileStrategy` / `IncrementalCompileStrategy`
- 通过 `CompilerInstance::SetCompileStrategy()` 动态切换

### 4.5 Pimpl 模式（编译防火墙）
- 广泛使用 Pimpl 隔离实现，如 `DiagnosticEngineImpl`、`DefaultCIImpl`、`FullCompileStrategyImpl`
- 降低头文件依赖，加速编译

---

## 5. 编码规范

### 5.1 格式规范（`.clang-format` 摘要）
- **缩进**：4 空格，不使用 Tab
- **行宽限制**：120 字符
- **大括号风格**：Custom — 函数定义后换行���`AfterFunction: true`），其余不换行
- **指针对齐**：左对齐（`int* ptr`）
- **命名空间**：不缩进（`NamespaceIndentation: None`）
- **Include 排序**：开启，LLVM/Clang 头文件优先级 2，第三方（gtest 等）优先级 3，其余优先级 1

### 5.2 命名约定
- **命名空间**：PascalCase（`Cangjie`、`Cangjie::AST`、`Cangjie::CHIR`、`Cangjie::CodeGen`）
- **类名**：PascalCase（`CompilerInstance`、`DiagnosticEngine`、`WalkerT`）
- **方法名**：PascalCase（`PerformParse()`、`HandleDiagnostic()`、`EmitDiagnose()`）
- **成员变量**：camelCase（`srcPkgs`、`invocation`、`diag`、`compileStrategy`）
- **枚举值**：全大写下划线（`PARSE`、`CONDITION_COMPILE`、`DS_ERROR`）
- **常量**：全大写下划线或 inline const 大写开头（`DEFAULT_PACKAGE_NAME`、`MAIN_INVOKE`）
- **宏**：全大写下划线（`CJC_ASSERT`、`CJC_ABORT`、`CANGJIE_CODEGEN_CJNATIVE_BACKEND`）

### 5.3 文件头规范
每个源文件必须包含版权声明和文件说明注释。

### 5.4 头文件保护
使用 `#ifndef` / `#define` 风格（非 `#pragma once`），格式：`CANGJIE_<MODULE>_<FILENAME>_H`

### 5.5 头文件/源文件组织
- **公共头文件**：`include/cangjie/<Module>/XXX.h`
- **源文件**：`src/<Module>/XXX.cpp`
- **模块内部头文件**：直接放在 `src/<Module>/` 下

---

## 6. 关键术语表

| 术语 | 含义 |
|------|------|
| **CHIR** | Cangjie High-Level IR，编译器自定义的中间表示层 |
| **AST** | 抽象语法树，由 Parser 生成 |
| **Sema** | 语义分析（Semantic Analysis）的缩写 |
| **Mangle / Mangling** | 符号名称修饰，将仓颉符号转换为链接器可识别的名称 |
| **Demangler** | Mangle 的反向操作，还原可读符号名 |
| **Desugar** | 脱糖，将语法糖转换为基本语法结构 |
| **cjo** | 仓颉编译产物的包文件格式 |
| **bchir** | 二进制 CHIR 格式 |
| **cjc** | 仓颉编译器可执行文件名 |
| **cjc-frontend** | 仅前端的编译器可执行文件 |
| **FlatBuffers** | 序列化库，用于 cjo 文件和宏实现 |
| **cjnative** | 原生代码编译后端标识 |
| **DiagCategory** | 诊断分类，用于按编译阶段组织错误 |
| **CompileStage** | 编译阶段枚举 |
| **OwnedPtr / Ptr** | 项目自定义的智能指针类型 |
| **MakeOwned** | 创建 OwnedPtr 的工厂函数 |

---

## 7. 构建系统

- **构建工具**：CMake + Python 构建脚本 (`build.py`)
- **核心构建命令**：
  ```shell
  python3 build.py clean             # 清理
  python3 build.py build -t release  # 构建（release/debug/relwithdebinfo）
  python3 build.py install           # 安装到 output/
  python3 build.py test              # 运行单元测试
  ```
- **条件编译宏**：
  - `CANGJIE_CODEGEN_CJNATIVE_BACKEND`：启用原生代码生成后端
  - `RELEASE`：发布模式
  - `CMAKE_ENABLE_ASSERT` / `NDEBUG`：断言控制
- **三方依赖**：LLVM（通过 `llvmPatch.diff` 定制）、FlatBuffers（通过 `flatbufferPatch.diff` 定制）、mingw-w64（Windows）、libboundscheck（安全函数库）

---

## 8. 平台支持

- **当前支持**：Linux x86-64/AArch64、Windows x86-64（交叉编译）、Mac x86/arm64
- **交叉编译目标**：ohos-aarch64
- **限制**：当前不支持 Windows 原生构建，需 Linux 交叉编译

---

## 9. 测试体系

- 单元测试位于 `unittests/` 目录
- 使用 `TestCompilerInstance` 类（继承 `CompilerInstance`）进行编译器功能测试
- 支持直接输入源码字符串进行测试（`TestCompilerInstance::code` 成员）
- 使用 Google Test 框架

---

## 10. 开发注意事项

1. **新增 AST 节点**：需要在 `ASTKind.inc` 中添加定义，同时更新 `Walker.cpp`、`Clone.cpp`、`Translator.h` 等遍历/翻译逻辑
2. **新增编译阶段**：需要在 `CompileStage` 枚举中添加，在 `CompilerInstance::InitCompilerInstance()` 中注册对应的 `Perform*` 方法
3. **诊断消息**：使用 `DiagnosticBuilder` 链式构建，���过 `DiagnosticEngine::DiagnoseRefactor()` 创建新格式诊断
4. **跨模块访问**：头文件在 `include/cangjie/` 下，使用 `#include "cangjie/<Module>/XXX.h"` 形式
5. **代码生成模块**：位于 `src/CodeGen/`，使用 `CGModule` 作为模块上下文，内含 LLVM Module 和相关工具方法
6. **宏/元编程**：`src/Macro/` 处理宏展开，`src/MetaTransformation/` 处理编译器插件式的元编程
7. **增量编译**：通过 AST Hash 进行变更检测（`ASTHasher`），配合 `IncrementalScopeAnalysis` 确定增量编译范围
8. **并行支持**：Parser 支持多线程并行解析多个文件，Mangling 支持分批并行处理
9. **线程安全**：`DiagnosticEngine` 和相关 Handler 使用 `std::mutex` 保护共享状态

---

## 11. 深层记忆维护指南

### 何时更新本文档
- 新增或删除编译阶段（`CompileStage`）
- 新增或重构核心模块目录
- 变更编码规范（`.clang-format` 或命名约定）
- 引入新的核心设计模式或框架
- 更改构建系统或依赖关系
- 新增核心术语

### 如何更新
1. 修改本文件并提交 PR
2. 在 PR 描述中标注 `[deep-memory-update]`
3. 通知团队成员刷新各平台的知识库配置

### 跨平台推广
本文档可直接用于以下平台的知识库/系统提示词：
- **GitHub Copilot**：作为 `.github/copilot-instructions.md`（当前位置）
- **Cursor**：复制到 `.cursor/rules/` 目录
- **Claude Projects**：作为 Project Knowledge 上传
- **ChatGPT**：作为 Custom Instructions 或 GPTs Knowledge
- **其他 IDE AI 插件**：作为项目根目录下的 `AI_CONTEXT.md`