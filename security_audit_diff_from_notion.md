# 补充发现：Gist 报告未覆盖的安全问题

> **Gist 报告**：https://gist.github.com/wyq0721/1043366fb37f56268bbb820a50c010bc （41 项：7 高、24 中、10 低）  
> **对比来源**：本 PR 中 `security_audit_report_compier_recheck.md` + `security_audit_report_compier_thirdtime.md`  
> **提取标准**：仅列出 Gist 报告中**完全未提及**或**遗漏的独立攻击面**

---

## 补充发现总览

| 风险等级 | 数量 |
|---------|------|
| 🔴 高风险 | 1 |
| 🟡 中风险 | 8 |
| 🟢 低风险 | 4 |
| ⚠️ 裁决分歧 | 1 |
| **总计** | **14** |

---

## 第一部分：高风险补充

---

### EXTRA-HIGH-01：宏 `.so` 库使用 `RTLD_GLOBAL` 加载导致全局符号劫持

| 属性 | 内容 |
| --- | --- |
| **来源** | 独立发现（Gist MED-04 仅覆盖 execvp PATH，未涉及 dlopen 标志） |
| **CWE** | CWE-114 (Process Control) |
| **文件** | `include/cangjie/Macro/InvokeUtil.h:80`、`src/Macro/MacroEvaluationCJNative.cpp:101`、`src/Macro/MacroCallResolve.cpp:199`、`src/Macro/MacroEvaluationSrv.cpp:186` |

**漏洞代码：**

```cpp
// include/cangjie/Macro/InvokeUtil.h 第 80 行 — 默认参数定义
HANDLE OpenSymbolTable(const std::string& libPath, int dlopenMode = RTLD_LAZY | RTLD_GLOBAL);

// src/Macro/MacroEvaluationCJNative.cpp 第 101 行 — 调用（使用默认参数）
auto handle = InvokeRuntime::OpenSymbolTable(dyfile);
// 同样模式出现在 MacroCallResolve.cpp:199、MacroEvaluationSrv.cpp:186
```

**触发方式：** 使用 `--macro-lib` 选项加载恶意宏库。

**攻击路径：** 宏库以 `RTLD_GLOBAL` 打开后，其导出的**全部符号**进入编译器全局符号命名空间。恶意宏库可定义与标准库同名的函数（如 `malloc`、`free`、`memcpy`），在后续编译流程中劫持编译器自身的函数调用。此攻击在 CI/CD 环境中尤其危险——攻击者可通过提交含恶意宏库的代码仓库，在构建服务器上劫持编译器进程。

**与 Gist 的区别：** Gist MED-04 关注的是 `execvp` 按 PATH 搜索宏服务器可执行文件的劫持风险，而 `RTLD_GLOBAL` 是完全不同的攻击面——即使宏库路径完全正确，其导出符号也能覆盖编译器自身函数。对比同文件的 `--plugin` 加载使用了 `RTLD_LOCAL`，说明开发团队了解两者区别但在宏库路径遗漏了。

**根本原因：** `OpenSymbolTable` 默认参数使用 `RTLD_GLOBAL` 而非 `RTLD_LOCAL`。

**影响：** 编译器进程内任意代码执行（通过符号覆盖）。

**修复建议：** 将默认参数改为 `RTLD_LAZY | RTLD_LOCAL`；对确需 `RTLD_GLOBAL` 的场景添加显式注释说明。

---

## 第二部分：中风险补充

---

### EXTRA-MED-01：`IntLiteral` 移位操作未检查移位量 ≥ 64（C++ UB）

| 属性 | 内容 |
| --- | --- |
| **来源** | 独立发现（Gist MED-11 覆盖 strict aliasing UB，但未涉及移位 UB） |
| **CWE** | CWE-190 (Integer Overflow or Wraparound) |
| **文件** | `src/AST/IntLiteral.cpp:461-471` |

**漏洞代码：**

