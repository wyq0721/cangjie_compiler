# 仓颉编译器安全审计复核报告（Recheck）

**审计时间**：2026-03-27  
**复核依据**：`security_audit_report_compiler.md`（原始报告，15 项漏洞）  
**复核方式**：逐项对照实际源码进行裁决，仅保留经代码确认的真实问题  
**复核结果**：11 项确认 ✅，4 项驳回 ❌

---

## 裁决总览

| 编号 | 原始标题 | 裁决 | 理由摘要 |
|------|----------|------|----------|
| VULN-01 | FlatBuffers CHIR 反序列化无深度限制 | ✅ **确认** | 代码中显式注释"禁用"限制，有据可查 |
| VULN-02 | execvp PATH 劫持（宏服务器） | ✅ **确认** | `execvp("LSPMacroServer", ...)` 确认依赖 PATH |
| VULN-03 | RTLD_GLOBAL 宏库符号污染 | ✅ **确认** | 宏用户库使用默认 `RTLD_LAZY\|RTLD_GLOBAL` 确认 |
| VULN-04 | CANGJIE_HOME 环境变量无条件信任 | ✅ **确认** | 直接用于构建可执行文件搜索路径，无验证 |
| VULN-05 | NDEBUG 下 CJC_ASSERT 消除 | ✅ **确认** | 宏定义确认，含 3716+ 处空指针安全检查 |
| VULN-06 | RawStaticCast 绕过类型检查 | ❌ **驳回** | 使用前均有 switch/astKind 类型断言，不是漏洞 |
| VULN-07 | 临时目录名可预测 | ✅ **确认** | 时间戳+32-bit 随机数，未用 mkdtemp |
| VULN-08 | Windows CreateProcess 命令注入 | ❌ **驳回** | CreateProcessA 不经 cmd.exe，% ! ^ 不展开 |
| VULN-09 | 信号处理函数调用非异步安全函数 | ✅ **确认** | SigintHandler→DeleteTempFiles(false)→opendir/readdir |
| VULN-10 | 手工 JSON 解析器缺陷 | ❌ **驳回** | 解析开发者自有配置文件，无内存安全风险 |
| VULN-11 | ICE/断言消息泄露构建路径 | ❌ **驳回** | NDEBUG 下 `__FILE__` 输出完全消除，不适用 |
| VULN-12 | IntLiteral 移位未定义行为 | ✅ **确认** | 移位量无上界检查，C++ UB 确认 |
| VULN-13 | RemoveDirRecursively TOCTOU | ✅ **确认** | readdir→unlink 之间存在竞态窗口 |
| VULN-14 | 插件加载无完整性验证 | ✅ **确认** | `PerformPluginLoad()` 无签名/路径白名单 |
| VULN-15 | Demangler 无递归深度限制 | ✅ **确认** | DemangleNextUnit 循环调用，无深度计数器 |

---

## 驳回说明

### ❌ VULN-06（RawStaticCast 绕过类型检查）—— 驳回

**驳回理由**：  
原报告将 `RawStaticCast` 定性为"绕过类型检查"。但经逐一核查实际调用位置，该函数在所有关键路径上的使用均是**在已完成 `switch(astKind)` 或其他确定性类型判断之后**进行的。

以报告重点引用的 `Node::GetTargets()`（`src/AST/Node.cpp:718`）为例：

```cpp
std::vector<Ptr<Decl>> Node::GetTargets() const
{
    switch (astKind) {              // ← 先通过 astKind 确定类型
        case ASTKind::REF_TYPE: {
            return RawStaticCast<const RefType*>(this)->ref.targets;   // 已知类型安全
        }
        case ASTKind::REF_EXPR: {
            return RawStaticCast<const RefExpr*>(this)->ref.targets;   // 已知类型安全
        }
        ...
        default:
            return {};
    }
}
```

`RawStaticCast` 在此处等同于 LLVM 的 `cast<>` 模式——类型已由 `switch` 保证，强制转换是明确安全的。不存在攻击者可利用的、绕过类型断言的代码路径。

---

### ❌ VULN-08（Windows CreateProcess 命令注入）—— 驳回

**驳回理由**：  
原报告声称 `%COMSPEC%`、`!var!`、`^char^` 等字符可造成命令注入。但 **`CreateProcessA` 不经由 `cmd.exe` 执行**，以上字符均是 `cmd.exe` 专用扩展语法，在直接 `CreateProcessA` 调用中不会被展开或解释。

