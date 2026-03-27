# 仓颉编译器安全审计复核报告

> **复核基准**：`security_audit_report_compiler.md`（初始报告，15 项发现）  
> **复核标准**：攻击手段须具备实际可行性——需存在现实攻击路径，而非仅存在理论风险  
> **复核结果**：**8 项确认** ✅，**7 项驳回** ❌

---

## 裁决总览

| 编号 | 原始严重级别 | 裁决 | 驳回理由概述 |
|------|------------|------|------------|
| VULN-01 | 高 | ✅ **确认** | — |
| VULN-02 | 高 | ✅ **确认** | — |
| VULN-03 | 高 | ✅ **确认** | — |
| VULN-04 | 中 | ✅ **确认** | — |
| VULN-05 | 中 | ❌ **驳回** | 编译器是开发者工具，非安全边界；无外部攻击者可利用路径 |
| VULN-06 | 中 | ❌ **驳回** | 所有调用点均在 `switch(astKind)` 保护下，不存在类型混淆路径 |
| VULN-07 | 低 | ❌ **驳回** | `/tmp` 目录竞争需要同机本地攻击者、精确时序窗口，实际利用极难 |
| VULN-08 | 中 | ❌ **驳回** | `CreateProcessA` 不经 `cmd.exe`，特殊字符不会展开 |
| VULN-09 | 低 | ❌ **驳回** | 仅在 Ctrl+C 时触发，编译器短生命周期进程中死锁概率极低 |
| VULN-10 | 低 | ❌ **驳回** | JSON 解析器仅处理开发者自有配置文件，不接受外部输入 |
| VULN-11 | 低 | ❌ **驳回** | `__FILE__` 在生产构建（NDEBUG）下完全消除 |
| VULN-12 | 低 | ✅ **确认** | — |
| VULN-13 | 低 | ✅ **确认** | — |
| VULN-14 | 中 | ✅ **确认** | — |
| VULN-15 | 中 | ✅ **确认** | — |

---

## 已确认问题列表（按源码目录分类）

### 📂 `src/CHIR/Serializer/`

#### VULN-01：FlatBuffers 反序列化器禁用深度与表数量限制（高）