```cpp
// src/AST/IntLiteral.cpp 第 461-471 行
IntLiteral IntLiteral::operator>>(const IntLiteral& rhs) const
{
    return IntLiteral(static_cast<int64_t>(uint64Val >> rhs.uint64Val), type, false);
    // ← rhs.uint64Val >= 64 时为 C++ 未定义行为
}

IntLiteral IntLiteral::operator<<(const IntLiteral& rhs) const
{
    return IntLiteral(static_cast<int64_t>(uint64Val << rhs.uint64Val), type, false);
    // ← 同上
}
```

**触发方式：** 用户编写含编译期常量移位的仓颉源码：

```cangjie
let x: Int64 = 1 << 200   // 编译时常量折叠触发 UB
let y: Int64 = 100 >> 128  // 结果不可预测
```

**根本原因：** C++ 标准 \[expr.shift\] 规定，移位量 ≥ 位宽时为 UB，但代码未做边界校验。

**影响：** 编译器生成错误的常量折叠结果，安全相关常量可能被静默篡改。

**修复建议：** 在移位操作前添加 `if (rhs.uint64Val >= 64)` 检查，返回 0 或报错。

---

### EXTRA-MED-02：无符号整数除零未保护（正数路径）

| 属性 | 内容 |
| --- | --- |
| **来源** | 独立发现（Gist 无对应条目） |
| **CWE** | CWE-369 (Divide By Zero) |
| **文件** | `src/AST/IntLiteral.cpp:418-440` |

**漏洞代码：**

```cpp
// src/AST/IntLiteral.cpp 第 418-440 行
IntLiteral IntLiteral::operator/(const IntLiteral& rhs) const
{
    if (sign + rhs.sign > 0) {
        return IntLiteral(uint64Val / rhs.uint64Val, type, false);
        // ← 未检查 rhs.uint64Val == 0，直接除法
    }
    // 后续 int64 路径有零检查，但此路径遗漏
}
```

**触发方式：**

```cangjie
let x: UInt64 = 100 / 0   // 无符号除零 → SIGFPE 或 C++ UB
```

**根本原因：** `sign + rhs.sign > 0`（两个正数）路径假定除数非零，但未验证。

**影响：** 编译器崩溃（SIGFPE）或 UB 导致不可预测行为。

**修复建议：** 在 `if (sign + rhs.sign > 0)` 路径内添加 `rhs.uint64Val != 0` 检查。

---

### EXTRA-MED-03：类型解析器 `ParseType()` 递归无深度限制

| 属性 | 内容 |
| --- | --- |
| **来源** | 独立发现（Gist MED-01 仅覆盖 `ParseExpr`，未覆盖 `ParseType`） |
| **CWE** | CWE-674 (Uncontrolled Recursion) |
| **文件** | `src/Parse/ParseType.cpp:297-310, 71, 330-360` |

**漏洞代码：**

```cpp
// src/Parse/ParseType.cpp 第 71 行
OwnedPtr<AST::Type> ParserImpl::ParseVarrayType()
{
    ret->typeArgument = ParseType();  // ← 递归回调 ParseType
}

// 第 330-360 行 — 泛型参数也递归回调 ParseType
```

**触发方式：**

```cangjie
let x: VArray<VArray<VArray<...<Int64, 1>, 1>, 1>, 1>  // 深层嵌套泛型类型
```

**与 Gist 的区别：** Gist MED-01 覆盖的是 `ParseExpr` → `ParseBlock` → `ParseExpr` 的表达式递归链，而 `ParseType` → `ParseVarrayType`/`ParseTypeArguments` → `ParseType` 是**独立的递归调用链**，可单独触发栈溢出。

**修复建议：** 在 `ParseType` 入口添加与 `ParseExpr` 共享的 `DepthGuard`。

---

### EXTRA-MED-04：宏消息 FlatBuffers 反序列化缺少空指针校验