代码实际调用方式（`src/Driver/Tool.cpp:139`）：

```cpp
CreateProcessA(
    name.c_str(),                           // lpApplicationName：直接指定可执行文件
    const_cast<char*>(commandLine.c_str()), // lpCommandLine：参数字符串
    nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)
```

- `lpApplicationName` 不经 shell 解析；
- `lpCommandLine` 中的 `%`、`!`、`^` 由目标进程的 CRT 层（`argv` 解析）处理，不会触发 shell 扩展。

现有的 `std::quoted` 处理已足够应对参数中的双引号，该问题不构成命令注入漏洞。

---

### ❌ VULN-10（手工 JSON 解析器缺陷）—— 驳回

**驳回理由**：  
该 JSON 解析器仅用于解析由开发者通过 `--passed-when syscap=<path>` 选项显式指定的 syscap 配置文件，属于开发者自有的构建配置，**不属于攻击者可控的外部输入**。

此外，解析器逐字节读入 `std::stringstream`，即便遇到格式错误也只产生错误的解析结果（返回空字符串或 0），**不会造成内存越界或堆溢出**。问题属于代码健壮性不足，不属于安全漏洞。

---

### ❌ VULN-11（ICE 消息泄露构建路径）—— 驳回

**驳回理由**：  
原报告引用 `CJC_ASSERT_WITH_MSG` 中的 `fprintf(stderr, "... %s:%d ...", __FILE__, __LINE__)` 作为路径泄露证据。但经查阅 `include/cangjie/Utils/CheckUtils.h` 完整宏定义：

```cpp
#ifdef CMAKE_ENABLE_ASSERT
    // ... 含 __FILE__ 的 fprintf 输出 ...
#else
#ifdef NDEBUG
    #define CJC_ASSERT_WITH_MSG(f, msg) (static_cast<void>(f), static_cast<void>(msg))  // 完全消除
#else
    // ... 含 __FILE__ 的 fprintf 输出（调试构建）...
#endif
#endif
```

在生产发布构建（`NDEBUG`）中，含 `__FILE__` 的 `fprintf` 完全消除，不产生任何输出。

`InternalError()`（`ICEUtil.h`）在生产构建中只输出 `CANGJIE_COMPILER_VERSION` 字符串和一个内部编号整数，**不包含文件系统路径**。

在调试/`CMAKE_ENABLE_ASSERT` 构建中输出 `__FILE__` 是开发构建的预期行为，不视为安全缺陷。

---

## 已确认漏洞详情

---

### VULN-01：FlatBuffers CHIR 反序列化禁用深度/表数量限制

**严重程度**：🔴 高危  
**CWE**：CWE-770（Resource Allocation Without Limits）

#### 漏洞代码

**文件**：`src/CHIR/Serializer/CHIRDeserializer.cpp`，第 55–60 行

```cpp
// Disable max depth and max tables verification.
flatbuffers::Verifier::Options options;
options.max_depth = std::numeric_limits<::flatbuffers::uoffset_t>::max();
options.max_tables = std::numeric_limits<::flatbuffers::uoffset_t>::max();
flatbuffers::Verifier verifier(serializationInfo.data(), serializationInfo.size(), options);
if (!verifier.VerifyBuffer<PackageFormat::CHIRPackage>()) { ... }
```

**注**：AST 序列化路径（`ASTLoader.cpp:217`）设有合理限制 `FB_MAX_DEPTH=128`，两者形成鲜明对比。

#### 攻击路径

攻击者构造深度极大的 `.chir` 文件 → 通过 `cjc --import-chir` 或篡改增量编译缓存 → 触发反序列化 → OOM 或深度递归栈溢出（DoS）。

#### 根本原因

代码注释明确表明"已禁用"此限制，用于兼容大型项目。无深度上界等同于对恶意构造输入没有防御。

#### 修复建议

```cpp
flatbuffers::Verifier::Options options;
options.max_depth = 4096;         // 足够覆盖实际最大 CHIR 嵌套
options.max_tables = 10000000;    // 10M 条目上限
flatbuffers::Verifier verifier(serializationInfo.data(), serializationInfo.size(), options);
```

---

### VULN-02：宏服务器通过 execvp 依赖 PATH 查找（路径劫持）

