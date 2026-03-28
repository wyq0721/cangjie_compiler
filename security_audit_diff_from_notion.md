# 本 PR 独有发现（链接报告未提及的问题）

> **对比基准**：[Gist 链接报告](https://gist.github.com/wyq0721/1043366fb37f56268bbb820a50c010bc)（41 项发现：7 高、24 中、10 低）  
> **对比对象**：本 PR 中的 `security_audit_report_compier_recheck.md` + `security_audit_report_compier_thirdtime.md`  
> **提取标准**：仅列出链接报告中**完全未提及**或**遗漏的独立攻击面**

---

## 汇总

| 类别 | 数量 |
|------|------|
| 本 PR 独有高风险 | 1 |
| 本 PR 独有中风险 | 8 |
| 本 PR 独有低风险 | 4 |
| 裁决分歧 | 1 |
| **合计** | **14** |

---

## 🔴 高风险（本 PR 独有）

### 1. VULN-03 / SCAN-13：宏 `.so` 库使用 `RTLD_GLOBAL` 加载

- **文件**：[`include/cangjie/Macro/InvokeUtil.h` 第 80 行](https://github.com/wyq0721/cangjie_compiler/blob/main/include/cangjie/Macro/InvokeUtil.h#L80)
- **CWE**：CWE-114（进程控制）
- **问题**：宏库通过 `dlopen` 以 `RTLD_LAZY | RTLD_GLOBAL` 加载，导出的全部符号进入编译器全局命名空间。恶意宏库可定义 `malloc`/`free`/`memcpy` 同名函数，劫持编译器自身的内存管理函数调用。
- **链接报告差异**：链接报告覆盖了 `execvp` PATH 劫持（MED-04）和管道消息长度（MED-05），但**未提及** `RTLD_GLOBAL` 标志导致的全局符号覆盖风险。

---

## 🟠 中风险（本 PR 独有）

### 2. VULN-12 / SCAN-05：`IntLiteral` 移位操作未检查移位量 ≥ 64（C++ UB）

- **文件**：[`src/AST/IntLiteral.cpp` 第 461–470 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/AST/IntLiteral.cpp#L461-L470)
- **CWE**：CWE-190（整数溢出/回绕）
- **问题**：`operator<<` 和 `operator>>` 在 `rhs.uint64Val >= 64` 时触发 C++ 未定义行为。编译器可能生成错误的常量折叠结果。

### 3. SCAN-06：无符号整数除零未保护（正数路径）

- **文件**：[`src/AST/IntLiteral.cpp` 第 418–440 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/AST/IntLiteral.cpp#L418-L440)
- **CWE**：CWE-369（除零错误）
- **问题**：`operator/` 在 `sign + rhs.sign > 0` 路径下直接执行 `uint64Val / rhs.uint64Val`，未检查 `rhs == 0`。

### 4. SCAN-04：类型解析器 `ParseType()` 递归无深度限制

- **文件**：[`src/Parse/ParseType.cpp` 第 297–310 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Parse/ParseType.cpp#L297-L310)
- **CWE**：CWE-674（不受控递归）
- **链接报告差异**：链接报告 MED-01 仅覆盖了表达式解析器 `ParseExpr`，未单独提及类型解析器 `ParseType` → `ParseVarrayType` → `ParseType` 的独立递归调用链。

### 5. SCAN-14：宏消息反序列化缺少空指针和大小校验

- **文件**：[`src/Macro/MacroEvalMsgSerializer.cpp` 第 231–262 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Macro/MacroEvalMsgSerializer.cpp#L231-L262)
- **CWE**：CWE-20（输入验证不当）
- **链接报告差异**：链接报告 MED-05 关注管道 `msgSize` 无上限（OOM），而本 PR 发现的是 FlatBuffers 消息内部 `key()` 等指针无空值校验导致的空指针解引用。

### 6. SCAN-20：模块反序列化 `ReferenceLoader` 索引越界访问

- **文件**：[`src/Modules/ASTSerialization/ReferenceLoader.cpp` 第 277–290 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Modules/ASTSerialization/ReferenceLoader.cpp#L277-L290)
- **CWE**：CWE-125（越界读取）
- **问题**：`LoadType()` 使用来自 `.cjo` 文件的 `index` 直接访问 `allTypes[index]`，无边界校验。恶意 `.cjo` 文件可触发越界读写。

### 7. SCAN-21（附加）：`Compression.cpp` 中 `ForwardIdentifier` 索引越界读取

- **文件**：[`src/Mangle/Compression.cpp` 第 237–241 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Mangle/Compression.cpp#L237-L241)
- **CWE**：CWE-125（越界读取）
- **链接报告差异**：链接报告 MED-13 关注的是 `stoi` 异常未捕获，而本 PR 发现的是循环条件 `idx < mangled.size()` 与实际访问 `mangled[idx + numberLen]` 不一致的越界读取。

### 8. SCAN-22（附加）：条件编译表达式求值递归无深度限制

- **文件**：[`src/ConditionalCompilation/ConditionalCompilation.cpp` 第 408–425 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/ConditionalCompilation/ConditionalCompilation.cpp#L408-L425)
- **CWE**：CWE-674（不受控递归）
- **问题**：`EvalConditionExpr` → `EvalBinaryExpr`/`EvalParenExpr` → `EvalConditionExpr` 递归链无深度限制。通过 `@when(((((...true...)))))`（深层嵌套括号）可触发栈溢出。

### 9. SCAN-23（附加）：CodeGen 数组偏移乘法无溢出检查

- **文件**：[`src/CodeGen/CJNative/CJNativeIntrinsicsCall.cpp` 第 250 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/CodeGen/CJNative/CJNativeIntrinsicsCall.cpp#L250)
- **CWE**：CWE-190（整数溢出）
- **问题**：LLVM IR 层 `CreateMul(elementSize, index)` 使用默认 wrapping 乘法，`elementSize * index` 溢出后产生小偏移值，生成的目标代码存在整数溢出漏洞。

---

## 🟡 低风险（本 PR 独有）

### 10. SCAN-02：数字字面量处理无长度限制

- **文件**：[`src/Lex/Lexer.cpp` 第 386–420 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Lex/Lexer.cpp#L386-L420)
- **CWE**：CWE-400（不受控的资源消耗）

### 11. SCAN-07：无符号整数取模零未保护

- **文件**：[`src/AST/IntLiteral.cpp` 第 442–459 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/AST/IntLiteral.cpp#L442-L459)
- **CWE**：CWE-369（除零错误）

### 12. SCAN-08：AST Walker 遍历无递归深度限制

- **文件**：[`src/AST/Walker.cpp` 第 39–150 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/AST/Walker.cpp#L39-L150)
- **CWE**：CWE-674（不受控递归）

### 13. SCAN-10：类型参数推断越界访问

- **文件**：[`src/Sema/TypeArgumentInference.cpp` 第 272–289 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Sema/TypeArgumentInference.cpp#L272-L289)
- **CWE**：CWE-125（越界读取）

---

## ⚠️ 裁决分歧

### VULN-14 / SCAN-18：`--plugin` 加载无签名/路径白名单验证

- **文件**：[`src/Frontend/CompilerInstance.cpp` 第 258–282 行](https://github.com/wyq0721/cangjie_compiler/blob/main/src/Frontend/CompilerInstance.cpp#L258-L282)
- **本 PR 裁决**：✅ **确认**（CI/CD 场景下从不可信源获取插件路径时有代码执行风险）
- **链接报告裁决**：❌ **驳回**（H-10 误报——认为用户显式指定路径与 GCC `-fplugin=` 设计一致，属合理安全预期）
- **分析**：两份报告的关键分歧在于是否将 CI/CD 构建管道中的自动化插件加载视为安全威胁。本 PR 认为在自动化构建场景下，插件路径可能来自不受信任的配置源。

---

## 📋 对比方法说明

链接报告共 41 项发现（19 确认 + 17 部分确认 + 5 误报），本 PR 三份报告共计 43 项发现（原始 15 + 第三轮 20 + 附加 4 + cjc -p 分析 1）。

两份报告**共同覆盖**的问题包括：
- Parser/Lexer 递归无深度限制（链接 MED-01/MED-02 ↔ 本 PR SCAN-01/SCAN-03）
- CHIR FlatBuffers 限制禁用（链接 MED-03 ↔ 本 PR VULN-01/SCAN-11）
- execvp PATH 劫持（链接 MED-04 ↔ 本 PR VULN-02/SCAN-12）
- CANGJIE_HOME 信任（链接 MED-10 ↔ 本 PR VULN-04/SCAN-15）
- Demangler 递归（链接 HIGH-03 ↔ 本 PR VULN-15/SCAN-19）
- TOCTOU 竞态（链接 MED-09 ↔ 本 PR VULN-13/SCAN-16）
- 泛型实例化递归（链接 MED-15 ↔ 本 PR SCAN-09）
- 缓存反序列化无完整性校验（链接 HIGH-05 ↔ 本 PR SCAN-24）