| 属性 | 内容 |
| --- | --- |
| **来源** | 独立发现（Gist MED-05 关注 msgSize 无上限 OOM，未涉及内部字段空指针） |
| **CWE** | CWE-20 (Improper Input Validation) |
| **文件** | `src/Macro/MacroEvalMsgSerializer.cpp:231-262` |

**漏洞代码：**

```cpp
// src/Macro/MacroEvalMsgSerializer.cpp 第 231-262 行
static void DeserializeItemsFromItemsBuf(...)
{
    uoffset_t num = itemsBuf.size();
    items.resize(num);          // ← 无大小限制
    for (uoffset_t i = 0; i < num; i++) {
        items[i].key = itemsBuf.Get(i)->key()->str();
        // ← key() 可能返回 nullptr，解引用导致崩溃
    }
}
```

**触发方式：** 宏服务器进程与编译器进程通过管道通信，恶意宏服务器发送构造的 FlatBuffers 消息，其中 `key` 字段为空。

**与 Gist 的区别：** Gist MED-05 关注的是管道读取的 `msgSize` 无上限导致 OOM，属于**传输层**问题；本发现关注的是 FlatBuffers 消息**内容层**的空指针字段未校验，是不同攻击面。

**修复建议：** 对 `key()`、`value()` 等返回值添加空指针检查。

---

### EXTRA-MED-05：模块反序列化 `ReferenceLoader` 索引越界访问

| 属性 | 内容 |
| --- | --- |
| **来源** | 独立发现（Gist H-12 被驳回为误报，但关注点不同） |
| **CWE** | CWE-125 (Out-of-bounds Read) |
| **文件** | `src/Modules/ASTSerialization/ReferenceLoader.cpp:277-290` |

**漏洞代码：**

```cpp
// src/Modules/ASTSerialization/ReferenceLoader.cpp 第 277-290 行
auto index = type - 1;
if (auto ty = allTypes[index]; ty) {    // ← 无 bounds check
    return ty;
}
// ...
allTypes[index] = TypeManager::GetInvalidTy();  // ← 越界写入
```

**触发方式：** 构造恶意 `.cjo` 模块缓存文件（如替换增量编译缓存），使 `type` 字段包含超出 `allTypes` 大小的值。

**与 Gist 的区别：** Gist H-12 关注的是 CJO 模块**缺少密码学签名**（被驳回，因 FlatBuffers Verifier 已做结构校验），而本发现关注的是 `LoadType` 中**具体的数组索引越界**——即使 FlatBuffers 结构合法，`type` 字段的**语义值**超出 `allTypes` 容量仍会导致越界。

**修复建议：** 在访问 `allTypes[index]` 前添加 `index < allTypes.size()` 检查。

---

### EXTRA-MED-06：`Compression.cpp` 中 `ForwardIdentifier` 索引越界读取

| 属性 | 内容 |
| --- | --- |
| **来源** | 独立发现（Gist MED-13 关注 `stoi` 异常未捕获，未涉及 OOB 读取） |
| **CWE** | CWE-125 (Out-of-bounds Read) |
| **文件** | `src/Mangle/Compression.cpp:237-241` |

**漏洞代码：**

```cpp
// src/Mangle/Compression.cpp 第 237-241 行
while (idx < mangled.size() && isdigit(mangled[idx + numberLen])) {
    numberLen++;   // ← idx + numberLen 可超出 mangled.size()
}
```

**根本原因：** 循环条件检查 `idx < mangled.size()`，但实际读取位置为 `mangled[idx + numberLen]`，当 `idx + numberLen >= mangled.size()` 时发生越界。

**与 Gist 的区别：** Gist MED-13 覆盖的是同文件第 32/240 行的 `stoi` 异常未捕获（字符串转整数失败），而本发现是第 237 行的**数组索引越界**——完全不同的 bug。

**修复建议：** 将循环条件改为 `idx + numberLen < mangled.size() && isdigit(mangled[idx + numberLen])`。

---

### EXTRA-MED-07：条件编译表达式求值递归无深度限制