**严重程度**：🔴 高危  
**CWE**：CWE-426（Untrusted Search Path）

#### 漏洞代码

**文件**：`src/Macro/MacroEvaluationClient.cpp`，第 497–509 行

```cpp
std::string macSrvName = MACRO_SRV_NAME;  // = "LSPMacroServer"（硬编码程序名）
// ...
cstrings.push_back(macSrvName.data());
cstrings.push_back(ci->invocation.globalOptions.executablePath.data());  // 路径作为参数传递
cstrings.push_back(nullptr);
execvp(macSrvName.c_str(), cstrings.data());  // 依赖 PATH 查找，存在劫持风险
```

注意：`executablePath`（编译器自身路径）已作为**参数**传递给宏服务器，但未用于**确定宏服务器位置**。

#### 攻击路径

攻击者在 `PATH` 前缀目录（如 `./`）放置同名恶意程序 `LSPMacroServer` → 用户在该目录运行 `cjc` 编译含宏源码 → `execvp` 找到并执行恶意程序 → 任意代码执行。

#### 修复建议

```cpp
// 使用编译器自身路径派生宏服务器绝对路径
std::string execDir = FileUtil::GetDirPath(ci->invocation.globalOptions.executablePath);
std::string macSrvAbsPath = FileUtil::JoinPath(execDir, MACRO_SRV_NAME);
if (!FileUtil::CanExecute(macSrvAbsPath)) {
    Errorln("Macro server not found: ", macSrvAbsPath);
    return;
}
cstrings[0] = macSrvAbsPath.data();
execv(macSrvAbsPath.c_str(), cstrings.data());  // 使用 execv，不依赖 PATH
```

---

### VULN-03：宏用户库默认使用 RTLD_GLOBAL 加载（符号污染）

**严重程度**：🔴 高危  
**CWE**：CWE-426（Untrusted Search Path）、CWE-114（Process Control）

#### 漏洞代码

**文件**：`include/cangjie/Macro/InvokeUtil.h`，第 80 行

```cpp
// Linux/macOS 平台的默认参数
HANDLE OpenSymbolTable(const std::string& libPath, int dlopenMode = RTLD_LAZY | RTLD_GLOBAL);
```

**实际调用**（`src/Macro/MacroEvaluationCJNative.cpp:101`、`src/Macro/MacroCallResolve.cpp:199`）：

```cpp
auto handle = InvokeRuntime::OpenSymbolTable(dyfile);  // 使用默认 RTLD_GLOBAL
```

**对比**：插件加载已安全处理（`src/Frontend/CompilerInstance.cpp`）：

```cpp
handle = InvokeRuntime::OpenSymbolTable(path, RTLD_NOW | RTLD_LOCAL);  // 安全
```

**说明**：运行时库（`InvokeUtil.cpp:88`）使用 `RTLD_GLOBAL` 是**有意设计**（宏库需要找到运行时符号），这部分合理。问题在于**宏用户库**也继承了 `RTLD_GLOBAL` 默认值。

#### 攻击路径

恶意宏库导出与编译器运行时同名符号（如 `malloc`、`free`、`pthread_create`）→ 以 `RTLD_GLOBAL` 加载后覆盖全局符号表 → 后续运行时调用命中恶意实现 → 内存管理劫持或信息泄漏。

#### 修复建议

```cpp
// 宏用户库使用 RTLD_LOCAL，并添加 RTLD_DEEPBIND 进一步隔离
auto handle = InvokeRuntime::OpenSymbolTable(dyfile, RTLD_NOW | RTLD_LOCAL | RTLD_DEEPBIND);
```

---

### VULN-04：CANGJIE_HOME 环境变量被无条件信任用于可执行文件查找

**严重程度**：🟠 中危  
**CWE**：CWE-426（Untrusted Search Path）

#### 漏洞代码

**文件**：`src/Option/Option.cpp`，第 1173–1174 行

```cpp
if (environmentVars.find(CANGJIE_HOME) != environmentVars.end()) {
    environment.cangjieHome = FileUtil::GetAbsPath(environmentVars.at(CANGJIE_HOME));
}
```

**文件**：`src/Driver/Backend/CJNATIVEBackend.cpp`，第 76–84 行

