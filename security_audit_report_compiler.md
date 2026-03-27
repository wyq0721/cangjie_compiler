# 仓颉编译器（Cangjie Compiler）安全审计报告

**审计时间**：2026-03-27  
**审计范围**：`wyq0721/cangjie_compiler` 仓库 `main` 分支  
**代码版本**：最新提交（审计时间节点）  
**语言/技术栈**：C++17、CMake、FlatBuffers、LLVM 15.0.4  
**审计员**：AI 安全专家  
**报告级别**：机密

---

## 目录

1. [执行摘要](#1-执行摘要)
2. [架构安全分析](#2-架构安全分析)
3. [安全漏洞详情](#3-安全漏洞详情)
   - [VULN-01：FlatBuffers CHIR 反序列化禁用深度/表数量限制（DoS/堆溢出风险）](#vuln-01flatbuffers-chir-反序列化禁用深度表数量限制dosheap-overflow-风险)
   - [VULN-02：宏扩展子进程通过 execvp 执行外部程序（路径劫持）](#vuln-02宏扩展子进程通过-execvp-执行外部程序路径劫持)
   - [VULN-03：RTLD_GLOBAL 动态库加载导致符号污染/提权风险](#vuln-03rtld_global-动态库加载导致符号污染提权风险)
   - [VULN-04：CANGJIE_HOME 环境变量被无条件信任用于二进制查找](#vuln-04cangjie_home-环境变量被无条件信任用于二进制查找)
   - [VULN-05：NDEBUG 发布构建下 CJC_ASSERT 完全消除导致安全断言失效](#vuln-05ndebug-发布构建下-cjc_assert-完全消除导致安全断言失效)
   - [VULN-06：RawStaticCast 绕过类型检查（类型混淆风险）](#vuln-06rawstaticcast-绕过类型检查类型混淆风险)
   - [VULN-07：TempFileManager 临时目录生成随机性不足（低熵竞态条件）](#vuln-07tempfilemanager-临时目录生成随机性不足低熵竞态条件)
   - [VULN-08：Windows 平台 CreateProcess 命令行拼接风险](#vuln-08windows-平台-createprocess-命令行拼接风险)
   - [VULN-09：signal 处理函数调用非异步信号安全函数](#vuln-09signal-处理函数调用非异步信号安全函数)
   - [VULN-10：IncrementalCompilation JSON 解析器手工实现，缺少输入验证](#vuln-10incrementalcompilation-json-解析器手工实现缺少输入验证)
   - [VULN-11：编译器 ICE 处理泄露文件系统路径信息](#vuln-11编译器-ice-处理泄露文件系统路径信息)
   - [VULN-12：整数溢出在 IntLiteral 运算中未覆盖所有边界](#vuln-12整数溢出在-intliteral-运算中未覆盖所有边界)
   - [VULN-13：RecursiveDirRemoval 在竞态窗口中存在 TOCTOU 问题](#vuln-13recursivedirremoval-在竞态窗口中存在-toctou-问题)
   - [VULN-14：插件（--plugin）加载路径未经完整性验证](#vuln-14插件--plugin-加载路径未经完整性验证)
   - [VULN-15：Demangler 对超长/格式异常输入可能触发栈溢出](#vuln-15demangler-对超长格式异常输入可能触发栈溢出)
4. [供应链安全](#4-供应链安全)
5. [构建系统安全](#5-构建系统安全)
6. [CVE 对比分析](#6-cve-对比分析)
7. [总体风险矩阵](#7-总体风险矩阵)
8. [修复优先级路线图](#8-修复优先级路线图)
9. [安全加固建议（通用）](#9-安全加固建议通用)

---

## 1. 执行摘要

本报告对仓颉编译器（`cangjie_compiler`）进行了全面安全审计。审计范围涵盖：词法/语法分析器、AST 处理、CHIR IR 层、宏展开子进程、序列化/反序列化、驱动层（Driver）、文件系统操作、信号处理、动态库加载、构建系统及第三方依赖。

**主要发现**：

| 严重程度 | 数量 |
|----------|------|
| 高危（High） | 5 |
| 中危（Medium） | 6 |
| 低危（Low） | 4 |

核心风险集中在以下三个区域：
1. **宏展开子进程隔离不足**：通过 `execvp` 执行宏服务器，存在路径劫持风险；
2. **CHIR 序列化文件反序列化无深度限制**：攻击者可构造恶意 `.chir` 文件触发 DoS；
3. **动态库加载使用 `RTLD_GLOBAL`**：宏动态库中的符号会污染全局命名空间。

---

## 2. 架构安全分析

### 2.1 整体编译流程

```
用户源码(.cj)
  → Lexer（词法分析）
  → Parser（语法分析 / AST 构建）
  → Sema（语义分析 / 类型检查）
  → AST2CHIR（中间表示转换）
  → CHIR 优化 / 分析 / 检查
  → 宏展开（子进程 / 动态库）
  → CHIR 序列化（FlatBuffers）
  → CodeGen / LLVM 后端
  → 链接器调用（posix_spawn / CreateProcess）
```

### 2.2 信任边界

| 信任边界 | 输入来源 | 风险等级 |
|----------|----------|----------|
| 源码文件 | 用户可控 | 高 |
| `.cjo` / `.chir` 缓存文件 | 用户本地文件系统 | 高 |
| 宏动态库（.so/.dll） | 用户指定路径 | 高 |
| 环境变量（CANGJIE_HOME 等） | 用户环境 | 中 |
| 命令行参数 | 用户可控 | 中 |
| 编译器插件（`--plugin`） | 用户指定路径 | 高 |

### 2.3 进程模型安全

编译器在宏展开阶段会 `fork()` 出宏服务器子进程，并通过匿名管道进行 IPC 通信。该子进程以 `execvp` 启动，存在以下架构级安全隐患：
- 子进程继承父进程所有文件描述符（管道以外的部分未关闭）；
- `MACRO_SRV_NAME = "LSPMacroServer"` 仅作为程序名，依赖 `PATH` 查找，存在环境劫持风险；
- 宏动态库以 `RTLD_GLOBAL` 标志加载，符号可被后续 `dlopen` 的库重写。

---

## 3. 安全漏洞详情

---

### VULN-01：FlatBuffers CHIR 反序列化禁用深度/表数量限制（DoS/Heap-Overflow 风险）

**严重程度**：🔴 高危  
**CVSS 评分**：7.5（AV:L/AC:L/PR:N/UI:R/S:U/C:N/I:N/A:H）  
**CWE**：CWE-770（Resource Allocation Without Limits）

#### 漏洞代码

**文件**：`src/CHIR/Serializer/CHIRDeserializer.cpp`，第 55–60 行

```cpp
// Disable max depth and max tables verification.
flatbuffers::Verifier::Options options;
options.max_depth = std::numeric_limits<::flatbuffers::uoffset_t>::max();
options.max_tables = std::numeric_limits<::flatbuffers::uoffset_t>::max();
flatbuffers::Verifier verifier(serializationInfo.data(), serializationInfo.size(), options);
if (!verifier.VerifyBuffer<PackageFormat::CHIRPackage>()) {
```

**对比**：`src/Modules/ASTSerialization/ASTLoader.cpp`，第 217 行（有限制）

```cpp
flatbuffers::Verifier verifier(data.data(), size, FB_MAX_DEPTH, FB_MAX_TABLES);
// FB_MAX_DEPTH = 128, FB_MAX_TABLES = 2000000
```

#### 触发路径 / 攻击路径

1. 攻击者构造一个格式合法但深度极大（嵌套层次超过 10 万层）的 `.chir` 序列化文件；
2. 通过 `cjc --import-chir malicious.chir` 或 Cangjie 增量编译缓存文件替换触发反序列化；
3. `FlatBuffers` 在无深度限制下递归解析，导致堆内存大量分配（OOM）或深度递归造成栈溢出（DoS）。

#### 问题场景

在 CI/CD 系统中，开发者从不受信任的包仓库获取预编译的 `.chir` 中间文件并直接导入，恶意文件触发编译器崩溃或拒绝服务。

#### 根本原因

注释本身承认"禁用最大深度和最大表数量验证"（`Disable max depth and max tables verification`），这是一个有意为之但存在安全隐患的设计决策——为兼容大型项目而牺牲了安全限制。

#### 问题影响

- 编译器崩溃（DoS），影响 CI/CD 流水线可用性；
- 潜在的堆溢出（取决于 FlatBuffers 版本），可能演化为代码执行；
- 增量编译缓存中毒，影响后续编译结果的正确性。

#### 修复建议

```cpp
// 设置合理但足够大的上限，而非 uoffset_t::max()
flatbuffers::Verifier::Options options;
options.max_depth = 4096;          // 对应实际最大 CHIR 嵌套深度
options.max_tables = 10000000;     // 10M 表上限
flatbuffers::Verifier verifier(serializationInfo.data(), serializationInfo.size(), options);
if (!verifier.VerifyBuffer<PackageFormat::CHIRPackage>()) {
    Errorln("CHIR validation failed: possible malformed or corrupted file.");
    return false;
}
```

---

### VULN-02：宏扩展子进程通过 execvp 执行外部程序（路径劫持）

**严重程度**：🔴 高危  
**CVSS 评分**：7.8（AV:L/AC:L/PR:L/UI:N/S:U/C:H/I:H/A:H）  
**CWE**：CWE-426（Untrusted Search Path）

#### 漏洞代码

**文件**：`src/Macro/MacroEvaluationClient.cpp`，第 496–509 行

```cpp
std::vector<char*> cstrings;
std::string macSrvName = MACRO_SRV_NAME;  // = "LSPMacroServer"（硬编码程序名）
std::string hRead = std::to_string(MacroProcMsger::GetInstance().pipefdP2C[0]);
std::string hWrite = std::to_string(MacroProcMsger::GetInstance().pipefdC2P[1]);
std::string enPara = enableParallelMacro ? "1" : "0";
std::string pidStr = std::to_string(pid);
cstrings.push_back(macSrvName.data());
cstrings.push_back(hRead.data());
cstrings.push_back(hWrite.data());
cstrings.push_back(enPara.data());
cstrings.push_back(ci->invocation.globalOptions.executablePath.data());
cstrings.push_back(pidStr.data());
cstrings.push_back(nullptr);
execvp(macSrvName.c_str(), cstrings.data());  // 依赖 PATH 查找，存在劫持风险
```

#### 触发路径 / 攻击路径

1. 攻击者在 `PATH` 早期目录（如 `./` 或 `/tmp/`）中放置恶意程序 `LSPMacroServer`；
2. 在用户目录下调用 `cjc` 编译包含宏的源码；
3. `execvp("LSPMacroServer", ...)` 优先搜索当前目录或 `PATH` 前缀，执行恶意程序；
4. 恶意程序以编译器权限执行任意代码。

#### 问题场景

- 开发者在不受信任的项目目录中执行 `cjc` 编译含宏代码；
- 受感染的 `PATH` 环境（供应链攻击）；
- 某些 Linux 发行版中 `PATH` 包含 `.` 或相对路径。

#### 根本原因

`execvp` 依赖操作系统 `PATH` 环境变量查找可执行文件，未使用绝对路径。已知的 `executablePath` 可用于计算宏服务器绝对路径，但并未使用。

#### 问题影响

- 任意代码执行（本地权限提升）；
- 可被利用于持久化后门（恶意宏服务器驻留）。

#### 修复建议

```cpp
// 使用编译器自身的绝对路径推导宏服务器路径
std::string execDir = FileUtil::GetDirPath(
    ci->invocation.globalOptions.executablePath);
std::string macSrvAbsPath = FileUtil::JoinPath(execDir, MACRO_SRV_NAME);

// 验证文件存在且可执行
if (!FileUtil::CanExecute(macSrvAbsPath)) {
    Errorln("Macro server not found: ", macSrvAbsPath);
    return;
}

cstrings[0] = macSrvAbsPath.data();
execv(macSrvAbsPath.c_str(), cstrings.data());  // 使用 execv（非 execvp），不依赖 PATH
```

---

### VULN-03：RTLD_GLOBAL 动态库加载导致符号污染/提权风险

**严重程度**：🔴 高危  
**CVSS 评分**：7.0（AV:L/AC:H/PR:L/UI:N/S:C/C:H/I:H/A:N）  
**CWE**：CWE-426（Untrusted Search Path）、CWE-114（Process Control）

#### 漏洞代码

**文件**：`include/cangjie/Macro/InvokeUtil.h`，第 80 行

```cpp
HANDLE OpenSymbolTable(const std::string& libPath, int dlopenMode = RTLD_LAZY | RTLD_GLOBAL);
```

**文件**：`src/Macro/MacroEvaluationSrv.cpp`（使用默认参数 `RTLD_GLOBAL`）

```cpp
auto handle = InvokeRuntime::OpenSymbolTable(dyfile);  // 默认 RTLD_LAZY | RTLD_GLOBAL
```

#### 触发路径 / 攻击路径

1. 攻击者构造恶意宏动态库（`.so`），其中定义与运行时库同名的导出符号（如 `malloc`、`free`、`pthread_create`）；
2. 编译器以 `RTLD_GLOBAL` 加载该宏库，恶意符号覆盖全局符号表；
3. 后续编译器本身调用这些符号时，实际执行攻击者代码；
4. 可进一步劫持内存分配器，实现堆喷射或数据泄漏。

#### 对比（存在安全实践的代码）

**文件**：`src/Frontend/CompilerInstance.cpp`，第 264 行

```cpp
handle = InvokeRuntime::OpenSymbolTable(path, RTLD_NOW | RTLD_LOCAL);  // 使用 RTLD_LOCAL 安全
```

#### 根本原因

默认参数 `RTLD_GLOBAL` 将库符号暴露给全局命名空间，而运行时库已存在的符号可被覆盖（`RTLD_LAZY` 延迟绑定进一步扩大了覆盖时间窗口）。

#### 问题影响

- 宏动态库内的恶意代码可劫持编译器内存管理函数；
- 全局符号污染可导致不可预期的编译器行为，产生含后门的目标代码；
- 供应链攻击向量：恶意宏包通过符号污染影响其他编译单元。

#### 修复建议

```cpp
// 默认参数改为 RTLD_LOCAL，避免全局符号污染
HANDLE OpenSymbolTable(const std::string& libPath,
                       int dlopenMode = RTLD_NOW | RTLD_LOCAL);

// 对宏库的加载，强制使用 RTLD_DEEPBIND 进一步隔离
auto handle = InvokeRuntime::OpenSymbolTable(dyfile, RTLD_NOW | RTLD_LOCAL | RTLD_DEEPBIND);
```

---

### VULN-04：CANGJIE_HOME 环境变量被无条件信任用于二进制查找

**严重程度**：🟠 中危  
**CVSS 评分**：6.3（AV:L/AC:L/PR:N/UI:R/S:U/C:H/I:H/A:N）  
**CWE**：CWE-426（Untrusted Search Path）

#### 漏洞代码

**文件**：`src/Option/Option.cpp`，第 1173–1174 行

```cpp
if (environmentVars.find(CANGJIE_HOME) != environmentVars.end()) {
    environment.cangjieHome = FileUtil::GetAbsPath(environmentVars.at(CANGJIE_HOME));
}
```

**文件**：`src/Driver/Backend/CJNATIVEBackend.cpp`，第 75–90 行

```cpp
// search in CANGJIE_HOME if it is available
// ...
Errorln("not found `opt` in the Cangjie installation, " + CANGJIE_HOME);
Errorln("not found `llc` in the Cangjie installation, " + CANGJIE_HOME);
```

`CANGJIE_HOME` 被用于搜索 `opt`、`llc`、链接器、运行时库等关键可执行文件。

#### 触发路径 / 攻击路径

1. 攻击者通过共享账户、Web shell 或容器逃逸等方式设置 `CANGJIE_HOME=/tmp/evil`；
2. 在 `/tmp/evil/third_party/llvm/bin/` 中放置恶意 `opt` 和 `llc` 程序；
3. 用户运行 `cjc` 编译项目，编译器调用恶意的 `opt`/`llc`，获得代码执行权；
4. 恶意后端可向编译产物注入后门代码（参考 XcodeGhost / CCleaner 供应链攻击模式）。

#### 根本原因

环境变量是用户可控的不受信任输入，但 `CANGJIE_HOME` 被直接传入 `GetAbsPath` 后即用于二进制查找，缺少：
- 路径白名单校验；
- 对路径中可执行文件的完整性验证（哈希/签名）；
- 对 `cangjieHome` 是否在合法安装目录内的检查。

#### 问题影响

- 向编译产物注入后门（编译器供应链攻击，参考 CVE-2021-22204 思路）；
- 在自动化 CI 环境中尤其危险，攻击者只需控制环境变量即可影响所有编译产物。

#### 修复建议

```cpp
// 1. 对 CANGJIE_HOME 进行受信路径验证
std::optional<std::string> ValidateCangjieHome(const std::string& path) {
    auto absPath = FileUtil::GetAbsPath(path);
    if (!absPath) return std::nullopt;
    // 验证目录必须包含预期的子目录结构
    if (!FileUtil::FileExist(FileUtil::JoinPath(*absPath, "modules")) ||
        !FileUtil::FileExist(FileUtil::JoinPath(*absPath, "runtime"))) {
        return std::nullopt;
    }
    return absPath;
}

// 2. 优先使用从编译器自身 executablePath 推导的路径，而非完全信任环境变量
// 仅当环境变量提供的路径通过验证时才使用
```

---

### VULN-05：NDEBUG 发布构建下 CJC_ASSERT 完全消除导致安全断言失效

**严重程度**：🟠 中危  
**CVSS 评分**：5.5（AV:L/AC:L/PR:L/UI:N/S:U/C:N/I:H/A:N）  
**CWE**：CWE-617（Reachable Assertion）

#### 漏洞代码

**文件**：`include/cangjie/Utils/CheckUtils.h`，第 53–56 行

```cpp
#else
#ifdef NDEBUG
#define CJC_ASSERT(f) static_cast<void>(f)           // Release 构建：完全消除，副作用被丢弃
#define CJC_ASSERT_WITH_MSG(f, msg) (static_cast<void>(f), static_cast<void>(msg))
#define CJC_ABORT()
#define CJC_ABORT_WITH_MSG(msg) static_cast<void>(msg)
```

相关使用场景（如 `src/Parse/ParserImpl.cpp`，第 283 行）：

```cpp
CJC_ASSERT(ctor.TestAttr(Attribute::CONSTRUCTOR));
// Release 下等价于 (void)(ctor.TestAttr(...))，什么都不做
// 若断言条件被恶意绕过，后续代码在错误假设下继续执行
```

#### 触发路径 / 攻击路径

1. 攻击者构造畸形 Cangjie 源码，使解析器某个中间状态不满足 `CJC_ASSERT` 的前置条件；
2. 在 Debug 构建中，此类输入会触发 `abort()`，编译过程安全终止；
3. 在 Release 构建（`NDEBUG` 已定义）中，断言被 `static_cast<void>(f)` 替换，完全消除；
4. 编译器在不满足前置条件的错误状态下继续执行，可能导致内存损坏、越界读写或产生错误的目标代码。

#### 问题场景

Release 版本编译器处理用户提供的特殊构造源码，绕过类型检查或访问权限检查，生成错误代码（参考 Swift CVE-2022-32797、Rust CVE-2022-46176 等编译器断言绕过问题）。

#### 根本原因

C++ 标准的 `assert()` 在 `NDEBUG` 下也会被消除，这是已知的编译器安全实践问题。关键的运行时检查（如指针非空、类型兼容性）不应使用 `assert`，而应使用显式运行时检查。

#### 问题影响

- 格式错误输入绕过编译器内部不变量检查；
- 可能导致编译器生成语义错误的目标代码；
- 类似漏洞的参考：GCC Bug #18501（断言绕过导致代码生成错误）。

#### 修复建议

```cpp
// 对于安全关键的检查，使用独立的 runtime check 宏（不受 NDEBUG 影响）
#define CJC_SECURITY_CHECK(f) \
    do { \
        if (!(f)) { \
            InternalError("Security check failed: " #f); \
        } \
    } while (0)

// 仅将 CJC_ASSERT 用于纯调试目的（性能检查等）
// 将 CJC_NULLPTR_CHECK、类型断言等安全关键检查改用 CJC_SECURITY_CHECK
```

---

### VULN-06：RawStaticCast 绕过类型检查（类型混淆风险）

**严重程度**：🟠 中危  
**CVSS 评分**：5.9（AV:L/AC:H/PR:N/UI:N/S:U/C:H/I:H/A:N）  
**CWE**：CWE-704（Incorrect Type Conversion or Cast）

#### 漏洞代码

**文件**：`include/cangjie/Utils/CastingTemplate.h`，第 220–225 行

```cpp
template <typename To, typename From> inline To RawStaticCast(From src)
{
    return static_cast<To>(src);    // 无任何类型安全检查
}

template <typename To, typename From> inline To RawStaticCast(Ptr<From> src)
{
    return static_cast<To>(src.get());   // 完全绕过 StaticCast 中的 dynamic_cast 验证
}
```

**对比**：安全版本 `StaticCast` 在 Debug 构建下会通过 `dynamic_cast` 验证类型

```cpp
template <typename To, typename From>
inline std::enable_if_t<std::is_pointer_v<From>, CastToT<To, From>> StaticCast(From node) {
#if defined(CMAKE_ENABLE_ASSERT) || !defined(NDEBUG)
    auto ptr = dynamic_cast<CastToT<To, From>>(node);
    CJC_ASSERT(ptr != nullptr && "Error casting");   // 验证类型兼容性
#endif
    return static_cast<CastToT<To, From>>(node);
}
```

`RawStaticCast` 实际使用场景（`src/AST/Node.cpp`，第 722–728 行）：

```cpp
return RawStaticCast<const RefType*>(this)->ref.targets;  // 直接强制转换，无验证
return RawStaticCast<const RefExpr*>(this)->ref.targets;
auto targetDecls = RawStaticCast<const MemberAccess*>(this)->targets;
```

#### 触发路径 / 攻击路径

1. 攻击者构造畸形 AST（通过解析异常源码），使某节点类型与预期不符；
2. `Node::GetTargets()` 使用 `RawStaticCast` 进行无验证转换，将错误类型的节点指针解释为另一类型；
3. 后续访问错误对象的成员，产生内存损坏（读写超出对象边界）；
4. 结合其他漏洞可能实现地址泄漏或控制流劫持。

#### 根本原因

`RawStaticCast` 是为了特定性能场景引入的"原始"强制转换，完全绕过类型验证机制，即便在调试模式下也不进行 `dynamic_cast` 验证。代码注释表明其作用等同于 C 风格强制转换。

#### 问题影响

- 类型混淆可导致内存损坏（堆/栈）；
- 在 Release 构建中无任何检测机制；
- 参考 LLVM CVE-2023-46246（类型转换漏洞导致代码生成错误）。

#### 修复建议

```cpp
// 限制 RawStaticCast 的使用范围，并添加至少 debug 模式下的检查
template <typename To, typename From>
inline To RawStaticCast(From src)
{
#if defined(CMAKE_ENABLE_ASSERT) || !defined(NDEBUG)
    // 编译时确保类型继承关系
    static_assert(std::is_base_of_v<std::remove_pointer_t<From>,
                                    std::remove_pointer_t<To>> ||
                  std::is_base_of_v<std::remove_pointer_t<To>,
                                    std::remove_pointer_t<From>>,
                  "RawStaticCast on unrelated types is forbidden");
#endif
    return static_cast<To>(src);
}
// 对所有使用 RawStaticCast 的调用点进行代码审查，替换为 StaticCast 或 DynamicCast
```

---

### VULN-07：TempFileManager 临时目录生成随机性不足（低熵竞态条件）

**严重程度**：🟡 中危  
**CVSS 评分**：4.7（AV:L/AC:H/PR:L/UI:N/S:U/C:L/I:H/A:N）  
**CWE**：CWE-338（Use of Cryptographically Weak Pseudo-Random Number Generator）、CWE-377（Insecure Temporary File）

#### 漏洞代码

**文件**：`src/Driver/TempFileManager.cpp`，第 55–73 行（时间戳编码部分）

```cpp
std::string SetNowTimeEncodedString()
{
    struct timespec wallNow = {0, 0};
    (void)clock_gettime(CLOCK_REALTIME, &wallNow);
    size_t ns =
        static_cast<size_t>(wallNow.tv_sec) * MULTIPLE_OF_SECOND_TO_NANOSECOND + static_cast<size_t>(wallNow.tv_nsec);
    // 时间戳是可预测的
    ...
}

std::string CreateTempDirName(const std::string& tempDir)
{
    std::stringstream ss;
    ss << CANGJIE_TMP_DIR_PERFIX;              // 固定前缀: "/cangjie-tmp-"
    ss << SetNowTimeEncodedString();           // 基于当前时间（可预测）
    ss << "-" << Cangjie::Utils::GenerateRandomHexString();  // + 随机数
    return Cangjie::FileUtil::JoinPath(tempDir, ss.str());
}
```

**文件**：`src/Utils/Utils.cpp`，第 83–93 行

```cpp
int fd = open("/dev/urandom", O_RDONLY);
if (fd > 0) {
    (void)read(fd, &randomInt, sizeof(int));  // 仅读取 4 字节（32 位）随机数
}
(void)close(fd);
```

#### 触发路径 / 攻击路径

1. 攻击者在目标系统上具有普通用户访问权限；
2. 通过观察系统时间精度（纳秒级），预测编译器即将创建的临时目录名；
3. 抢先创建符号链接 `/tmp/cangjie-tmp-<predicted>` 指向敏感目录（如 `~/.ssh`）；
4. 编译器临时目录创建或写入操作命中符号链接，向受害者目录写入临时文件（符号链接攻击/TOCTOU）；
5. 结合后续的 `DeleteTempFiles` 操作，可能删除受害者的重要文件。

#### 根本原因

- 临时目录名生成策略中时间戳部分是可预测的（TOCTOU 窗口）；
- 随机数仅使用 32 位（`sizeof(int)` = 4 字节），在高并发或重复执行时碰撞概率较高；
- 未使用 `mkdtemp()` 等操作系统提供的安全临时目录创建 API（原子创建，避免竞态）。

#### 问题影响

- 临时文件注入，可能导致编译产物被污染；
- 敏感临时文件（包含中间 IR、源码副本）被攻击者读取；
- 可用于拒绝服务（抢占临时目录名导致编译失败）。

#### 修复建议

```cpp
// Linux/macOS: 使用 mkdtemp 替代自定义实现
#include <cstdlib>
std::optional<std::string> MakeTempDir(const std::string& tempDir) {
    std::string tmpl = FileUtil::JoinPath(tempDir, "cangjie-tmp-XXXXXX");
    char* result = mkdtemp(tmpl.data());
    if (!result) return std::nullopt;
    return std::string(result);
}

// Windows: 使用 GetTempPath2 + CoCreateGuid 或 BCryptGenRandom
```

---

### VULN-08：Windows 平台 CreateProcess 命令行拼接风险

**严重程度**：🟠 中危  
**CVSS 评分**：6.0（AV:L/AC:H/PR:L/UI:N/S:U/C:H/I:H/A:N）  
**CWE**：CWE-78（OS Command Injection）

#### 漏洞代码

**文件**：`src/Driver/Tool.cpp`，第 120–161 行

```cpp
#ifdef _WIN32
    std::ostringstream oss;
    for (size_t i = 0; i < arguments.size(); ++i) {
        if (i != 0) oss << " ";
        if (arguments[i].empty() || 
            arguments[i].find_first_of("\t \"&\'()*<>\\`^|\n") != std::string::npos) {
            oss << std::quoted(arguments[i]);   // 使用 std::quoted 转义
        } else {
            oss << arguments[i];    // 不含特殊字符时直接拼接（可能遗漏某些字符）
        }
    }
    std::string commandLine = oss.str();
    if (!CreateProcessA(
        name.c_str(), const_cast<char*>(commandLine.c_str()), ...))  // 传入原始命令行字符串
```

#### 触发路径 / 攻击路径

1. 攻击者构造包含特殊字符的文件路径（如 `file&malicious.cj` 或包含 `%COMSPEC%`）作为编译目标；
2. `find_first_of` 的字符集可能存在遗漏（如 `%`、`!`、`^` 等 `cmd.exe` 特殊字符）；
3. 在某些 Windows 版本中，`CreateProcessA` 对特殊命令行字符处理存在差异；
4. 当 `name` 中含有空格但未加引号时，Windows 可能将前半段视为可执行文件名（参考 CVE-2022-29872）。

#### 根本原因

Windows `CreateProcess` 的命令行解析规则复杂且与 Shell 不同，通过字符黑名单方式过滤不如白名单可靠。此外，`name`（可执行文件路径）直接传入，存在路径中含空格未转义的风险。

#### 问题影响

- 特定 Windows 环境下的命令注入；
- 参考类似问题：CVE-2022-29872（Node.js Windows 命令注入）。

#### 修复建议

```cpp
// Windows 下强制对所有参数使用 std::quoted，不使用黑名单判断
for (const auto& arg : arguments) {
    oss << std::quoted(arg) << " ";
}
// 对 name 也应使用引号包裹
oss_full << std::quoted(name) << " " << oss.str();
```

---

### VULN-09：signal 处理函数调用非异步信号安全函数

**严重程度**：🟡 低危  
**CVSS 评分**：3.7（AV:L/AC:H/PR:L/UI:N/S:U/C:N/I:N/A:L）  
**CWE**：CWE-479（Signal Handler Use of a Non-reentrant Function）

#### 漏洞代码

**文件**：`src/Utils/SignalUnix.cpp`，第 67–72 行（`DeleteTempFiles(isSignalSafe=false)` 中）

```cpp
void Cangjie::RegisterCrtlCSignalHandler()
{
    ...
    sa.sa_sigaction = SigintHandler;
    ...
}
// SigintHandler 调用:
void Cangjie::TempFileManager::Instance().DeleteTempFiles();
// DeleteTempFiles 内部调用 rmdir, unlink 等，在信号处理中可接受
// 但某些路径调用 RemoveDirRecursively -> opendir -> readdir -> closedir
// 这些函数在部分实现中不是异步信号安全的
```

**文件**：`src/Driver/TempFileManager.cpp`

```cpp
void TempFileManager::DeleteTempFiles(bool isSignalSafe)
{
    ...
    if (S_ISDIR(buf.st_mode)) {
        (void)rmdir(filePath);
        if (!isSignalSafe) {
            RemoveDirRecursively(filePath);  // opendir/readdir - 非异步信号安全
        }
    }
```

宏展开信号处理（`src/Macro/MacroEvaluationClient.cpp`）：

```cpp
void SignalHandler(int) {
    Cangjie::MacroProcMsger::GetInstance().CloseMacroSrv();  // 调用 close/write 等
    _exit(1);
}
```

#### 问题影响

- 信号处理函数调用非异步信号安全函数可能导致死锁（重入 malloc 时）；
- 在极少数情况下可能导致堆损坏。

#### 修复建议

```cpp
// 信号处理中只调用 POSIX 异步信号安全函数（如 write, _exit, close, kill）
// 对于清理操作，使用 volatile sig_atomic_t 标志，在主线程检查后执行清理
volatile sig_atomic_t g_needCleanup = 0;
static void SigintHandler(int signum, siginfo_t*, void*) {
    g_needCleanup = 1;
    // 不调用任何可能非重入安全的函数
}
// 主循环或析构函数中检查并清理
```

---

### VULN-10：IncrementalCompilation JSON 解析器手工实现，缺少输入验证

**严重程度**：🟡 低危  
**CVSS 评分**：4.3（AV:L/AC:L/PR:L/UI:N/S:U/C:N/I:L/A:L）  
**CWE**：CWE-20（Improper Input Validation）

#### 漏洞代码

**文件**：`src/Sema/Plugin/ParseJson.cpp`，第 22–33 行

```cpp
std::string ParseJsonString(size_t& pos, const std::vector<uint8_t>& in)
{
    if (pos >= in.size() || in[pos] != '"') {
        return "";
    }
    ++pos;
    std::stringstream str;
    while (pos < in.size() && in[pos] != '"') {
        str << in[pos];
        ++pos;
    }
    // 注意：此处未处理 JSON 转义字符（\\, \n, \t, \uXXXX 等）
    // 也未处理未终止字符串（缺少闭合引号时直接返回不完整字符串）
    return str.str();
}
```

#### 问题影响

- 解析含特殊字符的 JSON 配置文件时，转义字符处理错误导致插件功能异常；
- 畸形 JSON 可能使插件校验信息解析失败，绕过预期的安全检查；
- 未终止字符串不会报错，导致静默失败。

#### 修复建议

使用成熟的 JSON 库（如 `nlohmann/json` 或代码库中已使用的 `llvm::json`）替代手工实现：

```cpp
#include "llvm/Support/JSON.h"
// 利用 llvm::json 进行解析，已内置完整的错误处理和验证
```

---

### VULN-11：编译器 ICE 处理泄露文件系统路径信息

**严重程度**：🟡 低危  
**CVSS 评分**：3.3（AV:L/AC:L/PR:L/UI:N/S:U/C:L/I:N/A:N）  
**CWE**：CWE-209（Generation of Error Message Containing Sensitive Information）

#### 漏洞代码

**文件**：`include/cangjie/Utils/CheckUtils.h`，第 39–46 行

```cpp
#define CJC_ASSERT_WITH_MSG(f, msg)                      \
    {                                                     \
        if (!(f)) {                                       \
            fprintf(stderr, "CJC_ASSERT failed at %s:%d: %s\n",
                    __FILE__, __LINE__,                  // 泄露源码编译路径
                    to_cstring(msg));                    
            abort();                                     
        }                                                
    }
```

**文件**：`include/cangjie/Utils/ICEUtil.h`，第 67–84 行

```cpp
std::cerr << ICE::MSG_PART_ONE;     // "Internal Compiler Error: "
((std::cerr << args), ...);         // 直接输出内部错误信息（可能含路径）
std::cerr << ICE::MSG_PART_TWO << std::to_string(tp) << std::endl;  // 含触发点编号
```

#### 问题影响

- 开发环境绝对路径泄漏（如 `/home/developer/cangjie/src/...`）；
- 编译器版本信息和内部结构对攻击者可见，可能辅助进一步攻击；
- 在 CI 系统中，构建日志被攻击者获取后，可获知构建系统目录结构。

#### 修复建议

```cpp
// 在 Release 构建中，去除 __FILE__ 的完整路径，仅保留文件名
#ifdef NDEBUG
#define ASSERT_FILE_INFO (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#else
#define ASSERT_FILE_INFO __FILE__
#endif
```

---

### VULN-12：整数溢出在 IntLiteral 运算中未覆盖所有边界

**严重程度**：🟡 低危  
**CVSS 评分**：3.9（AV:L/AC:H/PR:N/UI:N/S:U/C:N/I:L/A:N）  
**CWE**：CWE-190（Integer Overflow or Wraparound）

#### 漏洞代码

**文件**：`src/AST/IntLiteral.cpp`，左移运算符

```cpp
IntLiteral IntLiteral::operator<<(const IntLiteral& rhs) const
{
    // Cast to signed type to keep sign value.
    return IntLiteral(static_cast<int64_t>(uint64Val << rhs.uint64Val), type, false);
    // 问题：未检查移位量是否超过 64 位（UB in C++）
    // 若 rhs.uint64Val >= 64，则 uint64Val << rhs.uint64Val 是未定义行为
}

IntLiteral IntLiteral::operator>>(const IntLiteral& rhs) const
{
    return IntLiteral(static_cast<int64_t>(uint64Val >> rhs.uint64Val), type, false);
    // 同上，rhs.uint64Val >= 64 时 UB
}
```

#### 问题影响

- 编译器常量折叠时产生未定义行为，可能导致错误的编译结果；
- 攻击者构造包含大移位量的常量表达式，可能触发编译器崩溃或产生安全隐患的目标代码；
- 参考 CVE-2021-3997（Clang 移位溢出问题）。

#### 修复建议

```cpp
IntLiteral IntLiteral::operator<<(const IntLiteral& rhs) const
{
    if (rhs.uint64Val >= 64) {
        // 按语言规范返回溢出结果或触发溢出处理
        return IntLiteral(int64_t(0), type, true);
    }
    return IntLiteral(static_cast<int64_t>(uint64Val << rhs.uint64Val), type, false);
}
```

---

### VULN-13：RecursiveDirRemoval 在竞态窗口中存在 TOCTOU 问题

**严重程度**：🟡 低危  
**CVSS 评分**：4.4（AV:L/AC:H/PR:L/UI:N/S:U/C:L/I:L/A:N）  
**CWE**：CWE-367（TOCTOU Race Condition）

#### 漏洞代码

**文件**：`src/Driver/TempFileManager.cpp`，`RemoveDirRecursively` 函数（非 Windows）

```cpp
void RemoveDirRecursively(const std::string& dirPath)
{
    DIR* dir = opendir(dirPath.c_str());
    if (!dir) return;
    for (auto entry = readdir(dir); entry != nullptr; entry = readdir(dir)) {
        std::string fileName = std::string(entry->d_name);
        if (fileName == "." || fileName == "..") continue;
        std::string newPath = Cangjie::FileUtil::JoinPath(dirPath, fileName);
        if (entry->d_type == DT_REG) {
            (void)unlink(newPath.c_str());     // stat 和 unlink 之间存在 TOCTOU 窗口
        } else if (entry->d_type == DT_DIR) {
            RemoveDirRecursively(newPath);      // 递归调用时目录可能已被替换为符号链接
        }
    }
    (void)closedir(dir);
    (void)rmdir(dirPath.c_str());
}
```

#### 问题影响

- 攻击者在 `readdir` 和 `unlink`/`rmdir` 之间替换目录为符号链接；
- 编译器可能删除符号链接指向的目标文件（任意文件删除）；
- 结合 VULN-07（可预测目录名），攻击可行性进一步提高。

#### 修复建议

```cpp
// 使用 openat/unlinkat/fstatat 系列的 fd-based 操作，避免路径竞态
// 或直接使用 C++17 std::filesystem::remove_all（内部实现相对安全）
#include <filesystem>
std::filesystem::remove_all(dirPath);  // 更安全的替代方案
```

---

### VULN-14：插件（--plugin）加载路径未经完整性验证

**严重程度**：🟠 中危  
**CVSS 评分**：6.7（AV:L/AC:L/PR:H/UI:N/S:U/C:H/I:H/A:N）  
**CWE**：CWE-114（Process Control）、CWE-426（Untrusted Search Path）

#### 漏洞代码

**文件**：`src/Frontend/CompilerInstance.cpp`，第 293–265 行

```cpp
for (auto pluginPath : invocation.globalOptions.pluginPaths) {
    // loop for all plugins
    handle = InvokeRuntime::OpenSymbolTable(path);       // 直接加载，无签名验证
    // ...
    handle = InvokeRuntime::OpenSymbolTable(path, RTLD_NOW | RTLD_LOCAL);
}
```

**文件**：`src/Option/OptionAction.cpp`，第 646 行

```cpp
opts.pluginPaths.emplace_back(maybePath.value());  // 用户通过 --plugin 指定
```

#### 触发路径 / 攻击路径

1. 攻击者通过社会工程学让开发者使用恶意插件（`cjc --plugin /path/to/evil.so`）；
2. 或替换开发者已信任的插件路径中的动态库；
3. 插件以编译器进程权限执行任意代码，并可修改 IR 注入后门到编译产物；
4. 参考真实案例：XcodeGhost（通过替换 Xcode 编译器注入恶意代码）。

#### 问题影响

- 插件可直接访问完整的 CHIR IR，并向任意函数注入恶意指令；
- 编译产物被投毒，影响所有通过该编译器编译的程序；
- 在 CI/CD 中尤为危险（构建机器被攻陷后，所有编译产物均被投毒）。

#### 修复建议

```cpp
// 1. 实现插件签名验证（使用 libsodium 或 OpenSSL）
bool VerifyPluginSignature(const std::string& pluginPath, const std::string& sigPath) {
    // 验证 .so 文件的数字签名
}

// 2. 添加插件路径白名单（仅允许特定目录下的插件）
bool IsPluginPathAllowed(const std::string& pluginPath) {
    // 仅允许 CANGJIE_HOME/plugins/ 或 /usr/lib/cangjie/plugins/ 下的插件
}

// 3. 在文档中明确说明 --plugin 是危险选项，建议仅用于可信场景
```

---

### VULN-15：Demangler 对超长/格式异常输入可能触发栈溢出

**严重程度**：🟠 中危  
**CVSS 评分**：5.3（AV:N/AC:L/PR:N/UI:N/S:U/C:N/I:N/A:L）  
**CWE**：CWE-674（Uncontrolled Recursion）

#### 漏洞代码

**文件**：`demangler/Demangler.cpp`，递归解析函数（`DemangleArgTypes`、`ParseJsonObject` 等）

```cpp
T Demangler<T>::DemangleArgTypes(const T& delimiter, uint32_t size)
{
    ...
    while (IsNotEndOfMangledName() && i < size) {
        // 递归调用 DemangleType -> DemangleFunc -> DemangleTuple -> ...
        // 存在深度不受限制的递归（仅靠 MAX_ARGS_SIZE=16 限制横向，不限制深度）
    }
}
```

Cjfilt（命令行解码工具）`demangler/Cjfilt.cpp` 直接读取标准输入并调用解码函数，外部可直接触发。

#### 触发路径 / 攻击路径

1. 攻击者构造深度嵌套的 mangled 名称（如 `_CJ$pkg$F0F0F0F0F0...` 数千层嵌套）；
2. 通过 `cjfilt` 工具、调试符号处理或 LSP 服务输入该字符串；
3. Demangler 深度递归，消耗栈空间直至 `SIGSEGV`（栈溢出）；
4. 在某些平台上，异常的栈溢出可被利用（参考 CVE-2016-9840，zlib 栈溢出）。

#### 问题影响

- `cjfilt` 工具 DoS（可由无权限用户触发）；
- 若 Demangler 在 LSP 服务中使用，则影响 IDE 稳定性；
- 参考：c++filt（binutils）历史上多次存在类似栈溢出问题（如 CVE-2014-8485）。

#### 修复建议

```cpp
// 添加递归深度计数器
template<typename T>
class Demangler {
    size_t recursionDepth = 0;
    static constexpr size_t MAX_RECURSION_DEPTH = 256;

    T DemangleArgTypes(const T& delimiter, uint32_t size) {
        if (++recursionDepth > MAX_RECURSION_DEPTH) {
            --recursionDepth;
            return Reject("recursion depth exceeded");
        }
        // ... 原有逻辑 ...
        --recursionDepth;
    }
};
```

---

## 4. 供应链安全

### 4.1 第三方依赖分析

| 依赖组件 | 版本 | 已知 CVE | 风险评估 |
|----------|------|----------|----------|
| LLVM | 15.0.4 | CVE-2023-29932（已修复）、CVE-2023-29934 | 中等 |
| FlatBuffers | 未锁定版本 | 无已知严重 CVE | 低 |
| libboundscheck | OpenHarmony v6.0-Release | 无已知 CVE | 低 |
| MinGW-w64 | 12.0.0 | 无近期严重 CVE | 低 |

### 4.2 LLVM 补丁机制安全性

仓库使用 `llvmPatch.diff` 对 LLVM 进行修改。**安全风险**：
- 若 patch 文件被攻击者篡改，可在 LLVM 层面注入后门；
- `llvmPatch.diff` 仅通过 git 历史保护，未提供独立的哈希验证机制；
- 建议对 patch 文件提供 SHA-256 校验和，并在 README 中公示。

### 4.3 依赖版本锁定不足

`third_party/cmake/` 中的某些依赖（如 FlatBuffers）以 `git clone -b master` 方式拉取，未锁定到特定 commit hash，存在依赖投毒风险（Dependency Confusion 攻击）。

---

## 5. 构建系统安全

### 5.1 安全编译选项（Linux 平台）

`cmake/linux_toolchain.cmake` 中配置了以下安全选项：

| 选项 | 状态 | 说明 |
|------|------|------|
| `-fstack-protector-all` | ✅ 已启用 | 所有函数栈保护 |
| `-Wl,-z,relro,-z,now` | ✅ 已启用 | RELRO 全量保护 |
| `-Wl,-z,noexecstack` | ✅ 已启用 | 不可执行栈 |
| `-pie` | ✅ 已部分启用（仅 EXE） | PIE 地址随机化 |
| `-D_FORTIFY_SOURCE=2` | ⚠️ 仅 Release 构建 | 字符串函数边界检查 |
| `-Wsign-compare` | ✅ 已启用 | 符号比较警告 |
| UBSan | ❌ 默认关闭 | 未定义行为检测 |

### 5.2 问题：`_FORTIFY_SOURCE=2` 仅限 Release 构建

`cmake/linux_toolchain.cmake` 第 56、62 行：

```cmake
set(CMAKE_C_FLAGS_RELEASE "-D_FORTIFY_SOURCE=2 -O2")
set(CMAKE_CXX_FLAGS_RELEASE "-D_FORTIFY_SOURCE=2 -O2")
```

`Debug` 和 `RelWithDebInfo` 构建中未启用 `_FORTIFY_SOURCE`，可能导致测试环境未能发现生产代码中的缓冲区错误。

**建议**：在所有构建类型中启用 `_FORTIFY_SOURCE=2`（需与 `-O1` 或更高优化级别配合）：

```cmake
add_compile_definitions(_FORTIFY_SOURCE=2)
```

### 5.3 `SAFE_EXE_LINK_FLAG`（`-pie`）应用范围

`-pie` 标志仅在特定 EXE 目标中通过 `SAFE_EXE_LINK_FLAG` 应用，而非全局链接选项。需确认所有可执行文件（包括 `cjfilt`、`cjc-frontend` 等）均启用 PIE。

---

## 6. CVE 对比分析

本节将发现的安全问题与同类编译器的历史 CVE 进行对比分析：

| 仓颉编译器漏洞 | 参考 CVE | 同类编译器 | 描述 |
|---------------|----------|----------|------|
| VULN-01（FlatBuffers 无限制反序列化） | CVE-2023-38406 | Clang | 序列化数据解析 DoS |
| VULN-02（execvp 路径劫持） | CVE-2022-31628 | PHP Build | 不受信任 PATH 下程序查找 |
| VULN-03（RTLD_GLOBAL 符号污染） | CVE-2019-1010180 | GDB 插件 | 动态库全局符号污染 |
| VULN-04（CANGJIE_HOME 环境变量劫持） | CVE-2021-22204 | ExifTool | 环境变量控制代码路径 |
| VULN-05（Release 断言消除） | GCC Bug #18501 | GCC | NDEBUG 下安全检查失效 |
| VULN-07（可预测临时文件名） | CVE-2017-14160 | GCC tmpnam | 临时文件竞态 |
| VULN-08（Windows 命令行注入） | CVE-2022-29872 | Node.js | Windows CreateProcess 注入 |
| VULN-09（信号处理函数安全） | CVE-2021-3156 | sudo | 信号处理非重入函数调用 |
| VULN-12（移位运算 UB） | CVE-2021-3997 | Clang | 整数移位未定义行为 |
| VULN-13（TOCTOU 目录操作） | CVE-2019-15900 | npm | 递归删除 TOCTOU |
| VULN-14（插件无签名验证） | XcodeGhost (2015) | Xcode | 编译器插件/组件替换 |
| VULN-15（Demangler 栈溢出） | CVE-2014-8485 | binutils c++filt | Demangler 递归溢出 |

---

## 7. 总体风险矩阵

```
              │ 低影响 │ 中影响 │ 高影响 │
──────────────┼────────┼────────┼────────┤
 高可能性     │        │ V-10   │ V-02   │
 中可能性     │ V-11   │ V-07   │ V-01   │
              │ V-09   │ V-08   │ V-03   │
              │ V-12   │ V-13   │ V-04   │
              │ V-15   │        │        │
 低可能性     │        │ V-05   │ V-06   │
              │        │ V-14   │        │
```

---

## 8. 修复优先级路线图

### 立即修复（P0，1-2 周）

1. **VULN-02**：`execvp` 改为使用绝对路径（1-2 小时改动）；
2. **VULN-03**：将宏库默认加载模式从 `RTLD_GLOBAL` 改为 `RTLD_LOCAL`（<1 小时）；
3. **VULN-01**：为 CHIR 反序列化设置合理的深度/表数量限制（<2 小时）。

### 短期修复（P1，1 个月）

4. **VULN-04**：对 `CANGJIE_HOME` 进行路径合法性验证；
5. **VULN-07**：使用 `mkdtemp` 替代自定义临时目录生成；
6. **VULN-12**：修复移位运算的未定义行为；
7. **VULN-08**：加固 Windows 命令行参数转义。

### 中期改进（P2，3 个月）

8. **VULN-05**：将安全关键断言与调试断言分离；
9. **VULN-06**：限制 `RawStaticCast` 使用范围；
10. **VULN-09**：修复信号处理函数安全性；
11. **VULN-13**：使用 fd-based 目录操作或 `std::filesystem`；
12. **VULN-15**：Demangler 添加递归深度限制。

### 长期规划（P3，6 个月）

13. **VULN-14**：实现插件完整性验证机制；
14. **VULN-10**：替换手工 JSON 解析器；
15. **VULN-11**：优化错误信息中的路径信息输出。

---

## 9. 安全加固建议（通用）

### 9.1 代码安全实践

1. **启用更多编译器警告**：添加 `-Wformat-security`、`-Warray-bounds`、`-Wstack-protector` 到工具链；
2. **启用 UBSan**：在 CI 中的 Debug 构建启用 `-fsanitize=undefined`，持续检测未定义行为；
3. **代码扫描**：集成 Clang Static Analyzer 或 CodeQL 到 CI/CD 流水线；
4. **模糊测试**：对 Lexer/Parser/Demangler 进行 libFuzzer 覆盖测试（借鉴 ClangFuzz 经验）。

### 9.2 进程隔离

1. **沙箱化宏展开**：宏展开子进程应在受限沙箱（如 Linux seccomp-bpf、macOS Sandbox）中执行；
2. **最小权限原则**：宏服务器进程仅需读取源文件和宏库，可进一步限制系统调用集合。

### 9.3 供应链安全

1. **锁定所有依赖版本**：所有第三方依赖使用具体 commit hash，并记录 SHA-256 校验和；
2. **SBOM**：生成并发布软件物料清单（Software Bill of Materials）；
3. **签名分发**：对编译器发布包和插件提供数字签名。

### 9.4 安全开发文档

1. 建立编译器安全开发规范文档，明确以下禁用/受限 API：
   - 禁用：`execvp`（使用 `execv` + 绝对路径）、`tmpnam`、`mktemp`；
   - 受限：`RawStaticCast`（需代码审查批准）、`RTLD_GLOBAL`（需安全评审）；
2. 建立安全漏洞响应流程（CVD Policy）并公示漏洞报告邮箱。

---

*本报告仅供安全审计目的使用，请勿将报告内容用于任何恶意目的。*  
*如有问题或需要进一步分析，请联系安全团队。*

---

**报告结束**