| 属性 | 内容 |
| --- | --- |
| **来源** | 独立发现（Gist 无对应条目） |
| **CWE** | CWE-674 (Uncontrolled Recursion) |
| **文件** | `src/ConditionalCompilation/ConditionalCompilation.cpp:408-425, 242-248, 363-369` |

**漏洞代码：**

```cpp
// src/ConditionalCompilation/ConditionalCompilation.cpp 第 363-369 行
bool ConditionalCompilationImpl::EvalParenExpr(const ParenExpr& pe)
{
    return EvalConditionExpr(pe.expr.operator*());  // ← 递归回调
}
// EvalConditionExpr → EvalBinaryExpr/EvalParenExpr → EvalConditionExpr 循环递归
```

**触发方式：**

```cangjie
@when(((((((((((...true...))))))))))  // 深层嵌套括号
```

**影响：** 编译阶段栈溢出（需先通过解析器，但解析器也无深度限制，因此可联合利用）。

**修复建议：** 添加递归深度计数器，上限 64。

---

### EXTRA-MED-08：CodeGen 数组偏移乘法无溢出检查

| 属性 | 内容 |
| --- | --- |
| **来源** | 独立发现（Gist 无对应条目） |
| **CWE** | CWE-190 (Integer Overflow or Wraparound) |
| **文件** | `src/CodeGen/CJNative/CJNativeIntrinsicsCall.cpp:250`、`src/CodeGen/Base/IntrinsicsDispatcher.cpp:278` |

**漏洞代码：**

```cpp
// CJNativeIntrinsicsCall.cpp 第 250 行
llvm::Value* offset = CreateMul(GetSize_64(*arrTy.GetElementType()), index);
// ← 默认 wrapping 乘法，无溢出检测

// IntrinsicsDispatcher.cpp 第 278 行
auto dataSize = irBuilder.CreateMul(copyLen, typeSize, "arr.data.len");
// ← 同样问题
```

**根本原因：** LLVM IR `CreateMul` 默认使用 wrapping 乘法，`elementSize * index` 溢出后产生小偏移值，导致后续数组访问越界。

**影响：** 编译器生成的**目标代码**存在整数溢出漏洞，可能被恶意程序利用。

**修复建议：** 使用 `CreateNSWMul` 或 `CreateNUWMul` 替代，启用溢出检测。

---

## 第三部分：低风险补充

---

### EXTRA-LOW-01：数字字面量处理无长度限制

| 属性 | 内容 |
| --- | --- |
| **来源** | 独立发现（Gist 无对应条目） |
| **CWE** | CWE-400 (Uncontrolled Resource Consumption) |
| **文件** | `src/Lex/Lexer.cpp:386-420` |

**漏洞代码：**

```cpp
bool LexerImpl::ProcessDigits(const int& base, bool& hasDigit, const char* reasonPoint, bool* isFloat)
{
    for (int i{0}; ; ++i) {   // ← 无上限
        ReadUTF8Char();
    }
}
```

**触发方式：** 构造含超长数字字面量的源文件（如 10MB 连续数字 `111...1`），编译器持续消耗 CPU。

**修复建议：** 添加最大长度限制（如 4096 字符）。

---

### EXTRA-LOW-02：无符号整数取模零未保护

| 属性 | 内容 |
| --- | --- |
| **来源** | 独立发现（与 EXTRA-MED-02 相同模式） |
| **CWE** | CWE-369 (Divide By Zero) |
| **文件** | `src/AST/IntLiteral.cpp:442-459` |

**漏洞代码：** 与 `operator/` 相同模式，`operator%` 在 `sign + rhs.sign > 0` 路径下未检查 `rhs.uint64Val == 0`。

**修复建议：** 同 EXTRA-MED-02。

---

### EXTRA-LOW-03：AST Walker 遍历无递归深度限制

| 属性 | 内容 |
| --- | --- |
| **来源** | 独立发现（Gist 无对应条目） |
| **CWE** | CWE-674 (Uncontrolled Recursion) |
| **文件** | `src/AST/Walker.cpp:39-150` |