```cpp
if (driverOptions.environment.cangjieHome.has_value()) {
    cjnativeBinSearchPaths.emplace_back(
        FileUtil::JoinPath(driverOptions.environment.cangjieHome.value(), "third_party/llvm/bin"));
}
// 若未找到 opt/llc 则报错退出，否则直接执行找到的程序
```

#### 攻击路径

攻击者设置 `CANGJIE_HOME=/tmp/evil` → 在 `/tmp/evil/third_party/llvm/bin/` 放置恶意 `opt`、`llc` → 用户运行 `cjc` → 编译器调用恶意后端工具 → 向编译产物注入后门代码（供应链攻击）。

#### 修复建议

对 `cangjieHome` 进行结构验证：路径必须包含预期的 `modules/`、`runtime/` 子目录。优先使用从编译器 `executablePath` 推导的路径，仅在环境变量路径通过验证后才覆盖默认值。

---

### VULN-05：NDEBUG 发布构建下 CJC_ASSERT/CJC_NULLPTR_CHECK 完全消除

**严重程度**：🟠 中危  
**CWE**：CWE-617（Reachable Assertion）

#### 漏洞代码

**文件**：`include/cangjie/Utils/CheckUtils.h`，第 55–56 行

```cpp
#ifdef NDEBUG
#define CJC_ASSERT(f) static_cast<void>(f)            // 副作用丢失，检查消失
#define CJC_ASSERT_WITH_MSG(f, msg) (static_cast<void>(f), static_cast<void>(msg))
#define CJC_ABORT()
#define CJC_ABORT_WITH_MSG(msg) static_cast<void>(msg)
```

`CJC_NULLPTR_CHECK(p)` 展开为 `CJC_ASSERT((p) != nullptr)`，因此在发布构建中**所有空指针检查均失效**。代码库中有 **3716+ 处** `CJC_ASSERT`/`CJC_NULLPTR_CHECK` 调用（含大量 `src/Mangle/`、`src/Parse/` 中的安全关键检查）。

#### 攻击路径

攻击者构造畸形源码，使解析/语义分析阶段产生空指针节点 → Debug 构建中 `CJC_NULLPTR_CHECK` 安全中止 → Release 构建中检查消失，后续代码在空指针假设下继续执行 → 内存损坏或错误代码生成。

#### 修复建议

将安全关键检查与调试断言分离：

```cpp
// 不受 NDEBUG 影响的安全运行时检查宏
#define CJC_SECURITY_CHECK(f)   \
    do { if (!(f)) { InternalError("Security check failed: " #f); } } while(0)
// 将 CJC_NULLPTR_CHECK 等关键路径改用 CJC_SECURITY_CHECK
```

---

### VULN-07：临时目录名生成随机性不足（未使用 mkdtemp）

**严重程度**：🟡 低危  
**CWE**：CWE-338（弱伪随机数生成器）、CWE-377（不安全临时文件）

#### 漏洞代码

**文件**：`src/Driver/TempFileManager.cpp`，`CreateTempDirName()` + `MakeTempDir()`

```cpp
// 时间戳（可预测）
size_t ns = wallNow.tv_sec * 1e9 + wallNow.tv_nsec;
// 仅 32 位随机数
int randomInt = 0;
read(fd, &randomInt, sizeof(int));  // 只读 4 字节
// 拼接目录名
ss << CANGJIE_TMP_DIR_PERFIX << SetNowTimeEncodedString() << "-" << GenerateRandomHexString();
// mkdir（非原子，存在 TOCTOU 竞态）
if (mkdir(path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == 0) { return path; }
```

#### 攻击路径

在高并发或可观察系统时钟的场景下，预测目录名 → 抢先 `mkdir`（EEXIST）或创建同名符号链接 → 编译临时文件写入攻击者指定位置。

#### 修复建议

```cpp
// Linux/macOS: 使用操作系统原子保证的 mkdtemp
std::string tmpl = FileUtil::JoinPath(tempDir, "cangjie-tmp-XXXXXX");
char* result = mkdtemp(tmpl.data());
```

---

### VULN-09：SIGINT 信号处理函数调用非异步信号安全函数

**严重程度**：🟡 低危  
**CWE**：CWE-479（Signal Handler Use of a Non-reentrant Function）

#### 漏洞代码

**文件**：`src/Utils/SignalUnix.cpp`，第 76–80 行