- **源码位置**：[`src/CHIR/Serializer/CHIRDeserializer.cpp` 第 56–58 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/CHIR/Serializer/CHIRDeserializer.cpp#L56-L58)

```cpp
flatbuffers::Verifier::Options options;
options.max_depth = std::numeric_limits<::flatbuffers::uoffset_t>::max();
options.max_tables = std::numeric_limits<::flatbuffers::uoffset_t>::max();
```

- **攻击可行性**：✅ **可行**。攻击者构造恶意 `.chir` 文件并通过 `--import-chir` 参数传入编译器或替换增量编译缓存文件。FlatBuffers 验证器的深度和表数量限制被设置为 `uint32_t::max()`，完全失去对嵌套深度和表数量的保护。恶意文件可触发深度递归导致栈溢出，或构造超大表数量导致堆内存耗尽（DoS）。
- **CWE**：CWE-770（无限制资源分配）

---

### 📂 `src/Macro/`

#### VULN-02：宏服务器通过 `execvp` 依赖 PATH 查找启动（高）

- **源码位置**：[`src/Macro/MacroEvaluationClient.cpp` 第 37 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Macro/MacroEvaluationClient.cpp#L37)（常量定义），[第 497–509 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Macro/MacroEvaluationClient.cpp#L497-L509)（execvp 调用）

```cpp
const std::string MACRO_SRV_NAME = "LSPMacroServer";
// ...
execvp(macSrvName.c_str(), cstrings.data());
```

- **攻击可行性**：✅ **可行**。`execvp` 按 `PATH` 环境变量搜索可执行文件。攻击者只需在编译器工作目录下（或 `PATH` 靠前路径中）放置一个名为 `LSPMacroServer` 的恶意程序，当用户编译含宏的仓颉源码时，编译器将执行恶意程序而非真正的宏服务器。该攻击对于有写权限的共享构建环境尤其有效。编译器已持有 `executablePath`（自身路径），但未利用该信息推导宏服务器的绝对路径。
- **CWE**：CWE-426（不受信任的搜索路径）

#### VULN-03：宏 `.so` 库使用 `RTLD_GLOBAL` 加载（高）

- **源码位置**：[`include/cangjie/Macro/InvokeUtil.h` 第 80 行](https://github.com/wyq0721/cangjie_compiler/blob/main/include/cangjie/Macro/InvokeUtil.h#L80)（默认参数定义），[`src/Macro/MacroEvaluationCJNative.cpp` 第 101 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Macro/MacroEvaluationCJNative.cpp#L101)、[`src/Macro/MacroCallResolve.cpp` 第 199 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Macro/MacroCallResolve.cpp#L199)、[`src/Macro/MacroEvaluationSrv.cpp` 第 186 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Macro/MacroEvaluationSrv.cpp#L186)（调用点）

```cpp
// 头文件默认参数
HANDLE OpenSymbolTable(const std::string& libPath, int dlopenMode = RTLD_LAZY | RTLD_GLOBAL);

// 调用（均使用默认参数）
auto handle = InvokeRuntime::OpenSymbolTable(dyfile);
```

- **攻击可行性**：✅ **可行**。使用 `--macro-lib` 选项加载的宏库以 `RTLD_GLOBAL` 打开，导致宏库导出的全部符号进入编译器全局符号命名空间。恶意宏库可定义与标准库同名的函数（如 `malloc`、`free`、`memcpy`），在后续编译流程中劫持编译器自身的函数调用。此攻击在 CI/CD 环境中尤其危险——攻击者可通过提交含恶意宏库的代码仓库，在构建服务器上劫持编译器进程。
- **CWE**：CWE-114（进程控制）

---

### 📂 `src/Driver/` + `src/Option/`

#### VULN-04：`CANGJIE_HOME` 环境变量无条件信任（中）

- **源码位置**：[`src/Option/Option.cpp` 第 1173–1174 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Option/Option.cpp#L1173-L1174)（读取环境变量），[`src/Driver/Backend/CJNATIVEBackend.cpp` 第 75–81 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Driver/Backend/CJNATIVEBackend.cpp#L75-L81)（构建搜索路径），[`src/Driver/Toolchains/ToolChain.cpp` 第 361 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Driver/Toolchains/ToolChain.cpp#L361)（查找 LLVM 工具链）

```cpp
// Option.cpp
environment.cangjieHome = FileUtil::GetAbsPath(environmentVars.at(CANGJIE_HOME));

// CJNATIVEBackend.cpp
cjnativeBinSearchPaths.emplace_back(
    FileUtil::JoinPath(driverOptions.environment.cangjieHome.value(), "third_party/llvm/bin"));
```

- **攻击可行性**：✅ **可行**。攻击者控制 `CANGJIE_HOME` 环境变量后，编译器将从攻击者指定的路径查找 `opt`、`llc` 等后端工具和链接器。在共享构建环境中（如 CI 服务器、Docker 容器），攻击者可设置环境变量指向含恶意工具的目录，从而在编译输出中注入后门代码。编译器有基于 `executablePath` 推导的备用路径（`Driver.cpp:49`），但 `CANGJIE_HOME` 的优先级更高。
- **CWE**：CWE-426（不受信任的搜索路径）

#### VULN-13：`RemoveDirRecursively` 无 fd 锚定的 TOCTOU 竞态（低）

- **源码位置**：[`src/Driver/TempFileManager.cpp` 第 179–200 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Driver/TempFileManager.cpp#L179-L200)

```cpp
void RemoveDirRecursively(const std::string& dirPath)
{
    DIR* dir = opendir(dirPath.c_str());
    // ...
    for (auto entry = readdir(dir); entry != nullptr; entry = readdir(dir)) {
        std::string newPath = Cangjie::FileUtil::JoinPath(dirPath, fileName);
        if (entry->d_type == DT_REG) {
            (void)unlink(newPath.c_str());     // path-based, no fd anchoring
        } else if (entry->d_type == DT_DIR) {
            RemoveDirRecursively(newPath);     // recursive path-based traversal
        }
    }
}
```

- **攻击可行性**：✅ **可行但受限**。`readdir` 返回条目与 `unlink`/`rmdir` 执行之间存在时间窗口，同机攻击者可利用符号链接替换将删除操作重定向到任意文件。由于临时目录权限为 `775`（`S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH`），同组用户可写，攻击面真实存在。需要本地同机访问权限。
- **CWE**：CWE-367（TOCTOU 竞态条件）

---

### 📂 `src/AST/`

#### VULN-12：`IntLiteral` 移位操作未检查移位量边界（低）

- **源码位置**：[`src/AST/IntLiteral.cpp` 第 461–470 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/AST/IntLiteral.cpp#L461-L470)

```cpp
IntLiteral IntLiteral::operator>>(const IntLiteral& rhs) const
{
    return IntLiteral(static_cast<int64_t>(uint64Val >> rhs.uint64Val), type, false);
}

IntLiteral IntLiteral::operator<<(const IntLiteral& rhs) const
{
    return IntLiteral(static_cast<int64_t>(uint64Val << rhs.uint64Val), type, false);
}
```

- **攻击可行性**：✅ **可行**。用户编写的仓颉源码中包含编译期常量表达式时，`rhs.uint64Val >= 64` 将导致 C++ 标准下的未定义行为（UB）。不同编译器和优化级别下行为不可预测，可能导致编译器生成错误的常量折叠结果，进而影响目标代码正确性。攻击者可构造特定源码使编译器产生错误结果。
- **CWE**：CWE-190（整数溢出/回绕）

---

### 📂 `src/Frontend/`

#### VULN-14：`--plugin` 加载无签名或路径白名单验证（中）

- **源码位置**：[`src/Frontend/CompilerInstance.cpp` 第 258–282 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Frontend/CompilerInstance.cpp#L258-L282)（`MetaTransformPlugin::Get`），[第 291–304 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Frontend/CompilerInstance.cpp#L291-L304)（`PerformPluginLoad` 循环）

```cpp
MetaTransformPlugin MetaTransformPlugin::Get(const std::string& path)
{
    HANDLE handle = nullptr;
#ifdef _WIN32
    handle = InvokeRuntime::OpenSymbolTable(path);
#elif defined(__linux__) || defined(__APPLE__)
    handle = InvokeRuntime::OpenSymbolTable(path, RTLD_NOW | RTLD_LOCAL);
#endif
    // ... no signature check, no path allowlist
}
```

- **攻击可行性**：✅ **可行**。`--plugin` 选项接受任意文件系统路径，在 Linux/macOS 上以 `RTLD_NOW | RTLD_LOCAL` 加载（比宏库安全，但仍无完整性验证）。在 CI/CD 管道中，若构建脚本从不可信源获取插件路径，攻击者可替换为恶意 `.so`。插件加载后直接调用 `getMetaTransformPluginInfo` 和 `registerTo` 函数指针，恶意插件可完全控制编译流程。
- **CWE**：CWE-114（进程控制）

---

### 📂 `demangler/`

#### VULN-15：Demangler 无递归深度限制（中）

- **源码位置**：[`demangler/Demangler.cpp` 第 1412–1418 行](https://github.com/wyq0721/cangjie_compiler/blob/main/demangler/Demangler.cpp#L1412-L1418)（`DemangleNextUnit` 入口），[第 1422 行](https://github.com/wyq0721/cangjie_compiler/blob/main/demangler/Demangler.cpp#L1422)（`DemangleByPrefix` 分发），[第 1097–1098 行](https://github.com/wyq0721/cangjie_compiler/blob/main/demangler/Demangler.cpp#L1097-L1098)（`DemangleCFuncType` 递归），[第 1104–1108 行](https://github.com/wyq0721/cangjie_compiler/blob/main/demangler/Demangler.cpp#L1104-L1108)（`DemangleFunction` 递归）

递归调用链：`DemangleNextUnit` → `DemangleByPrefix` → `DemangleFunction`/`DemangleTuple`/`DemangleClass` → `DemangleArgTypes` → `DemangleNextUnit`（循环递归），全链路无深度计数器。

- **攻击可行性**：✅ **可行**。`cjfilt` 工具从 stdin 读取用户输入并直接传入 Demangler。攻击者可构造深度嵌套的 mangled 名称（如数千层嵌套的函数类型 `F0F0F0F0...`），触发不受限递归导致栈溢出崩溃。此 DoS 在以下场景中可被利用：
  - 开发者对不可信的二进制文件运行 `cjfilt`
  - 工具链集成中将 `cjfilt` 用于错误消息格式化
- **CWE**：CWE-674（不受控递归）

---

## 驳回说明

### ❌ VULN-05：`CJC_ASSERT` 在 NDEBUG 下消除

- **源码位置**：[`include/cangjie/Utils/CheckUtils.h` 第 55–56 行](https://github.com/wyq0721/cangjie_compiler/blob/main/include/cangjie/Utils/CheckUtils.h#L55-L56)
- **驳回理由**：编译器是**开发者工具**，不是面向终端用户的安全边界程序。`CJC_ASSERT` 保护的是编译器内部不变量（如 AST 节点非空、序列化完整性等），这些断言的触发前提是编译器自身存在 bug，而非外部攻击者可以通过输入源码直接触发。将编译器内部的调试断言策略定义为安全漏洞不具备攻击可行性——攻击者无法通过构造仓颉源码来选择性地利用被消除的断言。此外，`NDEBUG` 下消除 `assert` 是 C/C++ 生态的标准实践（GCC、Clang、MSVC 均如此）。

### ❌ VULN-06：`RawStaticCast` 绕过类型检查

- **源码位置**：[`include/cangjie/Utils/CastingTemplate.h` 第 220–227 行](https://github.com/wyq0721/cangjie_compiler/blob/main/include/cangjie/Utils/CastingTemplate.h#L220-L227)
- **驳回理由**：代码审查确认所有在 `Node.cpp` 中的 `RawStaticCast` 调用均位于 `switch (astKind)` 分支内（如 `SetTarget`、`GetTarget`、`GetTargets`、`GetConstInvocation` 等函数）。`astKind` 由 AST 节点构造时确定，在生命周期内不可变，类型判断先于转换执行。不存在攻击者能绕过 `switch` 分支检查、使 `RawStaticCast` 操作错误类型对象的代码路径。

### ❌ VULN-07：临时目录名可预测

- **源码位置**：[`src/Driver/TempFileManager.cpp` 第 114–120 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Driver/TempFileManager.cpp#L114-L120)
- **驳回理由**：虽然随机源为 32-bit `/dev/urandom` 且使用 `mkdir` 而非 `mkdtemp`，但利用此弱点需要攻击者：(1) 在同一台机器上有本地访问权限；(2) 精确预测纳秒级时间戳；(3) 在 `CreateTempDirName` 和 `mkdir` 之间的极短时间窗口内完成目录创建和符号链接攻击。编译器是短生命周期进程，临时目录在编译完成后立即删除。在实际攻击场景中，如果攻击者已有同机本地权限，有更多更直接的攻击手段可用，无需利用此时序窗口。

### ❌ VULN-08：Windows `CreateProcess` 命令注入

- **源码位置**：[`src/Driver/Tool.cpp` 第 126–140 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Driver/Tool.cpp#L126-L140)
- **驳回理由**：`CreateProcessA` 的第一个参数为 `name.c_str()`（可执行文件的绝对路径），直接创建进程而不经过 `cmd.exe` 解释器。`%VARIABLE%`、`!delayed!`、`^escape` 等字符仅在 `cmd.exe` 环境下才会被展开，在直接 `CreateProcess` 调用中是惰性字面量。现有的 `std::quoted` 处理已足够应对参数中包含空格和双引号的情况。

### ❌ VULN-09：SIGINT 信号处理中调用非异步信号安全函数

- **源码位置**：[`src/Utils/SignalUnix.cpp` 第 76–80 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Utils/SignalUnix.cpp#L76-L80)
- **驳回理由**：`SigintHandler` 调用 `DeleteTempFiles()` 时使用默认参数 `isSignalSafe=false`。但代码在 `DeleteTempFiles` 中对目录执行 `rmdir`（信号安全）后仅在 `!isSignalSafe` 条件下才调用 `RemoveDirRecursively`。更关键的是：(1) SIGINT 仅由用户主动 Ctrl+C 触发，不是远程可触发的攻击向量；(2) 编译器是短生命周期进程，信号处理中的死锁风险在实际中极低；(3) 即使发生死锁，后果仅为编译器进程挂起，无安全影响。这是代码质量问题，不构成安全漏洞。

### ❌ VULN-10：JSON 解析器缺陷

- **源码位置**：[`src/Sema/Plugin/ParseJson.cpp`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Sema/Plugin/ParseJson.cpp)
- **驳回理由**：该 JSON 解析器仅用于解析开发者通过编译选项指定的 syscap 配置文件（`PluginCustomAnnoChecker`），不处理来自外部不可信源的输入。解析器使用 `std::vector<uint8_t>` 做边界检查，逐字节读取，不存在内存安全问题。缺少转义处理和对格式错误的严格报告属于功能性问题，非安全漏洞。

### ❌ VULN-11：构建路径泄露

- **源码位置**：[`include/cangjie/Utils/CheckUtils.h` 第 42–50 行](https://github.com/wyq0721/cangjie_compiler/blob/main/include/cangjie/Utils/CheckUtils.h#L42-L50)
- **驳回理由**：含 `__FILE__` 的 `CJC_ASSERT_WITH_MSG` 和 `CJC_ABORT_WITH_MSG` 仅在 `CMAKE_ENABLE_ASSERT` 或 debug 构建（非 NDEBUG）下编译。生产构建（`NDEBUG`）中这些宏展开为 `static_cast<void>(...)` 或空操作，`__FILE__` 完全不出现在二进制中。`InternalError()` 仅输出版本字符串和触发点编号，不包含文件路径。

---

## 确认问题汇总表

| 编号 | 严重级别 | 源码目录 | 文件 | CWE |
|------|---------|---------|------|-----|
| [VULN-01](#vuln-01flatbuffers-反序列化器禁用深度与表数量限制高) | 🔴 高 | `src/CHIR/Serializer/` | [`CHIRDeserializer.cpp:56-58`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/CHIR/Serializer/CHIRDeserializer.cpp#L56-L58) | CWE-770 |
| [VULN-02](#vuln-02宏服务器通过-execvp-依赖-path-查找启动高) | 🔴 高 | `src/Macro/` | [`MacroEvaluationClient.cpp:509`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Macro/MacroEvaluationClient.cpp#L509) | CWE-426 |
| [VULN-03](#vuln-03宏-so-库使用-rtld_global-加载高) | 🔴 高 | `src/Macro/` + `include/` | [`InvokeUtil.h:80`](https://github.com/wyq0721/cangjie_compiler/blob/main/include/cangjie/Macro/InvokeUtil.h#L80) | CWE-114 |
| [VULN-04](#vuln-04cangjie_home-环境变量无条件信任中) | 🟠 中 | `src/Option/` + `src/Driver/` | [`Option.cpp:1173`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Option/Option.cpp#L1173) | CWE-426 |
| [VULN-12](#vuln-12intliteral-移位操作未检查移位量边界低) | 🟡 低 | `src/AST/` | [`IntLiteral.cpp:461-470`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/AST/IntLiteral.cpp#L461-L470) | CWE-190 |
| [VULN-13](#vuln-13removedirrecursively-无-fd-锚定的-toctou-竞态低) | 🟡 低 | `src/Driver/` | [`TempFileManager.cpp:179-200`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Driver/TempFileManager.cpp#L179-L200) | CWE-367 |
| [VULN-14](#vuln-14--plugin-加载无签名或路径白名单验证中) | 🟠 中 | `src/Frontend/` | [`CompilerInstance.cpp:258-282`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Frontend/CompilerInstance.cpp#L258-L282) | CWE-114 |
| [VULN-15](#vuln-15demangler-无递归深度限制中) | 🟠 中 | `demangler/` | [`Demangler.cpp:1412-1418`](https://github.com/wyq0721/cangjie_compiler/blob/main/demangler/Demangler.cpp#L1412-L1418) | CWE-674 |

---

## 修复优先级建议

| 优先级 | 编号 | 建议修复方案 |
|--------|------|------------|
| P0（立即） | VULN-02 | 将 `execvp` 改为基于 `executablePath` 派生的绝对路径调用 `execv` |
| P0（立即） | VULN-03 | 宏库调用 `OpenSymbolTable` 时显式传入 `RTLD_NOW \| RTLD_LOCAL` |
| P1（1 个月） | VULN-01 | 为 `max_depth` 和 `max_tables` 设置合理上限（如 128 / 10000000） |
| P1（1 个月） | VULN-04 | 对 `CANGJIE_HOME` 进行路径合法性验证，优先使用基于 `executablePath` 推导的路径 |
| P2（3 个月） | VULN-14 | 添加插件路径白名单机制，限制为 `$CANGJIE_HOME/plugins/` |
| P2（3 个月） | VULN-15 | 在 `DemangleNextUnit` 中添加递归深度计数器并限制（如 256） |
| P3（6 个月） | VULN-12 | 在移位操作前添加 `rhs.uint64Val >= 64` 检查 |
| P3（6 个月） | VULN-13 | 使用 `openat`/`unlinkat`/`fdopendir` 替代 path-based 操作，或改用 `std::filesystem::remove_all` |