**漏洞代码：** `Walker::Walk()` 使用 `visitedByWalkerID` 实现了环检测，但对树状深层嵌套结构无递归深度保护。通过深层嵌套声明（数千层嵌套 struct）可在遍历阶段触发栈溢出。

**修复建议：** 添加递归深度计数器。

---

### EXTRA-LOW-04：类型参数推断越界访问

| 属性 | 内容 |
| --- | --- |
| **来源** | 独立发现（Gist 无对应条目） |
| **CWE** | CWE-125 (Out-of-bounds Read) |
| **文件** | `src/Sema/TypeArgumentInference.cpp:272-289` |

**漏洞代码：** 使用 `typeArgs[i]` 直接索引，其中 `i` 来源于对 `typeParameters` 的遍历，假设 `typeArgs.size() >= typeParameters.size()`，但未显式验证。

**修复建议：** 添加 `typeArgs.size() >= typeParameters.size()` 前置校验。

---

## 裁决分歧

---

### 分歧：`--plugin` 加载无签名/路径白名单验证

| 属性 | 内容 |
| --- | --- |
| **Gist 裁决** | H-10 ❌ 误报（理由：插件路径由用户通过 `--plugin` 显式指定，与 GCC `-fplugin=` 设计一致，`OpenSymbolTable` 还使用了 `realpath` 规范化。要求对用户明确指定的插件进行签名验证不属于合理安全预期） |
| **本 PR 裁决** | ✅ 确认（理由：CI/CD 场景下构建脚本可能从不可信源获取插件路径，插件加载后直接调用函数指针，恶意插件可完全控制编译流程） |
| **文件** | `src/Frontend/CompilerInstance.cpp:258-282` |

**分歧分析：** 两份报告的关键分歧在于**威胁模型**不同。Gist 以开发者手动使用编译器为基准（用户显式指定 = 信任），本 PR 以 CI/CD 自动化构建管道为基准（配置可能被供应链攻击篡改）。两种角度均有合理性，建议根据仓颉编译器的实际部署场景决定。

---

## 补充发现汇总表

