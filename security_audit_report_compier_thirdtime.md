# 仓颉编译器安全漏洞扫描报告（第三轮）

> **扫描基准分支**：`main`（commit `d3c13b9`）  
> **扫描范围**：全源码目录系统性安全审计  
> **裁决标准**：攻击手段须具备实际可行性——存在可构造的恶意输入或环境条件触发漏洞  
> **源码链接基址**：`https://github.com/wyq0721/cangjie_compiler/blob/main/`

---

## 扫描重点方向

本次审计针对以下安全风险方向进行系统性扫描：

| 方向 | 关注点 |
|------|--------|
| **输入验证** | 词法/语法解析边界、数值溢出、Unicode 处理 |
| **解析逻辑** | 递归深度限制、DoS 触发路径、嵌套处理 |
| **边界检查** | 数组越界、整数溢出、缓冲区安全 |
| **格式化与序列化** | FlatBuffers 反序列化、缓存文件完整性 |
| **动态加载** | 共享库加载验证、PATH 查找安全 |
| **文件操作** | TOCTOU 竞态、路径遍历、临时文件安全 |
| **环境变量** | 未校验的信任传递、路径注入 |
| **代码生成** | 整数溢出、偏移计算安全 |

---

## 发现结果总览

**共发现 20 个安全问题**，按严重程度分布如下：

| 严重级别 | 数量 |
|---------|------|
| 🔴 高 | 5 |
| 🟠 中 | 9 |
| 🟡 低 | 6 |

---

## 📂 `src/Lex/` — 词法分析器

### SCAN-01：字符串插值递归无深度限制（🔴 高）