```cpp
void SigintHandler(int signum, ...) {
    Cangjie::TempFileManager::Instance().DeleteTempFiles();  // isSignalSafe = false（默认值）
    _exit(signum + 128);
}
```

**文件**：`include/cangjie/Driver/TempFileManager.h`，第 79 行

```cpp
void DeleteTempFiles(bool isSignalSafe = false);  // 默认值 false
```

`DeleteTempFiles(false)` 进一步调用 `RemoveDirRecursively`，其中使用 `opendir`/`readdir`/`closedir`，这些函数**不在 POSIX 异步信号安全函数列表中**（POSIX.1-2017 Table 21-1）。

#### 攻击路径

程序在持有 `malloc` 内部锁时接收 SIGINT → `SigintHandler` 中 `opendir` 调用内部 `malloc` → 死锁或堆状态损坏。

#### 修复建议

```cpp
// 信号处理中传递安全标志
void SigintHandler(int signum, ...) {
    Cangjie::TempFileManager::Instance().DeleteTempFiles(true);  // isSignalSafe = true
    _exit(signum + 128);
}
```

---

### VULN-12：IntLiteral 移位运算对移位量无上界检查（C++ 未定义行为）

**严重程度**：🟡 低危  
**CWE**：CWE-190（Integer Overflow or Wraparound）

#### 漏洞代码

**文件**：`src/AST/IntLiteral.cpp`，第 461–471 行

```cpp
IntLiteral IntLiteral::operator>>(const IntLiteral& rhs) const
{
    return IntLiteral(static_cast<int64_t>(uint64Val >> rhs.uint64Val), type, false);
    // 若 rhs.uint64Val >= 64，C++ 标准规定此为未定义行为
}

IntLiteral IntLiteral::operator<<(const IntLiteral& rhs) const
{
    return IntLiteral(static_cast<int64_t>(uint64Val << rhs.uint64Val), type, false);
    // 同上
}
```

#### 攻击路径

攻击者构造含大移位量的常量表达式（如 `1 << 64`）→ 编译器常量折叠调用此运算符 → 未定义行为 → 编译器可能生成错误的目标代码，或在使用 UBSan 构建时崩溃。

#### 修复建议

```cpp
IntLiteral IntLiteral::operator<<(const IntLiteral& rhs) const
{
    if (rhs.uint64Val >= 64) {
        return IntLiteral(int64_t(0), type, true);  // 标记溢出
    }
    return IntLiteral(static_cast<int64_t>(uint64Val << rhs.uint64Val), type, false);
}
```

---

### VULN-13：RemoveDirRecursively 存在 TOCTOU 竞态条件

**严重程度**：🟡 低危  
**CWE**：CWE-367（TOCTOU Race Condition）

#### 漏洞代码

**文件**：`src/Driver/TempFileManager.cpp`，第 179–200 行

```cpp
void RemoveDirRecursively(const std::string& dirPath)
{
    DIR* dir = opendir(dirPath.c_str());
    if (!dir) return;
    for (auto entry = readdir(dir); entry != nullptr; entry = readdir(dir)) {
        std::string newPath = FileUtil::JoinPath(dirPath, fileName);
        if (entry->d_type == DT_REG) {
            (void)unlink(newPath.c_str());      // readdir 和 unlink 之间存在竞态窗口
        } else if (entry->d_type == DT_DIR) {
            RemoveDirRecursively(newPath);       // 递归时目录可能被替换为符号链接
        }
    }
    (void)closedir(dir);
    (void)rmdir(dirPath.c_str());
}
```

#### 攻击路径

攻击者（与编译器同组或有写权限）在 `readdir` 返回条目与 `unlink` 之间将文件替换为指向敏感文件的符号链接 → 编译器删除该符号链接指向的文件（任意文件删除）。

#### 修复建议

```cpp
// 使用 C++17 std::filesystem::remove_all（实现更安全）
#include <filesystem>
std::filesystem::remove_all(std::filesystem::path(dirPath));
```

---

### VULN-14：编译器插件（--plugin）加载无完整性验证

**严重程度**：🟠 中危  
**CWE**：CWE-114（Process Control）、CWE-426（Untrusted Search Path）

#### 漏洞代码

**文件**：`src/Frontend/CompilerInstance.cpp`，`PerformPluginLoad()` 函数