| 编号 | 风险 | 源码目录 | 文件 | CWE | 类型 |
|------|------|---------|------|-----|------|
| EXTRA-HIGH-01 | 🔴 高 | `include/cangjie/Macro/` + `src/Macro/` | [`include/cangjie/Macro/InvokeUtil.h:80`](https://github.com/wyq0721/cangjie_compiler/blob/main/include/cangjie/Macro/InvokeUtil.h#L80) | CWE-114 | 符号劫持 |
| EXTRA-MED-01 | 🟡 中 | `src/AST/` | [`src/AST/IntLiteral.cpp:461`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/AST/IntLiteral.cpp#L461) | CWE-190 | 未定义行为 |
| EXTRA-MED-02 | 🟡 中 | `src/AST/` | [`src/AST/IntLiteral.cpp:418`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/AST/IntLiteral.cpp#L418) | CWE-369 | 除零崩溃 |
| EXTRA-MED-03 | 🟡 中 | `src/Parse/` | [`src/Parse/ParseType.cpp:297`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Parse/ParseType.cpp#L297) | CWE-674 | 栈溢出 |
| EXTRA-MED-04 | 🟡 中 | `src/Macro/` | [`src/Macro/MacroEvalMsgSerializer.cpp:231`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Macro/MacroEvalMsgSerializer.cpp#L231) | CWE-20 | 空指针崩溃 |
| EXTRA-MED-05 | 🟡 中 | `src/Modules/ASTSerialization/` | [`src/Modules/ASTSerialization/ReferenceLoader.cpp:277`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Modules/ASTSerialization/ReferenceLoader.cpp#L277) | CWE-125 | 越界读写 |
| EXTRA-MED-06 | 🟡 中 | `src/Mangle/` | [`src/Mangle/Compression.cpp:237`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Mangle/Compression.cpp#L237) | CWE-125 | 越界读取 |
| EXTRA-MED-07 | 🟡 中 | `src/ConditionalCompilation/` | [`src/ConditionalCompilation/ConditionalCompilation.cpp:408`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/ConditionalCompilation/ConditionalCompilation.cpp#L408) | CWE-674 | 栈溢出 |
| EXTRA-MED-08 | 🟡 中 | `src/CodeGen/CJNative/` | [`src/CodeGen/CJNative/CJNativeIntrinsicsCall.cpp:250`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/CodeGen/CJNative/CJNativeIntrinsicsCall.cpp#L250) | CWE-190 | 整数溢出 |
| EXTRA-LOW-01 | 🟢 低 | `src/Lex/` | [`src/Lex/Lexer.cpp:386`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Lex/Lexer.cpp#L386) | CWE-400 | 资源耗尽 |
| EXTRA-LOW-02 | 🟢 低 | `src/AST/` | [`src/AST/IntLiteral.cpp:442`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/AST/IntLiteral.cpp#L442) | CWE-369 | 除零崩溃 |
| EXTRA-LOW-03 | 🟢 低 | `src/AST/` | [`src/AST/Walker.cpp:39`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/AST/Walker.cpp#L39) | CWE-674 | 栈溢出 |
| EXTRA-LOW-04 | 🟢 低 | `src/Sema/` | [`src/Sema/TypeArgumentInference.cpp:272`](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Sema/TypeArgumentInference.cpp#L272) | CWE-125 | 越界读取 |

---

## 修复优先级建议

| 优先级 | 编号 | 修复方案 | 工作量 |
|--------|------|---------|--------|
| P0 | EXTRA-HIGH-01 | 将 `RTLD_GLOBAL` 改为 `RTLD_LOCAL` | 1 行 |
| P1 | EXTRA-MED-01 | 移位前添加 `>= 64` 检查 | 4 行 |
| P1 | EXTRA-MED-02 | 除法前添加 `!= 0` 检查 | 2 行 |
| P1 | EXTRA-MED-06 | 修正循环条件为 `idx + numberLen < size()` | 1 行 |
| P2 | EXTRA-MED-03 | 类型解析添加 DepthGuard | 半天 |
| P2 | EXTRA-MED-04 | 反序列化添加空指针检查 | 10 行 |
| P2 | EXTRA-MED-05 | 索引访问前添加边界检查 | 2 行 |
| P2 | EXTRA-MED-07 | 条件求值添加深度计数器 | 半天 |
| P2 | EXTRA-MED-08 | 使用 `CreateNSWMul`/`CreateNUWMul` | 2 行 |
| P3 | EXTRA-LOW-01~04 | 逐步修复 | 各 30 分钟 |

---

## 对比方法说明

Gist 报告共 41 项发现（19 确认 + 17 部分确认 + 5 误报），本 PR 三份报告共计 43 项发现（原始 15 + 第三轮 20 + 附加 4 + cjc -p 分析 1）。

**两份报告共同覆盖的问题**（不在本文件中重复列出）：

| Gist 编号 | 本 PR 编号 | 问题 |
|-----------|-----------|------|
| MED-01 | SCAN-01/SCAN-03 | Parser/Lexer 递归无深度限制 |
| MED-02 | SCAN-01 | Lexer 字符串插值递归 |
| MED-03 | VULN-01/SCAN-11 | CHIR FlatBuffers 限制禁用 |
| MED-04 | VULN-02/SCAN-12 | execvp PATH 劫持 |
| MED-09 | VULN-13/SCAN-16 | TOCTOU 竞态 |
| MED-10 | VULN-04/SCAN-15 | CANGJIE_HOME 信任 |
| MED-15 | SCAN-09 | 泛型实例化递归 |
| HIGH-03 | VULN-15/SCAN-19 | Demangler 递归 |
| HIGH-05 | SCAN-24 | 缓存反序列化无完整性校验 |