- **文件**：[`src/Lex/Lexer.cpp` 第 1164–1195 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Lex/Lexer.cpp#L1164-L1195)
- **CWE**：CWE-674（不受控递归）

**问题描述**：`ScanInterpolationStringLiteralHoleBalancedText()` 在遇到 `{` 时递归调用自身（第 1177 行），无任何深度计数器。

```cpp
bool LexerImpl::ScanInterpolationStringLiteralHoleBalancedText(const char* pStart, char endingChar, bool allowNewline)
{
    for (;;) {
        ReadUTF8Char();
        // ...
        if (currentChar == '{') {
            if (!ScanInterpolationStringLiteralHoleBalancedText(pStart, '}', allowNewline)) {  // ← 递归无限制
                return false;
            }
        }
    }
}
```

**攻击路径**：构造含深层嵌套花括号的 `.cj` 源文件：
```cangjie
let x = "${${${${${${...}}}}}}"   // 嵌套 10000+ 层
```

**影响**：栈溢出导致编译器崩溃，CI/CD 服务器拒绝服务

---

### SCAN-02：数字字面量处理无长度限制（🟡 低）

- **文件**：[`src/Lex/Lexer.cpp` 第 386–420 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Lex/Lexer.cpp#L386-L420)
- **CWE**：CWE-400（不受控的资源消耗）

**问题描述**：`ProcessDigits()` 中的循环变量 `i` 无上限，处理超长数字序列时持续消耗 CPU。

```cpp
bool LexerImpl::ProcessDigits(const int& base, bool& hasDigit, const char* reasonPoint, bool* isFloat)
{
    for (int i{0}; ; ++i) {   // ← 无上限
        // 逐字符处理数字
        ReadUTF8Char();
    }
}
```

**攻击路径**：构造含超长数字字面量的源文件（如 10MB 的连续数字 `111...1`）。

**影响**：编译器耗时大幅增加，CPU 资源耗尽

---

## 📂 `src/Parse/` — 语法解析器

### SCAN-03：表达式解析递归无深度限制（🔴 高）

- **文件**：[`src/Parse/ParseExpr.cpp` 第 372–413 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Parse/ParseExpr.cpp#L372-L413)（`ParseExpr` 入口），[第 499–551 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Parse/ParseExpr.cpp#L499-L551)（二元表达式递归），[第 570–603 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Parse/ParseExpr.cpp#L570-L603)（`ParseExprWithRightExprOrType`）
- **CWE**：CWE-674（不受控递归）

**问题描述**：`ParseExpr()` → `ParseExpr(tok)` → `ParseExprWithRightExprOrType()` → `ParseExpr(tok)` 构成递归调用链。`ParserImpl` 类中未定义任何递归深度计数器（已确认 `include/cangjie/Parse/Parser.h` 中无相关字段）。

```cpp
OwnedPtr<Expr> ParserImpl::ParseExpr(ExprKind ek)
{
    // ...
    ret = ParseExpr(Token{TokenKind::DOT}, nullptr, ek);  // ← 递归入口
}

void ParserImpl::ParseExprWithRightExprOrType(OwnedPtr<Expr>& base, const Token& tok, ExprKind ek)
{
    auto rExpr = ParseExpr(tok, nullptr, ek);  // ← 递归回调
}
```

**攻击路径**：构造深层嵌套括号或运算符链：
```cangjie
let x = (((((((((((((((((((((((1)))))))))))))))))))))))   // 10000+ 层
let y = 1 + 1 + 1 + 1 + 1 + ...  // 超长运算符链
```

**影响**：栈溢出导致编译器崩溃

---

### SCAN-04：类型解析递归无深度限制（🟠 中）

- **文件**：[`src/Parse/ParseType.cpp` 第 297–310 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Parse/ParseType.cpp#L297-L310)（`ParseType` 入口），[第 71 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Parse/ParseType.cpp#L71)（VArray 类型递归），[第 330–360 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Parse/ParseType.cpp#L330-L360)（泛型参数递归）
- **CWE**：CWE-674（不受控递归）

**问题描述**：`ParseType()` → `ParsePrefixType()` → `ParseBaseType()` → `ParseVarrayType()` → `ParseType()` 形成递归环。泛型参数中的 `ParseTypeArguments()` 也会回调 `ParseType()`，均无深度限制。

```cpp
OwnedPtr<AST::Type> ParserImpl::ParseVarrayType()
{
    ret->typeArgument = ParseType();  // ← 递归回调
}
```

**攻击路径**：
```cangjie
let x: VArray<VArray<VArray<...<Int64, 1>, 1>, 1>, 1>  // 深层嵌套泛型
```

**影响**：类型解析阶段栈溢出

---

## 📂 `src/AST/` — 抽象语法树

### SCAN-05：移位操作未校验移位量（🟠 中）

- **文件**：[`src/AST/IntLiteral.cpp` 第 461–471 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/AST/IntLiteral.cpp#L461-L471)
- **CWE**：CWE-190（整数溢出/回绕）

**问题描述**：左移和右移操作在 `rhs.uint64Val >= 64` 时属于 C++ 未定义行为（UB），但代码未做任何边界校验。

```cpp
IntLiteral IntLiteral::operator>>(const IntLiteral& rhs) const
{
    return IntLiteral(static_cast<int64_t>(uint64Val >> rhs.uint64Val), type, false);  // ← UB: rhs >= 64
}

IntLiteral IntLiteral::operator<<(const IntLiteral& rhs) const
{
    return IntLiteral(static_cast<int64_t>(uint64Val << rhs.uint64Val), type, false);  // ← UB: rhs >= 64
}
```

**攻击路径**：
```cangjie
let x: Int64 = 1 << 200   // 编译时常量折叠触发 UB
let y: Int64 = 100 >> 128  // 结果不可预测
```

**影响**：编译器生成错误的常量值，安全相关常量可能被静默篡改

---

### SCAN-06：无符号整数除零未保护（🟠 中）

- **文件**：[`src/AST/IntLiteral.cpp` 第 418–440 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/AST/IntLiteral.cpp#L418-L440)
- **CWE**：CWE-369（除零错误）

**问题描述**：当两个操作数均为正（`sign + rhs.sign > 0`），`operator/` 直接执行 `uint64Val / rhs.uint64Val`，未检查 `rhs.uint64Val == 0`。

```cpp
IntLiteral IntLiteral::operator/(const IntLiteral& rhs) const
{
    if (sign + rhs.sign > 0) {
        return IntLiteral(uint64Val / rhs.uint64Val, type, false);  // ← 未检查 rhs == 0
    }
    // 后续路径有零检查...
}
```

**攻击路径**：
```cangjie
let x: UInt64 = 100 / 0   // 无符号除零 → C++ UB
```

**影响**：编译器崩溃（SIGFPE 或 UB 导致不可预测行为）

---

### SCAN-07：无符号整数取模零未保护（🟠 中）

- **文件**：[`src/AST/IntLiteral.cpp` 第 442–459 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/AST/IntLiteral.cpp#L442-L459)
- **CWE**：CWE-369（除零错误）

**问题描述**：与 SCAN-06 相同模式，`operator%` 在 `sign + rhs.sign > 0` 路径下未检查 `rhs.uint64Val == 0`。

```cpp
IntLiteral IntLiteral::operator%(const IntLiteral& rhs) const
{
    if (sign + rhs.sign > 0) {
        return IntLiteral(uint64Val % rhs.uint64Val, type, false);  // ← 未检查 rhs == 0
    }
}
```

**攻击路径**：同 SCAN-06，使用 `%` 替代 `/`。

---

### SCAN-08：AST 遍历器无递归深度限制（🟡 低）

- **文件**：[`src/AST/Walker.cpp` 第 39–150 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/AST/Walker.cpp#L39-L150)
- **CWE**：CWE-674（不受控递归）

**问题描述**：`Walker::Walk()` 使用 `visitedByWalkerID` 实现了环检测（防止有向无环图中的重复访问），但对树状深层嵌套结构无递归深度保护。

**攻击路径**：通过深层嵌套声明（如数千层嵌套的 struct）使 AST 树足够深，在遍历阶段触发栈溢出。

**影响**：编译器崩溃（需与解析器无深度限制配合利用）

---

## 📂 `src/Sema/` — 语义分析

### SCAN-09：类型实例化递归无深度限制（🟠 中）

- **文件**：[`src/Sema/TypeManager.cpp` 第 188–362 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Sema/TypeManager.cpp#L188-L362)
- **CWE**：CWE-674（不受控递归）

**问题描述**：`TyInstantiator::Instantiate()` 根据类型种类递归调用自身（struct → 成员类型 → 泛型参数 → struct…），无深度限制。

**攻击路径**：
```cangjie
struct Node<T> {
    value: T
    next: Node<Node<T>>   // 递归泛型导致指数级实例化
}
var x: Node<Int64>
```

**影响**：编译阶段栈溢出或内存耗尽

---

### SCAN-10：类型参数推断越界访问（🟡 低）

- **文件**：[`src/Sema/TypeArgumentInference.cpp` 第 272–289 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Sema/TypeArgumentInference.cpp#L272-L289)
- **CWE**：CWE-125（越界读取）

**问题描述**：使用 `typeArgs[i]` 直接索引，其中 `i` 来源于对 `typeParameters` 的遍历，假设 `typeArgs.size() >= typeParameters.size()`，但未显式验证。

**攻击路径**：通过不完整的泛型参数列表触发（需语义分析前置阶段未能过滤此情况）。

**影响**：越界内存读取，编译器崩溃

---

## 📂 `src/CHIR/Serializer/` — CHIR 序列化

### SCAN-11：FlatBuffers 验证器深度/表数量限制被禁用（🔴 高）

- **文件**：[`src/CHIR/Serializer/CHIRDeserializer.cpp` 第 55–58 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/CHIR/Serializer/CHIRDeserializer.cpp#L55-L58)
- **CWE**：CWE-770（无限制资源分配）

**问题描述**：
```cpp
flatbuffers::Verifier::Options options;
options.max_depth = std::numeric_limits<::flatbuffers::uoffset_t>::max();   // ← 禁用深度限制
options.max_tables = std::numeric_limits<::flatbuffers::uoffset_t>::max();  // ← 禁用表数量限制
```

**攻击路径**：提供恶意构造的 `.chir` 文件（通过 `--import-chir` 或增量编译缓存替换），利用深层嵌套或海量表对象触发栈溢出/内存耗尽。

**影响**：DoS、栈溢出、堆内存耗尽

---

## 📂 `src/Macro/` — 宏处理

### SCAN-12：宏服务器通过 PATH 查找启动（🔴 高）

- **文件**：[`src/Macro/MacroEvaluationClient.cpp` 第 37 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Macro/MacroEvaluationClient.cpp#L37)（常量定义），[第 497–509 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Macro/MacroEvaluationClient.cpp#L497-L509)（`execvp` 调用）
- **CWE**：CWE-426（不受信任的搜索路径）

**问题描述**：
```cpp
const std::string MACRO_SRV_NAME = "LSPMacroServer";
// ...
execvp(macSrvName.c_str(), cstrings.data());  // ← PATH 查找，可劫持
```

**攻击路径**：攻击者在编译目录或 `PATH` 靠前路径中放置恶意 `LSPMacroServer` 可执行文件，编译含宏代码时自动执行。

**影响**：任意代码执行

---

### SCAN-13：宏库以 RTLD_GLOBAL 加载（🔴 高）

- **文件**：[`include/cangjie/Macro/InvokeUtil.h` 第 80 行](https://github.com/wyq0721/cangjie_compiler/blob/main/include/cangjie/Macro/InvokeUtil.h#L80)（默认参数），[`src/Macro/InvokeUtil.cpp` 第 34–50 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Macro/InvokeUtil.cpp#L34-L50)（`dlopen` 调用），[`src/Macro/MacroEvaluationCJNative.cpp` 第 101 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Macro/MacroEvaluationCJNative.cpp#L101)（调用点）
- **CWE**：CWE-114（进程控制）

**问题描述**：
```cpp
HANDLE OpenSymbolTable(const std::string& libPath, int dlopenMode = RTLD_LAZY | RTLD_GLOBAL);
```

宏库默认以 `RTLD_GLOBAL` 加载，其导出符号进入全局命名空间，可覆盖编译器自身函数。

**攻击路径**：恶意宏库定义 `malloc`/`free` 等同名函数，在编译流程中劫持编译器自身的内存管理。

**影响**：编译器进程内任意代码执行

---

### SCAN-14：宏消息反序列化缺少空指针和大小校验（🟠 中）

- **文件**：[`src/Macro/MacroEvalMsgSerializer.cpp` 第 231–262 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Macro/MacroEvalMsgSerializer.cpp#L231-L262)
- **CWE**：CWE-20（输入验证不当）

**问题描述**：`DeserializeItemsFromItemsBuf()` 直接使用 FlatBuffers 消息大小进行 `resize()`，无上限检查；对 `key()` 等指针无空值校验。

```cpp
static void DeserializeItemsFromItemsBuf(...)
{
    uoffset_t num = itemsBuf.size();
    items.resize(num);          // ← 无大小限制
    for (uoffset_t i = 0; i < num; i++) {
        items[i].key = itemsBuf.Get(i)->key()->str();  // ← 可能返回 nullptr
    }
}
```

**攻击路径**：宏服务器进程与编译器进程通过管道通信时，恶意宏服务器可发送畸形 FlatBuffers 消息。

**影响**：空指针解引用崩溃、内存耗尽

---

## 📂 `src/Driver/` — 编译驱动

### SCAN-15：`CANGJIE_HOME` 环境变量未校验（🟠 中）

- **文件**：[`src/Option/Option.cpp` 第 1173–1174 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Option/Option.cpp#L1173-L1174)（读取环境变量），[`src/Driver/Backend/CJNATIVEBackend.cpp` 第 75–81 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Driver/Backend/CJNATIVEBackend.cpp#L75-L81)（构建搜索路径）
- **CWE**：CWE-426（不受信任的搜索路径）

**问题描述**：`CANGJIE_HOME` 环境变量无条件信任，用于查找 `opt`、`llc` 等后端工具。

**攻击路径**：在共享构建环境中设置 `CANGJIE_HOME` 指向含恶意工具的目录。

**影响**：通过替换后端工具在编译产物中注入后门

---

### SCAN-16：`RemoveDirRecursively` TOCTOU 竞态（🟡 低）

- **文件**：[`src/Driver/TempFileManager.cpp` 第 179–200 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Driver/TempFileManager.cpp#L179-L200)（递归删除），[第 450–461 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Driver/TempFileManager.cpp#L450-L461)（stat/unlink 竞态）
- **CWE**：CWE-367（TOCTOU 竞态）

**问题描述**：`stat()` 检查文件类型后，`unlink()` 执行删除，时间窗口内可被符号链接替换。

```cpp
struct stat buf;
if (stat(filePath, &buf) != 0) { continue; }
if (S_ISREG(buf.st_mode)) {
    (void)unlink(filePath);     // ← stat 与 unlink 之间可替换为符号链接
}
```

**攻击路径**：需本地同机权限，在 stat-unlink 窗口内替换路径为符号链接，可删除目标文件。

**影响**：同机环境下的任意文件删除

---

### SCAN-17：`LD_LIBRARY_PATH` 拼接未校验（🟠 中）

- **文件**：[`src/Driver/Tool.cpp` 第 207–215 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Driver/Tool.cpp#L207-L215)
- **CWE**：CWE-426（不受信任的搜索路径）

**问题描述**：环境变量中的 `LD_LIBRARY_PATH` 值被直接拼接入子进程环境，未做路径合法性检查。

```cpp
std::string newLdLibraryPath = ldLibraryPath;
if (environmentVars.find(LD_LIBRARY_PATH) != environmentVars.end()) {
    newLdLibraryPath += ":" + environmentVars.at(LD_LIBRARY_PATH);  // ← 未校验
}
```

**攻击路径**：攻击者控制环境变量，注入恶意库路径，编译器调用链接器/汇编器时加载攻击者提供的共享库。

**影响**：通过子进程库劫持实现代码执行

---

## 📂 `src/Frontend/` — 编译器前端

### SCAN-18：`--plugin` 加载无签名/路径白名单验证（🟠 中）

- **文件**：[`src/Frontend/CompilerInstance.cpp` 第 258–282 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Frontend/CompilerInstance.cpp#L258-L282)（`MetaTransformPlugin::Get`），[第 291–304 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Frontend/CompilerInstance.cpp#L291-L304)（`PerformPluginLoad`）
- **CWE**：CWE-114（进程控制）

**问题描述**：插件通过 `--plugin` 选项指定任意路径加载，仅检查 `getMetaTransformPluginInfo` 符号是否存在，无签名验证或路径白名单。

```cpp
MetaTransformPlugin MetaTransformPlugin::Get(const std::string& path)
{
    handle = InvokeRuntime::OpenSymbolTable(path, RTLD_NOW | RTLD_LOCAL);
    // 无签名检查，无路径白名单
    void* fPtr = InvokeRuntime::GetMethod(handle, "getMetaTransformPluginInfo");
}
```

**攻击路径**：CI/CD 构建脚本中从不可信源获取插件路径时，可替换为恶意 `.so`。

**影响**：编译流程中的任意代码执行

---

## 📂 `demangler/` — 符号解码器

### SCAN-19：Demangler 递归无深度限制（🟠 中）

- **文件**：[`demangler/Demangler.cpp` 第 1412–1418 行](https://github.com/wyq0721/cangjie_compiler/blob/main/demangler/Demangler.cpp#L1412-L1418)（`DemangleNextUnit`），[第 1422–1475 行](https://github.com/wyq0721/cangjie_compiler/blob/main/demangler/Demangler.cpp#L1422-L1475)（`DemangleByPrefix`），[第 1097–1108 行](https://github.com/wyq0721/cangjie_compiler/blob/main/demangler/Demangler.cpp#L1097-L1108)（函数类型递归）
- **CWE**：CWE-674（不受控递归）

**问题描述**：递归调用链 `DemangleNextUnit` → `DemangleByPrefix` → `DemangleFunction`/`DemangleTuple` → `DemangleArgTypes` → `DemangleNextUnit` 无深度计数器。

**攻击路径**：向 `cjfilt` 工具提供深层嵌套的 mangled 名称：
```
TTTTTTTTT...T（10000+ 个 T 前缀表示嵌套元组类型）
```

**影响**：`cjfilt` 崩溃，处理不可信二进制文件时的 DoS

---

## 📂 `src/Modules/ASTSerialization/` — 模块序列化

### SCAN-20：反序列化索引越界访问（🟠 中）

- **文件**：[`src/Modules/ASTSerialization/ReferenceLoader.cpp` 第 277–290 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Modules/ASTSerialization/ReferenceLoader.cpp#L277-L290)
- **CWE**：CWE-125（越界读取）

**问题描述**：`LoadType()` 使用来自 `.cjo` 文件的 `index` 直接访问 `allTypes[index]`，无边界校验。

```cpp
auto index = type - 1;
if (auto ty = allTypes[index]; ty) {    // ← 无 bounds check
    return ty;
}
// ...
allTypes[index] = TypeManager::GetInvalidTy();  // ← 越界写入
```

**攻击路径**：构造恶意 `.cjo` 模块缓存文件（如替换增量编译缓存），使 `type` 字段包含超出 `allTypes` 大小的值。

**影响**：越界内存读写，编译器崩溃，潜在的代码执行

---

## 📂 `src/Mangle/` — 名称修饰

### SCAN-21（附加）：`ForwardIdentifier` 索引越界读取

- **文件**：[`src/Mangle/Compression.cpp` 第 237–241 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Mangle/Compression.cpp#L237-L241)
- **CWE**：CWE-125（越界读取）

**问题描述**：循环条件检查 `idx < mangled.size()`，但实际读取 `mangled[idx + numberLen]`，当 `idx + numberLen >= mangled.size()` 时越界。

```cpp
while (idx < mangled.size() && isdigit(mangled[idx + numberLen])) {
    numberLen++;   // ← idx + numberLen 可超出 mangled.size()
}
```

**攻击路径**：处理特殊构造的 mangled 名称时触发。

**影响**：越界内存读取

---

## 📂 `src/ConditionalCompilation/` — 条件编译

### SCAN-22（附加）：条件表达式求值递归无深度限制

- **文件**：[`src/ConditionalCompilation/ConditionalCompilation.cpp` 第 408–425 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/ConditionalCompilation/ConditionalCompilation.cpp#L408-L425)（`EvalConditionExpr`），[第 242–248 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/ConditionalCompilation/ConditionalCompilation.cpp#L242-L248)（`EvalLogicBinaryExpr`），[第 363–369 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/ConditionalCompilation/ConditionalCompilation.cpp#L363-L369)（`EvalParenExpr`）
- **CWE**：CWE-674（不受控递归）

**问题描述**：`EvalConditionExpr` → `EvalBinaryExpr`/`EvalParenExpr` → `EvalConditionExpr` 递归链无深度限制。

```cpp
bool ConditionalCompilationImpl::EvalParenExpr(const ParenExpr& pe)
{
    return EvalConditionExpr(pe.expr.operator*());  // ← 递归回调
}
```

**攻击路径**：
```cangjie
@when(((((((((((...true...))))))))))  // 深层嵌套括号
```

**影响**：编译阶段栈溢出（需先通过解析器，但解析器也无深度限制）

---

## 📂 `src/CodeGen/` — 代码生成

### SCAN-23（附加）：数组偏移乘法无溢出检查

- **文件**：[`src/CodeGen/CJNative/CJNativeIntrinsicsCall.cpp` 第 250 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/CodeGen/CJNative/CJNativeIntrinsicsCall.cpp#L250)（`CallArrayIntrinsicSet`），[`src/CodeGen/Base/IntrinsicsDispatcher.cpp` 第 278 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/CodeGen/Base/IntrinsicsDispatcher.cpp#L278)（`CallArrayIntrinsicCopyTo`）
- **CWE**：CWE-190（整数溢出）

**问题描述**：LLVM IR 层的 `CreateMul(elementSize, index)` 使用默认 wrapping 乘法（无溢出检测），`elementSize * index` 溢出后产生小偏移值。

```cpp
llvm::Value* offset = CreateMul(GetSize_64(*arrTy.GetElementType()), index);  // ← 无溢出检查
auto dataSize = irBuilder.CreateMul(copyLen, typeSize, "arr.data.len");       // ← 同样问题
```

**攻击路径**：此类溢出在运行时发生（非编译时），但编译器生成的目标代码缺少溢出保护，可能被恶意程序利用。

**影响**：生成的目标代码存在整数溢出漏洞（属于编译器安全性范畴）

---

## 📂 `src/IncrementalCompilation/` — 增量编译

### SCAN-24（附加）：缓存反序列化无完整性校验

- **文件**：[`src/IncrementalCompilation/CachedDataSerialization.cpp` 第 350–390 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/IncrementalCompilation/CachedDataSerialization.cpp#L350-L390)
- **CWE**：CWE-345（数据真实性验证不足）

**问题描述**：增量编译缓存文件仅校验版本号（第 355 行），无哈希/签名验证。反序列化后直接使用各字段值。

```cpp
if (package->version()->str() != CANGJIE_VERSION) {
    return {false, {}};   // ← 仅版本检查
}
// 后续直接使用反序列化数据，无完整性校验
cached.compileArgs.emplace_back(arg->str());
cached.specs = package->specs();
```

**攻击路径**：攻击者篡改缓存文件（版本号保持匹配），注入恶意编译参数或修改 AST 数据。

**影响**：缓存投毒导致编译产物被篡改

---

## 问题汇总表

| 编号 | 严重级别 | 源码目录 | 文件 | CWE | 类型 |
|------|---------|---------|------|-----|------|
| [SCAN-01](#scan-01字符串插值递归无深度限制-高) | 🔴 高 | `src/Lex/` | [`Lexer.cpp:1164`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Lex/Lexer.cpp#L1164) | CWE-674 | 栈溢出 |
| [SCAN-02](#scan-02数字字面量处理无长度限制-低) | 🟡 低 | `src/Lex/` | [`Lexer.cpp:386`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Lex/Lexer.cpp#L386) | CWE-400 | 资源耗尽 |
| [SCAN-03](#scan-03表达式解析递归无深度限制-高) | 🔴 高 | `src/Parse/` | [`ParseExpr.cpp:372`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Parse/ParseExpr.cpp#L372) | CWE-674 | 栈溢出 |
| [SCAN-04](#scan-04类型解析递归无深度限制-中) | 🟠 中 | `src/Parse/` | [`ParseType.cpp:297`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Parse/ParseType.cpp#L297) | CWE-674 | 栈溢出 |
| [SCAN-05](#scan-05移位操作未校验移位量-中) | 🟠 中 | `src/AST/` | [`IntLiteral.cpp:461`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/AST/IntLiteral.cpp#L461) | CWE-190 | 未定义行为 |
| [SCAN-06](#scan-06无符号整数除零未保护-中) | 🟠 中 | `src/AST/` | [`IntLiteral.cpp:418`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/AST/IntLiteral.cpp#L418) | CWE-369 | 除零崩溃 |
| [SCAN-07](#scan-07无符号整数取模零未保护-中) | 🟡 低 | `src/AST/` | [`IntLiteral.cpp:442`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/AST/IntLiteral.cpp#L442) | CWE-369 | 除零崩溃 |
| [SCAN-08](#scan-08ast-遍历器无递归深度限制-低) | 🟡 低 | `src/AST/` | [`Walker.cpp:39`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/AST/Walker.cpp#L39) | CWE-674 | 栈溢出 |
| [SCAN-09](#scan-09类型实例化递归无深度限制-中) | 🟠 中 | `src/Sema/` | [`TypeManager.cpp:188`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Sema/TypeManager.cpp#L188) | CWE-674 | 栈溢出 |
| [SCAN-10](#scan-10类型参数推断越界访问-低) | 🟡 低 | `src/Sema/` | [`TypeArgumentInference.cpp:272`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Sema/TypeArgumentInference.cpp#L272) | CWE-125 | 越界读取 |
| [SCAN-11](#scan-11flatbuffers-验证器深度表数量限制被禁用-高) | 🔴 高 | `src/CHIR/Serializer/` | [`CHIRDeserializer.cpp:55`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/CHIR/Serializer/CHIRDeserializer.cpp#L55) | CWE-770 | DoS/溢出 |
| [SCAN-12](#scan-12宏服务器通过-path-查找启动-高) | 🔴 高 | `src/Macro/` | [`MacroEvaluationClient.cpp:509`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Macro/MacroEvaluationClient.cpp#L509) | CWE-426 | 代码执行 |
| [SCAN-13](#scan-13宏库以-rtld_global-加载-高) | 🔴 高 | `src/Macro/` + `include/` | [`InvokeUtil.h:80`](https://github.com/wyq0721/cangjie_compiler/blob/main/include/cangjie/Macro/InvokeUtil.h#L80) | CWE-114 | 符号劫持 |
| [SCAN-14](#scan-14宏消息反序列化缺少空指针和大小校验-中) | 🟠 中 | `src/Macro/` | [`MacroEvalMsgSerializer.cpp:231`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Macro/MacroEvalMsgSerializer.cpp#L231) | CWE-20 | 崩溃/DoS |
| [SCAN-15](#scan-15cangjie_home-环境变量未校验-中) | 🟠 中 | `src/Option/` + `src/Driver/` | [`Option.cpp:1173`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Option/Option.cpp#L1173) | CWE-426 | 路径劫持 |
| [SCAN-16](#scan-16removedirrecursively-toctou-竞态-低) | 🟡 低 | `src/Driver/` | [`TempFileManager.cpp:450`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Driver/TempFileManager.cpp#L450) | CWE-367 | 文件删除 |
| [SCAN-17](#scan-17ld_library_path-拼接未校验-中) | 🟠 中 | `src/Driver/` | [`Tool.cpp:207`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Driver/Tool.cpp#L207) | CWE-426 | 库劫持 |
| [SCAN-18](#scan-18--plugin-加载无签名路径白名单验证-中) | 🟠 中 | `src/Frontend/` | [`CompilerInstance.cpp:258`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Frontend/CompilerInstance.cpp#L258) | CWE-114 | 代码执行 |
| [SCAN-19](#scan-19demangler-递归无深度限制-中) | 🟠 中 | `demangler/` | [`Demangler.cpp:1412`](https://github.com/wyq0721/cangjie_compiler/blob/main/demangler/Demangler.cpp#L1412) | CWE-674 | 栈溢出 |
| [SCAN-20](#scan-20反序列化索引越界访问-中) | 🟠 中 | `src/Modules/` | [`ReferenceLoader.cpp:277`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Modules/ASTSerialization/ReferenceLoader.cpp#L277) | CWE-125 | 越界读写 |

---

## 修复优先级建议

| 优先级 | 编号 | 建议修复方案 |
|--------|------|------------|
| **P0** | SCAN-12 | 使用 `executablePath` 推导宏服务器绝对路径，改用 `execv` |
| **P0** | SCAN-13 | 宏库加载时显式传入 `RTLD_NOW \| RTLD_LOCAL` |
| **P0** | SCAN-03 | 在 `ParserImpl` 中添加 `recursionDepth` 计数器，限制 ≤ 1024 |
| **P0** | SCAN-01 | 在 `ScanInterpolationStringLiteralHoleBalancedText` 中添加嵌套深度限制 |
| **P1** | SCAN-11 | 为 `max_depth` 和 `max_tables` 设置合理上限（如 128 / 10000000） |
| **P1** | SCAN-06 | 在 `operator/` 的 `sign + rhs.sign > 0` 路径添加 `rhs.uint64Val != 0` 检查 |
| **P1** | SCAN-05 | 在移位操作前添加 `rhs.uint64Val >= 64` 检查 |
| **P1** | SCAN-15 | 对 `CANGJIE_HOME` 进行路径合法性验证 |
| **P2** | SCAN-04 | 类型解析添加递归深度限制 |
| **P2** | SCAN-09 | 类型实例化添加递归深度限制 |
| **P2** | SCAN-18 | 插件添加路径白名单或签名验证 |
| **P2** | SCAN-19 | Demangler 添加递归深度计数器 |
| **P2** | SCAN-20 | 反序列化索引添加边界校验 |
| **P2** | SCAN-17 | `LD_LIBRARY_PATH` 拼接添加路径合法性检查 |
| **P3** | SCAN-14 | 宏消息反序列化添加大小限制和空指针校验 |
| **P3** | SCAN-16 | 使用 `openat`/`unlinkat` 替代 path-based 操作 |
| **P3** | SCAN-07 | 同 SCAN-06 修复模式 |
| **P3** | SCAN-08 | AST Walker 添加深度限制 |
| **P3** | SCAN-10 | 添加 `typeArgs.size() >= typeParameters.size()` 前置校验 |
| **P3** | SCAN-02 | 数字字面量添加最大长度限制 |