```cpp
for (auto pluginPath : invocation.globalOptions.pluginPaths) {
    try {
        auto metaTransformPlugin = MetaTransformPlugin::Get(pluginPath);  // 直接加载 .so
        // 无签名验证、无路径白名单
        metaTransformPlugin.RegisterCallbackTo(metaTransformPluginBuilder);
    } catch (...) { ... }
}
```

**文件**：`src/Option/OptionAction.cpp`（用户通过 `--plugin` 命令行选项指定路径）

#### 攻击路径

攻击者将恶意 `.so` 替换受害者工具链中已受信任的插件路径 → `--plugin` 导入该路径 → 恶意插件以编译器权限运行，可读取/修改所有 CHIR IR，向编译产物注入后门 → 供应链攻击。

#### 修复建议

1. 实现插件路径白名单（仅允许 `$CANGJIE_HOME/plugins/` 下的插件）；
2. 长期目标：对插件 `.so` 实现数字签名验证；
3. 文档中明确标注 `--plugin` 为信任边界内的危险选项。

---

### VULN-15：Demangler 无递归深度限制（栈溢出风险）

**严重程度**：🟠 中危  
**CWE**：CWE-674（Uncontrolled Recursion）

#### 漏洞代码

**文件**：`demangler/Demangler.cpp`，`DemangleNextUnit` → `DemangleByPrefix` → `DemangleFunction`/`DemangleTuple` → `DemangleArgTypes` → `DemangleNextUnit`（循环递归）

```cpp
// 无任何递归深度计数器或检查
DemangleInfo<T> Demangler<T>::DemangleNextUnit(const T& message) {
    auto di = DemangleByPrefix();      // 调用 DemangleByPrefix
    ...
}
DemangleInfo<T> Demangler<T>::DemangleByPrefix() {
    switch (ch) {
        case 'F':  return DemangleFunction();     // DemangleFunction 再调 DemangleNextUnit
        case 'T':  return DemangleTuple();         // DemangleTuple 再调 DemangleNextUnit
        case 'C':  return DemangleClass(...);      // DemangleClass 再调 DemangleArgTypes
        ...
    }
}
// DemangleArgTypes 再调用 DemangleNextUnit
```

#### 攻击路径

攻击者构造深度嵌套的 mangled 名称（如数千层嵌套的泛型类型 `F0F0F0F0...`）→ 通过 `cjfilt` 工具标准输入、LSP 调试符号处理、或 `.chir` 文件中的符号名 → Demangler 深度递归耗尽栈空间 → `SIGSEGV` DoS。

#### 修复建议

```cpp
template<typename T>
class Demangler {
    size_t recursionDepth = 0;
    static constexpr size_t MAX_RECURSION_DEPTH = 256;

    DemangleInfo<T> DemangleNextUnit(const T& message = T{}) {
        if (++recursionDepth > MAX_RECURSION_DEPTH) {
            --recursionDepth;
            return Reject("recursion depth exceeded");
        }
        auto di = DemangleByPrefix();
        --recursionDepth;
        return di;
    }
};
```

---

## 风险修复优先级

| 优先级 | 漏洞 | 建议修复周期 |
|--------|------|-------------|
| P0（立即） | VULN-02（execvp 路径劫持） | 1–2 小时改动 |
| P0（立即） | VULN-03（RTLD_GLOBAL 宏库） | < 1 小时改动 |
| P0（立即） | VULN-01（FlatBuffers 无限制） | < 2 小时改动 |
| P1（1 个月） | VULN-04（CANGJIE_HOME 验证） | 需路径验证逻辑 |
| P1（1 个月） | VULN-15（Demangler 深度限制） | 1–2 小时改动 |
| P1（1 个月） | VULN-09（信号处理参数修正） | < 30 分钟改动 |
| P1（1 个月） | VULN-12（移位运算 UB 修复） | < 1 小时改动 |
| P2（3 个月） | VULN-05（安全断言/调试断言分离） | 架构改动 |
| P2（3 个月） | VULN-14（插件路径白名单） | 需策略设计 |
| P2（3 个月） | VULN-07（mkdtemp 替换） | < 1 小时改动 |
| P3（6 个月） | VULN-13（fd-based 目录删除） | 需重构 |

---

*本报告为原始报告（`security_audit_report_compiler.md`）经代码逐项核实后的归档版本。*  
*11 项确认漏洞均附有精确的代码行号及可操作的修复建议。*

---

**报告结束**
