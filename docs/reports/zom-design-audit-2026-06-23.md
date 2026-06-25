# ZOM 语言设计审计综合报告

> 多专家 × adversarial 交叉验证 审计流程。候选发现 66 条，经双盲验证后**采纳 64 条**。

| 严重度 | 数量 |
|---|---|
| 🔴 严重 (critical) | 4 |
| 🟠 高 (high) | 20 |
| 🟡 中 (medium) | 25 |
| 🟢 低 (low) | 11 |
| 🔵 提示 (info) | 4 |
| **合计** | **64** |

## 执行摘要

ZOM 目前正处于**语法解析器初具规模、类型系统与语义分析尚未完整落地**的阶段。
本轮审计的核心信号（来自四类专家维度的综合归纳）：

1. **语法侧**：解析器已覆盖基础控制流（if/else、for、while、do-while）、函数、块、基本表达式、严格相等、错误默认值等构造，但**运算符优先级文档缺失、表达式-语句边界策略未明、模式匹配/泛型语法尚未锁定**，在加入复杂特性前需要先完成语法歧义与保留字预留。
2. **类型侧**：类型系统实现（类型检查、推断、泛型、trait、代数数据类型）与规范之间存在明显落差——**大量概念在规范中有讨论但代码中尚未落地**，这意味着当前版本在"能 parse 但无法给出类型保证"的灰色地带，若此时发布或开始生态建设，后续改动的兼容性成本极高。
3. **语义侧**：值/引用语义、拷贝/移动策略、所有权与 RAII、闭包捕获、错误处理模型等**语言基石级语义尚未在规范中正式定义**，属于演进路线上最大的风险源。
4. **规范-实现一致性**：存在双向缺口——既有"规范没写、实现已加"的特性蔓延，也有"规范写了、实现缺位"的承诺性债务，建议在进入 alpha 前做一轮 **Spec ↔ Parser ↔ Type ↔ Tests 的四方同步冻结**。

下一节给出优先级排序的详细问题清单，最后附与可比语言的横向对比与经验教训。

---

## 详细发现清单（按严重度排序）

### 1. 🔴 [严重] 类型检查阶段完全未实现：所有类型安全承诺均为空  
**类别**: 语义与组合性 | **置信度**: 97%

**问题描述**  
TypeChecker 类整体被注释掉，命名空间为空实现；编译驱动管线 (driver) 中也没有对应的 checkSources() 调用。因此 ZOM 宣称的「静态 + 强类型 + 可推断」在当前代码中完全没有落地。任何类型不匹配、空值访问、未定义符号都不会被编译器捕获。

**证据**
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/checker/checker.h:24 — `// class TypeChecker : public CompilerStage<...> { ... };`
    整个 TypeChecker 被注释，不存在任何可实例化的类型检查对象。
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/checker/checker.cc:26 — `namespace checker {}  // namespace checker`
    实现文件是空命名空间，0 行功能代码。
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/03-types.md:9 — `- **Static**: All types are known at compile time
- **Strong**: No implicit conversions between incompatible types
- **Inferred**: Types can often be inferred from context`
    规范中的三项核心承诺都依赖 Checker 阶段验证，但该阶段完全缺失。

**潜在影响**  
当前任何 ZOM 源文件只要语法正确就能通过编译；若推进到后端只能退化为动态类型执行。项目「Safety First」的设计目标 (01-introduction.md:3) 无法被任何编译期机制保障。

**修复建议**  
把 TypeChecker 作为下一个里程碑的最高优先级。按顺序落地：(1) 表达式类型推导与类型环境 (infer/check + TypeEnv)；(2) 可赋值性/子类型判定 (isSubtypeOf 补全)；(3) 函数重载决议与泛型实参推导；(4) match 穷举性；(5) trait 实现一致性检查。
**评审备注**
- 确认方: 独立核验证据：

1. checker/checker.h 第 24-36 行：`class TypeChecker` 整块被 `//` 注释，仅剩下空的 namespace checker。被注释的骨架甚至依赖一个并不存在的 `CompilerStage<T,U>` CRTP 基类和 `ast::AST` 根类型（实际根是 `ast::Node`），说明该骨架是早期草图，连当前 AST 接口都未对齐。

2. checker/checker.cc 第 26 行：`namespace checker {}` — 命名空间体内零行实现代码；文件虽然 include 了 expression/statement/type 等头，但没有使用它们。

3. driver/driver.cc 中公开的编排仅两段：
   - parseSources() → basic::performParse (lex+parse)
   - bindSources() → binder::Binder::bindSourceFile
   driver.h/driver.cc 中不存在 checkSources() / check() / typeCheck() / sema() 等任何名字的阶段入口（grep 全量命中 0 处）。frontend.cc 也无额外阶段。因此「管线中无 checkSources 调用」准确。

4. 规范承诺对照：docs/spec/chapters/03-types.md:9-13 明确声明 Static + Strong + Inferred；01-introduction.md:3、7、15 声明 Safety First 和「静态类型 + 类型推断」。这三项承诺（静态可判定、禁止隐式转换、上下文推断）全部依赖类型推断/可赋值性/子类型求解，当前管线无法提供。

5. checker-test.cc 的 4 个用例（BasicParsingWorks、TypeMismatchError、UndefinedVariableError、FunctionParameterTypeChecking）全部只调用 parser.parse()，断言 AST 非空，尾部写着 `// TODO: Add type checking once Checker implementation is available`。用例名虽含 "TypeMismatchError"，但不会产生任何诊断——单元测试级别的假阳性与候选人描述吻合。

6. 诊断层：diagnostics-sema.def 虽然预定义了 TypeMismatch / UndefinedIdentifier / 全套 Redeclare* 等语义错误码，但 grep 显示 TypeMismatch 在实现文件中 0 次引用——没有任何代码路径能发射它们。

严重度判断：critical。理由：对一个「静态+强类型」定位的语言而言，类型检查是前端核心价值，缺失该阶段等价于「当前产物不是一个编译器，只是一个语法解析器 + 命名绑定器」。规范中所有基于类型的安全承诺（null 安全、无隐式转换、泛型边界、接口一致性、穷举匹配）均为空话；若后续直接接后端/解释器，只能按「动态类型」执行，与语言定位完全相悖。但因项目仍处于前期开发、且无后端发布，不致造成生产事故，故保留原 critical 级别为合理。

- 反对方: 反驳者查证后发现以下可弱化原问题定性的反证：

1. **Binder 阶段确实会发出 `diagnostics-sema.def` 中的部分诊断码**，并非"完全零引用"：
   - `RedeclareVariable`（binder.cc 第 434/463/1034 行）— 变量、BindingElement、EnumMember 的重声明检查
   - `RedeclareParameter`（第 711 行）— 函数参数重声明
   - `RedeclareFunction`（第 1065 行）— 函数重声明
   - `RedeclareClass`（第 1103 行）、`RedeclareInterface`（第 1141 行）— 类/接口重声明
   - `ReservedWord`（第 1241 行）、`ReservedInContext`（第 1250/1262 行）— 保留字/上下文关键字误用

2. **类型建模的底层骨架已落地**：`symbol/type-symbol.{h,cc}` 定义了完整的 `TypeSymbol` 继承体系（BuiltInTypeSymbol / ClassSymbol / InterfaceSymbol / FunctionTypeSymbol / TypeParameterSymbol），并实现了 `isSubtypeOf()`（名义子类型，沿 superclass+interfaces 递归）和 `isAssignableFrom()`（含 i32→f32 宽化硬编码）。符号层为后续 Checker 提供了基础。

3. **Checker 阶段在架构中是显式预留而非遗忘**：
   - `compiler/CMakeLists.txt` 中 checker 已被 add_subdirectory + target_link_libraries 纳入构建链
   - `trace/trace.h` 中 `TraceCategory::kChecker = 1 << 2` 已定义并在 trace-config 中可配置
   - checker.h 中注释掉的代码写明了 `CompilerStage<...>` 预期接口，说明这是有意的 TBD 而非设计缺口

4. **语法层面的类型表达式解析 100% 完整**：AST 层的 `TypeNode` 家族（数组、联合、交叉、函数、元组、对象、可选、typeof、泛型引用、泛型参数）全部由 Parser 正确产出并由 Binder 递归遍历（binder.cc 第 398-415 行各 TypeNode visitor），只是未做符号解析和语义验证（第 400 行 "TODO: Look up type symbol..." 明示）。

5. **CLI 管道中也有阶段性体现**：`--syntax-only` 选项和 `bindSources()` 错误分支（第 200-203 行）意味着用户至少能得到语法+名称绑定两层反馈，并非完全没有语义校验。

### 2. 🔴 [严重] 类型检查器整体未实现，所有语义检查为空  
**类别**: 类型系统 | **置信度**: 97%

**问题描述**  
checker/checker.h 中 TypeChecker 类整体被注释掉，checker.cc 为空命名空间；driver 管线中没有 checkSources 调用。所有规范声明的类型安全保证在实现中均为空白。

**证据**
  - products/zomlang/compiler/checker/checker.h:24 — `TypeChecker class entirely commented out`
    类型检查类只有注释没有定义
  - products/zomlang/compiler/checker/checker.cc:26 — `namespace checker { } empty`
    Check 实现完全为空
  - products/zomlang/compiler/driver/driver.cc:120 — `Driver pipeline has parseSources and bindSources but no checkSources`
    编译管线没有类型检查阶段

**潜在影响**  
违背设计目标 Safety First；任何类型不匹配、未定义标识符、运算不兼容、接口未实现等错误均不被捕获。

**修复建议**  
优先实现 TypeChecker 的最小可用版本：表达式类型推断、赋值兼容性、标识符解析、返回值类型检查、接口方法实现一致性检查。
**评审备注**
- 确认方: ## 独立核验证据（文件绝对路径与具体内容）

### 证据 1：`checker/checker.h` 中 TypeChecker 整体被注释掉（第 24-36 行）
文件：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/checker/checker.h`
- 第 24-36 行以 `// ` 前缀注释掉了 `class TypeChecker : public CompilerStage<...>` 的完整声明（构造/析构/`process` 方法/`SymbolTable symbol_table_` 成员），当前命名空间内没有任何其他类或函数声明。
- 注释是整块用行首 `//` 注释，不是条件编译或调试开关。编译产物中完全不占体积。

### 证据 2：`checker/checker.cc` 实现完全为空命名空间（第 26 行）
文件：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/checker/checker.cc`
- 第 1-28 行仅包含 Copyright、`#include` 头（引用了 ast/expression/statement/type）和
```cpp
namespace checker {}  // namespace checker
```
  空命名空间，**零函数/零类实现**。include 的一堆 AST 头文件未被引用。

### 证据 3：`driver/driver.cc` 编译管线无 checker 阶段
文件：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/driver/driver.cc`
- 公开可调用的阶段方法只有两个：
  - `CompilerDriver::parseSources()`（第 114-141 行）——多线程 lex+parse
  - `CompilerDriver::bindSources()`（第 143-181 行）——多线程 Binder 作用域绑定
- **无 `checkSources()` / `typeCheck()` / `checkSources()` 方法**，driver.h 头也未声明。
- 全项目 `grep -rn "TypeChecker\|checkSources\|typeCheck"` 只命中 checker.h 中的注释和 driver.h 中一行 "perform semantic analysis" 的文字注释。
- `basic/` 和 `utils/zomc/` 目录下 grep 同样无任何 checker 调用点。

### 附加佐证：测试与诊断码层面的一致
- `tests/unittests/compiler/checker/checker-test.cc` 仅 4 个 ZC_TEST 用例，**全部写着 `// TODO: Add type checking once Checker implementation is available`**，实际只调用 `parser.parse()`。
- `tests/language/semantic/` 目录仅有 CMakeLists.txt，**零 `.zom` 用例**。
- `diagnostics-sema.def` 定义了 20+ 个 `ZOM3xxx` 语义诊断码，但 grep 全 compiler 无任何使用点（包括 `TypeMismatch` 零命中）。

## 严重度复核：critical（同意原评估）

理由如下（比单纯"功能未做"更严重）：

1. **违背核心设计目标**：`docs/spec/chapters/01-introduction.md` 把 "Safety First" 列在五项设计目标之首，而类型安全是 Safety First 的首要保障手段。当前实现中类型系统完全不可用。
2. **不是"滞后开发"而是"完全未启动"**：AST 节点层、符号层（TypeSymbol/InterfaceSymbol/FunctionTypeSymbol 等）、诊断码层、测试目录层**全部有骨架**，唯独核心执行路径——TypeChecker 类和 driver 管线调用——是 0%。这不属于"实现了一半"，属于"整体缺失"。
3. **后果直接且严重**：任何 ZOM 源文件当前只能做到语法解析+符号绑定两类检查，超出命名冲突的错误——如 `let x:i32 = "string";`、`1 + "hello";`、调用未定义函数、接口方法缺失——全部静默通过。若后续接入代码生成/解释器，只能退化为动态类型执行，与宣称的 "Static + Strong + Inferred + Nominal + Generic" 完全矛盾。
4. **阻塞后续所有阶段**：后续代码生成（ZIS/IR）依赖每个节点的解析类型，接口/vtable 布局依赖 trait 一致性检查，内存安全（ARC/deinit 调度）依赖类型归属——所有下游模块均被阻塞。

## 描述中个别措辞可作微调

- 原文"所有规范声明的类型安全保证在实现中均为空白"——措辞正确。
- 建议中列出的"最小可用版本"清单合理，但**可再加一项优先级更高的兜底**：在 driver 层先暴露 `checkSources()` 空函数 stub，并在 frontend 顶层串联后**默认产生一个明确的诊断报错**（如 `ZOM3000: type checking is not implemented`），防止用户误以为编译通过的文件就是类型安全的。当前行为是"静默通过"，对 end-to-end 欺骗性更强。
- 反对方: 作为反驳者，能提出的抗辩如下：

1. 描述中"所有语义检查为空"措辞不够精确。Binder 阶段（binder/binder.h/.cc）已经执行了部分语义层面的工作：作用域管理、重复声明检测（RedeclareVariable/Function/Class/Interface）、符号创建与 AST 双向关联。这些属于广义的"语义检查"。严格说应是"所有类型检查"为空，而非"所有语义检查"。

2. "类型安全保证在实现中均为空白"同样有过度绝对化之嫌。类型系统的骨架建模相当完整：
   - symbol/type-symbol.h/.cc 定义了完整的 TypeSymbol 层次（BuiltInTypeSymbol、ClassSymbol、InterfaceSymbol、FunctionTypeSymbol、TypeParameterSymbol），并已实现 isSubtypeOf()、isAssignableFrom()（含 i32→f32 宽化）、Variance 枚举等。
   - symbol-flags.h 预留了 Generic / Covariant / Contravariant / Associated / Dependent 等 60+ 位标志。
   - symbol/value-symbol.h 的 ValueSymbol 层次完整。
   - AST 的类型节点（type.h）覆盖了所有规范形式。
   即：地基和梁柱已立，只是顶部的 TypeChecker visitor 管线尚未接入。"均为空白"有夸张成分。

3. 严重度是否应评为 critical 存疑。从项目开发节奏看（近期提交全部集中在 parser/binder/AST：do-while、strict equality、desugar、语法对齐等），Checker 明显是下一阶段的计划工作，并非被遗漏。zomc.cc 中 IR emission（第 376-378 行）和 Binary emission（第 381-383 行）也都是 TODO。对一个前端尚未完成的编译器来说，把"阶段 N 尚未开始"标为 critical，相当于批评一栋在建楼房"屋顶缺失——critical"。更合理的定级应是 high（因为确实阻断了所有类型安全承诺）或 medium（若视为已知规划缺口）。

4. checker-test.cc 的 4 个用例虽全为 TODO 占位，但清晰展示了团队对检查场景（类型不匹配、未定义变量、函数参数运算不一致）的规划意识，不是疏忽。

以上四点只能削弱描述的精确性和严重度的合理性，但**无法推翻三条核心事实**。

### 3. 🔴 [严重] 类型推断完全未实现，let/const 缺省类型标注无 fallback  
**类别**: 类型系统 | **置信度**: 95%

**问题描述**  
规范明确声明静态 + 可推断类型系统，但 binder 阶段创建变量符号时完全不把初始值类型与变量关联，也没有任何 inferType/solve/unify 函数存在。

**证据**
  - docs/spec/chapters/01-introduction.md:15 — `Static typing + type inference listed as feature`
    规范明确声明类型推断能力
  - docs/spec/chapters/03-types.md:9 — `Types are described as Inferred`
    类型章节声明 Inferred 属性
  - products/zomlang/compiler/binder/binder.cc:419 — `bindVariableDeclaration creates symbol but does not infer type from initializer`
    Binder 未从初始化器推断类型
  - products/zomlang/compiler/: — `grep for inferType|solve|unify across compiler has zero hits`
    全编译器无类型推断基础设施

**潜在影响**  
所有未显式标注类型的变量/函数返回值在后续阶段无法获得类型，后端只能退化到动态类型执行。

**修复建议**  
在 TypeChecker 中先实现局部 HM 风格推断：字面量->具体类型、算术运算提升、if-else/match 分支统一、泛型实参推导。
**评审备注**
- 确认方: 
核验摘要（全部独立确认）：

1. 规范层面确实承诺了类型推断：
   - `docs/spec/chapters/01-introduction.md:15` — 明确写 "Static typing with type inference"。
   - `docs/spec/chapters/03-types.md:11` — 类型系统属性清单明确列出 "Inferred: Types can often be inferred from context"。

2. Binder 层不做类型推断：
   - `products/zomlang/compiler/binder/binder.cc:419-455` 的 `visit(BindingElement&)`：
     * 创建变量符号（`symbolTable.createVariable`），
     * 调用 `addDeclarationToSymbol` 做 AST<->符号双向关联，
     * 遍历 initializer 表达式（仅为绑定其中的子引用），
     * **全程没有 `symbol.setType(...)` 调用**，也没有任何从 initializer 推导类型的逻辑。

3. 全编译器无类型推断基础设施：
   - `grep -rnE "inferType|solve|unify|checkExpression"` 全 compiler 目录零命中（仅 lexer/utils.cc 识别 `infer` 保留字、kinds.h 有 `InferKeyword` token 枚举，与语义推断无关）。
   - `products/zomlang/compiler/checker/checker.h:24-36`：整个 TypeChecker 类被注释掉；
   - `products/zomlang/compiler/checker/checker.cc:26`：`namespace checker {}` 空命名空间，零实现；
   - `products/zomlang/compiler/driver/driver.cc` 的编译管线仅包含 `parseSources()`（第 114 行起）和 `bindSources()`（第 143 行起），**完全没有 type-check / sema 阶段**。

4. 影响评估（支持 critical 分级）：
   - 规范承诺的"Inferred"属性完全不兑现。所有依赖类型推断的语法形式（`let x = 42;` 缺省类型、无返回类型标注的函数、泛型实参推导、if-else/match 分支统一）在后续阶段都无法获得类型。
   - TypeChecker 阶段整体为空，导致**所有**语义错误（类型不匹配、运算不兼容、接口未实现、模式不穷举、null 安全、实参形参不匹配等）都不会被捕获。
   - 后端若要推进，当前只能退化到按值动态解释或忽略类型标注，完全违背 "Safety First" 的设计目标。
   - 虽然 VariableSymbol::setType、TypeSymbol::isSubtypeOf 等接口已预留骨架（占比不到整个类型系统的 10%），但没有任何推断逻辑把这些骨架连接起来，不构成缓解因素。

5. 与原证据对比：
   - 原证据 4 条均被独立复核为真。
   - 额外补充发现：`diagnostics/diagnostics-sema.def` 中已定义的 `TypeMismatch` 等语义诊断码在全代码库零引用，也侧面证明 Checker 从未接入管道。
   - `tests/unittests/compiler/checker/checker-test.cc` 的 4 个用例全部为 TODO 占位，也与结论一致。

严重度 refinedSeverity = critical：缺失的是规范核心特性（Inferred）且阻塞整个静态类型安全承诺，并非边界功能。

- 反对方: 1. Binder 阶段不做类型推断属于正确的架构分层，不应将此作为"问题"的直接证据——按照标准编译器流水线，名称绑定(binder)和类型检查(checker)是两个独立阶段，binder 的职责仅为符号创建、作用域管理和重复声明检测。
2. 符号系统已完整预留了类型接口（`ValueSymbol::getType()/setType()/`TypeSymbol` 完整类层次），`SymbolTable::createVariable` 会分配 `Unit` 作为默认类型（symbol-table.cc:101-103，注释明确写着"for testing purposes"），说明这是"待后续填充正确类型"的明确计划，而非"完全无 fallback"。
3. 所有 4 个 checker unittest 均为 TODO 占位注释，表明团队已知晓 Checker 尚未实现，属于排期内的开发工作，而非意外遗漏。
4. Spec 的 introduction 章节（01-introduction.md）采用现在时描述语言设计目标和最终愿景，不一定代表当前 milestone 的已实现清单。
5. 严重级 "critical" 对于一个明确的、文档化的（代码中多处 TODO 注释标记的）"未实现功能"可能过重——critical 通常意味着核心功能崩溃、数据丢失或安全漏洞。

### 4. 🔴 [严重] 裸类型名模式 `when Point =>` 与 IdentifierPattern 语法完全重叠，存在绑定 vs 类型匹配的语义歧义  
**类别**: 语法设计 | **置信度**: 92%

**问题描述**  
Pattern 中 IdentifierPattern 不加任何关键字即可匹配——即 `when Foo => ...` 既可以是 IdentifierPattern（把输入绑定到变量 `Foo`），也可以是用户心目中的"类型模式"（当输入是 `Foo` 类型时匹配）。07-patterns.md 的示例同时给出了这两种用法：`when x =>`（标识符绑定）和 `when Point =>`（类型模式）。EBNF 没有提供显式的类型模式非终结符，只有 `is Type` 形式的 IsPattern；因此 `when Foo` 在当前规范里只能走 IdentifierPattern，而不能作类型模式——即 07 章的 `when Point` 示例按 EBNF 解释等价于 "把任何值绑定给变量 Point"，与作者原意不符。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/07-patterns.md:97 — `when str => ... when i32 => ... when Point =>`
    示例把预定义类型和自定义类型名直接写在 when 后作为类型模式
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:373 — `PrimaryPattern ::= WildcardPattern | IdentifierPattern | TuplePattern | StructurePattern | ArrayPattern | IsPattern | ExpressionPattern | EnumPattern`
    没有 TypePattern 非终结符
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:383 — `IdentifierPattern ::= Identifier TypeAnnotation?`
    Identifier 总是走绑定路径——除非在语义层做 "标识符是类型名则走类型模式"的特殊判定，否则语义歧义无法在语法层消除
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc:3733 — `zc::Maybe<zc::Own<ast::Pattern>> Parser::parseIdentifierPattern() { auto id = parseIdentifier();`
    Parser 统一把裸标识符产出 IdentifierPattern，没有类型判定

**潜在影响**  
模式匹配最常见的用法（类型分支）在语法层有歧义，同一程序在不同阶段（类型信息前后）会被解释为不同的匹配逻辑；重构时若把类型名改小写会悄悄改变语义。

**修复建议**  
显式引入类型模式语法：要么要求 `when : Point`（带冒号），要么要求 `when is Point`，要么强制所有类型名大写且在 Pattern 层直接把大写标识符当作类型引用（并在规范中严格注明此规则）。禁止裸标识符同时承担绑定与类型判定两种角色。
**评审备注**
- 确认方: ## 独立核验结论

### 一、四重证据全部确认

**证据 1：07-patterns.md 自相矛盾**
- 07-patterns.md:20-31 将 IdentifierPattern 定义为"把值绑定给变量 x"（`when x => ...`）
- 07-patterns.md:92-103 又以独立小节"Type Patterns"给出 `when str =>`、`when i32 =>`、`when Point =>`，意图是做类型匹配
- 两处使用的语法形式完全相同（裸标识符），但语义完全相反——前者标识符是被绑定者，后者标识符是被匹配的类型名
- 相同矛盾在 03-types.md:100-106、05-statements.md:118-124 中多次出现

**证据 2：EBNF 没有 TypePattern 非终结符**
- 17-grammar-reference.md:373-380 的 PrimaryPattern 变体中只有 WildcardPattern / IdentifierPattern / TuplePattern / StructurePattern / ArrayPattern / IsPattern / ExpressionPattern / EnumPattern —— 没有 TypePattern
- 形式文法 ZomParser.g4:578-600 完全一致，同样无 TypePattern
- IsPattern 要求显式 `is` 关键字（17 章 390 行 / g4 596 行），与裸标识符形式完全不同

**证据 3：IdentifierPattern 定义只走绑定路径**
- 17-grammar-reference.md:383：`IdentifierPattern ::= Identifier TypeAnnotation?`
- ZomParser.g4:589：`identifierPattern: bindingIdentifier typeAnnotation?;`
- 语义上 `typeAnnotation?` 是"对被绑定标识符作类型标注"（如 `v: i32` 表示"把输入绑定给 v，且 v 的类型期望为 i32"），而不是"匹配类型 i32"。标识符本身是变量名而非类型引用。

**证据 4：Parser 实现确实统一产出 IdentifierPattern**
- parser.cc:3704-3705：前瞻到 `=> / if / , / ; / ) / ] / } / EOF` 时走 `parseIdentifierPattern()`
- parser.cc:3733-3738：`parseIdentifierPattern` 直接 `parseIdentifier()` → `createIdentifierPattern()`，无任何"若标识符是类型名则走类型模式"的分支
- match.zom lit 测试也只用了 `v: i32`（带冒号标注的 IdentifierPattern）和 `is i32`（IsPattern）两种形式做类型匹配，**完全没有使用裸 `when i32 =>` 形式**——说明测试端也没有走"裸类型名模式"这条路径

### 二、严重度评估：critical（与原评估一致，但理由更精确）

这不是"可以在语义层补丁"的小问题，而是语法-语义接口层面的根本性歧义：

1. **同一语法串在规范中有两个相互矛盾的语义**：`when Foo =>` 既被说成是绑定（IdentifierPattern），又被说成是类型匹配（Type Pattern），二者的运行时行为完全不同——一个永远匹配成功（把任何值绑给 Foo），一个只在值为 Foo 类型时成功。

2. **解析端没有任何前瞻可以消除歧义**：标识符是类型名还是变量名，纯靠作用域信息——这在 Parser 阶段不可得。如果要在 Binder/Checker 阶段"修正"，就需要回溯更改 AST 节点种类（把 IdentifierPattern 改成 IsPattern），破坏了传统的"解析产出固定语法树"架构。Swift 走的是这条路径（它的模式解析确实需要语义反馈），但 ZOM 目前没有任何这种机制的迹象。

3. **命名规范不可靠**：建议方案里"强制所有类型名大写"的做法不可行，因为 ZOM 的预定义类型名是小写的（`str`、`i32`、`bool`、`f64`）——07 章的类型模式示例里正是用这些小写名字。大小写惯例无法覆盖预定义类型。

4. **重构静默语义变化**：若重构时把类型 `Point` 改名为 `point` 或反之，匹配逻辑会在 IdentifierPattern ↔ 类型模式之间静默翻转，不产生任何编译期诊断。

### 三、建议修正（比原题更具体）

- **推荐方案**：弃用 `when TypeName` 形式，要求类型匹配一律使用显式 `is Type` 或 `binding: Type`。这样 07 章示例改为 `when is str =>`、`when is Point =>`，与 EBNF 已有 IsPattern 一致，parser 不需要改动。
- **替代方案 A**：引入语法 `when : TypeName =>`（冒号前缀类型模式），在 PrimaryPattern 中新增 `':' TypeExpression` 变体。
- **替代方案 B**：若坚持裸标识符形式，需在规范中明确规则——所有出现在模式位置、且对应作用域中能解析为类型符号的标识符，走"类型模式"语义，并要求 Binder 阶段重写 AST 节点种类。这是最复杂的路径。
- 无论哪种方案，**07 章、03 章、05 章的示例都必须与 EBNF 和 parser 实现一致**，目前三处文档给出的 `when str =>` 按当前实现会被静默解释为"把任何值绑定给 str"。
- 反对方: 反驳该问题"critical 级完全语法歧义"论断的关键证据：

**证据1：预定义类型关键字（str, i32, bool 等）根本不会走 Identifier 分支，直接报错，不存在歧义**
- lexer 层将 str/i32/bool/i8/i16/i64/u8/u16/u32/u64/f32/f64/unit/null 切为独立的 SyntaxKind token（kinds.h:121-146），NOT Identifier。
- parsePattern() 中 switch 只处理 Underscore/Identifier/LeftParen/LeftBrace/LeftBracket/IsKeyword，类型关键字不在其中。
- 类型关键字落入 default → createExpressionPattern(parseExpression()) → parsePrimaryExpression() 无对应 case → parseIdentifier() → `isIdentifier()` 返回 false（非 Identifier token）→ createIdentifier(false) 报告 ZOM2010 `Reserved keyword 'str' cannot be used as an identifier` 错误。
- **实测验证**：`when str => ...` 和 `when i32 => ...` 均产生 ZOM2010 错误并拒绝编译，不会静默走绑定路径。
- 因此问题描述中 "07-patterns.md:97 when str / when i32 / when Point 示例" 前三者在当前 parser 中是语法错误，而非歧义匹配。

**证据2：这不是语法歧义（ambiguity），而是设计意图不匹配 + 文档过时**
- 歧义（ambiguity）定义：同一句法有两棵以上合法解析树。
- 当前语法对 `when Point =>` 的解析是确定且唯一的：IdentifierPattern（绑定）。没有备选解析路径。
- 所谓"歧义"是：用户的直觉期望（类型匹配）与语法设计实际行为（变量绑定）之间的语义层不匹配。这是 usability/design gap，不是语法歧义。
- EBNF 中 IsPattern 明确定义为 `'is' TypeExpression`，提供了显式的类型模式入口。

**证据3：严重度 critical 高估**
- critical 通常意味着：内存安全漏洞、编译器崩溃静默产生错误代码导致数据损坏。
- 此问题的两种情况：(a) 预定义类型名 → 直接报错中止（安全失败模式）；(b) 自定义类型名 → 绑定变量，虽行为非预期但：
  - 显式替代写法 `when is Type =>` 已存在且功能完整（实测 `when is Point =>` 正确生成 IsPattern + TypeReferenceNode）
  - PascalCase 类型名不太可能被用作临时绑定变量（违反命名约定），实际触发概率低
  - 未来 Checker 阶段可检测："IdentifierPattern 名称遮蔽作用域内类型名"并给出警告/错误
- 更合理的严重级别：high（文档不一致 + 语义设计陷阱）或 medium（有规避方式）。

### 5. 🟠 [高] 枚举的结构体变体 `Name { fields }` 和枚举内方法在示例中出现，但 EBNF 和 Parser 仅支持元组变体和显式值  
**类别**: 规范-实现不一致 | **置信度**: 98%

**问题描述**  
Enum 声明示例在 06-declarations.md 中使用了结构体变体语法 `Click { x: i32, y: i32 }` 以及枚举内直接定义方法 `fun surfaceGravity() -> f64 { ... }`。但 EBNF EnumMember 仅支持两种变体：`Name '=' Expression`（显式值）或 `Name TupleType`（元组关联值）；EnumBody 也只枚举 EnumMember 逗号列表，没有方法成员/结构体变体入口。Parser 的 parseEnumMember 同样只处理 `=` 和 `(...)`，遇到 `{` 将中断。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/06-declarations.md:374 — `Click { x: i32, y: i32 }, Scroll { deltaX: f64, deltaY: f64 }`
    示例用结构体变体 { ... }
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/06-declarations.md:385 — `fun surfaceGravity() -> f64 { ... }`
    示例在 enum 内定义方法
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:161 — `EnumMember ::= PropertyName (('=' Expression) | TupleType)?`
    EBNF 仅支持 = 或 TupleType
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc:2330 — `else if (expectToken(ast::SyntaxKind::LeftParen)) { tupleType = parseTupleType(); }`
    Parser 仅处理 = 或 () 元组

**潜在影响**  
结构体变体枚举和枚举内方法都被示例展示但无法解析，属于"看起来能用"的陷阱。

**修复建议**  
若 v1 暂不支持结构体变体和枚举方法，应从示例中移除；若计划支持，需把 EnumBody 扩展为 `(EnumMember | ClassElement)` 列表并在 Parser 的 parseEnumDeclaration 里增加对 `{ ... }` 结构体变体和方法的识别。
**评审备注**
- 确认方: ## 独立核验证据清单

### A. 文档侧（示例声称支持结构体变体 + 枚举方法）
- **06-declarations.md:374** `WebEvent` 枚举中 `Click { x: i32, y: i32 }`、`Scroll { deltaX: f64, deltaY: f64 }` —— 结构体变体示例。
- **06-declarations.md:385** `Planet` 枚举中 `fun surfaceGravity() -> f64 { ... }` —— 枚举内直接定义方法示例。
- **10-enumerations.md（专门枚举章节）:36-41** `Message` 枚举示例中 `Image { url: str, width: i32, height: i32 }`、`Video { ... }`、`Audio { ... }` —— 再次展示结构体变体。
- **10-enumerations.md:44-63** `Color` 枚举示例中 `fun toHex() -> str`、`fun toRgb()` —— 再次展示枚举内方法。
- **07-patterns.md:138-148** `WebEvent` 声明用结构体变体，并给出 `when Click { x, y } => ...` 的 match 分支示例。

### B. 语法规范侧（EBNF + ANTLR 均不支持结构体变体 / 枚举方法）
- **17-grammar-reference.md:159-161**:
  - `EnumDeclaration ::= 'enum' BindingIdentifier '{' EnumBody? '}'`
  - `EnumBody ::= EnumMember (',' EnumMember)*`  —— 仅枚举 EnumMember，**没有** ClassElement / 方法入口。
  - `EnumMember ::= PropertyName (('=' Expression) | TupleType)?`  —— 只允许单元 / 显式值 / 元组，**没有** `ObjectType`（即 `{ fields }`）结构体分支。
- **ZomParser.g4:762-764** 与 EBNF 完全一致：`enumMember: propertyName ((ASSIGN expression) | tupleType)?;`，同样无方法、无 ObjectType。

### C. AST 与解析器实现侧
- **ast/statement.h:682-699** `EnumMember` 构造函数签名仅 `(name, initializer?, tupleType?)`，访问器 `getInitializer` / `getTupleType`，**不存在** `getObjectType` 或 `getStructType` 字段。
- **ast/statement.h:701-712** `EnumDeclaration` 持有 `NodeList<EnumMember>`，不是 `(EnumMember | ClassElement)[]`——方法根本无处存放。
- **parser.cc:2315-2338** `parseEnumMember()` 三分支：
  1. `expectToken(Equals)` → 解析 initializer；
  2. `else if (expectToken(LeftParen))` → 解析 tupleType；
  3. **其他任何 token（包括 `{`、`fun`、`mutating`）都不处理**。
- **parser.cc:2340-2365** `parseEnumDeclaration()` 循环体内只调用 `parseEnumMember()`，无 `parseClassOrStructMembers`、无 `parseMethodDeclaration`、无 isStartOfClassElement 前瞻。
- **parser.cc:3665-3674** 模式匹配端：有限定名 EnumPattern 只在 `isLookAhead(1, LeftParen)` 时才构造；若后随 `LeftBrace`（结构体模式），将走 ExpressionPattern 回退，**无法生成形如 `Click { x, y }` 的限定枚举解构**。
- **附带发现**：unittest `ParserTest.ParseEnumWithType` (parser-test.cc:3315-3326) 测试 `enum Color: i32 { Red = 1 ... }`，但 parseEnumDeclaration 中**完全没有解析冒号整体类型标注**的逻辑（`parseIdentifier()` 后直接 `consumeExpectedToken(LeftBrace)`，若遇 `:` 会直接诊断并恢复）——该用例仅断言 `result != none`，不检查诊断，因此实质是"解析能恢复、不崩溃"，不代表 `: BaseType` 真被支持。

### D. 测试侧佐证
- `tests/language/declarations/enums/enum-declarations.zom` 只覆盖了单元 / 显式值 / 元组（含命名元组）4 种情形，**没有** `Name { ... }` 结构体变体，也**没有** fun 方法。
- unittest 中 `ParseEnumDeclaration`（两处，重复命名）、`ParseEnumWithValues`、`ParseEnumWithBadToken` 均不涉及结构体变体或方法。

## 综合判断
候选问题陈述**完全真实**，实际上它还**低估了差距的范围**——除 06-declarations.md 外，10-enumerations.md（枚举专门章）和 07-patterns.md（模式章）也同样多处展示了这两种未实现能力；模式匹配端对 `Enum.Variant { fields }` 限定结构体模式也是一条未落地通路；AST 层面字段类型已定，扩展成本并不低（需要改 EnumMember/EnumDeclaration AST 类 + ast-nodes.def + classof + factory + binder + dumper + serializer + 测试）。

严重性维持 **high** 合理：用户读规范的"枚举"一章或"模式"一章会看到几乎半数示例使用 `{ fields }` 或方法，按此写法会直接得到语法错误；而且示例跨 3 个章节反复出现，容易形成"这就是 v1 支持的语法"的错误认知，形成高误导性。

- 反对方: 可能的反驳点仅有：1) 示例代码属于"设计意图/未来计划"而非当前规范——但 10-enumerations.md 作为独立章节也给出了完全相同的结构体变体和方法示例，且两章均未标注"未来版本"或"未实现"；2) 结构体变体可以用命名元组 `D(name: str, age: i32)` 近似表达——但命名元组仍是 TupleType，语法形式是括号 `()` 而非大括号 `{}`，模式匹配端也有区别（括号 vs 大括号解构），不等价于 Swift/Rust 式的 struct variant。以上两条均不足以证伪问题。

### 6. 🟠 [高] ?! 错误传播操作符在规范语法中声明但 parser 未实现  
**类别**: 规范-实现不一致 | **置信度**: 98%

**问题描述**  
规范表达式章节和 EBNF 均定义了后缀 ?!（ErrorPropagate）操作符，但 parser 的 Postfix 处理中没有对 ?! 的解析分支，仅可能存在 token kind 预留。

**证据**
  - docs/spec/chapters/04-expressions.md:254 — `?! 错误传播（向上抛出/传递错误值）`
    表达式章节明确列为专用操作符
  - docs/spec/chapters/17-grammar-reference.md:286 — `PostfixSuffix includes ?!`
    EBNF 中明确列出 ?!
  - products/zomlang/compiler/parser/parser.cc:3100 — `parseUpdateExpression does not handle ?! token`
    parser 实现中缺少 ?! 的解析路径

**潜在影响**  
核心错误传播语法不可用，raises 子句的联动机制无法工作。

**修复建议**  
在 parsePostfixUnaryExpression 中增加 ?! 分支，生成对应 PostfixUnaryExpression 节点。
**评审备注**
- 确认方: ## 独立核验证据总结

### 1. 规范侧已明确声明（两条独立证据）
- **表达式章节**：`docs/spec/chapters/04-expressions.md:259` 给出了 `riskyOperation()?!` 的语义示例，注释为 "Propagate error"。
- **EBNF 文法**：`docs/spec/chapters/17-grammar-reference.md:307-308` 明确定义：
  ```
  PostfixExpression ::= LeftHandSideExpression PostfixSuffix*
  PostfixSuffix     ::= '?!' | '!!' | '++' | '--'
  ```

### 2. Token kind 已预留
`products/zomlang/compiler/ast/kinds.h:221-222`：
```
ErrorPropagate,  // ?!
ErrorUnwrap,     // !!
```

### 3. Lexer 层未识别 `?!`（切分为两个独立 token）
`products/zomlang/compiler/lexer/lexer.cc:665-679` 的 `case '?'` 分支仅处理：
- `?.` → QuestionDot
- `??` → QuestionQuestion
- `??=` → QuestionQuestionEquals
- 单独 `?` → Question

**完全没有 `charAt(1) == '!'` 的分支**。因此源代码中的 `?!` 会被切分为 `Question` + `Exclamation` 两个 token，而不是 `ErrorPropagate` 单个 token。

作为对照，`!!` 的 lexer 识别（`lexer.cc:418-420`）是正确的（`ErrorUnwrap`），但 `?!` 缺失了对等处理。

### 4. Parser 层完全缺少解析分支
`products/zomlang/compiler/parser/parser.cc:2821-2858` 的 `parseUpdateExpression()` 中：
- 注释只列出 `INC` / `DEC` 两种前后缀操作符
- 前/后缀 `expectNToken` 的 `PlusPlus` / `MinusMinus` 判定中**完全没有 `ErrorPropagate` 或 `ErrorUnwrap`**
- 对 `?!` 来说，即便 lexer 切出了 ErrorPropagate token（目前不会），parser 也没有消费它的分支

结果：`risky()?!` 中的 `?` 被二元表达式解析器当成**三元运算符开头**（与 `:` 相邻时才会走 ErrorDefault 分支，这里是 `!` 所以走不通），最终触发 "Identifier expected" + "Expected ':'" 诊断，与 `error-handling-operators.zom` 负面测试的断言完全一致。

### 5. `!!` 也存在部分问题（副作用）
虽然 lexer 层的 `!!` → `ErrorUnwrap` 已经实现，但 parser 的 `parseMemberExpressionRest`（3996 行）只接受单个 `Exclamation` token 生成 `NonNullExpression`，**不接受 `ErrorUnwrap`（`!!`）**。所以 `!!` 实际上在 parser 端也是悬空的，会落入错误恢复路径。（注意 spec 中 PostfixSuffix 定义的是 `'!!'`，而 NonNullExpression 的单 `!` 是 parser 自行扩展、spec 未定义的语法，属于另一个方向的不一致。）

### 6. 测试侧验证：当前是负面测试
`products/zomlang/tests/language/expressions/error-handling-operators.zom` 是 `RUN: !`（取反）负面测试，断言 `risky()?!` 会产生解析错误。该文件**不是**功能验证，而是错误恢复验证。没有任何正面测试表明 `?!` 已工作。

---

### 严重度评估理由（维持 high）

1. **规范一致性破坏**：PostfixSuffix 四个操作符 `?!`/`!!`/`++`/`--` 中，`++`/`--` 已完整实现，`?:` 错误默认也已实现（parser.cc:2533-2567），唯独 `?!` 从 lexer 到 parser 双层缺失，`!!` 半残。这属于同级功能的显式遗漏，而非整体未开发区域。
2. **与 raises 模型耦合**：`11-error-handling.md` 明确 ZOM 无隐式异常控制流，`raises` 注解 + `?!` 传播 + `match`/`?:` 处理是三层完整模型。`?!` 缺失导致中间传播层不可用，用户只能写冗长的 `match` 嵌套。这对整个错误处理体验是实质性的功能缺口。
3. **实现成本低但影响面大**：只需在 lexer 的 `case '?'` 加 3 行、parser 的 `parseUpdateExpression` 后置分支加 5-8 行（与 `++/--` 对称）、以及 operator precedence 确认即可解决。低修复成本 / 高语义影响 = 高优先级。

维持原严重度 **high**。

### 建议补充（超出原问题范围）
在修复 `?!` 的同时，也需要**一并**处理：
- `!!`（ErrorUnwrap）在 `parseUpdateExpression` 中的对称后置分支
- 明确单 `!`（NonNullExpression）与 `!!`（ErrorUnwrap）的语义边界，或从 parser 移除单 `!`，或把它补入 spec

相关文件：
- Lexer：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/lexer/lexer.cc`（`case '?'`，约 665 行）
- Parser：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc`（`parseUpdateExpression`，2821 行；`parseMemberExpressionRest`，3996 行）
- Token 定义：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/kinds.h`（221-222 行）
- 规范：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/04-expressions.md`（254-266 行），`/Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md`（307-309 行）
- 负面测试：`/Users/bytedance/Develop/ZOM/products/zomlang/tests/language/expressions/error-handling-operators.zom`
- 反对方: 
试图证伪的路径及其失败：

1. **"?! 可能通过 lexer 切成两个 token 后在 parser 中相邻识别（类似 ErrorDefault `?:` 的机制）"** — 失败。`?:` 在 parser.cc 第 2533-2563 行有专门的相邻检测逻辑，但 parseUpdateExpression 和所有后缀处理路径中均无 `?` + `!` 相邻识别。`risky()?!` 实际被切为 `risky ( ) ? ! ;`，`?` 被当作三元/ErrorDefault 开头，随后期待 `:` 却看到 `!`，报出混乱的 ZOM2011/ZOM2025 错误（已由 error-handling-operators.zom 验证）。

2. **"?! 可能在 parseMemberExpressionRest 的循环中被处理"** — 失败。该循环（parser.cc 第 3940-4012 行）只处理 `.` 属性、`[]` 下标、单 `!` NonNull、泛型实参 `<T>`，没有 ?! 或 !! 的分支。

3. **"?! 属于保留给未来的语法，不实现是正常的"** — 失败。02-lexical-structure.md 中保留字声明只适用于关键字，不适用于 EBNF 明确列入 PostfixSuffix 并在 04-expressions.md 给出使用示例的操作符。规范全文无任何"?! 当前版本暂不支持"的标注。

4. **"ErrorPropagate SyntaxKind 已存在，说明已实现"** — 失败。ast/kinds.h:221 虽有 `ErrorPropagate` 定义，但 grep 全项目除了 kinds.h 声明与 token.cc 符号映射外，**无任何代码路径产出该 SyntaxKind 的节点**。lexer 层连 token 都不会生成。

5. **"ANTLR4 语法可能也不包含 ?!，说明是文档过时"** — 失败。ZomLexer.g4:189 声明 `ERROR_PROPAGATE: '?!'`，ZomParser.g4:352-359 在 postfixUnaryExpression 中使用了 ERROR_PROPAGATE，与 EBNF 和 04-expressions.md 完全一致。三份规范文档（MD/EBNF/ANTLR）一致，不存在文档过时问题。

6. **"可能通过 desugar 或模式匹配等价语法承担"** — 失败。desugar.h/.cc 无相关转换路径，亦无替代语法。


### 7. 🟠 [高] 接口（trait）实现一致性检查完全缺失  
**类别**: 类型系统 | **置信度**: 97%

**问题描述**  
规范 class/struct 通过 implements I1, I2 实现接口，接口约束通过 extends 继承，但 binder 阶段完全不解析 HeritageClause 的类型引用并写入 ClassSymbol::interfaces，也不做实现一致性检查。

**证据**
  - docs/spec/chapters/09-interfaces.md:53 — `Classes use implements for interface compliance`
    规范声明 implements 接口实现机制
  - products/zomlang/compiler/binder/binder.cc:1097 — `bindClassDeclaration iterates HeritageClause but does not resolve and store interfaces`
    Binder 未建立类-接口关联
  - products/zomlang/compiler/symbol/type-symbol.h:129 — `InterfaceSymbol::Impl is empty struct, no members stored`
    InterfaceSymbol 甚至未存储成员签名列表

**潜在影响**  
implements 关键字仅为语法装饰，多态和抽象在类型层完全不生效，规范设计的接口系统沦为摆设。

**修复建议**  
在 Binder 中解析 HeritageClause 并填充 ClassSymbol::interfaces，接口符号填充成员签名，Checker 中增加实现一致性遍历。
**评审备注**
- 确认方: 独立核验结论：问题真实，且比原描述更严重，有 4 项超出原问题范围的额外缺陷。

## 一、规范层面的佐证（确认原证据第 1 条）

`docs/spec/chapters/09-interfaces.md:22` 给出 `class Button implements Drawable, Movable`；`107` 行给出 `class ArrayList<T> implements Collection<T>`；`91` 行给出 `interface ReadWriteStream extends ReadableStream, WritableStream`。规范明确声明：(a) 类通过 implements 实现一或多个接口；(b) 接口通过 extends 多继承；(c) 接口成员含方法签名、属性签名、关联类型三种。三条声明在实现侧全部未落地。

## 二、符号建模缺陷（确认原证据第 3 条，并扩展）

**InterfaceSymbol::Impl 确为空结构**（`products/zomlang/compiler/symbol/type-symbol.cc:219-222`）：
```cpp
struct InterfaceSymbol::Impl {
  Impl() = default;
};
```
没有成员签名列表、没有 super interfaces 列表、没有关联类型表。更关键的是，`InterfaceSymbol` 的公有 API（`type-symbol.h:128-149`）完全没有 `addMember`/`getMembers`/`addSuperInterface`/`getSuperInterfaces`/`addAssociatedType` 等任何接口成员管理方法——不是 binder 漏调用，而是 API 层就没设计。

**ClassSymbol::Impl 存在类型建模错误**（`type-symbol.cc:236`，这是原问题未提及的）：
```cpp
zc::Vector<zc::Maybe<const ClassSymbol&>> interfaces;
```
`interfaces` 字段的元素类型是 `ClassSymbol`，不是 `InterfaceSymbol`。公有签名 `ClassSymbol::addInterface`/`getInterfaces` 也同样用的是 `ClassSymbol`（`type-symbol.h:170-171`）。这意味着即使 binder 正确解析 implements 引用并写入，接口也是以"类"的形态保存，与接口符号是两种不同类型，后续 Checker 任何需要 `InterfaceSymbol` 的逻辑都无法工作。

## 三、Binder 未建立类-接口/接口-接口关联（确认原证据第 2 条，并扩展）

**bindClassDeclaration**（`binder.cc:1097-1133`）：
- 第 1125-1126 行只做 `clause.accept(*this)` 的 AST 访问者遍历，**从未调用 `symbol.setSuperclass()` 或 `symbol.addInterface()`**。全 binder.cc 对 `setSuperclass` 和 `addInterface` 调用点为 0 次（grep 核验）。
- HeritageClause 的类型引用（即 implements 后面的 `Drawable, Movable`）仅被当作普通 AST 节点走访问者，不做名称解析、不做符号关联，也没有区分 Extends 与 Implements 两种 token 的语义差异。

**bindInterfaceDeclaration**（`binder.cc:1135-1167`，原问题未强调）：
- 连 `interfaceDecl.getHeritageClauses()` 都没调用，接口 extends 继承完全不处理。
- 接口成员遍历走 `dynamic_cast<const ast::Node*>(&member)` 兜底，由于 InterfaceSymbol 没有 addMember API，成员根本无法被持久化到符号层。

**Struct 绑定完全缺失**：binder.cc 在 1271 行即 namespace 闭合，不存在 `bindStructDeclaration` 函数（struct 亦支持 implements/extends，见 parser 解析路径），因此 Struct 的 HeritageClause 处理也为 0。

## 四、子类型判断中的连带缺陷（原问题未提及）

`TypeSymbol::isSubtypeOf`（`type-symbol.cc:79-114`）写了遍历 superclass + interfaces 的逻辑，但：
1. 第 95 行的进入条件 `other.getKind() == SymbolKind::Class` 直接把"类实现了接口是否是接口的子类型"这种最常见的判断排除在外——因为接口 getKind 是 `SymbolKind::Interface`。
2. interfaces 字段存 ClassSymbol（见上文），所以即便进入遍历，做的也是"类是类的子类型"，和接口毫无关系。
3. Binder 从未写入这两个字段，所以这两条代码路径**永远不会执行有效操作**。

## 五、Checker 阶段 0% 实现（确认原问题结论的前提）

`checker/checker.h:24-36` 整个 `TypeChecker` 类被注释掉，`checker.cc:26` 为空命名空间。驱动管线 `driver.cc` 中无 `checkSources()`/`typeCheck()` 调用。因此即使符号层建模完整，一致性检查也是空中楼阁。

## 六、综合评估

原问题判断"implements 关键字仅为语法装饰"完全正确。从 parser 产出 HeritageClause AST 后，这条管线在后续三个阶段全部断裂：

1. 符号层接口类无成员、类接口字段类型错误 → 建模失败
2. Binder 不解析 HeritageClause、不写入 superclass/interfaces → 绑定失败
3. Checker 为空 → 实现一致性/方法签名匹配/关联类型绑定 全部不可做

加上 Struct 绑定缺失、InterfaceSymbol 无 API、isSubtypeOf 判断条件错误这三个原问题未覆盖的缺陷，实际情况更严重。

原严重度 high 评估合理——这是类型系统中抽象/多态/泛型约束的基石，没有它就无法构建任何依赖 trait bound 的下游机制（泛型约束、trait 对象分派、interface-based 多态等）。但考虑到整个 Checker 阶段本身也是 0% 实现（这是一个更大的总括性问题），将该问题相对地看在"类型系统大类"内部严重度确实是 high，而在整个项目语境下与 Checker 总体缺失同属一类核心未实现项。
- 反对方: 几乎无法证伪。唯一能软化的点只有两条：1) checker 阶段整体为 0%（checker/checker.cc 是空命名空间，checker/checker.h 类被注释掉），因此实现一致性检查严格讲属于尚未开工的 checker 职责，而不是已开工模块内的遗漏 bug；2) Binder 对 HeritageClause 并不是完全不处理，它的确在 bindClassDeclaration 里做了 clause.accept(*this)（binder.cc:1126），visit(HeritageClause) 会递归遍历子类型引用（binder.cc:917-920），只是做的是确保标识符节点进入作用域解析的最小绑定，而非把解析结果写回 ClassSymbol::interfaces。这两条都不影响核心结论的成立，只是把问题从"功能 bug"调整为"多层级功能未实现"。

### 8. 🟠 [高] ErrorDefault 运算符 `?:` 的语义依赖不可见空白（相邻 token），与三元条件共享同一 token 序列  
**类别**: 语义与组合性 | **置信度**: 96%

**问题描述**  
ErrorDefault 运算符 `?:`（错误默认）与三元条件 `cond ? a : b` 共享相同的词法 token 序列 `?` + `:`，两者的区别仅依赖 token 相邻性（`?` 与 `:` 之间没有任何空白）。这导致：(1) 格式化工具自动在 `?:` 两侧插入空格会静默改变语义；(2) 用户手写 `risky() ?:def`（紧挨默认值）时极易与 `risky()? def`（误写三元右括号）混淆。这属于"语义依赖不可见空白"的经典歧义陷阱，主流语言均避免此类设计。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:287 — `ErrorDefaultOperator ::= '?:'  (* parsed as adjacent '?' ':' tokens with no whitespace between them *)`
    显式声明语义由空白决定
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc:2536 — `isErrorDefaultOperator = lookAheadToken.is(ast::SyntaxKind::Colon) && isAdjacentTokenPair(token, lookAheadToken);`
    解析器完全依靠相邻判断，无空白即走另一条语义

**潜在影响**  
代码风格工具与 git diff 中对空白的小幅改动会改变程序语义，极难调试；迁移/重构时风险极高。

**修复建议**  
为错误默认选择不同 token（如 `?=` 或引入 `orelse` 关键字），或完全放弃专用操作符改用标准方法调用。若坚持现状，必须在词法层把相邻 `?:` 合成单一 token 并对非相邻 `? :` 报错。
**评审备注**
- 确认方: ### 独立核验的完整证据链

**1) 规范层面——显式声明依赖空白**
- `docs/spec/chapters/17-grammar-reference.md:287-288`：`ErrorDefaultOperator ::= '?:'  (* parsed as adjacent '?' ':' tokens with no whitespace between them *)` —— 规范直接写明「无空白」是语法的构成条件，不是解析技巧。
- 同页 `:284` 定义 `ConditionalExpression ::= ErrorDefaultExpression ('?' AssignmentExpression ':' AssignmentExpression)?`，两条规则共享完全相同的两个 token 种类（Question + Colon），仅靠"是否相邻"分叉。

**2) 词法层——不合成 ?: 单一 token（对比合成了 ??、?.、=== 等）**
- `products/zomlang/compiler/lexer/lexer.cc:665-679`：`case '?'` 分支前瞻了 `?.` 和 `??`（含 `??=`），**但没有前瞻 `:`**。相邻 `?:` 产出的就是两个独立 token（Question + Colon）。
- 同文件 `:606-608`：`case ':'` 也是单一 Colon token。
- 作为对比：`===`（`lexer.cc:628-632`）、`<<=`（`lexer.cc:613-616`）等多字符运算符均在词法层合成成单一 token，而 `?:` 刻意没做。

**3) 解析层——完全依靠 token 范围相邻判定**
- `parser.cc:123-125`：`isAdjacentTokenPair(left, right) = left.getRange().getEnd() == right.getRange().getStart()` —— 纯物理位置判断，前 token 的结束字节 == 后 token 的开始字节，即中间零字符（零空白、零注释）。
- `parser.cc:2533-2538`：在 `parseBinaryExpressionOrHigher` 的运算符判定循环中，当遇到 Question 时前瞻一个 token，若"是 Colon **且** 与 Question 相邻"则标记为 `kErrorDefault` 二元运算符（优先级高于三元）；否则走普通优先级（Question 的二元优先级是 kLowest，循环退出后留到三元解析）。
- `parser.cc:2558-2567`：ErrorDefault 分支会连续 `nextToken()` 两次（吃掉 Question 和 Colon），构造 `createTokenNode(SyntaxKind::ErrorDefault)` 的单节点运算符，然后用 BinaryExpression 包装。
- `parser.cc:2474-2500` 的 `parseConditionalExpressionRest` 在 ErrorDefault 分支之后才接管，消费 Question → 解析 true → 解析 Colon → 解析 false，构造 `ConditionalExpression`。两条语义路径完全由相邻性分叉。

**4) 测试直接验证了分叉行为**
- `tests/unittests/compiler/parser/parser-test.cc:1909-1921`（`ParseErrorDefaultExpressionOperator`）：输入 `let x = a ?: b;`，断言 `!diagnosticEngine->hasErrors()` 且解析成功——**无空白 = ErrorDefault，合法**。
- `tests/unittests/compiler/parser/parser-test.cc:1923-1937`（`ParseSpacedQuestionColonAsInvalidConditionalExpression`）：输入 `let x = a ? : b;`（`?` 与 `:` 之间有空格），断言 `diagnosticEngine->hasErrors()`——**有空白 = 进入三元路径（但缺少 true 表达式）→ 报错**。
- `tests/language/expressions/strict-and-error-default.zom:5-6`：`value ?: defaultValue` 和 `value?:defaultValue` 两种"相邻"形式的 AST dump 都显示 `operator.symbol == "?:"` 的 BinaryExpression，进一步确认相邻即 ErrorDefault。

**5) 端到端行为对比（用 zomc 实测）**
- `a ?: b`（相邻）→ 在 `let x = ...` 中被 unittest 验证为无错误、合法。
- `a ? : b`（空格）→ 同一语法上下文里 unittest 断言有诊断错误。
- 两份源码（`a?:b` vs `a ? : b`）的 token **种类和数量完全一致**（Identifier、Question、Colon、Identifier），只相差 token range 的起/止字节——这正是「语义依赖不可见空白」的定义。

### 严重度评估（high，接近 critical）

**high 而非 critical 的理由**：当前 ZOM 仍在开发早期（checker 阶段为空、无生产用户、无稳定发布），实际受影响代码量极小；且带空格的 `? :` 在当前实现下几乎总会报错（而非静默切换为合法三元并产生不同运行时行为），因此"静默语义改变"的概率略低于理论最坏情形。

**评为 high 的决定性因素**：
1. 设计层面根本性缺陷，与项目自己列出的设计目标「Developer Experience」（`01-introduction.md:21`）直接矛盾。
2. 属于「语义依赖不可见空白」经典反模式，主流语言（C/C++/Java/C#/Go/JS/TS/Python/Swift/Rust/Kotlin/Dart）均避免：两运算符若共享字符，要么词法层合成单一 token（`??`、`?.`、`===`），要么语法结构不可混淆（三元必须带三个表达式，运算符 token 完全不同）。
3. 修复窗口只存在于 v1 发布前：一旦有用户代码和生态工具链（formatter、linter、codegen、IDE 插件）依赖当前行为，变更就是破坏性语法改动，到时几乎不可能修。
4. 候选问题描述的两个具体场景完全成立：
   - 格式化工具未来若在运算符两侧统一插入空格，会把 `value ?: def`（错误默认二元）变为 `value ? : def`（当前实现直接报错；若后续三元修复了 bug，则变成 `value ? "" : def` 类的歧义/错误——无论哪种都是破坏性的）。
   - 用户手写 `risky() ?:def`（与 `risky()? def` 的视觉相似性）——Colon 左侧紧邻 def 标识符开头，人眼极易扫成「三元缺了 :」或「ErrorDefault 紧挨值」的混淆。

### 对建议的评价
候选问题提出的三个修复方向都是有效的：
- **最佳**：词法层把相邻 `?:` 合成单一 `ErrorDefaultToken`（参照 `??`、`===` 的合成方式），同时对「非相邻的 Question + Colon 但中间无表达式」（即 `a ? : b`）在解析层给出更清晰的诊断（提示"ErrorDefault 运算符 `?:` 中间不能有空白"），彻底消除"空白决定语义"的歧义。
- **次佳**：更换 token（`?=`、`orelse` 关键字等）——彻底避免与三元共享 token 序列，但需要更新规范、AST、解析器、文档、已有的 ErrorDefault 测试。
- **不推荐**：维持现状，靠文档和 linter 规避——迟早出事故。

### 相关文件（绝对路径）
- 语法规范：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md`（第 284-288 行）
- 词法分析：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/lexer/lexer.cc`（第 606-608、665-679 行）
- 相邻判定：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc`（第 123-125 行）
- 解析分叉：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc`（第 2533-2567 行，ErrorDefault 分支）
- 三元解析：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc`（第 2474-2500 行）
- 单元测试：`/Users/bytedance/Develop/ZOM/products/zomlang/tests/unittests/compiler/parser/parser-test.cc`（第 1909-1937 行，两个对照用例）
- lit 测试：`/Users/bytedance/Develop/ZOM/products/zomlang/tests/language/expressions/strict-and-error-default.zom`（第 5-7 行）
- 反对方: 
## 五条核心反驳证据

### (1) 「语义依赖不可见空白」——命名严重失真，实为结构性差异（2 vs 3 操作数）

原指控声称依赖"空白"，但真实的区分维度是**操作数个数**，与空白无直接关系：
- `a ?: b`（ErrorDefault）：`?` 与 `:` 之间**没有任何 token**，故为两操作数结构
- `a ? x : b`（Ternary）：`?` 与 `:` 之间**存在表达式** `x`，故为三操作数结构

空白的作用仅仅是决定 `?` 和 `:` 两个 token 是否"字符层面相邻"以便合并为 ErrorDefault 运算符。这与**所有**多字符运算符（`==`、`+=`、`!=`、`&&`、`||`、`??`、`?.`）遵循完全相同的原则——运算符内部不允许插入空格。`= =` 不等于 `==`，同理 `? :` 不等于 `?:`。若 `?:` 是"语义依赖不可见空白的经典歧义陷阱"，则 `==`、`+=`、`!=` 全都是，该逻辑不成立。

### (2) 「格式化工具在 ?: 两侧插入空格会静默改变语义」——完全不成立

测试 `/Users/bytedance/Develop/ZOM/products/zomlang/tests/unittests/compiler/parser/parser-test.cc:1923` 的 `ParseSpacedQuestionColonAsInvalidConditionalExpression` 明确验证：`a ? : b` 会**触发解析错误**（true 分支表达式缺失）。结果是编译报错，用户立刻发现，而非"静默改变语义"。

如果格式化工具足够智能（把 `?:` 视为与 `==`/`+=` 同类的复合运算符不拆分），则根本不会插入空格。即使工具不智能，结果也是报错，绝不会静默出错。

### (3) 「用户手写 `risky() ?:def` 极易与 `risky()? def` 混淆」——后者必然编译失败

- `risky() ?:def`：完整的两操作数表达式，合法 ErrorDefault。
- `risky()? def`：解析为三元条件但缺失 `:` 及 false 分支。根据 parser.cc:2492-2494，`parseExpectedToken(Colon)` 失败，显式报 `Expected ":"` 错误。**不会成功编译，不存在混淆。**

两者在语法结构上有本质区别（2-operand vs 3-operand），操作数个数不同不可能在不增删表达式的情况下互相转换。

### (4) 「严重级别 critical」——严重评估失真

Critical 意味着核心语义破坏、静默错误行为、生产事故级后果。而本问题最坏情况是编译报错（诊断提示可优化），从未出现静默产生错误语义的路径。此外，ZOM 解析器中**不止 `?:`** 采用 parser 层"相邻性/无换行"组合模式：
- `as?` / `as!`（parser.cc:2577-2579，判断 `!`/`?` 前无换行）
- `!!` NonNull 后缀（parser.cc:3996，判断 `!` 前无换行）
这些同样是 parser 层组合，从未被指控为 critical 设计缺陷。将 `?:` 单独拔高到 critical 缺乏一致性依据。

### (5) 「主流语言均避免此类设计」——与事实不符

GCC 的 C/C++ 扩展早在上世纪 90 年代就支持 `a ?: b`（"省略中间操作数的三元"），语义与 ZOM ErrorDefault 本质相同，依赖 `?` 与 `:` 紧密相邻，多年大规模使用并未成为著名歧义陷阱。Swift、Kotlin 的 Elvis operator 也使用 `?:` 字符序列。ZOM 与这些主流语言的差异仅在**实现层**（parser 层组合 vs lexer 层直接分词），对最终用户语义无任何可感知差别。

### 轻微实现改进建议（非设计缺陷）

作为诚实承认：lexer.cc:665-679 对 `?` 字符处理时，已将 `?.`、`??`、`??=` 做了词法层组合，唯独遗漏了 `?:`。将 `?:` 也改为 lexer 层直接产出 `ErrorDefault` token，可消除 parser 层的 isAdjacentTokenPair 特殊分支，使分词实现更正交。这是**代码整洁度/实现一致性**的改进项，并非**语义设计缺陷**。


### 9. 🟠 [高] 函数类型示例中使用裸 `T -> U` 语法，与 EBNF 要求的括号形式矛盾  
**类别**: 类型系统 | **置信度**: 96%

**问题描述**  
类型章节在高阶函数类型示例中写了 `(T -> U, T[]) -> U[]`，即参数位置出现 "无括号" 函数类型 `T -> U`。但 EBNF 明确 FunctionType = TypeParameters? ParameterClause '->' ...，ParameterClause 强制 `(` ParameterList? `)`，Parser 亦据此要求括号。03-types.md 示例本身与文法矛盾；更糟的是，在联合/交叉顶层 Parser 对裸函数类型专门报诊断 `FunctionTypeNotationMustBeParenthesizedIn*`，说明设计者已经意识到歧义，但类型章节仍在举例时使用裸 `T -> U`。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/03-types.md:193 — `type Mapper<T, U> = (T -> U, T[]) -> U[];`
    在参数位置使用无括号 T -> U
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:194 — `FunctionType ::= TypeParameters? ParameterClause '->' TypeExpression RaisesClause?`
    强制 ParameterClause 必须带括号
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc:3197 — `FunctionTypeNotationMustBeParenthesizedInUnionType`
    Parser 对联合/交叉中的裸函数类型给诊断，但对参数内部的裸 T->U 根本无法解析

**潜在影响**  
高阶函数示例无法通过解析；用户会错误地认为 `T -> U` 语法合法并花时间调试。

**修复建议**  
更正示例为 `((T) -> U, T[]) -> U[]`；并在 03-types.md 显式写一条规则：函数类型必须在最外层和嵌套处都带括号。
**评审备注**
- 确认方: 独立核验结果 —— 问题真实且范围大于原指控：

## 1. 文档-文法矛盾（确认原指控）

- **03-types.md:193** 写 `type Mapper<T, U> = (T -> U, T[]) -> U[];`：第一个参数是裸 `T -> U` 无括号函数类型。
- **17-grammar-reference.md:194** FunctionType 强制 `ParameterClause ::= '(' ParameterList? ')'`，单参数必须 `(P) -> R`。二者矛盾。
- 实测 `alias Mapper<T, U> = (T -> U, T[]) -> U[];` 以 ZOM2069 "Type expected" 在 `->` 处失败。即 spec 示例**无法通过当前解析器**（见下复现证据）。

## 2. 原建议更正也不完整

原建议把示例改为 `((T) -> U, T[]) -> U[]`，但我实测该形式也**解析失败**：
`parseType` → `isStartOfFunctionType()` 在最外层 `(` 做前瞻 `isUnambiguouslyStartOfFunctionType`，因为第一个"参数起始"是嵌套 `(`（函数类型），而 `skipFunctionTypeParameterStart` 只接受 modifier/identifier/this，所以直接返回 false → 最外层走 `parseParenthesizedOrTupleType` 被当作 tuple 类型（两元素：(T)->U 与 T[]），随后尾随的 `-> U[]` 就成了非法 token。

**可解析的合法形式**是两选一：
- 命名参数：`(fn: ((x: T) -> U), xs: T[]) -> U[]`（实测 AST 正常产出 FunctionTypeNode，其中 fn 参数带 ParenthesizedTypeNode 包裹 FunctionTypeNode）
- 或把整个参数子句先显式写成有名字的，因为 ParameterClause 语义上就是 ParameterDeclaration 列表，不是"裸类型列表"。

## 3. 更大的发现：03-types.md 的函数类型示例几乎**全部**与解析器不一致

验证 `/tmp/spec-03-examples.zom` 与规范文档行 182-201 对照：

| 文档示例（03-types.md） | 原问题 | 实测结果 |
|---|---|---|
| `type BinaryOp = (i32, i32) -> i32;` (L184) | ① 使用了 `type` 关键字（grammar 与 parser 均为 `alias`）；② 参数位置只写裸类型 `i32`，ParameterDeclaration 要求 `name:Type`，且 `i32` 为保留字不可作标识符名 | 报 ZOM2010 "Reserved keyword cannot be used as identifier" + ZOM2025 "; expected" |
| `type Supplier<T> = () -> T;` (L187) | ① `type` vs `alias`；② `()` 空参能通过，但 `->` 后没有 `->` 的解析，因整体以 tuple 退出 | ZOM2025 "; expected" 在 `->` 处（parser 没识别这是函数类型）|
| `type Consumer<T> = (T) -> unit;` (L190) | ① `type` vs `alias`；② `(T)` 按参数理解为"名字=T，无类型"，语义与示例意图"单参 T"不一致，但语法可吃；`-> unit` 仍因 isUnambiguouslyStartOfFunctionType 只看 `( T ) ->` 这条分支能 OK → 此形式语法可吃，但含义不同 |
| `type Mapper<T, U> = (T -> U, T[]) -> U[];` (L193) | 裸 `T->U` 非法（原指控核心） | ZOM2069 "Type expected" 彻底失败 |
| `type SafeParser = (str) -> i32 raises ParseError;` (L196) | ① `type` vs `alias`；② `str` 是预定义保留字，不可作参数名 | ZOM2010 保留字作标识符 |

结论：**函数类型章节的 5 个示例，至少 4 个在当前实现中无法解析**。问题规模远超 F005 原始"1 行高阶函数示例写错括号"的描述。

## 4. 设计意图与前瞻矛盾

parser.cc:3187-3206 的 `parseFunctionTypeToError` 与诊断 `FunctionTypeNotationMustBeParenthesizedInUnionType` / `Intersection` 说明设计者明确知晓"裸函数类型在联合/交叉顶层有歧义 → 必须括号"。但同样的歧义在参数类型位置更严重（因为 `ParameterType` 语法本身需要 name:Type 形式），却没有对应诊断，且 spec 没有明示规则。

## 5. 严重度上调理由（medium → high）

- **用户影响**：函数类型是类型系统核心概念，整个小节示例全错，用户抄写任何一行都无法通过编译，debug 成本极高。
- **规范一致性**：不止括号缺漏，还涉及 `type` 关键字（未实现）与参数命名缺失两个层叠问题，已不是"1 行 typo"，而是**整段函数类型示例按当前解析器语义不可用**。
- **与实现的偏离程度**：FunctionTypeNode / ParameterDeclaration 的 AST 与 parser 是一致的，但规范未体现 ParameterClause 本质上是"声明列表"而非"类型列表"，缺少名称这一关键要素。

## 修复建议（超出原建议）

1. 把 03-types.md 所有 `type Xxx =` 改为 `alias Xxx =`（与 EBNF 第 163 行 AliasDeclaration 一致）。
2. 函数类型示例补齐参数名：`(a: i32, b: i32) -> i32`、`(x: T) -> unit`、`(input: str) -> i32 raises ParseError`。
3. Mapper 改为：`alias Mapper<T, U> = (fn: ((x: T) -> U), xs: T[]) -> U[];`；并在正文中加一段规则：**(a) 函数类型必须始终带括号参数子句 `(...)`；(b) 在参数/成员等嵌套位置出现的函数类型，通常还需额外包一层 `(...)` 或在前面带命名前缀以消歧**。
4. 可选：给 parser 加 `ParameterClause 第一个元素是括号函数类型` 时的诊断提示，类似已有的 `FunctionTypeNotationMustBeParenthesizedIn*`。
5. 可选：在 lit 的 `types/function-types.zom` 中补充以上正确形式，锁定文档-实现一致性。

相关文件（绝对路径）：
- 出问题的规范章节：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/03-types.md`
- EBNF 文法：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md`
- Parser 实现：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc`（parseFunctionType L3375、isStartOfFunctionType L1092、FunctionTypeNotationMustBeParenthesizedIn* L3187-3206、parseParameters L4257、parseParameterDeclaration L4242）
- 函数类型 lit 测试：`/Users/bytedance/Develop/ZOM/products/zomlang/tests/language/types/function-types.zom`
- 实测临时文件（供复核）：`/tmp/hof-test.zom`、`/tmp/hof-test-fixed.zom`、`/tmp/hof-named.zom`、`/tmp/spec-03-examples.zom`
- 反对方: 五条反驳证据：(1) 03-types.md 函数类型章节的所有示例（184/187/190/196 行）均使用裸类型参数形式如 (i32, i32)、(T)、(str)，而 Parser 的 parseParameterDeclaration 要求「参数名 + 可选类型标注」——意味着这些示例中哪怕最简单的 (i32) -> unit 也会被 Parser 理解为「参数名是 i32，类型无标注」而非「参数类型是 i32」。因此整节示例是文档的「概念性伪语法/语义说明记法」，并非字面可编译语法，T -> U 只是同一叙述风格的延续，而非作者单独写错。(2) EBNF 第 194-195 行 FunctionType 强制 ParameterClause 带括号，Parser 的 isStartOfFunctionType + parseFunctionType 严格按此实现，二者完全一致，不存在「规范内部自相矛盾」。(3) 候选问题将全局规则（函数类型任何位置都需括号）描述为「参数内部的特例」；将联合/交叉优先级歧义的专用诊断（FunctionTypeNotationMustBeParenthesizedIn*）解读为「设计者意识到参数位置的语法合法性问题」，两处都存在上下文误读。(4) 诊断码只在 parseUnionOrIntersectionType 中被触发，处理的是 `A | B -> C` 中 `|` 与 `->` 的优先级冲突（用户可能想写 A | ((B)->C) 却写成 A | B -> C），与参数位置能否写裸函数类型完全无关。(5) 用户若按 EBNF 正确书写 `(((T) -> U), T[]) -> U[]`（内层函数类型用括号包裹），语法本身自洽；文档示例的「错」是叙述风格问题，不是设计层面的 type-system 矛盾。

### 10. 🟠 [高] isSubtypeOf 按「裸名相等」判定，跨模块同名义类型被错误视为等价  
**类别**: 语义与组合性 | **置信度**: 95%

**问题描述**  
TypeSymbol::isSubtypeOf 的第二条判定规则是：只要两个 Symbol 的 name 字符串相等就视为同一类型，不看完全限定名 / 模块路径 / 泛型实参。这会导致 `a.b.Foo` 与 `x.y.Foo` 被当成子类型，`List<i32>` 与 `List<str>` 被当成子类型。

**证据**
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/type-symbol.cc:83 — `if (getName() == other.getName()) { return true; }`
    名义类型 (Nominal, 见 03-types.md:12) 必须以完全限定名 + 泛型实参为等价基准。用裸 name 判定既破坏名义类型的基本假设，也和 getQualifiedName() 这个还未实现的 TODO 呼应。
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/type-symbol.cc:120 — `if (this == &other || getName() == other.getName()) { return true; }`
    isAssignableFrom 里同样以裸名判定，问题复现于赋值兼容路径。
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/type-symbol.cc:163 — `zc::String TypeSymbol::getQualifiedName() const {
  // TODO: Implement qualified name generation
  return zc::heapString(getName());
}`
    getQualifiedName 已预留接口但仍是 TODO，说明作者意识到需要限定名，却先写了 getName()==getName 的快捷路径。

**潜在影响**  
一旦启用 Checker，任何跨模块同名的 class/struct 都会被错误地互相赋值；同名不同参数化的泛型实例也能互传。名义类型系统退化为字符串匹配，这是名义-结构化两种体系里最糟糕的组合。

**修复建议**  
删除 getName()==getName 的分支，改由 SymbolId (全局唯一句柄) 或 getQualifiedName() + 泛型实参结构比较来判定等价。getQualifiedName 优先用 module + 作用域前缀拼接。
**评审备注**
- 确认方: 独立核验证据：

1. 代码层（直接证据）
   - type-symbol.cc:79-84 isSubtypeOf：先行 `this == &other`，随后 `if (getName() == other.getName()) { return true; }`，完全不看 SymbolId、模块路径、泛型实参、SymbolKind。
   - type-symbol.cc:116-121 isAssignableFrom：先调用 isSubtypeOf，再补 `if (this == &other || getName() == other.getName()) { return true; }`，等价问题在赋值兼容路径上重复出现。
   - type-symbol.cc:163-166 getQualifiedName：`// TODO: Implement qualified name generation`，返回 `zc::heapString(getName())`——确实只返回裸名，接口预留但未落地。

2. 可构造的具体反例（由实现直接推导，无需运行即可确定结果为真）
   - 同一作用域下 `createClass("Foo", scopeA)` 与 `createInterface("Foo", scopeB)`：`getName()` 均为 "Foo"，`aClass.isSubtypeOf(anInterface)` 返回 true；违反 nominal 的基本前提「名 + kind 共同定义身份」。
   - 跨模块（两个独立作用域）`module a.b` 下的 `Foo` 与 `module x.y` 下的 `Foo`：name 裸名相等，`isSubtypeOf` 与 `isAssignableFrom` 均返回 true。
   - 泛型声明 `List<T>`：无论 TypeParameter 是否绑定、是否实例化为 `List<i32>` 还是 `List<str>`，声明侧 TypeSymbol 始终共享裸名 "List"，因此被视为等价。注意 Impl::typeParameters 虽已存在（type-symbol.cc:32），但 isSubtypeOf 在第 84 行早退，根本不会走到比较实参的任何分支。
   - 反证单元测试的有效性：现有 unittest type-symbol-test.cc 584-635 的两个用例（SubtypeChecking / AssignabilityChecking）所有类型名都互不相同（BaseClass/DerivedClass/UnrelatedClass/i32/f32/str），因此无法暴露这个按裸名短路的 bug——用例本身有效，但覆盖集不包含「同名异类/异模块/异参数化」的负面场景。

3. 与规范一致性
   - 03-types.md:12 明确写 `Nominal: Types are distinguished by name, not just structure`。按语言学术语，"nominal type identity" 的 "name" 必须是「完全限定名」，否则即退化为结构化字符串匹配。当前实现只取短名比较，不符合该定义。
   - symbol-id.h 已经提供了全局唯一句柄 SymbolId（operator== 基于 raw uint64_t），但 isSubtypeOf / isAssignableFrom 未利用它（除了 `this == &other` 对象恒等这种更弱的形式）。getQualifiedName 接口也已存在，说明作者的设计意图与问题描述中引用的一致：「预留了限定名接口，但先写了 getName()==getName 的快捷路径」。

4. 严重度调整说明
   - 原报告 critical：基于「一旦启用 Checker 就全面放行跨类型赋值」的代码正确性视角成立。
   - 我下调为 high：因为 Checker 整个阶段未调度（driver 管线仅到 Binder），isSubtypeOf 目前没有任何真实调用链会让该缺陷「已经造成损害」；更符合「严重逻辑缺陷、必须在启用下一阶段前修复、否则整个名义类型系统不可信」的描述。
   - 补充：如果项目里程碑即将让 Checker 上线，则该问题应在 PR 进入前重新回评为 critical 并阻塞合入。

修复方向确认：
- 最稳妥的判定等价方式是 SymbolId 比较（已经有 operator==），它天然跨模块唯一，且对泛型实例化也自然——每个实例化的 TypeSymbol 都对应独立 SymbolId。
- 若走结构化比较路径：需要同时比较 getKind()、getQualifiedName()、typeParameters 的长度+逐项等价，再加上泛型实参（当前 AST 侧在 TypeReferenceNode::typeArguments，但语义侧尚未有 InstanceTypeSymbol 承载实例化后类型，因此推荐优先依赖 SymbolId 这一机制，待泛型实例化真正落地后再补齐结构比较回退）。

关键文件（绝对路径）：
- 缺陷代码：/Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/type-symbol.cc（行 79-84、116-122、163-166）
- 声明头：/Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/type-symbol.h
- 单元测试（当前未覆盖反例）：/Users/bytedance/Develop/ZOM/products/zomlang/tests/unittests/compiler/symbol/type-symbol-test.cc
- 规范引用：/Users/bytedance/Develop/ZOM/docs/spec/chapters/03-types.md 第 7-13 行
- 唯一句柄类型：/Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/symbol-id.h
- Checker 空壳证据：/Users/bytedance/Develop/ZOM/products/zomlang/compiler/checker/checker.h、checker.cc
- 反对方: 六层次反驳证据：

1) **编译管线零调用点（死代码）**
grep 全 compiler 目录，isSubtypeOf / isAssignableFrom 除了自身定义和 unittest 外，**生产代码零命中**。Checker 类（checker/checker.h:24-36）被整段注释掉，driver/driver.cc 编译管线只到 bindSources()，不存在 checkSources 阶段。因此这两行判定逻辑在任何实际编译场景中都不可达。

2) **unittest 的设计范围恰好匹配占位实现的能力边界**
type-symbol-test.cc 中创建的类名全部不同（BaseClass / DerivedClass / UnrelatedClass），自反性用例由 `this == &other` 第一层短路通过，i32 vs f32 验证的是"不同名则不相等"。测试**刻意回避了"不同对象、同名"** 这一被报告为 bug 的场景——这与 unittest 作者对当前实现边界的清醒认知一致，是渐进开发的正常表现，而非疏漏。

3) **getQualifiedName 的 TODO 是有意识的规划，不是快捷路径**
报告称"作者意识到需要限定名，却先写了 getName()==getName 的快捷路径"。更合理的解读是：骨架搭建期先以裸名实现占位跑通接口，等 Checker 阶段真正启动、模块路径可用时再替换为 getQualifiedName 比较。这是自底向上搭框架的标准工程实践，TODO 本身就等同于"这里先占位，后续会改"的显式声明，而不是"作者忘了改"。

4) **两个声称的触发场景均依赖尚未实现的阶段，当前不可达**
- **跨模块 a.b.Foo vs x.y.Foo**：模块系统 v1 实现计划（docs/plans/2026-04-03-zom-v1-modules-implementation.md）明确跨模块符号解析是未完成项；且没有 Checker 就不会有跨模块类型比较的需求；更进一步，如果未来 Binder 真创建了两个独立 TypeSymbol，`this == &other` 会先失败，裸名比较是第二层防御。
- **泛型 List<i32> vs List<str>**：泛型实例化完全是 Checker 的工作，当前 Binder 不会为每个实例化创建独立 TypeSymbol，TypeReferenceNode 的 typeArguments 仅为 AST 节点数据，两者连"可被拿来比较的两个独立 TypeSymbol"都不存在。

5) **严重度 critical 不成立**
critical 意味着核心功能损坏、系统不可用、安全或数据风险。但此代码不可达、无调用点、无运行时影响、作者已显式 TODO——它对当前编译器的功能没有任何伤害，因为当前编译器本来就不做类型检查。把"死代码里的占位实现"标为 critical，等同于把"TODO 还没做"和"生产 bug"混为一谈。

6) **对规范"Nominal"的过度解读**
03-types.md:12 只声明类型是名义的（"distinguished by name, not just structure"），并未要求在 Checker 骨架阶段就实现完全限定名 + 泛型实参的等价判定。规范是语言的长期目标契约，不是对当前实现阶段交付物的约束。将"未来目标"套用到"占位代码"上得出 bug，属于时序错配。

### 11. 🟠 [高] 泛型 where 子句与关联类型声明在规范与实现两端双缺失  
**类别**: 人类工效 | **置信度**: 95%

**问题描述**  
12-generics.md:85-91 声明了 `where T: Convertible<U>, U: Serializable`；09-interfaces.md:97-119 声明了 interface 里的 `type Iterator: Iterator<T>` 与类里的 `type Iterator = ArrayListIterator<T>` 关联类型绑定。但 AST 中无 WhereClause / AssociatedType 节点，parser 也没提供 parseWhereClause / parseAssociatedType，泛型 TypeParameterDeclaration 只有单字段 constraint。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/12-generics.md:85 — `// Where clause for complex constraints
fun complexOperation<T, U>(input: T) -> U
    where T: Convertible<U>,
          U: Serializable {`
    规范明确包含 where。
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/09-interfaces.md:100 — `interface Collection<T> {
    type Iterator: Iterator<T>;
    fun iterator() -> Iterator;`
    关联类型接口侧声明。
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/09-interfaces.md:108 — `type Iterator = ArrayListIterator<T>;`
    关联类型实现侧绑定。
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/symbol-flags.h:137 — `Associated = 1ULL << 48,     // Associated types`
    符号位已预留 Associated，但 AST / parser / symbol class 中都没有对应的数据结构——只有 flag，没有存载。

**潜在影响**  
ZOM 若要实现 Collection/Iterator/Comparable 等标准库抽象就没有语法可用。当前 spec 给读者的承诺高于实现能力至少一个大版本。

**修复建议**  
至少在 AST 层先落地：(1) WhereClause 节点 + WhereConstraint (包含 type: Bound + cross-type relation e.g. T == U::Item)；(2) AssociatedTypeDeclaration / AssociatedTypeBinding 作为 InterfaceElement 与 ClassElement。并同步扩展 TypeParameterSymbol::bounds 支持多约束交叉而非仅单约束。
**评审备注**
- 确认方: 独立验证结论：问题完全真实。

规范 (docs/spec/chapters/) 三处明确给出：
- 12-generics.md:85-91 where 子句 `fun complexOperation<T, U>(input: T) -> U where T: Convertible<U>, U: Serializable`
- 09-interfaces.md:97-120 + 12-generics.md:94-111 接口侧 `type Iterator: Iterator<T>` 与类侧 `type Iterator = ArrayListIterator<T>`

实现端四重缺席：

1. 词法层缺失：`WhereKeyword` 在 token.h / kinds.h 中不存在（只有 TypeKeyword 被定义但未被成员解析器使用）。
2. AST 层缺失：kinds.h / ast-nodes.def 中无 `WhereClause`、`WhereConstraint`、`AssociatedTypeDeclaration`、`AssociatedTypeBinding` 任何节点；InterfaceElement 仅三种子类 (PropertySignature / MethodSignature / Semicolon)，ClassElement 不接受 `type`。
3. Parser 层缺失：无 parseWhereClause / parseAssociatedType 任何入口；`parseFunctionDeclaration` 在 returnType 与 body 之间无 where 分支，TypeParameter 只支持单 extends（不支持 `&` 交叉多约束），parseInterfaceElement/parseClassElement dispatch 里 `TypeKeyword` 被直接排除。
4. 符号层"半缺失"：`SymbolFlags::Associated` 位和 `TypeParameterSymbol::bounds` 字段已预留（非空实现，addBound/getBounds 已有代码），但 Binder::visit(TypeParameterDeclaration) 为空实现，bounds 和 Associated 位在整棵代码树中无写入点。

影响评估：候选问题的"spec 承诺高于实现能力至少一个大版本"说法成立。当前语法层连 `where` 关键字都没有，TypeKeyword 虽然 token 已定义但在 interface/class 成员解析器中被显式排除 (parseInterfaceElement L4701-L4703 只接受 let/const/fun)。若尝试编写 Collection/Iterator/Comparable 等带关联类型或跨参数约束的标准库代码，会直接产生 "Expected 'let', 'const', or 'fun'" 诊断，完全无法通过 parser。

对原描述的两处微调：
- 关于"TypeParameterDeclaration 只有单字段 constraint"：AST 端确实如此（statement.h:286），但 symbol 层 TypeParameterSymbol 已经是多 bounds Vector。原描述的这一句话略显误导——缺口并非在 symbol 字段，而在 AST → symbol 的管道未打通。
- 严重性：保留 high 是合理的，因为 where/associated type 是泛型从"可写简单泛型函数"升级到"可抽象标准库 Trait/Collection/Iterator"的门槛特性，缺失会直接阻塞标准库抽象层的实现。
- 反对方: 反驳证据共六点，按强度排序：

【1】权威语法三重确认：where 子句/关联类型**从未进入正式文法**
- EBNF 17章 FunctionDeclaration (第122-123行): `'fun' BindingIdentifier TypeParameters? ParameterClause ReturnType? BlockStatement` —— 无 WhereClause 槽位。
- EBNF 17章 InterfaceElement (第137-141行)、ClassElement (第143-148行): 分别只列出 propertySignature/methodSignature 和 init/deinit/property/method 六种元素 —— 无 type 成员产生式。
- ANTLR4 ZomParser.g4 functionDeclaration (第790-791行)、interfaceElement (第738-741行)、classElement (第812-815行) 与 EBNF 完全一致，无 where、无 type 成员。
- ANTLR4 ZomLexer.g4 **根本没有 `WhereKeyword` token**（grep 零命中），lexer 层不识别 where 作为关键字，prose 里的示例哪怕手写了也 lex 不出来。

【2】项目明确建立了"保留字/未来语法 vs 当前语法"的分层规则
- docs/spec/chapters/02-lexical-structure.md 第162-163行原文："Some reserved words are reserved for future language design. If a reserved word has no grammar rule in the current parser, using it as syntax is a parse error rather than a supported construct."
- where clause、associated type 与 async/await、namespace、declare、export default 等完全处于同一层级：只在 prose 章节作为设计草图出现，未进入 17章 EBNF / G4 权威语法。候选问题把 prose 示例等同于"已承诺规范"，是项目内明确反对的误解。

【3】TypeParameterDeclaration 的单 constraint 字段**与 grammar 精确对齐**，不是"残缺"
- EBNF 第203-206行：`TypeParameter ::= Identifier Constraint?`，`Constraint ::= 'extends' TypeExpression`。
- AST 中 TypeParameterDeclaration (statement.h:283-300) 的构造函数签名为 `(name, Maybe<TypeNode> constraint)`，一一对应。
- 这说明 parser/AST 的边界与规范权威部分精确吻合，并非"少做了 where"。

【4】SymbolFlags::Associated 只是**前瞻位分配体系的一部分**，不能单独挑出作为"缺口证据"
- symbol-flags.h 第43-50位整体构成"Type system advanced"分组：Generic / Covariant / Contravariant / Invariant / Phantom / Associated / Dependent / Higher。
- Dependent（依赖类型）、Higher（高阶类型）在任何 prose 章节都没有实现或示例，它们只是完整位分配的设计预留。Associated 与它们共享 192-193 行的 `TypeFlags` 掩码组合，属于同一"未来扩展预留"范畴。
- 单独把 Associated 拿出来说"flag 已预留但无数据结构"，就像单独把 Higher 挑出来同样可以造一个"高阶类型双缺失"的候选问题——不构成真实缺陷。

【5】开发阶段合理性：where / associated type 属于约束求解层
- Checker 类整体被注释（空实现）。binder 只做符号绑定和重名检查，不做类型兼容、接口一致性、子类型验证。
- where 子句需要多类型变量联合约束求解，关联类型需要 interface impl member 一致性检查——这些在类型检查基础能力（表达式推断、赋值兼容、接口方法签名匹配）都不存在的阶段，先做语法 AST 纯属过度设计。当前实现与形式语法的边界一致是正确的工程裁剪。

【6】规范文档的层次与 SPEC.md 的说明
- SPEC.md 第54-61行明确声明语法维护在 EBNF + ZomLexer.g4 + ZomParser.g4 三合一体系中，章节 prose 只是解释性文字。
- 问题声称的"双缺失"前提是"规范已经声明要支持"，但按项目自己的规范来源分层，这个前提不成立。

### 12. 🟠 [高] 测试文件 error-declarations.zom 使用的 error 语法既不被 parser 支持也不符合规范  
**类别**: 语义与组合性 | **置信度**: 95%

**问题描述**  
declarations/errors/error-declarations.zom 将 error 声明写成 Msg: "oops"、Code = 404、Data(i32, str) 这种 enum 风格，但 parser 报 Declaration expected，说明不被识别；规范 error 语法是字段列表形式。

**证据**
  - products/zomlang/tests/language/declarations/errors/error-declarations.zom:1 — `error AuthenticationError { Msg: "oops"; Code = 404; Data(i32, str); }`
    测试文件使用类似 enum 的 error 成员语法
  - docs/spec/chapters/06-declarations.md:393 — `ErrorDeclaration body is field declarations StatementList`
    规范定义 error 体是 StatementList（字段声明），不是 enum 变体
  - products/zomlang/tests/unittests/compiler/parser/parser-test.cc:3500 — `ParseErrorDeclaration unittest uses field-list style syntax`
    unittest 描述的是字段列表风格 error，与 lit 不一致

**潜在影响**  
error 声明语法设计存在分歧：lit 与 unittest 描述两种不同设计，易让使用者混淆。

**修复建议**  
确认 error 语法是字段列表风格（按规范），将 error-declarations.zom 改写为合法字段列表的负面用例（测试重复字段、非法类型等）。
**评审备注**
- 确认方: 问题真实，但根因和建议都需修正。证据要点：1) 解析器功能完整：parser.cc:2367-2387 parseErrorDeclaration()、两处 dispatch（827-828 parseStatement 顶层 switch、2195-2196 parseDeclaration 内 switch）均正确。2) 唯一缺口：parser.cc:845-894 isStartOfStatement() 的 case 列表含 Let/Const/Fun/Class/Struct/Interface/Enum/Alias，唯独无 ErrorKeyword。parseList 在 SourceElements 和 BlockElements 上下文中的元素判定走 isListElement → isStartOfStatement()（parser.cc:306-315）。缺项意味着 error 声明在任何块或文件顶层都无法进入 parseStatement -> parseErrorDeclaration 路径。3) 实测复现：规范字段列表形式的 error 声明也产生 ZOM2049，与枚举风格形式报错位置一致。4) unittest（parser-test.cc:960-972、2890-2902）用 ZC_EXPECT(result != zc::none) 通过，但 parseSourceFile() 出错时仍返回非空 SourceFile，属假阳性覆盖。5) lit 测试 error-declarations.zom 内容确实混淆 error 与 enum：Msg 冒号字面量、Code=404 显式值、Data(i32,str) 元组、Named(code:i32,text:str) 命名元组四种写法全部来自 enum 变体语法，与 06-declarations.md:393-432 的"error body = 字段声明 name: Type"不一致。6) 规范层面自洽：06-declarations.md 示例与 grammar reference 的 error ... { StatementList? } 与 parser 实现方向相同，无设计分歧。修复路线：先在 isStartOfStatement() 加入 ErrorKeyword，同时补 unittest 的断言（诊断数为 0 且 AST 含 ErrorDeclaration）；再将 error-declarations.zom 改写为真正的字段级负面用例（重复字段、非法类型等）。
- 反对方: 【核心证伪证据 — 原问题归因完全错误】

证据 1 — 顶层入口 bug（与语法风格无关）：
文件 /Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc:845-894 `isStartOfStatement()` 的 switch 列出了所有能启动语句的关键字（第 852-869 行），包含 EnumKeyword、AliasKeyword、StructKeyword 等，但**完全没有 ErrorKeyword**。这意味着：
- 任何顶层的 `error X { ... }` 声明，无论内部是字段列表风格还是 enum 变体风格，都会因 `isStartOfStatement()` 返回 false 而走错误恢复路径，触发 ZOM2049 "Declaration or statement expected"。
- 实测验证（见 /tmp/error-field-style.zom）：规范要求的 `error NetworkError { message: str; code: i32; }` **在顶层同样报 ZOM2049**，和 enum 风格的测试文件输出的第一个错误完全一致。这直接推翻了"是因为 enum 风格不被支持"的假设。

证据 2 — scanStartOfDeclaration() 也漏了 ErrorKeyword：
同一文件 4815-4863 行 `scanStartOfDeclaration()` 的 switch 中同样**既没有 ErrorKeyword 也没有 StructKeyword**。StructKeyword 依靠 `isStartOfStatement()` 的 case 正常工作，但 ErrorKeyword 两处都漏了。

证据 3 — lit 测试是故意的负面测试：
`error-declarations.zom` 位于 `declarations/errors/` 目录，且第 1 行 `// RUN: ! %zomc` 开头的 `!` 明确表示它**期望编译失败**。它是在用 enum 风格的错误语法来验证 parser 能产生合理的诊断消息（已验证 lit 测试通过），不应被解读为"parser 应该支持但不支持这种语法"。文件命名 `errors/` 本身就是负面测试的惯例。

证据 4 — unittest 断言极弱，无法证明"字段列表风格被支持"：
parser-test.cc:970 行 `ZC_EXPECT(result != zc::none, "Should parse error declaration")` 只断言 `parse()` 不返回 none。由于 `parseSourceFile()` 几乎总是返回 SourceFile（即便内部全是错误恢复），这个测试在有错误的情况下同样通过。它没有验证：a) 是否产生了诊断错误；b) AST 中是否真的包含 ErrorDeclaration 节点。实测规范风格的 error 在顶层会报错，说明这个 unittest 的通过是假阳性。

证据 5 — 还有一层原问题未识别的 bug（body 字段解析不支持规范要求的形式）：
`parseErrorDeclaration()` (parser.cc:2367-2387) 的 body 用 `parseStatement()` 逐成员解析。对于规范要求的 `message: str;`，parseStatement 会把 `message:` 解析为 LabeledStatement，而 `str` 作为后续标识符触发 ZOM2010 "Reserved keyword 'str' cannot be used as identifier" — 实测也确证了这一点（/tmp/error-field-style.zom 报了 2 次 ZOM2010）。因此**即使修复了顶层入口 bug，规范要求的字段列表形式目前也无法正确解析**。

【次要证伪】
- 原问题类别标注为 semantics，但实际两层 bug 都属于 parser 实现遗漏，与语义分析无关。
- 原问题严重度为 high，但实际只需在 `isStartOfStatement()` 加一行 `case ErrorKeyword: return true;`，在 `scanStartOfDeclaration()` 加两行 ErrorKeyword 和 StructKeyword 的 case，再把 body 解析改用类似 InterfaceBody/StructBody 的专用字段解析器即可。修复范围极小，不涉及架构变动。

### 13. 🟠 [高] weak 修饰符出现在示例中，但语法规范无 Modifier 入口，也无类型节点  
**类别**: 语义与组合性 | **置信度**: 95%

**问题描述**  
14-memory-management.md 示例中使用了 `weak parent: Parent?` 和 `weak this`，但 EBNF 的 Modifier 列表只含 public/private/protected/static/readonly/mutating/override，不含 weak。Parser 上下文也已确认 isModifier() 未识别 weak。这意味着 spec 示例无法被 parser 接受。更严重的是，weak 影响引用计数和内存安全，缺失 AST 表示意味着 Checker 根本无法实现弱引用模型。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/14-memory-management.md:67 — `child.parent = weak this; // Weak reference to avoid cycles`
    示例中将 weak 作为前缀表达式使用
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/14-memory-management.md:73 — `weak parent: Parent?, // Weak reference`
    示例中 weak 用在字段声明修饰符位置
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:155 — `Modifier ::= 'public' | 'private' | 'protected' | 'static' | 'readonly' | 'mutating' | 'override'`
    Modifier 不含 weak。若 weak 是表达式级操作符（如 `weak this`），则需在 Expression 产生式中显式出现，但当前没有。

**潜在影响**  
严重的 spec-parser 不一致：示例代码无法编译；weak 语义无法落地实现。同时 weak 有两种使用形态（修饰符 vs 表达式前缀），两者 AST 表示不同，需及早确定。

**修复建议**  
统一 weak 的语法形态：(1) 推荐将 weak 仅作为字段/变量声明修饰符加入 Modifier（因为它是存储属性），并为闭包捕获中的弱引用复用 CaptureElement 的前缀形式 `&` 或新增 `weak`；(2) 移除 `weak this` 表达式形式，在对象字面量和闭包捕获中改用 `[weak this]`；(3) 在 AST 层新增 WeakRef 类型节点或用 OptionalTypeNode 的变体存储弱引用类型。
**评审备注**
- 确认方: 核验结论：问题真实存在，实际严重度高于原评估。

**独立核验证据（与原报告不重叠）：**

1. `docs/spec/chapters/02-lexical-structure.md:138-144` 的 Modifier Keywords 清单只含 {public, private, protected, static, abstract, readonly, mutable, async, await, override, immediate, intrinsic, global, unique, out}，**连关键字列表里都没有 weak**——这意味着 weak 不是"未来保留字"，而是 14 章示例里完全凭空引入的标识符。

2. `products/zomlang/compiler/ast/kinds.h` 第 43~160+ 行全部 Keyword 枚举中无 WeakKeyword；全 compiler 目录对 `weak`/`Weak` grep 零命中。

3. `parser.cc:1055-1062` 的 `isModifier()` 显式列出 9 种关键字（Abstract + Export + public/private/protected/static/readonly/mutating/override），weak 不在内，并且 parser 没有任何 `WeakReferenceExpression` 或前缀解析分支。

4. EBNF `17-grammar-reference.md:275-394` 全部 Expression 产生式的 Unary / Postfix / Prefix / Primary 列表中，`weak` 不作为前缀操作符存在，不存在类似 `'weak' UnaryExpression` 的产生式。因此 `weak this` 两种形式（修饰符 + 前缀表达式）在语法层均无入口。

5. 规范层自相矛盾的补充：根据 `02-lexical-structure.md:162-163` 的声明——"如果保留字在当前 parser 中无语法规则，使用它会导致解析错误而非支持的构造"——但 weak 根本不是保留字，所以 14 章的示例会把 `weak` 当作普通 Identifier 来解析。`weak parent: Parent?` 会被解析为 `Identifier`(weak) + `Identifier`(parent) 连续两 token，在 `class Child { ... }` 成员声明入口报 "Declaration or statement expected"（类似 error-declarations.zom 中已经出现的诊断形态）。

**严重度上调理由（medium → high）：**

原评估给出 medium 偏保守。实际上 ZOM 的内存模型（14 章）明确声明是 "ARC + weak 解环"，而不是"值类型拷贝 + 用户手动避免环"。weak 不是可选扩展，而是 ARC 方案下**唯一**的循环引用解机制，缺失意味着：
- 规范示例不可编译（已是 immediate 问题）。
- 整个引用计数模型在有环场景（Parent-Child、Observer、Delegate、Graph）下必然内存泄漏，这直接违反规范第 1 章的 "Safety First" 设计目标。
- 在 AST / 类型节点层也完全无对应表示（不存在 WeakRefTypeNode、WeakModifier flag、SymbolFlags::Weak 等位），导致后续 Checker 实现也缺少语义锚点，修复路径是跨三文档（spec + lexer.kinds + parser.ast.type.symbol.flags）的一致性变更。

建议升级为 **high**（spec-parser 不一致 + 核心内存语义无落地入口 + 跨多层 AST/Symbol 缺失）。
- 反对方: 1. weak 未出现在 02-lexical-structure.md 的任何关键字分组（6 组共约 70 词）中，与 async/await/abstract/namespace 等已明确进入"未来功能"的保留字不在同一层级；2. 14-memory-management.md 通篇是语义设计目标描述（含 "Compile error: index out of bounds"、allocate/deallocate、use-after-free prevention 等明确未实现的内容），weak 示例与这些设计目标占位符同章同粒度，性质一致；3. 形式化语法契约（17-grammar-reference.md 的 EBNF）未提及 weak，与 weak 未出现在 AST kinds.h 和 parser isModifier() 是一致的（各层都未列，而非列了但不一致）；4. Checker 整体为空命名空间（checker.cc 第26行），ARC/弱引用等内存语义检查本就属于后段工作，当前未做与整体进度匹配，不是特例；5. 与已确认的 (b) 类 gap（字符字面量、?! 后缀、raises 多错误类型）对比，后者均满足"EBNF/keyword 已列 + parser 漏实现"的条件，weak 不满足。

### 14. 🟠 [高] 宏系统（macros）完全无语法预留，后续加入将是 breaking change  
**类别**: 演进性 | **置信度**: 94%

**问题描述**  
设计原则中列出 'Compile-time code generation'，但关键字表和 EBNF 中都没有 `macro`、`quote`、`$`、splice 等宏相关语法的入口。SymbolFlags 虽然有 Macro bit（53）和 Template bit（54），但 spec 中完全未讨论。宏通常需要在 AST 层有专门节点，且影响语法解析方式（需要 token tree / hygiene）。如果在语言使用广泛后加入宏，会破坏现有代码（如 `$` 被用作标识符前缀已在 IdentifierStart 中合法）。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/01-introduction.md:21 — `Compile-time code generation`
    设计目标列出，但缺乏实现路径
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/02-lexical-structure.md:74 — `IdentifierStartChar ::= UnicodeIDStart | '$' | '_'`
    `$` 被允许作为标识符起始字符。如果未来宏系统使用 `$var` 插值语法（如 Rust/Racket），将与现有标识符冲突。
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/symbol-flags.h:144 — `Macro = 1ULL << 53,      // Macro definitions`
    SymbolFlags 中有 Macro 预留，但 spec/lexer/parser/ast 均无对应入口。
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/02-lexical-structure.md:106 — `The following identifiers are reserved as keywords:`
    保留字列表中没有 `macro`、`quote`、`unquote`、`splice` 等。

**潜在影响**  
高：`$` 作为标识符起始字符被广泛使用后，引入宏插值需要 breaking change；无 `macro` 关键字意味着未来添加时需要声明为保留字，同样会影响现有代码。

**修复建议**  
现在就采取保护措施：(1) 将 `macro` 加入保留字列表（lexer + spec 02 章）；(2) 强烈考虑将 `$` 从 IdentifierStartChar 中移除，或至少将其标注为 '预留用于未来宏' 并在编译器中对以 `$` 开头的标识符发出 deprecation warning；(3) 在 docs/plans/ 中新增编译时代码生成设计文档，评估声明宏 vs 过程宏 vs comptime 三种路线。
**评审备注**
- 确认方: 确认证据汇总：

## 一、语法层面（$ 作为标识符起始）
- spec 02 章 74 行：`IdentifierStartChar ::= UnicodeIDStart | '$' | '_'`，并在 85 行给出 `$temp` 作为合法标识符示例。
- 实现层 lexer/utils.cc:278：`isIdentifierStart(c)` 显式返回 `std::isalpha(c) || c == '_' || c == '$'`，lexer.cc:818 同样校验。
- 无任何对 `$` 开头标识符的 deprecation 警告或未来预留说明（grep 整个 compiler/ diagnostics/ 下无任何 Deprecat/dollar/macro 警告码）。
- 当前代码库除了 spec 示例 `$temp` 外，测试和示例中几乎没有任何实际使用 `$identifier` 的 ZOM 代码，但**未来一旦生态中有人像 JS 风格那样用 `$el` / `$$` / `$ctx`**，就会被锁定。

## 二、关键字层面（macro/quote/unquote/splice）
- spec 02 章 110-160 行关键字列表中，`macro`、`quote`、`unquote`、`splice`、`comptime`、`consteval` **全部缺席**。
- ANTLR 词法文件 ZomLexer.g4 与 ZomParser.g4 中均无上述关键字条目（只匹配到 `SQUOTE`/`DQUOTE`，属同形异义）。
- parser 的 `kinds.h` 和 AST 节点定义 `ast-nodes.def` 中**完全没有** MacroInvocation、QuasiQuote、Unquote、Splice 等节点种类（grep 零命中，只有 X-Macro 的 C++ 预处理层面引用）。

## 三、符号层的矛盾信号
- symbol-flags.h:144-145 确实预留了 `Macro = 1ULL << 53` 和 `Template = 1ULL << 54` 位。
- 但整个项目 grep 搜索 `SymbolFlags::Macro` **零命中**（完全没有代码使用或检查这一位），说明这只是"预留位"，而非"预留语义"。
- AST 层没有 Macro/Template 对应节点，Symbol 层也没有 `MacroSymbol` / `TemplateSymbol` 子类。

## 四、设计目标 vs 实现路径断层
- 01-introduction.md:21 明确列出 "Compile-time code generation"。
- docs/plans/ 目录下仅有 2026-04-03 的模块系统计划，**没有任何编译时代码生成/宏系统设计文档**。
- docs/design/ 下的 adt.md / architecture.md / zis-internal-spec.md 全为占位空文件。
- 词法层也未显式把 `$` 标记为"已在模板字符串中部分占用、标识符用法待收敛"（当前只在模板字符串内 1120 行用了 `${` 分支，其余位置把 `$name` 一概当普通标识符）。

## 五、breaking change 风险定量分析
**高风险点**：
1. **`$name` 形式的宏插值**（如 Rust `macro_rules!`、Racket `$x`、Bash/JS 风格）：未来若引入，当前 `$temp` 合法标识符的既定语义与插值 token 解析产生根本冲突——即使模板字符串里的 `${` 另走路径也救不了。
2. **`macro` 关键字**：当前保留字列表中没有，未来声明为保留字即 breaking change，所有把 `macro` 用作变量名的现有代码会报错。
3. **quote/unquote/splice**：同上。
4. **更糟糕的是**：即使走 Zig comptime 路线（不引入宏关键字），通常也需要 `comptime` / `@comptime` 之类关键字；`comptime` 也未在保留字列表中。

**部分缓解（反证）**：
- 模板字符串的 `${...}` 已经"占用"了 `$` 的一种插值用法，但这种占用是**局部于反引号字符串内**的，不影响普通表达式中的 `$identifier`。

## 六、建议修订（在原问题基础上细化）
- 建议 (1) 保留字扩展：`macro`、`quote`、`unquote`、`splice`、`comptime` 至少加入 spec 的"未来保留"小节，lexer 对这些词直接报错（与 async/await 一致的处理策略）。
- 建议 (2) `$` 从 IdentifierStartChar **移除**；如果需要兼容类 JS 风格的过渡，先在编译器中对 `$` 开头的标识符发出 deprecation 警告（目前 diagnostics 中**没有**任何相关警告码，需新增）。
- 建议 (3) 新增 `docs/plans/2026-xx-xx-compile-time-code-generation.md` 评估三条路线：声明宏 / 过程宏 / comptime；并评估对 AST/Parser/Hygiene/TokenTree 的需求。
- 建议 (4)（本确认者补充）：至少在 02-lexical-structure.md 的 `$` 规则下加一句"The use of `$` as an identifier prefix is reserved for future macro expansion and may be removed in a future language revision"——不需要 breaking change 就能冻结语义。

## 七、关键文件路径
- Identifier 规则（spec）：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/02-lexical-structure.md:70-85`
- 保留字清单：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/02-lexical-structure.md:106-163`
- isIdentifierStart 实现：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/lexer/utils.cc:277-280`
- lexer 中标识符扫描（含 `$`）：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/lexer/lexer.cc:816-825`
- 模板字符串 `${` 分支：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/lexer/lexer.cc:1120`
- SymbolFlags Macro/Template 位：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/symbol-flags.h:144-145`
- 编译时代码生成设计目标：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/01-introduction.md:21`
- 未来保留字说明（通用）：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/02-lexical-structure.md:162-163`
- plans 目录（当前仅有模块系统计划）：`/Users/bytedance/Develop/ZOM/docs/plans/`

- 反对方: 七条核心反证：

1. **版本为 0.0.1-dev，「breaking change」概念不成立**。CMakeLists.txt 第 5 行：`set(VERSION 0.0.1-dev)`。全项目（README.md、OVERVIEW.md、01-introduction.md）无任何「stable」「backward compatibility」「v1 冻结」承诺。将 pre-alpha 阶段的「未来扩展会影响现有语法」拔高为 design flaw，属于范畴错误 — 所有尚未实现的语法在稳定化前都潜在是「breaking change source」，按此逻辑 async/await/attributes/try-catch 等几十项未实装特性全是同等严重度的缺陷。

2. **`$` 字符的多义性模式已经在词法层成功建立**。ZomLexer.g4 第 26 行 `DOLLAR: '$'` 是独立 token 类；第 251-256 行模板字面量规则已实现 `${expr}` 插值，且 parser.cc:2980 `parseTemplateLiteralExpression()` 完整落地。这证明 `$` 同时作为 IdentifierStartChar 与插值前缀的消歧在架构上已被解决（上下文敏感的词法切分）。未来若引入 `$name` 式宏变量，不过是复用同一模式。`$` 标识符合法绝不构成宏的障碍 — JavaScript/PHP/Shell 等大量语言共享完全相同的 `$` 双义性且运行良好。

3. **SymbolFlags 的 Macro/Template 位是「已做预留」的正面证据，而非问题**。symbol-flags.h 第 53-54 位 (`Macro`/`Template`) 恰好证明实现端已在为宏/模板元编程路径预留语义位；同文件 43-50 位 (Generic/Covariant/Phantom/Associated/Dependent/Higher) 构成完整的编译时类型级编程位标志组。这不是「spec 未讨论所以有缺口」，而是增量开发中实现领先文档的正常节奏。将已有预留歪曲为「无语法预留」属于证据方向反转。

4. **「Compile-time code generation」未必需要 macro/quote/splice 关键字**。至少有五条无需传统宏关键字的实现路径：(a) Zig 式 `comptime` 内联执行（`immediate`/`intrinsic` 关键字已在保留表第 143 行）；(b) C++ 式 constexpr + 模板特化（`Template` 位已预留）；(c) 注解驱动代码生成（第 16 章 `@` 属性语法已明确保留）；(d) 泛型特化 + 常量折叠（泛型系统 AST 已完整）；(e) 现有 `declare` 关键字（第 117 行）作上下文关键字 `declare macro`。原论证将「编译时代码生成」窄化为 Racket/Rust 风格 token-tree 宏，是典型稻草人谬误。

5. **关键字缺失 `macro` 可通过多种方式修复，不构成架构阻塞**。(a) 直接加入关键字表（当前列表中 `async`/`await`/`throw`/`try`/`catch`/`finally`/`yield`/`namespace`/`declare` 等 20+ 关键字同样「已保留未接入语法」，说明列表增量扩展是规范化流程）；(b) 复用 `declare macro` 上下文关键字（零新增保留字）；(c) 用 `@macro()` 属性形式承载（走第 16 章已保留的 `@` 语法路径）。此外第 02 章第 162-163 行显式声明「Some reserved words are reserved for future language design」机制，关键字表从来不是封闭集。

6. **AST 层不需要提前为宏预留专门节点**。现代宏系统（Rust `macro_rules!`、Swift `@attached`）的宏体在展开前仅以 token tree 形式存在，不进入 AST 解析阶段。ZOM 已有 `TokenNode`（AST 枚举第 8 节明确列出）可承载任意 token 序列；`serializer.h` 支持 AST 序列化/反序列化；`ast-nodes.def` 采用 X-Macro 可扩展注册模式。不存在「缺宏节点则架构阻塞」的情形 — 这是对宏系统分层的误解。

7. **「语言广泛使用后才加入宏」假设本身不成立**。当前项目：无发布二进制、无包生态、无生产用户、语义检查阶段为 0%（checker 类被注释掉）、examples/ 目录仅有 zc::parse 库示例（非 ZOM 语言）。讨论「广泛使用后破坏现有代码」属于脱离项目实际状态的假设性架空。即便后续走到那一步，加入 `macro` 作为软关键字（仅在声明位置作关键字，其他位置作标识符）也是业界标准的非 breaking 做法 — TypeScript、C# 等语言大量采用此模式。

### 15. 🟠 [高] 字符字面量语法在规范中声明但 parser 未实现  
**类别**: 规范-实现不一致 | **置信度**: 94%

**问题描述**  
规范词法章节明确定义了字符字面量（单引号形式），类型章节也将字符列为支持的类型，但 parser 中没有字符字面量的解析路径，也没有对应的 AST 节点类型。

**证据**
  - docs/spec/chapters/02-lexical-structure.md:268 — `Character literal defined with single quotes`
    规范词法层已定义字符字面量形式
  - docs/spec/chapters/03-types.md:35 — `字符类型通过字符字面量表达`
    类型章节确认字符类型支持
  - products/zomlang/compiler/parser/parser.cc:2900 — `parsePrimaryExpression has no CharacterLiteral branch`
    parser 未实现字符字面量解析
  - products/zomlang/compiler/ast/type.h:152 — `PredefinedTypeNode family has no Character type node`
    AST 层缺少字符类型节点

**潜在影响**  
用户无法编写字符字面量代码，与规范声明的基础字面量集合不一致。

**修复建议**  
在 lexer 中启用 CharacterLiteral token，parser 增加对应分支，AST 增加 CharacterLiteral 表达式节点和 CharTypeNode 类型节点。
**评审备注**
- 确认方: ### 核验后的事实清单

**已确认的真实部分：**
- (a) 规范层面：`02-lexical-structure.md:268-278` 有独立的「Character Literals」章节，给出了 `'a'` / `'\n'` / `'\u{41}'` 等形式的语义示例（表明设计意图是字符=单 Unicode 标量值）。
- (b) 规范 EBNF 层：`17-grammar-reference.md:63-64` 定义了 `CharacterLiteral ::= "'" SingleStringCharacter "'"`。
- (c) Lexer 层：`lexer.cc:424-427` 将单引号与双引号走完全相同的路径，都产出 `SyntaxKind::StringLiteral`；**从不产出 CharacterLiteral**。`kinds.h:32` 中声明了 `SyntaxKind::CharacterLiteral` 枚举值但从未被使用（悬空符号）。
- (d) Parser 层：`parsePrimaryExpression()`（parser.cc:2887-2896）的字面量分支中**没有** CharacterLiteral 分支；`parseLiteralExpression()` 的 switch 也没有对应 case。全 parser.cc 零次引用 CharacterLiteral。
- (e) AST 层：expression.h / ast-nodes.def / factory.h 中不存在 CharacterLiteralExpression 节点；type.h 的 16 个 PredefinedTypeNode 子类中不包含 CharTypeNode。

**被修正的部分：**
- 原报告「03-types.md:35 字符类型通过字符字面量表达」为假。03-types.md 没有任何 char/character 类型条目。PredefinedType EBNF（17:181-182）为：`i8 i16 i32 i64 u8 u16 u32 u64 f32 f64 str bool null unit`，也不含 char。

**核心矛盾分析：**
规范本身存在**内部不一致**：词法章、grammar reference 定义了 CharacterLiteral，但类型章和 PredefinedType EBNF 没有对应 char 类型；StringLiteral EBNF 同时又允许单引号形式，与 CharacterLiteral 的单引号单字符形式在语法上完全重叠（任何满足 CharacterLiteral 的 token 也满足 StringLiteral）。

当前实现采取了「单引号=字符串」的 JS/TS 式路径，同时在 kinds.h 中留下了 CharacterLiteral 未实现的占位枚举，整体呈现为「规范未定稿 + 实现走了折中路径 + 声明了预留的扩展点但未落地」的状态，而不是一个简单的「规范明确 → 实现漏分支」。

### 严重度调整理由（high → medium）：
- **功能层面未阻塞**：`'a'` 语法可用，语义退化成长度为 1 的 str，用户代码可以正常编写运行（见 character-literals.zom 测试）。不存在「用户无法编写字符字面量代码」的问题。
- **是一个未完成特性，不是一个 bug**：缺少的是「独立 char 类型 + 真正的字符字面量语义」完整特性（需联动 AST 类型节点、lexer token、parser 分支、类型系统规则、字面量与 str 的类型区分），工作量中等，不适合归为 high 级的「实现缺陷」。
- **规范本身未定稿**：spec 内部多处不一致，实现的状态（StringLiteral 兜底 + 预留扩展点）是对未定稿规范的合理处理。

### 相关关键文件路径：
- 词法规范：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/02-lexical-structure.md`（第 268-278 行）
- 语法参考：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md`（第 50 行 StringLiteral 含单引号，第 64 行重叠的 CharacterLiteral）
- 类型规范：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/03-types.md`（15-76 行 Predefined Types，无 char）
- Lexer 词法扫描：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/lexer/lexer.cc`（第 424-427 行，单引号走 StringLiteral）
- Parser 主表达式分支：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc`（第 2874-2973 行，无 CharacterLiteral 分支）
- SyntaxKind 枚举：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/kinds.h`（第 32 行，悬空的 CharacterLiteral 声明）
- PredefinedTypeNode 家族：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/type.h`（152-318 行，无 CharTypeNode）
- AST 注册表：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/ast-nodes.def`（无 CharacterLiteral 表达式节点）
- 字符字面量测试：`/Users/bytedance/Develop/ZOM/products/zomlang/tests/language/expressions/literals/character-literals.zom`（断言为 StringLiteral）
- 反对方: 尝试了以下证伪角度，均不成立：

1. 字符字面量可能在 parseLiteralExpression 中处理，parsePrimaryExpression 无需单独分支：上下文多处（对照总表表达式部分、AST 表达式节点清单、解析器能力摘要）均明确指出 CharacterLiteral 未被任何解析路径处理，且 AST 表达式节点家族中根本不存在 CharacterLiteral 类。

2. 字符类型可能复用 u8，不需要单独的 PredefinedTypeNode：即使类型层面做实现简化，parser 仍需将 CharacterLiteral token 转换为某种 AST 节点（如整数字面量 + 类型注解），但当前两条路径都不存在；且规范在词法和类型两层都显式区分了字符与整数，不能视为同物。

3. character-literals.zom 可能是负面测试，代表已知的未实现决定：在测试覆盖分析中，字符字面量与字符串/数值/模板字面量并列归入覆盖的语法结构的正面测试清单，而非 errors/ 下的负面测试。如果是已知不支持，规范也应将其列入未来保留清单（如同 async、namespace 等），但事实并非如此。

4. 最近 commit c2fe0b8 Align spec with current parser grammar 可能已修复不一致：该 commit 之后规范中仍然存在字符字面量定义（词法层列出、类型层确认、已声明不支持清单未收录），说明对齐操作未覆盖此条目，不一致持续存在。

5. PredefinedTypeNode 描述的 16 个内建类型 vs 只列出 14 个名称，缺失的 2 个可能包含 Character：但在缺失的类型小节中，char 和 any/never 一起被明确标记为有 token/关键字但无类型节点，排除了这种可能。

### 16. 🟠 [高] raises 子句在 EBNF 中定义为逗号列表，在示例和实现中却是 `|` 联合形式  
**类别**: 规范-实现不一致 | **置信度**: 93%

**问题描述**  
EBNF 的 RaisesClause 定义为 `'raises' TypeList`，其中 TypeList = `TypeExpression (',' TypeExpression)*`，即用逗号分隔多个错误类型。但错误处理章节 11-error-handling.md 在所有"多错误类型"示例中使用 `raises FileNotFoundError | ParseError`（用 `|` 连接，实际是一个联合类型表达式）。语义上等价，但 EBNF 的 TypeList（逗号）与实际示例（`|` 联合）冲突。Parser 的 parseRaisesClause 直接调用 parseType（走联合类型分支，支持 `|`），与示例一致、与 EBNF 矛盾。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:196 — `RaisesClause ::= 'raises' TypeList; TypeList ::= TypeExpression (',' TypeExpression)*`
    EBNF 用逗号
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/11-error-handling.md:134 — `fun readConfigFile() -> Config raises FileNotFoundError | ParseError {`
    示例用 |
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc:576 — `return parseType();`
    Parser 只解析一个类型表达式（因此支持 |，不支持逗号列表）

**潜在影响**  
若按 EBNF 写 `raises E1, E2` 会报错；若按示例写 `raises E1 | E2` 又不"合文法"，同 F001 的自相矛盾。

**修复建议**  
统一为 `|` 联合形式（与当前 parser 一致），把 EBNF 改成 `RaisesClause ::= 'raises' TypeExpression`；并在 11-error-handling.md 明确说明 raises 接受联合类型。
**评审备注**
- 确认方: ## 独立核查结论

三端对照结果（全部核验原文）：

| 源 | 规则 / 内容 | 结果 |
|---|---|---|
| EBNF `17-grammar-reference.md:196` | `RaisesClause ::= 'raises' TypeList` | 逗号列表 |
| EBNF `17-grammar-reference.md:214` | `TypeList ::= TypeExpression (',' TypeExpression)*` | 显式逗号分隔 |
| ANTLR `ZomParser.g4:794` | `raisesClause: RAISES type;` | 单个类型（含联合分支） |
| Parser `parser.cc:565-577` | `parseRaisesClause() { ... return parseType(); }` | 单个类型（`parseType` 走 `parseUnionOrIntersectionType`，支持 `\|`） |
| Spec 示例 `11-error-handling.md:134` | `fun readConfigFile() -> Config raises FileNotFoundError \| ParseError {` | `\|` 联合形式 |
| lit 测试 `raises.zom` | `fun fail() -> unit raises Error { }` | 仅单类型覆盖 |
| lit 测试 `closure-function-definitions/raises.zom` | — | （未核验但无反证）|

**真实性：问题属实。**

存在明显的三方不一致：
1. **EBNF ↔ ANTLR 文法文件互相矛盾**（同是"形式语法"层面，一个 TypeList 逗号，一个 type 联合）。
2. **EBNF ↔ 示例文档矛盾**（逗号 vs `|`）。
3. **ANTLR ↔ Parser ↔ 示例文档**这三方是彼此一致的（均接受 `|` 联合形式，不接受逗号列表）。

因此当前语法实现的**事实规范**是：`raises` 接受"一个类型表达式，含联合类型 `\|`"，而 EBNF 过时/错误地写成了 TypeList（逗号列表）。

## 严重度：下调为 medium（原评 high）

理由：
- **不影响当前用户**：没有任何实现端（Parser / ANTLR / 示例）支持 `raises E1, E2` 这种 EBNF 所描述的写法。按 EBNF 写逗号会被当前实现拒绝，但实际上所有示例和实现都指向 `\|` 形式，用户基本没机会被 EBNF 误导。
- **不影响编译器正确性**：Parser 行为与示例一致，端到端没有歧义。
- **但不是 trivial**：EBNF 是"完整语法参考"章节（第 17 章）的内容，属于规范的**官方权威来源**。同时还有 ANTLR4 文件作为另一份"正式文法"，二者互相矛盾——这意味着若后续有任何工具链（语法高亮、IDE、lint、第二实现）依据 EBNF 开发，会和编译器形成生态分叉。

下调到 **medium**，介于 "文档 typo"（low）和 "真正语法歧义/前后端不兼容"（high）之间。

## 修正建议

同原建议方向，补充一份需要同步修改的精确清单：

1. **`docs/spec/chapters/17-grammar-reference.md`**
   - 第 196 行：`RaisesClause ::= 'raises' TypeList` → `RaisesClause ::= 'raises' TypeExpression`
   - 第 214 行：`TypeList` 规则（`TypeExpression (',' TypeExpression)*`）—— 检查是否还有其他引用（grep 结果里没有），若未被引用可直接删除，或保留为将来的语法点并标注未使用。

2. **`docs/spec/chapters/11-error-handling.md`**
   - 在 "Multiple Error Types" 小节（第 124 行附近）增加一句说明：多错误类型通过**联合类型表达式**`A | B | C` 表示，`raises` 关键字之后接受任意类型表达式。

3. **验证性补充（推荐）**
   - 在 `tests/language/declarations/functions/function-definitions/raises.zom` 中增加一个多错误类型的端到端用例 `fun f() -> T raises E1 | E2 { }`，并断言 AST 中 `errorType` 是 `UnionTypeNode`（而不是假设 parser 未来走逗号路径），同时补一个负面用例确认 `raises E1, E2` 会产生诊断，永久锁定此决策。

4. **同步文件（次要）**
   - `docs/spec/ZomParser.g4` 中 `raisesClause` 规则（第 794 行）与当前实现一致，无需改。但可在注释中显式说明支持 union，方便未来对照。

相关文件（绝对路径）：
- EBNF 语法参考：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md`（L196、L214）
- ANTLR 文法：`/Users/bytedance/Develop/ZOM/docs/spec/ZomParser.g4`（L794）
- Parser 实现：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc`（L565-577）
- 示例文档：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/11-error-handling.md`（L124-164）
- 单类型 raises 测试：`/Users/bytedance/Develop/ZOM/products/zomlang/tests/language/declarations/functions/function-definitions/raises.zom`
- 反对方: 三条部分削弱该指控的反证：

1. **规范文档内部并不统一**：ANTLR 文法（`docs/spec/ZomParser.g4:794`）明确定义 `raisesClause: RAISES type;`——使用 `type`（单个类型表达式，支持联合类型 `|`），**而非** `typeList`（逗号列表）。所以这不纯粹是 "spec vs impl" 的矛盾，而是 **规范内部 EBNF 章节与 ANTLR 正式文法之间的不一致**。ANTLR 与实现、示例三方一致，只有 EBNF 出错。

2. **最近的"对齐"提交遗漏了 grammar reference**：提交 `c2fe0b8`（"Align spec with current parser grammar"）修改了 10 个 spec 文件，但**完全没有触碰** `17-grammar-reference.md`。结合该文件在该提交之前的最近一次更新 `0c2f447` 已经推进了 parser 实现，强烈说明 EBNF 章节是**编辑疏忽**导致的过时，而非故意保留的设计分歧。

3. **语义上确有等价性**：逗号列表与联合类型在"表示多种错误之一"的含义上确实等价，二者并非语义冲突，只是表面语法层面未对齐。

### 17. 🟠 [高] 模式匹配穷举性检查完全缺失，ADT 安全承诺落空  
**类别**: 语义与组合性 | **置信度**: 93%

**问题描述**  
07-patterns.md 完整定义了 8 种模式和 enum 变体解构；spec 也把 Algebraic Data Types 列作语言特性 (01-introduction.md:17)。但当前没有任何地方对 match 语句做「是否覆盖全部可能输入」的静态检查；default 也没有强制放最后、禁止重复等规则。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/01-introduction.md:17 — `- 模式匹配 + 代数数据类型`
    特性被明确列为一等公民。
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/type-symbol.h:63 — `bool isEnumType() const;`
    枚举类型查询 API 存在，但 checkEnumExhaustiveness 之类的接口从未出现。
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/diagnostics/diagnostics-sema.def:15 — `DIAG(TypeMismatch, kError, ...)
DIAG(UndefinedIdentifier, kError, ...)
...`
    诊断码表中完全没有 NonExhaustiveMatch / UnreachableMatchBranch / RedundantMatchPattern 等穷举性相关诊断。

**潜在影响**  
用户用 match 处理 `Option<T>` 或 `Result<T,E>` 忘记写 None/Failure 分支时编译器毫无提醒。核心安全特性名存实亡。

**修复建议**  
在 TypeChecker::visit(MatchStatement) 内实现：(1) 判别式类型的「所有可居住子情形」枚举：对 enum 取全部 variant，对联合类型取全部分支，对 bool 取 {true,false}，对可空取 {T,null}，对 never 要求 0 分支；(2) 诊断码 NonExhaustivePatterns（列出缺失项）、MatchUnreachableBranch、RedundantPatternGuard 至少 3 种；(3) default 分支须是末分支。
**评审备注**
- 确认方: 独立核查结论：F08 问题真实存在，且描述的严重性被低估。

核查到的证据链（独立于候选问题原文）：

1. **规范侧承诺真实**：01-introduction.md:17 明确列"Pattern matching and algebraic data types"为语言特性；07-patterns.md 完整定义了字面量/标识符/通配符/元组/数组/对象/类型/守卫/枚举共 9 种模式；10-enumerations.md:66-77 给出了带关联值的 Result<T,E> 与 match 穷举处理的标准示例（不写 default，仅列 Success+Failure 两分支），形成了"所有情况都会被匹配处理"的隐式契约。

2. **语义检查阶段整体为零**：checker/checker.h 的 TypeChecker 类被整块注释掉（第 24-36 行），checker.cc 只有 `namespace checker {}` 空实现。整个编译流水线 driver.cc 在 bindSources() 后直接终止，没有 checkSources()。因此不仅是 match 穷举性，所有类型/语义检查（包括赋值兼容性、运算合规性、返回值类型等）都不存在。候选问题将其视为一个孤立的"match 穷举检查缺失"是低估了——事实是整个 checker 根本不存在。

3. **诊断码零覆盖**：diagnostics-sema.def 共 28 个错误码（TypeMismatch、UndefinedIdentifier 到 SemanticError），全文 38 行，未出现任何与 NonExhaustiveMatch、NonExhaustivePatterns、UnreachableMatchBranch、RedundantMatchPattern、MatchDefaultNotLast、DuplicateMatchDefault 相关的条目。grep 全 compiler 目录对 "Exhaustive/NonExhaustive/UnreachableBranch/RedundantPattern" 零命中。

4. **TypeSymbol API 中没有相关接口**：type-symbol.h 第 56-100 行的 TypeSymbol 公共接口只提供 isEnumType()/isUnionType()/isSubtypeOf() 等分类与子类型查询，无任何"枚举全部变体"、"枚举联合全部 arm"、"获取可居住子情形集合"的接口；EnumMember 的遍历功能只存在于 AST 层（EnumDeclaration::getMembers），语义符号层并未暴露。

5. **Binder 阶段只做名称绑定**：binder.cc:317-339 对 MatchStatement/MatchClause/DefaultClause 的 visit 仅递归遍历 discriminant/pattern/guard/body，没有任何覆盖性、顺序、重复性检查。

6. **default 位置规则的文法侧状况**：文法 17-grammar-reference.md:253 为 `MatchClause* DefaultClause?`，即语法层面 default 确实只能在末位且最多一个。因此候选问题中的子断言"default 也没有强制放最后、禁止重复等规则"——**在语法层并不成立**（parser 已通过 EBNF 结构强制）。但语义层（例如用户写了通配符标识符模式 `when x =>` 本质等价于 default 之后又继续 when 分支导致后置分支不可达，或 default 后的冗余守卫）仍需检查，而语义层缺失。

严重度调整理由：候选问题评 medium，但考虑到：
- 整个语义/类型检查阶段（Checker）尚未开工，这是更上层的路线图状态，不是"match 模块特有的 bug"。
- 然而，ZOM 的核心定位是"Safety First"（01-introduction.md:7），其宣传的"无隐式异常控制流 + raises + Result 类型 + 模式匹配"组合（11-error-handling.md:3）**必须依赖穷举检查才能兑现安全承诺**。缺少穷举检查意味着所有 raises/Result 风格的代码都可能因为漏分支而产生未定义/静默 fallthrough，这直接击穿了"Safety First"的广告。
- 因此单独看"match 穷举检查"这个 feature gap 本身是 medium，但结合它是整个语义检查阶段缺失的一部分、且直接影响被宣传为核心卖点的错误模型安全性，我把 refinedSeverity 升为 high。

相对/绝对文件路径清单（绝对路径）：
- 规范介绍：/Users/bytedance/Develop/ZOM/docs/spec/chapters/01-introduction.md
- match 语句规范：/Users/bytedance/Develop/ZOM/docs/spec/chapters/05-statements.md:89-133
- 模式定义：/Users/bytedance/Develop/ZOM/docs/spec/chapters/07-patterns.md
- 枚举规范：/Users/bytedance/Develop/ZOM/docs/spec/chapters/10-enumerations.md
- 错误处理模型：/Users/bytedance/Develop/ZOM/docs/spec/chapters/11-error-handling.md
- EBNF：/Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:252-255
- TypeSymbol 接口：/Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/type-symbol.h:56-105
- 语义诊断码表：/Users/bytedance/Develop/ZOM/products/zomlang/compiler/diagnostics/diagnostics-sema.def
- Checker 空壳：/Users/bytedance/Develop/ZOM/products/zomlang/compiler/checker/checker.h:24-36
- Checker 空实现：/Users/bytedance/Develop/ZOM/products/zomlang/compiler/checker/checker.cc:26
- Binder 对 Match 的 visit 实现（仅递归）：/Users/bytedance/Develop/ZOM/products/zomlang/compiler/binder/binder.cc:317-339
- 反对方: 1) 文法 `MatchBlock ::= '{' MatchClause* DefaultClause? '}'`（17-grammar-reference.md:253）已形式化规定 default 至多一个且必须末尾；指控"没有任何规则"在规范层不成立。2) 模式匹配穷举性缺失并非专项缺陷——TypeChecker 整体为 0%（checker 类被注释、driver 无 check 阶段），所有基础语义诊断（TypeMismatch/UndefinedIdentifier 等）也全部零触发；此问题只是 checker 未开发的一个子集。3) Enum 绑定本身未落地：binder.cc:248-251 visit(EnumDeclaration) 全 TODO，EnumCaseSymbol 不存 payload 类型与父枚举引用——穷举性检查的符号层前置条件未满足，不能归咎于"少了一个 checkEnumExhaustiveness 接口"。4) 规范全文无 "exhaustive" / "must cover all variants" / "non-exhaustive match" 等强制性措辞；01-introduction 仅在特性清单中列名，不能等同于已经写入语义承诺。

### 18. 🟠 [高] 联合类型 / 可选类型 / 错误类型三者语义重叠、无规范化  
**类别**: 类型系统 | **置信度**: 92%

**问题描述**  
可选 `T?`、联合 `T|null`、`raises E` 返回类型 `T|E`、用户自造的 `enum Result<T,E>` 四种表示方式同时存在，但 Checker、子类型、模式匹配都没有把它们归一：`T?` 与 `T|null` 是否等价、`null` 是否是 null 类型的唯一值、`raises` 的联合是否包含所有成功分支的联合归约，均无规则。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/03-types.md:130 — `Optional types represent values that may or may not exist:
let maybeNumber: i32? = 42;`
    T? 独立语法。
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/03-types.md:89 — `Union types represent values that can be one of several types:
type StringOrNumber = str | i32;`
    T|U 独立语法。规范没有章节说明 T? 与 T|null 的等价性。
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/11-error-handling.md:3 — `ZOM has no implicit error control flow - all errors are handled through explicit pattern matching.`
    错误模型本身 OK，但与 `?!` (错误传播操作符) 的交互不清楚：`?!` 的语义是「若是错误则返回」本质是隐式控制流，和第 3 行的断言冲突。
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/11-error-handling.md:99 — `Note that these are just regular enums and are not treated as error types by the compiler.`
    spec 明确把 Result 当普通枚举处理；但同时提供了原生 error 类型 + raises 子句 + `?!/!!/?:` 三种操作符，再叠加可选 `T?` + null，空/错两条轴线并存，用户心智负担高。

**潜在影响**  
Checker 阶段实现时必须先定义一套归一化规则 (canonicalization)，否则子类型判定会指数级爆炸。例如 `(i32|null)|Error`、`(i32|Error)?`、`i32? | Error` 是否等价？不提前决定的话，后端正交化成本极高。

**修复建议**  
在 03-types.md 与 11-error-handling.md 之间增加一个统一章节，明确：(1) `T?` 语法糖等价 `T | null_type`；(2) 联合类型的归约：去重、扁平、`null` 合并进 `?`；(3) `raises E1|E2` 对返回类型 R 的真实类型是 `R | E1 | E2`；(4) `?!` 的正式语义：若表达式类型是 `T | E` 且 E 是 raises 声明中的错误超集，等价于 `match expr when e: E => return e; when t => t`；这样它就不是隐式异常控制流，而是显式语法糖。
**评审备注**
- 确认方: ## 独立核验证据

**证据1 — `T?` 与 `T|null` 的等价性未显式声明（03-types.md:130-143 vs 63-68）**
`03-types.md:130` 的 Optional Types 小节只给了语法形式 `i32? = 42`、`str? = null`，完全没有说明 `T?` 在语义上与 `T | null` 是否是同一类型。同一文件第 89 行的 Union Types 小节也没有任何"可选是联合的特例"的叙述。两者在 AST 层被实现为完全不同的节点：`OptionalTypeNode`（单字段 `type`）vs `UnionTypeNode`（`TypeNode[]`），这意味着 Checker 阶段必须手动处理两种表示才能做子类型判定。严重。

**证据2 — `raises E1|E2` 的返回类型与联合的归约无规则（03-types.md:178-202 + 11-error-handling.md:65）**
`03-types.md:196` 示例：`type SafeParser = (str) -> i32 raises ParseError;`，以及语法 `17-grammar-reference.md:194`：`FunctionType ::= ... '->' TypeExpression RaisesClause?`。`11-error-handling.md:65` 给出：
```zom
fun readConfigFile() -> Config raises FileNotFoundError | ParseError
```
这里的 `FileNotFoundError | ParseError` 本身就是一个联合类型（不是逗号分隔）。但规范从未回答：
- 返回类型的完整语义是 `Config | FileNotFoundError | ParseError`（扁平），还是 `Config | (FileNotFoundError | ParseError)`（嵌套联合）？
- 如果函数体返回的是另一个 raises 函数经 `?!` 传播后的错误，子类型判定如何对嵌套联合做扁平化？
- 当 T 本身就是联合（如 `str|i32`）且再叠加 `raises E` 时，`(str|i32) | E` vs `str|i32|E` 是否等价？

AST 层 `ReturnTypeNode::errorType` 是一个单独的 `Maybe<TypeNode>`，没有任何语义约束它必须是 error 类型的联合，也没有说明它和返回 `TypeExpression` 的并集要如何归约。

**证据3 — `?!` 操作符与"无隐式错误控制流"存在表述冲突（11-error-handling.md:3 vs 04-expressions.md:259）**
`11-error-handling.md:3` 明确写道："ZOM has no implicit error control flow - all errors are handled through explicit pattern matching."
但 `04-expressions.md:259` 定义 `let result = riskyOperation()?!;  // Propagate error`。语法文法 `17-grammar-reference.md:308` 把 `?!` 作为 `PostfixSuffix` 的一员。
如果 `?!` 的语义是"当表达式为错误类型时 return 传播"，那它就是一个隐式控制流（等价于 try/catch 的简化语法糖），与规范第 3 行的断言字面上冲突。
如果它被定义为显式语法糖（等价于一段 match 展开），那它就不是隐式的，也不冲突——但规范没有给出这个展开式定义。缺失这个定义是当前规范的真实漏洞。

**证据4 — 空/错两条轴线的心智负担（11-error-handling.md:99）**
`11-error-handling.md:99` 明确："Note that these are just regular enums and are not treated as error types by the compiler." 这意味着 ZOM 同时存在四层表示：
1. `null` 字面量 + `null` 类型（第 65 行 Predefined）
2. `T?` 可选语法糖（OptionalTypeNode）
3. `T|U` 通用联合（UnionTypeNode），包括 `T|null`
4. `raises E` 注解 + `error` 第一类类型（ErrorDeclaration）
5. 用户自造的 `enum Result<T,E> { Success(T), Failure(E) }`（纯 enum，编译器不特殊对待）

1、2、3 都能表达"空/缺省"；3、4、5 都能表达"错误/失败"。对 `?!` `!!` `?:` 三个操作符而言，它们是只对 `raises` 类型生效，还是对任意联合 `T|E` 生效，还是对可选 `T?` 也生效？——这在规范的"Error Handling Operators"小节（04:254-266）中只给了示例，没有给出前提。尤其 `!!` 被标注为 "panics if null"，暗示它同时作用于可选和错误联合，而 `?!` 则写 "Propagate error"，两者的覆盖范围是否一致？没有规则。

**证据5 — 从实现侧看：Checker 阶段 0%，但联合/可选/raises 的 AST 节点已分别声明**
`checker/checker.h:24-36` 整个 TypeChecker 被注释掉，`checker.cc` 为空命名空间。但 AST 层分别有 `OptionalTypeNode`、`UnionTypeNode`、`IntersectionTypeNode`、`ReturnTypeNode(errorType)` 四种不同结构。一旦开始实现子类型和模式匹配穷举检查，没有归一化规则将直接导致：
- 子类型判定组合爆炸：`(i32|null)|Error` vs `(i32|Error)?` vs `i32?|Error` vs `i32|Error|null` 是否互相 subtype，需要手动枚举
- 模式匹配穷举检查需要同时识别 OptionalTypeNode 与 "含 null 的 UnionTypeNode"，否则漏分支不报错

## 严重度评估
原严重度 **medium** 合理。我略上调至 **high**：不是因为设计本身混乱（四者的区分在语义层面其实是合理的：null/可选处理缺省，raises+error 处理显式错误，Result 留给用户做带标签枚举），而是因为**规范缺形式化条款会直接阻塞 Checker 阶段的实现路径**。当前 checker 完全空白，而这个问题正是 checker 要解决的第一个十字路口——不先写归一化规则，后面写的每一个子类型/模式匹配分支都要处理四种表示，返工代价很大。这是"实现阻塞级"的规范缺失，而非文档美观问题。
- 反对方: 【关键证伪点】1. Checker 全局未实现：checker.h 整个类被注释（24-36行），checker.cc 为空命名空间，driver 管线中无 checkSources()。"T?/联合/raises 未归一"无法与泛型约束、接口一致性、运算符检查等 ALL OTHER Checker 功能区分——不是此三元组的特定缺陷，是全局开发阶段未到。2. ?! 是显式控制流，非隐式：11-error-handling.md:60-62 原文明确写 "all error handling is explicit"，且 04-expressions.md:256 将 ?! 归类为 "explicit control flow (no try/catch)"。F07 偷换"隐式"定义——Rust ? 做同样的 early return，Rust Book 同样说"no exceptions"，按 F07 逻辑 Rust 也有 bug。3. 四形式并存是标准分层：T?=语法糖、T|null=显式联合、raises=编译器校验的错误通道、用户 Result=自由降级机制。Swift 同时有 T?/Optional<T>/throws/自定义 Result；Rust 有 Option/Result/自定义枚举。这是四层抽象不是四重冗余。4. "文档未声明等价"≠"语义冲突"：spec 没写 T? 与 T|null 等价 ≠ spec 声明它们不等价。行业常规就是 Optional 作为 Union|null 的语法糖在 Checker/Desugar 阶段统一，AST 阶段保留两种节点是正常实现策略（ast/type.h 中 OptionalTypeNode 和 UnionTypeNode 分别存在，但这不妨碍后续归一）。5. "心智负担高"是主观判断：可选/错误双轴解耦在 Swift/Kotlin/Dart/TS 中全部采用，空(不存在)与错(失败)语义必须分开；F07 未提供任何用户研究数据，属个人偏好陈述。

### 19. 🟠 [高] 接口 (Interface) 系统只有符号壳：无成员、无实现检查  
**类别**: 语义与组合性 | **置信度**: 92%

**问题描述**  
InterfaceSymbol::Impl 是空结构，没有任何成员列表/方法签名/关联类型；同时 Binder 对 ClassDeclaration 的 HeritageClause 只做 accept 递归而不解析，也不把 implements 指向的类型写入 ClassSymbol::interfaces 数组。接口名存实亡。

**证据**
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/type-symbol.h:146 — `private:
  struct Impl;
  zc::Own<Impl> impl;`
    InterfaceSymbol 仅声明 Impl，但在 type-symbol.cc 中 Impl 是空结构。
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/type-symbol.cc:219 — `struct InterfaceSymbol::Impl {
  Impl() = default;
};`
    确认 InterfaceSymbol::Impl 无任何字段。没有成员列表，没有关联类型表，没有 super-interface 链。
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/binder/binder.cc:1125 — `const auto& heritageClauses = classDecl.getHeritageClauses();
for (const auto& clause : heritageClauses) { clause.accept(*this); }`
    对 HeritageClause 只做 visitor 分发，没有任何解析 TypeReference→setSuperclass / addInterface 的逻辑。ClassSymbol::interfaces 数组永远为空。
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/type-symbol.cc:264 — `void ClassSymbol::addInterface(zc::Maybe<const ClassSymbol&> interface) {
  impl->interfaces.add(interface);
}`
    更严重的 API 问题：addInterface 的参数是 `const ClassSymbol&`，把接口类型的指针存进了 ClassSymbol 的容器——接口符号的类型是 InterfaceSymbol，根本无法通过此 API 关联。这是一个明显的类型错配 bug。
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/09-interfaces.md:21 — `class Button implements Drawable, Movable {`
    spec 中明确要求 implements 多个接口，且 Collection<T> 要求关联类型 (97-119 行)。当前实现全部未满足。

**潜在影响**  
所有抽象 / 多态 / 泛型约束全部无法工作。用户写的 `implements I<T>` 是纯注释。TypeChecker 若按现有 ClassSymbol::interfaces 检查，也只会因类型错配而在运行时崩溃 (把 InterfaceSymbol 按 ClassSymbol 切片)。

**修复建议**  
三件事并行：(1) 把 InterfaceSymbol::Impl 补齐成员列表 (MethodSignature/PropertySignature/AssociatedType)、super-interfaces；(2) 把 ClassSymbol::interfaces 的元素类型从 ClassSymbol 改为 TypeSymbol 或 InterfaceSymbol；(3) 在 Binder::bindClassDeclaration 中针对 HeritageClause 的 token 分别调用 setSuperclass(extends) 和 addInterface(implements)，并在 Checker 阶段实现「实现完整性检查」。
**评审备注**
- 确认方: ## 独立核验的证据链（全部确认成立）

### (1) InterfaceSymbol::Impl 是空结构 —— 确认
`type-symbol.cc:219-222`：
```cpp
struct InterfaceSymbol::Impl {
  Impl() = default;
};
```
零字段。与 ClassSymbol::Impl 对比（后者含 superclass / interfaces / members / constructors 四个字段，234-241 行），InterfaceSymbol 完全没有对应字段：
- 无 `members`（方法签名、属性签名、关联类型全部无法存储）
- 无 `superInterfaces` / `extends` 链（接口多继承无存储位）
- 无 `associatedTypes`（spec 09 章 97-119 行要求的 Collection<T>::Iterator 等关联类型无处落脚）
- 甚至连 `typeParameters` 也只能依靠父类 TypeSymbol::Impl 的 `superTypes` 等字段，没有接口专用空间

对比 InterfaceDeclaration 的 AST（`statement.h:642` 起）有 `heritageClauses` 和 `InterfaceElement[]` 成员，符号层完全没有对应的落地点，**断层明确**。

### (2) Binder 对 HeritageClause 只 accept、不写符号 —— 确认
`bindClassDeclaration`（binder.cc:1125-1126）：
```cpp
const auto& heritageClauses = classDecl.getHeritageClauses();
for (const auto& clause : heritageClauses) { clause.accept(*this); }
```
然后进入 `visit(HeritageClause&)`（binder.cc:917-920）：
```cpp
void Binder::visit(const ast::HeritageClause& node) {
  for (const auto& t : node.getTypes()) { t->accept(*this); }
}
```
**关键缺失：完全没有 `if (clause.getToken() == ExtendsKeyword) setSuperclass(...)` / `ImplementsKeyword addInterface(...)` 的分支。**
HeritageClause AST 明明有 `getToken()` 区分 Extends/Implements（statement.h:368），Binder 对此视而不见。TypeReference 的 accept 最多触发 visit(ExpressionWithTypeArguments) / visit(Identifier)，不会反向把解析出的符号写回 ClassSymbol。
实际效果：`ClassSymbol::impl->superclass` 永远是 zc::none，`ClassSymbol::impl->interfaces` 永远是空 Vector。

额外发现（报告未提但加剧严重性）：`bindInterfaceDeclaration`（1135-1167 行）**完全没遍历 `interfaceDecl.getHeritageClauses()`**，连 clause.accept 都没做——接口的 `extends I1, I2` 不仅没写进 InterfaceSymbol（本来也没位置写），连子句里的类型引用都不会被 visit。语法允许 `extends`（spec 第 91 行 `interface ReadWriteStream extends ReadableStream, WritableStream`），但 binder 端直接丢弃。

### (3) addInterface 参数类型错配 —— 确认（编译期就会拦截，是硬 bug）
`type-symbol.h:170-171`：
```cpp
zc::Array<zc::Maybe<const ClassSymbol&>> getInterfaces() const;
void addInterface(zc::Maybe<const ClassSymbol&> interface);
```
接口符号的 C++ 类型是 `InterfaceSymbol`（独立类，与 ClassSymbol 是兄弟，同为 TypeSymbol 子类）。二者既无继承关系，`classof` 也不能互转。**任何试图把 InterfaceSymbol 引用传给 addInterface 的代码都无法通过编译。**

这意味着即使之后有人在 Binder 里补上 `symbol.addInterface(resolvedIface)` 这行，也会因类型不匹配而编译失败。`isSubtypeOf`（type-symbol.cc:104-111）在遍历 `getInterfaces()` 时还直接 `static_cast<const ClassSymbol&>(iface)` 再 `isSubtypeOf(otherClass)`——若接口数组里真的按未来修正（改成存 InterfaceSymbol 指针）填了值，这段代码就是**切片式 undefined behavior**。目前由于接口数组永远为空，这段死代码没暴露问题。这是类型错配在另一个位置的镜像 bug。

### (4) spec 要求 vs 实现对照 —— 确认全部未满足
spec 09-interfaces.md 要求：
- L22：`class Button implements Drawable, Movable` → Binder 不写入 ClassSymbol::interfaces，见 (2)
- L91：`interface ReadWriteStream extends ReadableStream, WritableStream` → InterfaceSymbol 无 super 链存储；Binder 对 InterfaceDeclaration 的 HeritageClause 甚至不遍历
- L97-119：`type Iterator: Iterator<T>` 关联类型语法 → InterfaceSymbol::Impl 无字段承载，Binder 的 bindInterfaceDeclaration 对 InterfaceElement 仅用 `dynamic_cast<const ast::Node*>()` 兜底（1161-1163 行），MethodSignature/PropertySignature/关联类型均不做区别处理

## 严重性再评估

报告原评 "high"。我 **维持 high**，并给出更细的分级理由：

**为何不是 critical**：
- 当前 TypeChecker 整体是空壳（checker.cc 为空命名空间，driver 无 checkSources 阶段），整个语义检查阶段尚未接入编译管线。因此"接口系统不工作"只是**大面积缺失的一个子集**，并非独立致命问题——单修接口也无法使用（仍缺表达式类型推断、模式穷举、泛型实例化等）。
- 诊断层面也没有任何语义错误码（TraitNotSatisfied / MethodNotImplemented / AssociatedTypeMissing 等均不存在于 diagnostics-sema.def）。因此用户写出 `implements I<T>` 不会被报"未实现某方法"，而是被**静默忽略**——这是语义缺失而非崩溃（sanitizer 下也不会触发）。
- 目前的测试体系（lit 全走 `--dump-ast`，checker unittest 全是 TODO）也没有对 "接口一致性" 做任何断言，不会影响构建通过率。

**为何仍是 high**：
- **类型错配 bug 具有前向破坏性**：`addInterface(ClassSymbol&)` 签名和 `isSubtypeOf` 里对 interfaces 元素的 `static_cast<const ClassSymbol&>` 形成了相互依赖的错误。一旦未来有人只改 Binder 而不改签名 / 只改签名不改 isSubtypeOf，都会直接引入编译失败或 UB。这是接口修复路上的**类型地雷**，必须同步改两处。
- 架构级偏差：`InterfaceSymbol` 作为 TypeSymbol 子类却毫无字段，等于在符号体系中挂了个"永远不能表示任何契约"的占位壳。Pimpl 模式下的空结构暗示这个类根本**从未进入实现阶段**（不是 TODO 注释，而是完全没有任何成员/API 被声明）。
- 与规范的三重脱节：spec 要求 (a) 接口有成员 + 关联类型 + extends 链，(b) class 的 implements 指向接口符号，(c) 子类型检查沿 implements 走。当前 (a)(b) 为空，(c) 写死了错误类型。三件事缺一不可，必须并行修复。

## 建议与原报告相同（验证后确认三件事都必要）：

1. **补齐 InterfaceSymbol::Impl**：加 `members: Vector<Maybe<Symbol&>>`（MethodSignatureSymbol / PropertySignatureSymbol / AssociatedTypeSymbol）、`superInterfaces: Vector<Maybe<const InterfaceSymbol&>>`、`typeParameters` 继续复用父类，但要公开访问 API。
2. **修正 ClassSymbol::interfaces 的元素类型**：从 `Maybe<const ClassSymbol&>` 改为 `Maybe<const InterfaceSymbol&>`；同时修正 `getInterfaces()` / `addInterface()` 签名；同步修 `TypeSymbol::isSubtypeOf` 里 `static_cast<const ClassSymbol&>(iface)` 的逻辑，改为沿 InterfaceSymbol 自己的 superInterfaces 链走。
3. **Binder 端双修复**：
   - `bindClassDeclaration`：遍历 HeritageClause 时按 `getToken()` 区分，`ExtendsKeyword` 解析 TypeReference 后调用 `setSuperclass`（注意目标必须是 ClassSymbol），`ImplementsKeyword` 解析后逐个 `addInterface`（目标必须是 InterfaceSymbol）。
   - `bindInterfaceDeclaration`：补上 heritageClauses 遍历，把 `extends` 接口写入新的 `superInterfaces` 字段；另外处理 MethodSignature / PropertySignature / 关联类型三种 InterfaceElement 分别建符号加入 members。
4. （附加）Checker 阶段启动后再补"实现完整性检查"、默认方法、关联类型绑定验证，但这是后续工作，不改变当前问题本身的真实性。

**关键文件清单（均已核验）**：
- `/Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/type-symbol.h`（L128-194 接口/类符号声明，含错配签名）
- `/Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/type-symbol.cc`（L219-266 InterfaceSymbol::Impl 空结构、ClassSymbol::Impl、isSubtypeOf 错用 static_cast）
- `/Users/bytedance/Develop/ZOM/products/zomlang/compiler/binder/binder.cc`（L917-920 HeritageClause visitor 空处理、L1125-1126 bindClassDeclaration 不解析 heritage、L1135-1167 bindInterfaceDeclaration 漏掉 heritageClauses 遍历）
- `/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/statement.h`（L360-369 HeritageClause 有 getToken() 区分关键字，L382/L642/L664 三个声明类都带 heritageClauses）
- `/Users/bytedance/Develop/ZOM/docs/spec/chapters/09-interfaces.md`（L22 implements、L77-95 extends 多继承、L97-119 关联类型——三者全部未实现）
- 反对方: 1. Symbol 基类提供 getDeclarationNodes()（symbol.h:136-137, symbol.cc:48），接口成员可通过回溯 InterfaceDeclaration AST 获取，InterfaceSymbol::Impl 为空是设计模式而非缺陷；ClassSymbol 有显式 getMembers 只是另一种缓存方式。2. ClassSymbol::addInterface 接受 ClassSymbol& 的类型不匹配是占位 API，全项目零调用点，不触发实际问题；修复仅需将参数/容器改为 TypeSymbol& 或 InterfaceSymbol&。3. 整个 TypeChecker 阶段为 0% 完成度（checker.cc 空命名空间、driver 无 checkSources），class 的 extends 解析、赋值兼容、泛型约束、模式穷举等类型语义同样全部不满足；接口未实现是更宏观问题的子集而非独有缺陷。4. Binder 中大量 "TODO: when interface is available" 注释表明这是有意的分阶段实现策略，不是偷偷遗漏。

### 20. 🟠 [高] any 和 never 特殊类型在规范声明但 AST 和 Symbol 层均缺失  
**类别**: 类型系统 | **置信度**: 92%

**问题描述**  
规范 03-types.md Special Types 列出 any 和 never，但 AST 没有 AnyTypeNode / NeverTypeNode，kinds.h 虽有 AnyKeyword、NeverKeyword，却没有 parsePredefinedType 分支识别。

**证据**
  - docs/spec/chapters/03-types.md:63 — `any and never listed in Special Types table`
    规范明确声明 any/never 为特殊类型
  - products/zomlang/compiler/ast/type.h:152 — `PredefinedTypeNode family does not include AnyTypeNode or NeverTypeNode`
    AST 层缺失 any/never 节点
  - products/zomlang/compiler/parser/parser.cc:2200 — `parsePredefinedType has no case for AnyKeyword or NeverKeyword`
    parser 不识别 any/never 为类型

**潜在影响**  
用户无法使用规范声明的特殊类型，泛型边界、异常返回、fallback 语义受限。

**修复建议**  
在 AST 增加 AnyTypeNode/NeverTypeNode，parser 的 parsePredefinedType 增加分支，Symbol 层增加对应 BuiltInTypeSymbol。
**评审备注**
- 确认方: ## 独立核验证据

### (1) 规范侧 — any / never 已被正式声明
- `docs/spec/chapters/03-types.md:63-76` "Special Types" 小节明确列出 `null`、`unit`、`never`、`any` 四项，并给出了 `never` 的代码示例（`fun loopForever() -> never`）。任何读过规范章节的用户都会假定这四个特殊类型可用。
- 反证据的文法不一致（17-grammar-reference.md:181-182 只列 14 个 PredefinedType）本身更像是规范文档的漏写，而不是"不打算支持"：03-types.md 带完整使用示例且紧跟在 null/unit 之后并列叙述，设计意图清晰。

### (2) AST 侧 — any/never 节点完全缺失
- `products/zomlang/compiler/ast/type.h:152-318` 实际声明的 `PredefinedTypeNode` 子类共 16 个：Bool / Str / Unit / Null / I8-I64 / U8-U64 / F32-F64。**没有 AnyTypeNode、没有 NeverTypeNode**。
- `products/zomlang/compiler/ast/factory.h:491` 的 `createPredefinedType(zc::StringPtr name)` 是通用工厂，但 AST 类级别缺少对应的 classof 条目与类型节点类，即便后续 parser 调用它也无法被访问者/类型判别正确识别。
- `products/zomlang/compiler/ast/ast-nodes.def`（X-Macro 注册表）也没有 AnyTypeNode / NeverTypeNode 登记（由"AST 上下文"的类型节点清单佐证）。

### (3) Parser 侧 — parsePredefinedType 未接入 any/never
- `products/zomlang/compiler/parser/parser.cc:3610-3643` 的 `parsePredefinedType()` 中 switch 只覆盖 I8~U64 / F32 / F64 / Str / Bool / Null / Unit（**共 15 项，不含 AnyKeyword、不含 NeverKeyword**）。
- 附带问题：第 1205-1224 行 `isStartOfType()` 探路函数虽然包含了 `AnyKeyword`（第 1208 行），却**没有** `NeverKeyword`——即便是部分接入也不一致；但即使被 `isStartOfType` 识别为类型起点，后续 `parsePredefinedType` 仍会落入 default 分支并返回 `zc::none`，最终解析失败。

### (4) Symbol 侧 — BuiltInTypeSymbol 不完整
- `products/zomlang/compiler/symbol/type-symbol.h:117-121` 和对应 `type-symbol.cc:189-217` 只实现了 5 个内建类型的创建方法：I32 / F32 / Str / Bool / Unit。连 Null 都没有符号，更别说 Any / Never。
- 这与 AST 层的 16 个 PredefinedTypeNode 本就不对齐，any/never 缺失是整个符号层内建类型未铺完的一部分。

## 关于严重度的评估（high → high，维持原判）
- **high 合理**：规范已以"特殊类型"顶层语义声明 any/never，并给出示例代码。用户写下 `fun f() -> never { while(true){} }` 或 `let x: any = ...` 时会在解析阶段直接失败，属于"规范承诺功能不可用"的实现缺口。
- **降为 medium 的理由不成立**：虽然当前 Checker 层尚未开工、语义端影响暂不显性，但"缺失整个类型构造"属于语法层/类型系统的基础能力，不先补 AST+Parser 节点就无法开展后续的子类型规则（never 为底、any 为顶）、可达性检查、泛型边界等工作；并且 null/unit 都已经完整实现，同表中的 any/never 缺失就是明显的 gap。
- **结论：维持 high**。

## 与原问题描述的细微修正
原问题称"kinds.h 虽有 AnyKeyword、NeverKeyword，却没有 parsePredefinedType 分支识别"——基本正确。补充两点：
1. `isStartOfType()` 中包含 AnyKeyword 却漏掉 NeverKeyword，说明接入状态是"部分、不完整"，不是"完全未考虑"。
2. Symbol 层的状态比"未增加对应 BuiltInTypeSymbol"更不完整：连 Null 类型的符号都未创建，内建类型族整体未对齐。建议在修复时一次性把 Null/Any/Never 三类符号补齐并预先注册到全局作用域。

相关文件（绝对路径）：
- 规范声明：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/03-types.md`（63-76 行）
- 文法（反证侧，未包含 any/never，规范自身不一致）：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md`（181-182 行）
- AST 类型节点：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/type.h`（152-318 行）
- AST 工厂：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/factory.h`
- 关键字：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/kinds.h`（45、89 行）
- 解析器：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc`（1205 行 isStartOfType、3610-3643 行 parsePredefinedType）
- 符号层：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/type-symbol.h`（108-121 行）、`type-symbol.cc`（189-217 行）
- 反对方: 最强反驳证据：形式语法层面完全不支持 any/never。
1) EBNF（最权威的规范）中 PredefinedType 定义为：'i8'|'i16'|'i32'|'i64'|'u8'|'u16'|'u32'|'u64'|'f32'|'f64'|'str'|'bool'|'null'|'unit'，共 14 项，明确不含 'any' 和 'never'（17-grammar-reference.md:181-182）。
2) ZomParser.g4（ANTLR 形式语法）中对 "any" 和 "never" 零引用。
3) parser.cc 的 parsePredefinedType 注释和 switch 分支与 EBNF 1:1 对齐，实现与形式语法完全一致。
4) 02-lexical-structure.md:162-163 明确声明："Some reserved words are reserved for future language design. If a reserved word has no grammar rule in the current parser, using it as syntax is a parse error rather than a supported construct."——any/never 正是这种情况：有 token kind（保留字）但无 grammar rule，属于文档已声明的合法状态。
5) 整个 checker 阶段为空壳（TypeChecker 类被注释掉，driver 无 checkSources 管线），即使 AST 层补上 AnyTypeNode/NeverTypeNode，语义层也完全无法消费——any/never 的"缺失"是类型系统整体未开工的一部分，不是孤立的 high-severity 缺陷。
6) 全项目测试（lit 和 unittest）中没有任何 `: any` 或 `-> never` 的正面用例，反映出这两个类型并未被纳入"当前支持集"。

### 21. 🟠 [高] 语义矛盾：if 仅是语句但 match 是表达式，条件表达式不统一  
**类别**: 语义与组合性 | **置信度**: 91%

**问题描述**  
05-statements.md 第一行明确声明 'Statements perform actions but do not produce values (unlike expressions)'，但同一章节的 match 示例中 `let result = match (operation) { ... }` 将 match 作为右值使用。同时 if 语句**不能**作为表达式（无 `let x = if cond { a } else { b }`），与 match 的表达式化形成不对等，降低了表达式-语句的统一性。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/05-statements.md:3 — `They perform actions but do not produce values (unlike expressions).`
    该声明与 match 作为表达式的用法直接冲突
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/05-statements.md:103 — `let result = match (operation) { when "add" => a + b ... default => 0 };`
    match 在此处是表达式（赋值右侧），但它出现在 statement 章节并仅在语法中定义为 MatchStatement
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:276 — `Expression ::= AssignmentExpression (',' AssignmentExpression)*`
    Expression 产生式中没有 MatchExpression，但 statement 章节示例要求它是表达式

**潜在影响**  
语言表达力受限：用户不能写 `let x = if cond { a } else { b }`，只能用 match 或三元运算符，增加了心智负担；同时 spec 存在自相矛盾，影响规范可信度。

**修复建议**  
要么：(a) 在 Expression 产生式中加入 MatchExpression 和 IfExpression，使所有控制流成为表达式（Rust/Swift 风格）；要么 (b) 移除 match 的表达式用法，只保留三元运算符。推荐方案 (a)，它更符合设计原则中的 'Expressiveness'。
**评审备注**
- 确认方: ## 核验结果：真实问题，建议降为 high

### 一、spec 内部自相矛盾（三层证据）

1. **05-statements.md 第 3 行**："They perform actions but do not produce values (unlike expressions)."——定义语句不产生值。
2. **同文件第 102-109 行**：`let result = match (operation) { when "add" => a + b ... default => 0 };` 将 match 作为赋值右值（即表达式）使用。该示例所在小节标题仍是 "`match` Statements"（第 89 行），却给出表达式级用法。
3. **04-expressions.md 第 262-265 行**：再次出现 `let handled = match (riskyOperation()) { when Ok(v) => v, when Err(e) => handleError(e) };`——在表达式章节的错误处理操作符示例中使用 match 作为表达式。
4. **17-grammar-reference.md 第 230-252 行**：`Statement` 产生式含 `MatchStatement`，`IfStatement`；**Expression 层级（第 276-339 行）中既无 MatchExpression 也无 IfExpression**。EBNF 只把 match/if 列为语句。

结论：spec 示例与章节标题、EBNF 形式文法三者之间互斥。

### 二、parser 实现与 EBNF 一致，但无法编译 spec 示例

- `parser.cc` 第 799 / 813 行：`if` 和 `match` 只在 `parseStatement()` 的 switch 中被识别，表达式层级（`parsePrimaryExpression` / `parseAssignmentExpressionOrHigher` 等）完全没有 `match` 或 `if` 分支。
- AST 只有 `IfStatement` / `MatchStatement`（`ast-nodes.def` 第 120 / 129 行、`statement.h` 第 433 / 604 行），**没有任何 `IfExpression` / `MatchExpression` 节点**。
- 端到端实测 `let result = match (operation) { when "add" => a + b default => 0 };`：zomc 报错 `Reserved keyword 'match' cannot be used as an identifier`（ZOM2010），后续串出 9 条级联错误；`let x = if (cond) { a } else { b };` 同样 ZOM2010。两条语句均无法解析。

### 三、"if 与 match 不对等"确实成立

- 当前三元运算符 `cond ? a : b` 是表达式层唯一可用的"条件表达式"（ConditionalExpression，第 284 行）。
- `if` 和 `match` 都是纯语句，二者对称。真正的不对称是：**spec 示例暗示 match 可作为表达式，却从未给 if 相同待遇，而 parser 中两者均不可**。所以 "if 不是表达式、match 也不是表达式、但 spec 说 match 是" 才是核心问题；"if 和 match 不对等"是这个核心问题在语言设计层面的延伸讨论。

### 四、关于严重度：从 critical 下调为 high

原报告标注 critical。我认为应下调，理由：
- **不影响已写的正确代码**：当前用户只要按 EBNF 语法（match 作语句、三目或 `let x; if cond { x = a } else { x = b }` 写法）使用，语义与编译器完全一致，可正常编译运行。不存在"正确代码无法通过"或"错误代码静默通过"的安全问题。
- **不存在未定义行为**：冲突仅在 spec 文本 vs EBNF/实现 之间，运行时没有歧路。
- **影响范围是 spec 可信度 + 表达力缺失**：前者是文档工程问题；后者是设计缺口，需要明确取舍（采纳建议 a 或 b）。两项都严重但不到"语言无法工作"的 critical 级。

因此 refinedSeverity = **high**：spec 自相矛盾导致"按照示例写的代码无法编译"，且缺失了一个现代语言常见的表达力特性，但不破坏现有合法程序。

### 五、对建议的意见

- 推荐保留方案 (a)：**同时加入 MatchExpression 和 IfExpression**。理由：
  1. 符合设计原则第 3 条 "Expressiveness"（01-introduction.md:6）。
  2. 与 Rust/Swift/Kotlin 等同类语言惯例一致，降低迁移心智负担。
  3. 三目运算符 `?:` 已经是表达式层条件构造，加入 if-expression 和 match-expression 是将语法统一，不是引入新概念。
  4. 消除了当前 spec 中两处 "match 作表达式" 示例与 EBNF 的矛盾；否则若选方案 (b)，需要删除 05-statements 和 04-expressions 两处示例，且会让 error-handling 章节的推荐写法无处落地。
- AST 层改造最小化：可让 `IfExpression`/`MatchExpression` 在实现上复用 `IfStatement`/`MatchStatement` 的子结构（条件/分支/子句），只附加"作为表达式使用时各分支的类型必须一致"的语义规则（交由后续 Checker）。

## 相关文件路径

- 05 章语句定义与矛盾示例：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/05-statements.md:3, 102-109`
- 04 章表达式章节的 match 表达式示例：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/04-expressions.md:262-265`
- EBNF 语句 vs 表达式分层：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:230-252, 275-339`
- AST 节点定义：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/ast-nodes.def:120,129`
- parser 分派点（证明表达式层无 match/if）：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc:799,813,2118-2160,2870-2919`
- 端到端复现命令：`build-sanitizer/products/zomlang/utils/zomc/zomc compile --dump-ast`（文件 `let x = match (...) {...};`）触发 ZOM2010
- 反对方: 候选问题声称存在"语义矛盾"和"critical"级设计缺陷，实际可被大幅证伪：

1. **关于 "05-statements.md:3 与 match 示例的直接冲突" —— 属于对文档措辞的误读。**
   - `05-statements.md` 第一行写的是 "They perform actions but do not produce values (unlike expressions)"，这是在"Statements are the building blocks..."段落中对"典型语句"的通俗描述。紧接着同一章节 89 行的标题就是 `### match Statements`，并且 102 行的注释明确写了 `// Match with expressions`。也就是说规范本身就承认 match 既能当语句又能当表达式——这是一个有意识的设计（和 Kotlin `when`、Rust `match`、Swift `switch` 作为表达式的行为一致），而不是"矛盾"。把开头那句概括性措辞当作"所有语句绝对不能产出值"的公理，是典型的断章取义。
   - 进一步佐证：现代语言规范（C#/Kotlin/Rust/Swift）中，match/switch/when 几乎都被同时视作语句与表达式（"双栖构造"）。因此 match 出现在 `05-statements.md` 完全不构成它不能是表达式的证据；章节标题只是按"语法形态"归类而非"值/非值"的排他分类。

2. **关于 "EBNF 中 Expression 没有 MatchExpression" —— 这是规范/文法的未实现项，不是语义矛盾。**
   - 核查 `17-grammar-reference.md` 的 `Statement` 产生式（226-240 行）确实列出了 `MatchStatement`；核查 `Expression` 层级（275-393 行）确实没有 `MatchExpression`。这是 **EBNF 与 prose/示例不一致** 的文档瑕疵，不是"语义冲突"。规范 prose（05-statements.md 示例 + "Match with expressions"注释）本身已经声明了该能力的设计意图，只是 grammar reference 未同步写出来。
   - 核查 AST：`products/zomlang/compiler/ast/ast-nodes.def:129` 与 `statement.h:604` 仅存在 `MatchStatement : public Statement`，无 `MatchExpression : public Expression`。核查 parser：`parser.h:445` 仅声明 `parseMatchStatement()`，`parser.cc:812-813` 仅在 statement 分发处进入 `parseMatchStatement()`；`parsePrimaryExpression`、`parseAssignmentExpressionOrHigher`、`parseInitializer`（2437 行调用 `parseAssignmentExpressionOrHigher`）三条表达式入口均**无 `MatchKeyword` 分支**，全项目共只有 3 处 `MatchKeyword` 引用，全部走语句路径。
   - 核查测试：`tests/language/statements/match.zom`、`test_match.zom`、`control-flow.zom`、`control-flow/conditionals.zom` 中 match 全部用作顶层语句；搜索 `let .* = .*match` / `= match` 全仓库测试用例 —— **零命中**。没有任何端到端或单元测试验证过 `let x = match (...) { ... };` 形式。
   - 结论：match-as-expression 目前是**规范 prose 中已声明但 grammar/AST/parser/test 四级均未落地的未实现特性**，而非"语义矛盾"。文档先行、实现后补，这在任何渐进开发项目中都很常见。

3. **关于 "if 不能是表达式与 match 是表达式不对等，降低统一性" —— 这是主观审美判断，不是缺陷。**
   - ZOM 已提供 `cond ? a : b` 三元条件表达式（`04-expressions.md:294-305`，EBNF 284 行 `ConditionalExpression`）来承担"双分支的值选择"职责。`if` 语句 + 三元表达式 + `match` 表达式，三者分工是主流语言的常见模式（JS/TS/Kotlin/C++ 皆如此）：双分支用三元、多分支用 match、语句级副作用用 if。不存在所谓的"统一性缺失"缺陷 —— 统一性本身也不是必须追求的设计目标（Kotlin/Scala 让 if 成表达式，JS/C#/Swift 不，两者都成立）。
   - 更进一步：若真要追求"所有条件都是表达式"的统一，那该补的是 if-as-expression，而不是把 match 贬为"矛盾"。候选问题在这里的论证方向是倒果为因 —— 以"统一性"作为预设价值观反过来审判当前设计，这不是缺陷评估而是设计品味争议。

4. **严重度降级理由：**
   - `critical` 意味着当前设计会阻塞编译管线或产生不可调和的语义冲突，但事实是：
     - 语法上 ZOM 目前没有 match-as-expression，因此不会出现 "语句产生值" 的运行时矛盾；
     - 类型检查阶段（checker）当前是空壳（`checker.cc` 命名空间为空，另见上下文文档），不存在需要处理"语句产出类型"的 Sema 冲突；
     - 所有现有测试都通过，没有任何当前用户代码会触发此"矛盾"。
   - 真正存在的仅是：**一处 prose 超前于 grammar 与实现的文档不一致**。正确的定性是"文档瑕疵/规范待补齐"，严重度至多 `medium`，若把文档同步看作低优先级任务甚至可降到 `low`。

5. **可定性为真实的最小范围：**
   - 仅以下两点可承认为实：
     a. 规范 prose 的 match-as-expression 示例与 grammar reference（EBNF）未对齐（EBNF 缺 MatchExpression 产生式）；
     b. 解析器/AST 目前未实现 match-as-expression（与 prose 不一致）。
   - "语义矛盾"、"统一性降低" 以及 critical 级判定均不成立。

### 22. 🟠 [高] 闭包捕获语法 CaptureClause 存在但捕获语义完全未规范  
**类别**: 语义与组合性 | **置信度**: 89%

**问题描述**  
EBNF 定义了 `CaptureClause ::= 'use' '[' CaptureList? ']'` 和 `CaptureElement ::= '&'? Identifier | 'this'`，即显式声明式捕获（Rust 风格）。但 spec 正文中完全没有讨论捕获语义：默认是移动还是拷贝、引用捕获的生命周期、`this` 的捕获方式、闭包的 Fn/FnMut/FnMove trait 层级、是否支持 move || 风格的强制移动、与 ARC 的交互。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:365 — `FunctionExpression ::= 'fun' TypeParameters? ParameterClause CaptureClause? ReturnType? BlockStatement`
    CaptureClause 已进入正式 EBNF
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:366 — `CaptureClause ::= 'use' '[' CaptureList? ']'`
    use [...] 子句语法已定义
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/04-expressions.md:338 — `## Function Expressions ... let multiply = fun (x: i32) -> i32 { return x * multiplier; };`
    唯一的闭包例子隐式捕获 multiplier，但未说明：这是错误？还是默认隐式捕获？与 use [] 语法的关系如何？

**潜在影响**  
存在极大语义空白：use [] 是强制显式捕获（如 early-Rust）还是仅作为可选优化标注？隐式捕获的变量是引用还是拷贝？这直接影响内存安全（ARC 环、use-after-free）与并发正确性。

**修复建议**  
制定闭包捕获模型并写入 spec：(1) 建议模式：类似 Swift，默认隐式捕获 + 强引用，use [&a, weak b, copy c, self] 语法用于显式覆盖默认行为；(2) 明确引用捕获 `&x` 在值类型和引用类型上的差异；(3) 指定 `this`/`self` 捕获与 ARC 的交互（默认强引用 + 隐式 self 或必须显式声明？）；(4) 定义 Fn/FnMut/FnMove 或 Callable trait 层级作为函数类型的子类型基础。
**评审备注**
- 确认方: ## 独立核验证据链

### 1. 语法层：CaptureClause 确实已完整落地

- **EBNF**：`17-grammar-reference.md:365-368` 明确定义：
  - `FunctionExpression ::= 'fun' TypeParameters? ParameterClause CaptureClause? ReturnType? BlockStatement`
  - `CaptureClause ::= 'use' '[' CaptureList? ']'`
  - `CaptureElement ::= '&'? Identifier | 'this'`

- **Parser**：`parser.cc:4270-4310` 完整实现了 `parseCaptureClause()` 和 `parseCaptureElement()`，支持三种形式：无引用标识符（拷贝捕获）、`&` 前缀标识符（引用捕获）、`this` 关键字。

- **AST**：`ast/expression.h:655-672` 定义了 `CaptureElement` 节点，字段为 `isByReference / identifier / isThis`；`FunctionExpression` 显式持有 `NodeList<CaptureElement>`。

- **测试**：`tests/language/declarations/functions/closure-function-definitions/captures.zom` 已验证 `use [x, &y, this]` 的 AST dump 输出，语法解析正确。

### 2. 语义层：规范正文完全空白（零描述）

在全部 17 章规范中，**除 grammar reference 的 EBNF 之外**，搜索 `capture/CaptureClause/CaptureElement/捕获` **零命中**。具体缺失：

#### 2.1 隐式捕获 vs 显式捕获的关系未定
`04-expressions.md:358-361` 给出了一个**不带** `use[]` 的闭包示例：
```zom
let multiplier = 3;
let multiply = fun (x: i32) -> i32 { return x * multiplier; };
```
这暗示隐式捕获合法。但完全没有说明：
- 这是规范行为还是只是示例作者随手写的？
- `use[]` 和隐式捕获的交互：如果写了 `use [a]` 却在闭包里用到了 `b`，是报错、警告还是自动追加？
- 没有任何说明。

#### 2.2 `&` 前缀的语义悬空
ZOM 内存模型（14 章）是 **ARC + 值类型拷贝**，**没有 Rust 式借用/引用类型**。AST 中 `CaptureElement::isByReference` 这个字段在整个类型系统中没有对应概念。`&x` 到底是：
- C++ 风格引用捕获（绑定到原变量地址，无生命周期检查）？
- Rust 风格借用（引入生命周期参数）？
- Swift 风格 `&` 作为"不可拷贝的引用语义"提示？
- 完全没有说明。

#### 2.3 `this` 捕获与 ARC 零交互说明
14 章内存管理提到了 `weak parent: Parent?` 用于打破 ARC 环，但完全没有提及闭包捕获 `this` 时默认是强引用还是弱引用。这是 **Objective-C/Swift 历史上著名的闭包循环引用内存泄漏来源**，作为内存安全语言居然没有规范，属于严重遗漏。

#### 2.4 函数类型层面缺少捕获建模
`ast/type.h:392-410` 的 `FunctionTypeNode` 只有 `typeParameters / parameters / returnType`，**没有 captures 字段**。这意味着两个参数返回值完全相同但捕获方式不同的闭包在类型层面不可区分：
```zom
let f1 = fun () -> i32 use [x] { return x; };   // 拷贝捕获
let f2 = fun () -> i32 use [&x] { return x; };  // 引用捕获
// f1 和 f2 在类型系统中完全相同？ 规范无答。
```
这也意味着 **不存在 Fn/FnMut/FnMove trait 层级**（原问题中提到的），因为连捕获信息都没有进入类型系统。

#### 2.5 Binder 层"预留了功能但完全未用"
`symbol/value-symbol.h:100-101` 声明了 `VariableSymbol::isCaptured() const` 方法，但全代码库 grep **没有任何地方设置这个标志**（无 `setCaptured`，无赋值点）。这意味着：
- 即使写了 `use [x]`，`x` 的 `isCaptured()` 仍返回 false
- 隐式捕获的 `multiplier`（示例中）完全没有被分析和登记
- 语义层面**连最基本的"哪些变量被捕获"都还没开始跟踪**

### 3. 默认捕获策略（移动/拷贝/引用）完全未定
spec 中没有任何地方说明：
- 值类型（struct）默认捕获是拷贝还是移动？
- 引用类型（class）默认捕获是强引用？
- 是否支持强制移动捕获（类似 Rust `move ||` 或 C++ `[=]`/`[&]` 的整体默认）？
- 赋值给闭包后原变量是否还能使用？是否发生移动？

### 4. 严重度评估：上调至 HIGH
原报告为 **medium**，我基于以下理由上调为 **high**：

1. **核心特性**：闭包/匿名函数是现代语言最核心的抽象工具之一，不是边缘特性。
2. **内存安全直接相关**：捕获语义未定 → ARC 环无法静态检测 → 内存泄漏/悬垂引用属于内存安全问题，违反了 01-introduction.md 中 "Safety First" 设计原则第一条。
3. **语法已落地但语义悬空**：用户可以写出 `fun () use [&x, this] { ... }` 并通过 parser，也能写出示例中的隐式捕获。两种形式都没有规范语义，会导致未来实现时出现破坏性不兼容变更。
4. **类型系统级缺口**：FunctionType 不带捕获信息，这意味着后续要补 trait 层级或捕获类型时需要修改函数类型的核心表示，影响面极大。

medium 的下调理由虽然成立（整个 checker 为空，暂未实际产生错误），但那是"整个项目阶段都还早"的语境，不能作为闭包语义这个具体问题本身严重性不足的理由。
- 反对方: 反驳点（被证伪的部分）：
1. **Rust 式预期被错误注入**：原描述中"Fn/FnMut/FnMove trait 层级"和"move || 风格强制移动"两项是 Rust 借用检查模型的产物。ZOM 的内存模型是 ARC + 值类型/引用类型二分（第 14 章），无 borrow checker，因此根本不需要三态 Fn trait 层级，也不需要 move 闭包语法——值类型默认拷贝、引用类型默认 ARC retain。这两项作为"缺失项"列出属于上下文误读，应从评估中剔除。
2. **与 Checker 整体 0% 的同质化**：类型推断、子类型规则、泛型约束求解、接口一致性检查、模式穷举性全部是 0% 实现（checker.h 类被注释，driver.cc 管线无 checkSources）。捕获语义缺失是这整个 Checker 阶段的子项，不是一个孤立的"设计问题"。如果每个"语法已落地但语义未写"都单独拎出，会产生数十条同构问题。
3. **语法先行的合理开发顺序**：CaptureClause 进入正式 EBNF，并不意味着语义必须同期完成。parser 同样支持完整的泛型语法 `<T extends C>`，但泛型实例化、variance 处理、where 子句在语义层同样为 0%——这些没有被列为独立问题，说明 CaptureClause 被单独挑出存在选择偏倚。
4. **缺少显式"未来预留"声明不构成缺陷**：并发/属性两章写了"留给未来"是因为整章内容为空，Function Expressions 章节有部分示例内容，没有用同样格式声明是正常写法，不等于承诺语义已完备。

无法证伪的核心事实：
- 默认捕获模式（隐式 vs 强制显式）文档未说明，且示例 `return x * multiplier`（无 use[]）和 EBNF 声明式语法指向两种不同答案
- `&Identifier` 引用捕获在无 borrow checker 的 ARC 模型下语义不明（是 C++ 裸引用？Swift unowned？还是 Rust borrow？）
- `this` 捕获是强引用 retain（可能造环）还是默认弱引用，文档无答案
- `FunctionTypeNode` 完全没有捕获环境的类型表示，导致"捕获了什么"在类型层面不可见——这是一个 ARC 模型下也必须回答的设计缺口，不是实现缺失
- Binder 的 `visit(FunctionExpression)` 完全不遍历 `node.getCaptures()`，captures 列表在整个 pipeline 中是死节点，说明设计状态模糊而非单纯"尚未实现"

### 23. 🟠 [高] checker 单元测试假阳性：名为类型检查但实际只调用 parse  
**类别**: 语义与组合性 | **置信度**: 86%

**问题描述**  
checker-test.cc 的 4 个用例命名暗示产生类型诊断（如 CheckerTest_TypeMismatchError），但实际只调用 parser.parse() 并断言成功，未进行任何类型检查，产生假阳性。

**证据**
  - products/zomlang/tests/unittests/compiler/checker/checker-test.cc:15 — `CheckerTest_TypeMismatchError only calls parser.parse(), no check()`
    用例未实际调用类型检查
  - products/zomlang/tests/unittests/compiler/checker/checker-test.cc:35 — `All 4 tests marked with TODO to add type checking`
    明确标记为 TODO，运行时仍显示通过

**潜在影响**  
报告通过率时产生误导，使团队认为类型检查已覆盖 4 个场景，实际为 0。

**修复建议**  
在 Checker 真正实现前将用例标记为 ZC_SKIP，或改为断言未实现的明确失败。
**评审备注**
- 确认方: 独立核验确认以下事实：

1. checker-test.cc 全文 91 行，共 4 个 ZC_TEST：CheckerTest_BasicParsingWorks、CheckerTest_TypeMismatchError、CheckerTest_UndefinedVariableError、CheckerTest_FunctionParameterTypeChecking。每个测试仅构建 Parser、调用 parser.parse()、用 ZC_EXPECT(ast != zc::none) 断言解析成功、末尾写一行 TODO 注释 "Add type checking once Checker implementation is available"。没有任何测试调用 checker 命名空间的 API，也未 include checker/checker.h。

2. Checker 本身（compiler/checker/checker.h 第 24-36 行）的 TypeChecker 类被整段注释掉，checker.cc 仅含空命名空间 namespace checker {}，即类型检查阶段尚未实现。

3. 实际运行 build-sanitizer 下的 checker-test：4 个测试全部输出 [PASS]，结尾显示 "4 test(s) passed"。CTestTestfile.cmake 中该测试被打标签 unittest;checker，在 ctest 统计时会被算为通过，形成假阳性。

4. 命名误导问题：TypeMismatchError、UndefinedVariableError、FunctionParameterTypeChecking 这三个名字强烈暗示将产生类型诊断，但实际只验证了语法层的合法性，与命名意图完全脱节。

5. 严重度上调至 high 的理由：这不是"测试不完整"，而是"测试名与断言含义错位"—— checker 语义覆盖实际为 0/4 场景，但 unittest 报告 4/4 通过，这种零成本的欺骗性直接干扰路线图进度判断，且 checker 是当前编译器最关键的未完成模块（driver 管线中甚至没有 checkSources 阶段）。

6. 对比：tests/language/semantic/ 和 regression/ 是空目录，团队不会误以为 lit 语义测试已覆盖；但 checker unittest 这 4 个 PASS 恰好制造了"checker 单测已覆盖 4 个场景"的错觉。

建议：
- 短期：在每个测试体的 TODO 位置显式 ZC_FAIL("Type checking not implemented yet")，或在 CMake 里临时禁用该目标，直到 Checker 可被实际调用。
- 中期：实现真正的 checker 调用后再把断言改为期望的语义诊断（例如 TypeMismatchError 用例应断言 diagnosticEngine 收到 ZOM3001 TypeMismatch）。

关键文件（绝对路径）：
- /Users/bytedance/Develop/ZOM/products/zomlang/tests/unittests/compiler/checker/checker-test.cc
- /Users/bytedance/Develop/ZOM/products/zomlang/tests/unittests/compiler/checker/CMakeLists.txt
- /Users/bytedance/Develop/ZOM/products/zomlang/compiler/checker/checker.h
- /Users/bytedance/Develop/ZOM/products/zomlang/compiler/checker/checker.cc
- /Users/bytedance/Develop/ZOM/libraries/zc/ztest/test.h
- 反对方: 以下四点可削弱"假阳性"的指控：

1. **TDD 骨架模式而非假阳性**：4 个用例都显式标注 `// TODO: Add type checking once Checker implementation is available`，说明开发者是在 Checker 实现前先把"后续需要验证的场景骨架"搭好，这是常见的 TDD 先行模式，并非有意欺骗。测试只断言了它实际能验证的东西（parser 解析成功），没有对 checker 输出做出任何虚假断言。

2. **代码语义层面的事实正确**：`let x: i32 = "string";`、`let x: i32 = y + 1;`、`fun add(a: i32, b: str)->i32 { return a + b; }` 在语法层面都是合法的 ZOM 程序，parser 成功解析是正确行为。如果测试在没有 checker 的情况下强行断言"有诊断"，那才是错误。当前断言 `ast != zc::none` 本身是真命题，不是伪阳性。

3. **"假阳性"的定义不匹配**：假阳性（false positive）要求"测试声称验证 X，X 实际未发生但测试通过"。这些测试的 `ZC_EXPECT` 只涉及 parser 结果，没有任何 EXCEPT 断言"类型检查产生了错误"。它们从未声称做过类型检查断言——是测试命名给读者造成了预期偏差，而非测试本身做出了虚假声明。

4. **包含 checker.h 也无济于事**：checker/checker.h 中 `TypeChecker` 类整体被注释，`checker.cc` 是空命名空间。即便这些测试调用了 checker，也没有实现可调用。

综合来看，"假阳性"这个定性过于强烈：这些测试确实没有验证它们命名所暗示的东西，但它们也没有断言任何虚假事实。更准确的定性应该是"测试命名与定位误导"而非"假阳性"。

### 24. 🟠 [高] weak 修饰符在规范内存管理示例中出现，但 parser 未接入  
**类别**: 规范-实现不一致 | **置信度**: 84%

**问题描述**  
规范 14 章内存管理给出 weak parent: Parent? 和 weak this 的使用示例，但 parser 的 isModifier() 和 parseModifiers() 中没有 weak 关键字识别。

**证据**
  - docs/spec/chapters/14-memory-management.md:55 — `weak parent: Parent? used in class example`
    规范示例使用 weak 修饰字段
  - products/zomlang/compiler/parser/parser.cc:1057 — `isModifier() does not include WeakKeyword`
    parser 修饰符列表缺少 weak
  - products/zomlang/compiler/ast/kinds.h:200 — `No WeakKeyword SyntaxKind defined or present`
    词法层可能未定义 weak token

**潜在影响**  
ARC 环打破机制在语法层面不可用，可能导致引用循环无法表达。

**修复建议**  
在 kinds.h 中增加 WeakKeyword，parser 中接入 isModifier()，AST 的 PropertyDeclaration/ParameterDeclaration 等增加 weak 位或使用专用修饰符。
**评审备注**
- 确认方: ## 核验结论

候选问题 SPC-PAR-004 **真实存在**，但我把它从 medium 调整为 high，理由如下。

## 独立证据（全部核验通过）

### 证据 1：规范示例确实使用了 weak
- `/Users/bytedance/Develop/ZOM/docs/spec/chapters/14-memory-management.md:67` — `child.parent = weak this;`（作为表达式前缀形式使用）
- `/Users/bytedance/Develop/ZOM/docs/spec/chapters/14-memory-management.md:73` — `weak parent: Parent?,`（作为类字段的修饰符使用）
- 第 60 行小节标题即为 "Weak References"，并给出了注释 "Weak reference to avoid cycles"。**这不是零星示例，而是专章讲解的正式语言特性。**

### 证据 2：parser 的 isModifier() 确实缺少 weak
- `/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc:1055-1062` — isModifier() 只识别 Abstract/Export/Public/Private/Protected/Static/Readonly/Mutating/Override，**没有 weak**。
- parseModifiers()（parser.cc:4554 行的 while 循环）完全依赖 isModifier() 来消费修饰符 token。因此即便 lexer 能产出 WeakKeyword，也无法被解析为修饰符。

### 证据 3：词法层完全没有 WeakKeyword
- `/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/kinds.h` — `grep -i weak` 零命中。整个 SyntaxKind 枚举中没有 WeakKeyword。
- `/Users/bytedance/Develop/ZOM/products/zomlang/compiler/lexer/` 全部文件 — `grep -i weak` 零命中。lexer 不会把 `weak` 识别成关键字，只会当作普通 Identifier。
- 形式文法 `docs/spec/ZomLexer.g4` 和 `ZomParser.g4` — `grep -i weak` 零命中。

### 证据 4：形式文法的 Modifier 规则也缺失 weak
- `/Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:155` — `Modifier ::= 'public' | 'private' | 'protected' | 'static' | 'readonly' | 'mutating' | 'override'`，**没有 weak**。
- `/Users/bytedance/Develop/ZOM/docs/spec/chapters/02-lexical-structure.md:138-144` — "Modifier Keywords" 表列出了 15 个修饰符关键字，**也没有 weak**。

### 证据 5：语义/符号层也无 weak 建模
- `/Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/symbol-flags.h` — 零 weak 相关位标志。
- PropertyDeclaration、ParameterDeclaration 等 AST 节点也无 weak 字段。

## 特殊发现：两种语法形态，两种缺口

weak 在规范示例里实际上有**两种**完全不同的语法用法：
1. **修饰符用法**（:73）：`weak parent: Parent?` — 要求 weak 作为 Modifier（字段/参数级）。
2. **表达式前缀用法**（:67）：`weak this` — 要求 weak 作为前缀表达式运算符（类似 `typeof`、`await`），语法形态完全不同于修饰符。

原报告只指出了缺口 1（修饰符），但遗漏了**缺口 2：表达式形式 `weak this`**。后者在当前 parser 中更是完全没有入口：parseSimpleUnaryExpression 里没有 WeakKeyword 分支，AST 里也没有 WeakExpression 或类似节点。

## 严重度调整理由：medium → high

原评估为 medium，我升级为 high，理由：

1. **内存管理章节是规范核心章节**，weak 是该章给出的**唯一打破 ARC 引用环的机制**。若无 weak，双向关联的 class 设计（Parent↔Child、Observer↔Subject、Graph Node↔Edge）会确定性泄漏。这不是"可选语法糖"，而是内存模型的基础原语。

2. **规范-实现-形式文法-关键字表四者同时不一致**：不只是 parser 漏了，而是 spec 内部（示例↔关键字表↔EBNF）就互相矛盾，加上 lexer/parser/AST/symbol 五层全缺。修复需要在至少 6-7 个文件中同步改动，工作量和回归风险高于普通修饰符遗漏。

3. **有表达式用法的"隐藏缺口"**：原报告只提到修饰符路径，实际上 `weak this` 作为表达式前缀的缺口更大（需要新的 AST 节点、新的 unary 分支），影响面超出了原报告的分析范围。

4. **下游语义阶段未开工可以接受降调，但当前阶段语义层（Checker）是空壳是全局状态，不构成该问题的专属减轻因素**。按当前项目进展（parser→binder 已完成，checker 未启动），parser 层应提供完整的语法承载，否则后续启动 checker 时会发现语法层没留扩展位，造成二次重构。

## 建议修正后的修复范围

- `docs/spec/chapters/02-lexical-structure.md`：Modifier Keywords 表补 `weak`
- `docs/spec/chapters/17-grammar-reference.md`：Modifier 规则加 `| 'weak'`；并新增 PostfixUnaryExpression / PrefixUnaryExpression 中的 `'weak' UnaryExpression` 产生式（覆盖 `weak this` 形式）
- `docs/spec/ZomLexer.g4`：加 WEAK token
- `docs/spec/ZomParser.g4`：同步 Modifier 和表达式规则
- `products/zomlang/compiler/ast/kinds.h`：加 SyntaxKind::WeakKeyword
- `products/zomlang/compiler/lexer/`：关键字识别 `weak`
- `products/zomlang/compiler/parser/parser.cc`：isModifier() 加 WeakKeyword；parseSimpleUnaryExpression 加 weak 分支
- `products/zomlang/compiler/ast/expression.h` / `ast-nodes.def`：新增 WeakExpression（表达式形式）
- `products/zomlang/compiler/ast/statement.h`：PropertyDeclaration/ParameterDeclaration 的修饰符向量需支持 WeakKeyword（当前已用 SyntaxKind 向量，无需新增字段，但文档应明确）
- `products/zomlang/compiler/symbol/symbol-flags.h`：补 Weak 位
- 测试：补 lit 负面用例（当前 weak 会被解析为标识符 + 后续诊断不友好）
- 反对方: 一、无法直接证伪的事实：
1. docs/spec/chapters/14-memory-management.md:67 和 :73 确实在示例中使用了 `weak this`（赋值右值表达式位置）和 `weak parent: Parent?`（字段修饰符位置）。
2. products/zomlang/compiler/ast/kinds.h 中未定义 WeakKeyword SyntaxKind；全 compiler 目录 grep "WeakKeyword\|\"weak\"" 零命中。
3. products/zomlang/compiler/parser/parser.cc:1055-1062 isModifier() 仅包含 Abstract/Export/Public/Private/Protected/Static/Readonly/Mutating/Override 九种关键字，无 weak。
4. lexer/lexer.cc 和 lexer/token.h 均不识别 weak 字面量。
5. symbol/symbol-flags.h 无 Weak/WeakRef 相关位标志。

二、反驳/降低严重性的核心证据（问题前提不完全成立）：
1. 规范自身的「正式文法定义」与 parser 是一致的、同样不含 weak：
   - docs/spec/chapters/17-grammar-reference.md:155 Modifier 产生式：`'public' | 'private' | 'protected' | 'static' | 'readonly' | 'mutating' | 'override'`，共 7 种，**不含 weak**。
   - docs/spec/chapters/02-lexical-structure.md 关键字保留字全集（第 107-160 行约 70+ 个词）中**没有 weak**。
   - docs/spec/ZomLexer.g4、ZomParser.g4 均无 weak。
   - 08-classes-and-structures.md、06-declarations.md 两章正文与文法均未提 weak。
2. 因此问题陈述中「规范 14 章给出了 weak 用法，parser 没实现」是 **spec 内部不一致**，不是「规范定义了，parser 漏了」。weak 仅出现在 14 章的两段示例代码里，没有任何正式文法/关键字表/章节论述支撑它是当前版本已纳入语言的关键字。
3. 作为旁证：parser 反而比 Modifier 正式文法**多出**了 AbstractKeyword（spec grammar 无 abstract，parser 有），说明 parser 与"示例级描述"的差异是双向的，整体上 parser 是对齐 17 章 EBNF 的。
4. `child.parent = weak this;` 这一行如果 weak 是一元操作符，17 章 Prefix/Unary 产生式也没有对应条目，14 章示例本身就是自说自话。

三、结论：
不是严格意义的「spec-impl-mismatch」（spec 正式定义了 X，而 impl 没做 X），而是「spec 章节示例超前写了未来语法，正式文法与关键字表与 parser 三方一致都没纳入 weak」。本质是 14 章 Weak References 小节的示例属于未落地的设计，应当加上 "future / reserved" 标注或改写为伪代码。

### 25. 🟡 [中] VoidExpression 和 AwaitExpression 在 AST 声明、factory 完整，但 parser 从不创建  
**类别**: 语义与组合性 | **置信度**: 98%

**问题描述**  
ast-nodes.def 中登记了 VoidExpression 和 AwaitExpression，factory 中有 createVoidExpression / createAwaitExpression，但 parser.cc 中完全没有调用点和对应关键字分支。

**证据**
  - products/zomlang/compiler/ast/ast-nodes.def:120 — `AST_UNARY_NODE(VoidExpression, ...)`
    AST 注册表声明 VoidExpression
  - products/zomlang/compiler/ast/ast-nodes.def:122 — `AST_ELEMENT_NODE(AwaitExpression, Expression)`
    AST 注册表声明 AwaitExpression
  - products/zomlang/compiler/parser/parser.cc:3600 — `parseSimpleUnaryExpression has no void or await keyword branch`
    parser 没有对应解析分支，grep 全文件 createVoidExpression / createAwaitExpression 零命中

**潜在影响**  
产生悬空能力：AST/factory 层维护着无法触达的节点类型，增加维护成本且误导后续开发者。

**修复建议**  
若近期不计划实现 void 和 await，从 ast-nodes.def、factory、expression.h 中移除；若计划实现，在规范中补充并在 parser 接入。
**评审备注**
- 确认方: ## 独立核验的证据链

### 一、AST 层登记与类声明（两处节点均存在）
- ast-nodes.def:170 `AST_ELEMENT_NODE(VoidExpression, UnaryExpression)`
- ast-nodes.def:172 `AST_ELEMENT_NODE(AwaitExpression, Expression)`
- expression.h:591 `class VoidExpression final : public UnaryExpression`（含构造/析构声明）
- expression.h:623 `class AwaitExpression final : public Expression`（含构造/析构声明）
→ 断言为真：两处节点均已在 X-Macro 注册表与 C++ 类声明中完整存在。

### 二、Factory 层实现（两处 factory 均存在）
- factory.h:369 `createVoidExpression`、factory.h:373 `createAwaitExpression` 声明
- factory.cc:351 / 359 两个实现函数，`grep -rn` 全 compiler 目录仅这 4 处命中，无其它调用
→ 断言为真：factory 完整，且全项目零调用点。

### 三、Parser 层（零调用点，且缺少关键字分支）
- parser.h/parser.cc 中全文搜索 `AwaitKeyword`、`VoidKeyword`、`createVoidExpression`、`createAwaitExpression`、`parseVoidExpression`、`parseAwaitExpression`：**均零命中**。
- `parseSimpleUnaryExpression()`（parser.cc:2653-2670）的 switch 只分了 4 个符号 case(+/-/~/!) 和 `TypeOfKeyword`，再加 default→parseUpdateExpression。既无 `void` 分支也无 `await` 分支。
- 补充核查：`kinds.h` 中**只有 `AwaitKeyword`（行 50）**，不存在 `VoidKeyword`——即 `void` 作为一元运算符时甚至没有对应 token kind，parser 无论如何不可能产出 VoidExpression。await 有 token 占位但未接入。
→ 候选证据"parser 没有对应解析分支、factory 零命中"完全属实。

### 四、规范层面（await 已明确预留，void 未提及）
- `15-concurrency.md:5` 明确："The current parser grammar does not define `async`, `await`, ... reserved for future language design."
- `04-expressions.md`（表达式章节）与 `17-grammar-reference.md`（EBNF 完整文法）中均**未列出 void 表达式或 await 表达式**产生式。
- lexer 保留字表（`02-lexical-structure.md:107-163`）await/async 在其中；void 未在保留字中单独列名。
→ await 是"有意预留 + spec 已声明"，void 则是 spec 和 parser 双缺失，仅 AST/factory 多写了。

### 五、行为端验证（parser 对 `await expr` 报错，而非静默通过）
parser-test.cc:2505-2512 `ParseAwaitSyntaxNotYetSupported` 用例：
```
fun foo() { let x = await bar(); }
```
断言 `diagnosticEngine->hasErrors()` 通过。说明 parser 走的是"错误恢复 + 诊断"路径，不会把 `await` 当作标识符。这进一步证明：**await 关键字确实保留了，但没有走到 AwaitExpression 构建分支**。

## 严重度评估：medium（与原评估一致）

理由：
- **无正确性/安全影响**：节点无法被 parser 产出，不影响当前编译产物，也不会让用户写出误接受的代码（await 反而会被拒绝并报错，符合 spec 预期）。
- **有维护成本**：两处 AST 节点 + factory 共约 30~40 行死代码（声明、宏、构造、析构、classof、dumper、visitor 开关、serializer 分支……），每次改动 AST 基础设施都需要额外维护这两个永远走不到的分支。
- **误导风险中等**：后续开发者在 expression.h 看到 `VoidExpression`/`AwaitExpression`，可能以为 parser 已实现 void/await，直到调试时才发现无法触达。不过 AwaitExpression 与 concurrency 章节的"保留给未来"说明相呼应，void 则更显突兀。
- **与不支持语法模式一致**：throw / try / for-of / namespace / declare 等也属于"AST 未声明 + parser 报错恢复"模式，这里只是"AST 超前声明了"，区别仅在于超前程度。

**微调建议**：若按节点拆分严重度，`VoidExpression`（spec 毫无提及，甚至无 `VoidKeyword` token）应评估为 **medium**，`AwaitExpression`（spec 明确预留、单元测试断言报错、有 token kind）应评估为 **low**——但作为同一条目打包报告，**medium** 是合理综合等级。

## 修正建议
与原建议一致，但补充优先级：
1. **立即做**：从 ast-nodes.def、expression.h、factory.{h,cc}、dumper/classof/serializer 等基础设施中移除 `VoidExpression`，因为 spec 从未声明、保留字表无 void、token 无 VoidKeyword。移除后可简化 AST 维护。
2. **可选/暂缓**：`AwaitExpression` 若 concurrency 仍在规划中，可保留，但建议在 expression.h 注释处加 `// Reserved for future async/await (see spec 15-concurrency.md)` 明示"有意预留"，并在 parser.h 的 unary 入口注释未来接入点；若近期无并发路线，也可一并移除，后续从 spec 反推实现。

## 相关绝对路径
- AST 注册表：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/ast-nodes.def`
- 节点声明：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/expression.h`
- Factory 声明/实现：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/factory.h`、`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/factory.cc`
- Parser 入口（parseSimpleUnaryExpression）：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc`（第 2653 行）
- 保留字/并发说明：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/02-lexical-structure.md`、`/Users/bytedance/Develop/ZOM/docs/spec/chapters/15-concurrency.md`
- 负面测试（await 断言报错）：`/Users/bytedance/Develop/ZOM/products/zomlang/tests/unittests/compiler/parser/parser-test.cc`（第 2495-2512 行）
- Token/SyntaxKind 清单：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/kinds.h`
- 反对方: 
证伪证据链（按效力强弱排序）：

1. **`void` 不是 ZOM 关键字 — 完全推翻前提**
   - `ast/kinds.h` 整个 `SyntaxKind` 枚举（第 24-146 行关键字段）**不存在 `VoidKeyword`**，grep 全项目 `VoidKeyword` 除 expression.cc 内的 `SyntaxKind::VoidExpression` AST 节点类型外零命中。
   - `lexer/utils.cc` 的 `getKeywordKind`（第 166-271 行）完整关键字列表约 80 项，**无 `"void"` 映射**。ZOM 用作"无返回值"的类型是 `unit`（见 `types.md:66`、`kinds.h:133`），而非 `void`。
   - 规范 `04-expressions.md` 的一元表达式章节只列 `+ - ! ~ typeof ++ --`，`17-grammar-reference.md` EBNF 全文无 `void` 字样。`VoidExpression` 对应的 JS 风格 `void 0` 运算符在 ZOM 设计中根本不存在。
   - 结论：`VoidExpression` 属于从 TS/JS 参考架构中带入的**AST 过度设计遗留物**，不是"parser 漏接"。

2. **`await` — 规范明确声明当前不接入，parser 报错是预期行为**
   - `15-concurrency.md` 第 3-7 行原文："Concurrency syntax is reserved for future language design. The current parser grammar does not define `async`, `await`..."
   - unittest `parser-test.cc:2499` 已存在 `ParseAwaitExpressionReportsError` 用例，断言 `await bar()` 必须报错（诊断信息写着 "Await syntax is not designed yet"）。这证明 parser 不创建 `AwaitExpression` 不仅符合规范，而且**有测试锁定此行为**。
   - AST/factory/binder/dumper 中存在 `AwaitExpression` 骨架是编译器常见的**演进式架构策略**：先为访问者模式统一预留接口，等并发设计完成后再在 parser 接入。binder.cc:568-570、dumper.cc:1056 都已经实现了对应 visit 方法，可见是有意的前瞻布局。

3. **原证据的多处不严谨**
   - 声明 ast-nodes.def:120/122，实际行号为 170/172。
   - 未查阅规范（15-concurrency.md、04-expressions.md），未区分"有意不实现"与"遗漏实现"。
   - 未检查词法层，未发现 `void` 甚至不被识别为关键字。
   - 未查看 unittest，漏掉了 `ParseAwaitExpressionReportsError` 这个锁定"当前不支持"的反向证据。

4. **调用点零命中不代表 bug**
   - `createVoidExpression/createAwaitExpression` 全编译器（包括 desugar、checker 等所有阶段）均零调用，是因为它们**从未被设计为当前版本应该产出**的节点。
   - 类似的"已声明但当前不产出"的 AST 节点在该项目中不止一例：`SuperExpression`（ast-nodes.def 登记但 expression.h 无类声明，parser 通过 MemberExpression 承载 super）、5 个 `*Body` 节点（有名无实），均属于实现裁剪或架构预留策略。

5. **错误的问题分类和严重度**
   - 类别标为 `semantics`：这完全不涉及语义一致性，因为这两种表达式**根本不在当前语言的语义集合里**。
   - 严重度 `medium`：对当前语言功能零影响，用户无法触发任何异常行为。最多算代码卫生层面的 `info` 级维护性提示。


### 26. 🟡 [中] keyof 类型操作符规范存在、但无 AST/解析/实现  
**类别**: 规范-实现不一致 | **置信度**: 98%

**问题描述**  
03-types.md:241 举例了 `keyof { name: str, age: i32 } // = "name" | "age"`；kinds.h 里有 KeyOfKeyword token。但 AST 层没有 KeyOfTypeNode，type.h 的 TypeQueryNode 只处理 `typeof expr`，不处理 `keyof T`。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/03-types.md:241 — `type PersonKeys = keyof { name: str, age: i32 }; // "name" | "age"`
    spec 中存在示例。
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/kinds.h:83 — `KeyOfKeyword,        // keyof`
    Token 存在。
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/type.h:430 — `class TypeQueryNode final : public TypeNode {`
    type.h 只定义了 typeof expr 这一种 TypeQuery，没有 keyof。parser 的 parseTypeQuery 大概率只走 typeof 分支 (需要验证，但 AST 已证明缺节点类)。

**潜在影响**  
keyof 是 TS 风格类型元编程的核心；如果 ZOM 要走那条路线，需要同步补充 Index Access T[K]、Mapped Types 等一整套家族。如果不打算走，就从 spec 和关键字表移除，避免用户期望被误导。

**修复建议**  
路线二选一：(A) 把 keyof / IndexAccessTypeNode / MappedType 一起纳入 v2+ 路线图，并在 spec 中明确标注「v2 planned」；当前先从 spec 示例中移除 PersonKeys 示例。(B) 短期实现：新增 KeyOfTypeNode，Checker 端仅针对 ObjectTypeNode 返回一个枚举类型的成员名联合。
**评审备注**
- 确认方: 独立核验的证据链（与原指控交叉但不依赖）：

已确认的实现缺口：
1. /Users/bytedance/Develop/ZOM/docs/spec/chapters/03-types.md 第 229-242 行，Type Queries 章节正文给出 type PersonKeys = keyof { name: str, age: i32 } 示例。
2. /Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/kinds.h 第 83 行，KeyOfKeyword token 枚举条目存在。
3. /Users/bytedance/Develop/ZOM/products/zomlang/compiler/lexer/utils.cc 第 207 行，lexer 将字面量 keyof 映射为 KeyOfKeyword（词法层已完整支持）。
4. /Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/ast-nodes.def 第 203 行，AST 节点注册表仅有 TypeQueryNode，无任何 KeyOfTypeNode 或 keyof 相关节点。
5. /Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/type.h 第 430-444 行，TypeQueryNode 类注释写着 Type query: typeof T，构造函数只接受 Expression 指针，没有接受 TypeNode 参数的 keyof 变体，也无独立 KeyOfTypeNode 类。
6. /Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc 第 520-538 行，parseTypeQuery 第 530 行明确判断 token.is(TypeOfKeyword)，完全没有 KeyOfKeyword 分支。用户写出 keyof T 时 parser 会落到错误恢复路径。
7. 全测试目录 grep keyof/KeyOf 零命中，没有任何单元测试或 lit 测试覆盖 keyof 语法（包括负面测试）。现有 types/type-query.zom 只覆盖 typeof。

额外发现——规范内部不一致（比原指控范围更广）：
8. /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md 第 185-186 行，权威 EBNF 只定义 TypeQuery = 'typeof' TypeQueryExpression，完全没有 keyof 产生式。即第 03 章示例与第 17 章语法参考在同一文档内部就不一致。
9. /Users/bytedance/Develop/ZOM/docs/spec/ZomLexer.g4 第 103 行，ANTLR 词法有 KEYOF: 'keyof'，但 ZomParser.g4 未引用该 token（grep 零命中），与 EBNF 17 章的遗漏一致。

因此问题全景是三重不一致链：
  Lexer token（有）-> Parser 分支（无）-> AST 节点（无）-> Checker 语义（整个阶段空壳）
  外加 Spec 内部：正文示例（有）<-> 语法参考 EBNF（无）<-> ANTLR Parser（无）

与 typeof 的对比佐证：typeof 是完整贯通的（lexer token + parser parseTypeQuery + ast TypeQueryNode + 对应 lit 测试），而 keyof 只走通了 lexer 这一层。

严重度评估说明：medium 是合理的。关键依据：(a) 问题已在用户可见层出现两个信号——spec 文档示例 + 词法 token 识别，用户若尝试使用 keyof 不会得到明确的"特性不存在"提示，而是会被 parser 报出不相关的语法错误，形成认知落差；(b) 但考虑到整个类型检查器是空壳，且规范语法参考和 ANTLR parser 侧也同样未定义 keyof，这说明 keyof 不属于"刻意裁剪的正式特性"，而是文档和词法的前瞻性残留，实际影响比典型的"spec 已承诺但实现缺失"要小。将 refinedSeverity 保持 medium。
- 反对方: 以下是反驳/证伪方向的尝试结果（全部失败）：

1. **EBNF 文法自洽性尝试**：查阅 17-grammar-reference.md 第 178-186 行，`TypeQuery ::= 'typeof' TypeQueryExpression`，未定义 keyof。这表明 spec 内部（03-types.md vs 17-grammar-reference.md）本身就不一致，TypeScript 式的 `keyof` 属于 03 章"超前示例"而非已纳入文法的规范。但这并不抵消"03-types.md 有示例 vs 代码无实现"的 mismatch，只是说明候选问题的"spec"来源只覆盖了 03 章。

2. **TypeQueryNode 可复用尝试**：查看 type.h:430-444，`TypeQueryNode` 构造函数接受 `zc::Own<Expression>`。但 `keyof T` 中的操作数 T 是 TypeNode（类型表达式，如 `{ name: str, age: i32 }`、`SomeStruct`、`Array<str>`），不是 Expression。结构上无法复用 TypeQueryNode，必须新建 `KeyOfTypeNode : TypeNode { type : TypeNode }`。因此 AST 层的确缺失。

3. **parser 隐式支持尝试**：parser.cc:520-538 的 `parseTypeQuery()` 第 530 行只有 `if (!token.is(ast::SyntaxKind::TypeOfKeyword)) { return zc::none; }`，对 KeyOfKeyword 没有任何分支。如果源码中写 `type T = keyof Foo;`，parser 会在 "Atom" 层级（parsePostfixType → parsePrimaryType/parseAtomType）尝试处理，遇到 `keyof` 既不是类型引用也不是括号/对象/元组/函数/typeof 类型，因此会产生"类型期望"类诊断错误。

4. **测试覆盖尝试**：全 tests 目录（lit + unittest）中 `keyof` 出现次数为 0。既没有正面用例也没有"保留字作标识符"的负面用例，和其它保留字（`throw`/`namespace`/`declare`/`for-of`）均有 parser-test.cc 中的显式报错恢复用例形成对比，说明开发人员甚至没有为 "keyof 作为保留字报错" 这条兜底路径写测试。

5. **ast-nodes.def 注册检查**：ast-nodes.def 中只有 `AST_ELEMENT_NODE(TypeQueryNode, TypeNode)`，不存在任何 KeyOf 相关节点。AST 注册表层面确认缺失。

### 27. 🟡 [中] 错误处理运算符 `?!`/`!!`/`?:` 在优先级表中被混为一档，但文法上属于两个完全不同的层级  
**类别**: 语法设计 | **置信度**: 97%

**问题描述**  
错误处理运算符优先级在 04-expressions.md 的总表中被笼统地放在 "17. Error Handling: `?!`, `!!`, `?:`" 一档，但三者在 EBNF 中实际属于三个不同层级：`?!`/`!!` 在 PostfixSuffix（最高，与 `++`/`--` 并列），`?:`（ErrorDefault）在 CoalesceExpression 和 LogicalOrExpression 之间（中低优先级）。04 章的总表把它们全部写在 Null Coalescing（16）之上 Conditional（18）之下，意味着认为 ErrorDefault 的优先级介于 ?? 与三目之间——这与 EBNF 重合，但把后缀级的 `?!`/`!!` 也列进同一档，导致读者误以为 `risky()?! ?? fallback` 会左结合或优先级相同，实际 `?!` 是后缀、优先级远高于 `??`。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/04-expressions.md:383 — `17. Error Handling: ?!, !!, ?:`
    三个运算符在同一优先级档列出
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:308 — `PostfixSuffix ::= '?!' | '!!' | '++' | '--'`
    ?! 和 !! 是后缀级，与 ++ -- 同层
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:286 — `ErrorDefaultExpression ::= CoalesceExpression ('?:' CoalesceExpression)*; CoalesceExpression ::= LogicalORExpression ('??' LogicalORExpression)*`
    ?: ErrorDefault 位于逻辑或和 ?? 之间，属中低优先级

**潜在影响**  
程序员会在 `a!! ?? b` 这类写法的括号上浪费时间；或者为 `a ?: b ?? c` 的结合性争论不休。

**修复建议**  
把 04 章优先级表拆为三档：`2. Postfix (++, --, ?!, !!)`、`15.5 ErrorDefault (?:)` 分别列出，明确差异。
**评审备注**
- 确认方: 独立核验要点：
1) 04-expressions.md:368 的 Postfix 档只列了 ++/--，未列 ?!/!!，确有遗漏；
2) 04-expressions.md:383 的 "17. Error Handling: ?!, !!, ?:" 把后缀级运算符与中低优先级的 ErrorDefault(?:) 混成一档，确有错误；
3) 17-grammar-reference.md:307-308 的 PostfixSuffix 和 :284-290 的表达式分层完全与该总表矛盾；
4) 新增独立证据：ast/operator.h 的 OperatorPrecedence 枚举中，kPostfix(48) 比 kErrorDefault(34) 高 14 个档级，实现侧确证二者不是同一层；
5) 原问题的示例 risky()?! ?? fallback 在语义上因后缀形式不会真歧义，但 a!! * 2 等更典型情形会让读者被文档误导而加不必要的括号；
6) 档位编号顺序（16:?? 在 17:?: 前面 → ?? 优先级更高）本身与 EBNF 是一致的，编号方向没有问题，问题在内容组合。

相关文件绝对路径：
- 优先级总表（矛盾点）：/Users/bytedance/Develop/ZOM/docs/spec/chapters/04-expressions.md 第 363-386 行
- EBNF 后缀定义：/Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md 第 307-308 行
- EBNF 表达式分层（ErrorDefault/Coalesce 关系）：/Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md 第 284-290 行
- 实现侧优先级枚举（独立佐证）：/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/operator.h 第 29-51 行
- 反对方: 反驳者能提出的最有力证伪点只有一条：当前解析器根本没有实现 `?!` 和 `!!` 作为后缀运算符（parser.cc 中对 ErrorPropagate/ErrorUnwrap 零引用），词法分析器甚至都不把 `?!` 切成单独 token（只会切成 `?` + `!`），因此优先级表的错写目前 "不会对实际用户代码产生影响"。但这属于"实现缺失掩盖了文档不一致"，并不能否定文档不一致本身的存在。另外需要指出：优先级表第 2 档 Postfix 虽然漏掉了 `?!`/`!!`，但同一档同样漏掉了 `++`/`--` 以外的单 `!` NonNull 后缀（这是解析器额外实现的）——这意味着整个 Postfix 档本身就是简写不完整的，存在"优先级表是概览而非精确定义"的辩护空间。但即便按"概览"的宽松标准，把后缀级的运算符和中低优先级的 `?:` 塞进同一档，也超出了"简写"的合理边界。

### 28. 🟡 [中] 规范声明的 any / never / char / i16 等内置类型在 AST 层缺位  
**类别**: 规范-实现不一致 | **置信度**: 97%

**问题描述**  
03-types.md Special Types 中列出了 any (顶类型) 与 never (底类型)；词法层有 CharacterLiteral token；Predefined Types 表又漏掉了 i16，EBNF 又有 i16。整体呈现「规范/词法/PredefinedTypeNode 三端不一致」。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/03-types.md:63 — `- **null**: ...
- **unit**: ...
- **never**: The bottom type, for functions that never return
- **any**: The top type, can hold any value`
    spec 明确列了 any 与 never。
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/kinds.h:45 — `AnyKeyword,          // any`
    词法层有 AnyKeyword / NeverKeyword / KeyOfKeyword / BigIntKeyword 等。
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/kinds.h:89 — `NeverKeyword,        // never`
    关键字已存在。
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/type-symbol.h:117 — `static zc::Own<BuiltInTypeSymbol> createI32(SymbolId id, ...);
...
static zc::Own<BuiltInTypeSymbol> createUnit(SymbolId id, ...);`
    BuiltInTypeSymbol 仅提供 createI32/F32/Str/Bool/Unit 共 5 种，没有 createI8/I16/I64/U8/U16/U32/U64/F64/Any/Never/BigInt/Char/Null。连 spec 列出的 i8/i64 都没有工厂方法。
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/03-types.md:20 — `| i8 | ... |
| i32 | ... |
| i64 | ... |
| u8 | ... | ...`
    spec 中 Predefined Types 表跳过了 i16，但 EBNF 有 i16，AST 有 I16PredefinedTypeNode——三端不一致。

**潜在影响**  
任何用到 `any` / `never` 的代码都无法被解析或检查；i16 是否是合法类型在文档与规范之间互相打架，用户无法预测。另外 Null 类型没有对应的内置符号，T? = T|null 的可选语义无法以统一类型建模。

**修复建议**  
以 EBNF (17-grammar-reference.md:181-182) 为单一真源：(1) 对齐 03-types.md Predefined Types 表，把 i16 补全、明确 char/bigint/symbol/undefined 的保留或纳入；(2) 在 BuiltInTypeSymbol 工厂一次性补齐 16 种预定义 + any/never；(3) 新增 AnyTypeNode/NeverTypeNode 的 AST 节点与解析入口。
**评审备注**
- 确认方: ## 核验结论（确认者独立判断）

### 一、逐项核真度

| 子断言 | 判定 | 独立证据 |
|---|---|---|
| spec 列出 any/never | **真实** | 03-types.md:67-68 明确写入 Special Types，且有代码示例 `fun loopForever() -> never` |
| 词法层有 AnyKeyword / NeverKeyword | **真实** | kinds.h:45 AnyKeyword, :89 NeverKeyword；另外还有 BigIntKeyword(:51)、SymbolKeyword(:107)、UndefinedKeyword(:112) |
| CharacterLiteral token 存在 | **真实** | kinds.h:32 `CharacterLiteral,`；02-lexical-structure.md:270-274 也有字符字面量章节；但 parser 零引用（grep 全 parser.cc 无命中） |
| BuiltInTypeSymbol 工厂只有 5 种 | **真实** | type-symbol.h:117-121 仅 createI32/F32/Str/Bool/Unit；createI8/I16/I64/U8/U16/U32/U64/F64/Null/Any/Never/Char/BigInt/Symbol/Undefined 全部不存在 |
| 03-types Predefined Types 表缺 i16 | **真实** | 03-types.md:21-27 表内有 i8/i32/i64/u8/u16/u32/u64，唯独缺 i16；grep -nE "^\s*\|\s*\`i16\`" 零命中 |
| EBNF 有 i16 | **真实** | 17-grammar-reference.md:181 `PredefinedType ::= 'i8' \| 'i16' \| 'i32' \| 'i64' \| ...` |
| AST 有 I16PredefinedTypeNode | **真实** | type.h:212 `class I16TypeNode final : public PredefinedTypeNode`；还有 I8/I64/U8/U16/U32/U64/F32/F64/Bool/Str/Unit/Null 共 14 个具体子类 |
| parsePredefinedType 支持全部 14 种关键字 | **真实** | parser.cc:3620-3634 switch-case 覆盖 I8..Unit 共 14 条分支；但无 Any/Never/Char/BigInt/Symbol/Undefined 分支 |
| factory::createPredefinedType 分发全部 14 种 | **真实** | factory.cc:492-524 name 字符串分发，else 分支 ZC_UNREACHABLE——若 parser 正确拦截则到不了 else |
| Parser unittest ParseAllPredefinedTypes | 已覆盖 i8/i16/i64/u8..u64/f32/f64 共 10 种 (parser-test.cc:3457-3472)，但未覆盖 bool/str/unit/null——i16 分支已验证可解析 |

### 二、三端一致性矩阵（1=存在，0=缺失）

| 类型 | 03-types 表格 | 03-types Special | 02 词法保留字 | EBNF Predef | AST TypeNode 类 | Parser 分支 | BuiltInTypeSymbol 工厂 |
|---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| i8 | 1 | 0 | 1 | 1 | 1 | 1 | 0 |
| **i16** | **0** | 0 | 1 | 1 | 1 | 1 | 0 |
| i32 | 1 | 0 | 1 | 1 | 1 | 1 | 1 |
| i64 | 1 | 0 | 1 | 1 | 1 | 1 | 0 |
| u8..u64 | 1 | 0 | 1 | 1 | 1 | 1 | 0 |
| f32/f64 | 1 | 0 | 1 | 1 | 1 | 1 | 1/0 |
| str | 1 | 0 | 1 | 1 | 1 | 1 | 1 |
| bool | 1 | 0 | 1 | 1 | 1 | 1 | 1 |
| null | 1 | 1 | 1 | 1 | 1 | 1 | 0 |
| unit | 1 | 1 | 1 | 1 | 1 | 1 | 1 |
| **any** | 0 | **1** | 1 | **0** | **0** | **0** | 0 |
| **never** | 0 | **1** | 1 | **0** | **0** | **0** | 0 |
| **char** | 0 | 0 | 字面量章节 (02-lexical:270) | 0 | **0** | **0** | 0 |
| bigint/symbol/undefined | 0 | 0 | 1 (02-lexical:135 保留字行) | 0 | 0 | 0 | 0 |

### 三、问题真实性与严重度

**真实性：真实**。证据全部核验通过，无捏造。唯一可以商榷的描述是：原问题把 any/never 的缺位归结为"AST 层缺位"，更准确的说法应该是"spec 内部不一致 + EBNF 未纳入 + parser/AST 未实现"，因为 EBNF 作为真源也没有包含它们——也就是说这不是纯实现问题，而是 spec 自身尚未定稿。

**严重度：我下调为 `medium` 而非 `high`。** 理由：

1. **当前阶段语义检查器 (Checker) 完全是空壳**（checker.h 被注释、checker.cc 空 namespace）。BuiltInTypeSymbol 工厂即使有 14 种，实际上也还没有任何下游阶段消费这些符号。也就是说 i8/i64/null 这些"有 AST 无符号工厂"的问题，和 any/never 一样，对编译器整体功能的影响被 Checker 缺位所放大掩盖——它们处在同一层级的 gap，不存在"any/never 比 i64/null 更紧急"的差异。
2. **i16 的文档缺失只是 Predefined Types 表的一行漏写**，EBNF + AST + Parser + unittest 四端一致支持它，实际功能并不受影响。用户看到 i16 关键字写代码完全能过解析，不会产生"无法预测"的行为。这属于文档 bug，不是实现 bug。
3. **char / CharacterLiteral**：词法层有 token，02 章也有字面量章节，但 parser 根本无消费路径（不产出表达式节点，也无 char 类型节点）。这才是真正"三端不一致"最严重的一条——因为用户写 `'a'` 很可能直接报 parser error 或者落入错误表达式。这点原问题提到了，但没有突出其优先级（char 对任何写过代码的用户都是直觉的，比 any 更容易撞到）。
4. **null 没有 BuiltInTypeSymbol**：对 T? = T\|null 的可选语义确实会有长远影响，但当前 Checker 缺失意味着可选/联合类型根本还没进入语义阶段，属于下一阶段任务，不应在此用 high 标签。

### 四、调整后的建议优先级

1. **最高**：决定 char 类型的地位（纳入 PredefinedType + 新增 CharTypeNode + parser 解析 CharacterLiteral 为字面量表达式），或从 02 词法章节移除字面量说明 + 从 kinds.h 删除 token。因为这是唯一"用户写一个最常见的字符字面量 'a' 就立即撞墙"的场景。
2. **中**：补 03-types.md Predefined Types 表中 i16 这一行（1 行文档，30 秒修改）。
3. **中**：BuiltInTypeSymbol 工厂一次性补齐 i8/i16/i64/u8/u16/u32/u64/f64/null（9 种）。any/never/char/bigint 待定（见下条）。
4. **低 / 延后到语义阶段**：决定 any/never 是否进入语法。若进入：在 17-grammar-reference.md PredefinedType 规则末尾加入 `\| 'any' \| 'never'`，同步新增 AnyTypeNode/NeverTypeNode 类，以及 parsePredefinedType() 的对应分支；若不进入：从 03-types.md Special Types 移到"未来保留"段落。当前 Checker 未实现，不阻塞。

### 五、关键证据文件路径

- 03-types Spec：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/03-types.md`（第 17-76 行，表格缺 i16；Special Types 列 any/never）
- 02 词法：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/02-lexical-structure.md`（135 行保留字；270 行字符字面量）
- EBNF：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md`（181-182 行 PredefinedType，无 any/never）
- Token/关键字：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/kinds.h`（32/45/51/89/107/112 行）
- TypeNode 类：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/type.h`（152-318 行，14 个 PredefinedTypeNode 子类，无 any/never/char/bigint/symbol/undefined）
- 类型工厂：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/factory.cc`（492-524 行，14 路分发；未覆盖的 keyword 会走 ZC_UNREACHABLE）
- 解析入口：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc`（1208 行 isStartOfType 含 AnyKeyword；3610-3643 行 parsePredefinedType switch-case，Any/Never/Char/BigInt/Symbol/Undefined 全走 default 返回 none）
- 符号工厂：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/type-symbol.h`（117-121 行，5 种 BuiltInTypeSymbol）
- Unittest：`/Users/bytedance/Develop/ZOM/products/zomlang/tests/unittests/compiler/parser/parser-test.cc`（3457-3472 行 ParseAllPredefinedTypes，i16 已实际验证可解析）
- 反对方: 四条子指控逐条证伪如下：

1. any / never "AST 层缺位" — 假。
   - EBNF `PredefinedType`（17-grammar-reference.md:181-182）仅有 14 种：i8/i16/i32/i64/u8/u16/u32/u64/f32/f64/str/bool/null/unit，**不含 any/never**。
   - `parsePredefinedType()`（parser.cc:3620-3634）switch 正好 14 个 case，与 EBNF 1:1 对应，无 AnyKeyword/NeverKeyword 分支。
   - `createPredefinedType()`（factory.cc:492-524）if-else 链正好 14 种，else 走 ZC_UNREACHABLE，与 EBNF 一致。
   - AST 层 14 个具体子类（type.h:152-318）与 EBNF 1:1。
   - kinds.h 中 AnyKeyword/NeverKeyword 位于通用 KEYWORDS 段（45、89行）而非 Type keywords 段（120-142行），属于保留字，符合 02-lexical-structure.md:162-163 "reserved for future language design" 声明。
   - 因此 03-types.md Special Types 段的 any/never 文字描述是**文档超前于权威文法**，不是实现缺位。

2. char 类型缺位 — 假（伪命题）。
   - 03-types.md 全文 grep "char" 零命中；EBNF `PredefinedType` 无 'char'；kinds.h Type keywords 段无 CharKeyword。规范**从未声明 char 是预定义类型**。
   - 仅有词法层 `CharacterLiteral` token（kinds.h:32）和 EBNF 字符字面量规则（17-grammar-reference.md:64）—— 这是字面量形式的词法描述，不承诺存在独立 char 类型，类比 TypeScript：有单引号字符字面量、类型却为 string。

3. i16 "三端不一致" — 假。仅文档表格漏一行，实现端与 EBNF 完全一致。
   - 03-types.md Integer Types 表（21-27行）确实漏了 i16，但这是**笔误**：同一张表 u 系列 u8/u16/u32/u64 完整，i 系列 i8/i32/i64 跳过 i16 显然是单行遗漏。
   - 反证：EBNF 有 i16、AST 有 I16TypeNode、parser 有 I16Keyword case、factory 有 i16 分支、kinds.h Type keywords 段有 I16Keyword，五端全对齐。

4. BuiltInTypeSymbol 只有 5 种工厂 — 偷换层级，归类错误。
   - BuiltInTypeSymbol 属于语义层（symbol/目录）。当前 Checker（类型检查主阶段）**整体 0% 实现**（checker.cc 为空命名空间、driver 管线无 checkSources）。
   - Parser→AST→Binder 前端流水线对 14 个预定义类型的语法解析已完整。语义层工厂数量属"功能未实现"，非"规范-实现不符"。

综上，F03 四个子项无一成立为 high 级 spec-impl-mismatch；最具价值的发现是 03-types.md 的 i16 表格漏行（笔误，low）与 any/never 在 Special Types 段与 EBNF 的文档同步问题（low/info）。

### 29. 🟡 [中] 单 ! 后缀（NonNullExpression）在 parser 中存在但规范未声明  
**类别**: 规范-实现不一致 | **置信度**: 97%

**问题描述**  
规范 PostfixSuffix 只写了 ?! / !! / ++ / --，完全没有单个 ! 的后缀形式。但 parser 实际上实现了 expr! 的强制非空断言。

**证据**
  - docs/spec/chapters/17-grammar-reference.md:286 — `PostfixSuffix ::= '?!' | '!!' | '++' | '--'`
    EBNF 中不存在单 ! 后缀
  - products/zomlang/compiler/parser/parser.cc:3998 — `NonNullExpression produced after member expression for single !`
    parser 实现了单 ! 强制非空后缀，超前于规范
  - products/zomlang/compiler/ast/expression.h:410 — `NonNullExpression class exists`
    AST 层已定义对应节点

**潜在影响**  
同 TS 的 non-null assertion，但规范未声明，属于隐式特性蔓延；若后续打算移除则会破坏现有代码。

**修复建议**  
在规范表达式章节补充 expr! 非空断言操作符，或在 parser 禁用并报错。
**评审备注**
- 确认方: 独立验证证据：
1) **规范 EBNF（17 章第 307-308 行）**：`PostfixSuffix ::= '?!' | '!!' | '++' | '--'`，单 `!` 确实完全不存在；ANTLR 语法 `ZomParser.g4:353-359` 的 `postfixUnaryExpression` 也只允许 `ERROR_PROPAGATE | FORCE_UNWRAP | INC | DEC`，没有 NOT/single-bang。表达式章节（04-expressions.md）只介绍 `?!` 错误传播、`!!` 强制解包、`?:` 错误默认、`??` 空合并，全文无任何 "非空断言 expr!" 说明（grep 0 命中）。
2) **AST 层（expression.h:639）**：`class NonNullExpression final : public LeftHandSideExpression` 已声明，`ast-nodes.def` 已注册，`factory::createNonNullExpression` 存在。
3) **Parser 实现（parser.cc:3996-3999）**：`parseMemberExpressionRest` 中 `if (expectToken(SyntaxKind::Exclamation) && !currentToken().hasPrecedingLineBreak()) { nextToken(); expression = finishNode(createNonNullExpression(...)); continue; }` ——确实在成员访问链中消费单个 `!` Token 并构造 NonNullExpression。此外 164/169/4430/4433 行还有多处 `isa<NonNullExpression>` 的处理（可选链边界回退）。
4) **单元测试验证**：`parser-test.cc:3059-3070` 显式存在 `ParseNonNullExpression` 用例，输入 `"let x = foo!;"` 并断言解析成功。说明单 `!` 被明确设计为 parser 特性，而非偶然的错误恢复。
5) **对比反证**：规范里写了的 `!!`（ErrorUnwrap / ForceUnwrap）和 `?!`（ErrorPropagate）在 parser 里**反而未作为后缀接线** —— `parseUpdateExpression`（第 2821 行）只处理 `++/--`，未分支 ErrorUnwrap/ErrorPropagate Token。词法层（lexer.cc:418-420）已能把 `!!` 识别成独立 `ErrorUnwrap` Token（第 420 行 `formToken(SyntaxKind::ErrorUnwrap)`），但 `?!` 连 lexer 都没识别（case '?' 里仅分支 `?.`、`??`、`??=`，没有 `?!`，第 665-679 行）。所以实际上 **"spec 写了的 !!/?!" 两个都不完整，而 "spec 没写的 !" 反而落地了** ——这进一步坐实了 "特性蔓延 + 规范实现错位"。

严重度复核：维持 **medium**。理由：
- 单 ! 是 TypeScript 熟悉的非空断言，用户极易意外依赖，即使 checker 没做任何检查，AST 已接受意味着后续任何人基于 AST 做代码生成/重构都会被动兼容，形成隐性契约。
- 已有 unittest 锁定行为，不是废弃路径，后续移除成本非零。
- 但当前 Checker 为空，未到用户侧产生语义差异的阶段，尚不构成 high。

建议：与原报告一致——要么在 04-expressions.md 与 17 章 EBNF 中补充 `PostfixSuffix ::= ... | '!'` 并说明 "强制非空断言" 语义（与 `!!` 的关系：`expr!` 为类型层断言，`expr!!` 为运行时强制解包 panic）；要么在 parser 3996 行改为发射诊断并移除 unittest。同时应补齐 `!!` / `?!` 的 postfix 接线实现，避免反向错位。

关键文件路径：
- 规范 EBNF：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:307-310`
- 表达式章节：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/04-expressions.md:254-266`
- ANTLR 语法：`/Users/bytedance/Develop/ZOM/docs/spec/ZomParser.g4:352-359`
- NonNullExpression 类：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/expression.h:639`
- Parser 实现：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc:3996-3999`
- Lexer 的 !! 识别（未被 parser 使用）：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/lexer/lexer.cc:418-420`
- Lexer 的 ?! 缺失（确认未识别）：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/lexer/lexer.cc:665-679`
- ParseNonNullExpression 用例：`/Users/bytedance/Develop/ZOM/products/zomlang/tests/unittests/compiler/parser/parser-test.cc:3059-3070`
- Kinds 中 ErrorPropagate / ErrorUnwrap 已声明：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/kinds.h:221-222`
- 反对方: 已穷尽反驳角度但均失败，列举如下：

1. "也许规范的 EBNF 17 章其他位置或 ANTLR 文法中声明了单 ! 后缀" — 反证失败：
   - EBNF 第308行 PostfixSuffix ::= '?!' | '!!' | '++' | '--' 明确只有四项
   - EBNF 第320-324行 MemberExpression 只含 []、.、new、SuperProperty
   - ANTLR ZomParser.g4 第352-359行 postfixUnaryExpression 只有 ERROR_PROPAGATE / FORCE_UNWRAP / INC / DEC
   - ANTLR ZomParser.g4 第293-299行 memberExpression 只有 LBRACK expression RBRACK / PERIOD identifier
   - 以上四处均无单 ! 后缀条目

2. "也许 04-expressions.md 或 11-error-handling.md 的文字描述中包含单 !" — 反证失败：
   - docs/spec/ 全文 grep "nonNull"、"NonNull"、"强制非空"、"assertNonNull"、"单 !" 均无命中
   - 04-expressions.md:260 唯一提到的是 `optionalValue!!`（双感叹号 Force unwrap），未提及单感叹号

3. "也许单 ! 只是双感叹号 !! 的实现中间产物/分解步骤" — 反证失败：
   - Lexer.cc 第418-423行明确把 `!!` 合并为一个 ErrorUnwrap token（非两个 Exclamation）
   - Parser 处理单 ! 时匹配的是 SyntaxKind::Exclamation token，而不是 ErrorUnwrap
   - `foo!!` 在当前实现下不会产生两层 NonNullExpression，路径完全独立
   - unittest `ParseNonNullExpression` 显式测试 `let x = foo!;`（只含一个感叹号）

4. "也许这只是解析器的内部表示/错误恢复伪影/TypeScript 语法残留，并非设计意图" — 反证失败：
   - 代码位于 parseMemberExpressionRest 主循环中（parser.cc:3996-4000），是主动解析分支而非错误恢复
   - 有 `!hasPrecedingLineBreak()` 保护，遵循 ASI 规则，设计明显经过考虑
   - 配套有 `findOptionalChainBoundary` / `markNonNullOptionalChain` 等辅助函数与可选链交互逻辑

5. "也许 SPEC.md 或 design/adt.md 等其他文档中有补充说明" — 反证失败：全文档检索无果。

**额外发现（与该问题方向相反的不匹配，加剧整体不一致性）：**
- Spec 声明的 `?!`（ErrorPropagate）PostfixSuffix：Lexer.cc 中未切分为独立 token，Parser 中也无消费路径；error-handling-operators.zom 第3行 `risky()?!;` 实际报 ZOM2011/ZOM2025（被误当作三元 `?:` 开头），证实未实现
- Spec 声明的 `!!`（ErrorUnwrap / ForceUnwrap）PostfixSuffix：Lexer.cc 已切分为 ErrorUnwrap token，但 Parser.cc 全文无 `ErrorUnwrap` 引用，无消费路径

### 30. 🟡 [中] 逗号表达式存在但无说明，与语句分号分隔符语义不清  
**类别**: 规范-实现不一致 | **置信度**: 96%

**问题描述**  
EBNF 中 Expression 顶层支持逗号分隔：`Expression ::= AssignmentExpression (',' AssignmentExpression)*`，即 `a, b, c` 是合法表达式（从左到右求值，取最后值）。但 spec 正文 04-expressions.md 完全未提及逗号运算符。同时 `for (let i = 0, j = 10; i < j; ++i, --j)` 中 for-init 和 for-update 也使用逗号，两种用法的边界无明确说明。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:276 — `Expression ::= AssignmentExpression (',' AssignmentExpression)*`
    顶层逗号表达式正式存在于语法中
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/04-expressions.md:5 — `1. Primary Expressions 2. Postfix Expressions 3. Prefix Expressions 4. Binary Expressions 5. Conditional Expressions 6. Assignment Expressions 7. Function Expressions`
    7 大类别中无 Comma Expression；整个 04-expressions.md 也没有任何逗号运算符的例子或说明

**潜在影响**  
用户可能偶然写出 `let x = (a(), b(), c());` 并获得 c() 的值而不理解语义；for 循环内的逗号与表达式级逗号容易混淆。

**修复建议**  
在 04-expressions.md 末尾新增 'Comma Operator' 子节，明确：(1) 左结合、最低优先级；(2) 按顺序求值每个子表达式，返回最后一个的值与类型；(3) 说明 for-init/for-update 中逗号与逗号表达式的关系（推荐：for-update 用 ExpressionList 而非直接复用逗号表达式以避免歧义）。
**评审备注**
- 确认方: ## 独立核验结论：问题完全真实

### 证据 A：EBNF 正式存在逗号表达式
`docs/spec/chapters/17-grammar-reference.md:276` 明确定义
```
Expression ::= AssignmentExpression (',' AssignmentExpression)*
```
这是逗号表达式在语法顶层的合法入口，和 JS/C 一致。

### 证据 B：Parser 已完整实现逗号表达式
`parser.cc:2415-2434` parseExpression() 显式处理逗号：
- 注释写明："Parses a comma-separated list of assignment expressions"
- 以 `createBinaryExpression(left, CommaTokenNode, right)` 构建左结合 AST
- unittest `ParserTest.ParseCommaExpression` 和 `ParseCommaExpressionInVariableDeclaration`（`parser-test.cc:2398-2423`）已验证 `a, b, c;` 和 `let x = (a, b, c);` 均能解析

### 证据 C：04-expressions.md 完全没有逗号运算符的位置
1. 七大类清单（`04-expressions.md:5-13`）：Primary/Postfix/Prefix/Binary/Conditional/Assignment/Function，**无 Comma**
2. Operator Precedence 表（`04-expressions.md:363-385`）共 19 级，从 Primary 到 Assignment，**完全未出现 `,`**（实际应为最低的第 0/20 级）
3. `grep -ni comma` 在 04-expressions.md 和 05-statements.md 全文 **零命中**

### 证据 D：Operator 层面的规范/实现不一致细节
- `ast/operator.h:31` 定义了 `kComma` 优先级值（位于 kAssignment 之下，即最低）
- 但 `parser.cc:53-96 getBinaryOperatorPrecedence()` 的 switch 中**没有 `Comma` 的 case**，实际逗号处理是在 parseExpression 外层专门处理的，而没有走通用二进制运算符优先级解析——这是实现分层的选择，并非 bug，但进一步说明：**逗号运算符是一种"例外的"特殊二元运算符，规范必须单独说明**
- 正因为如此，`04-expressions.md:363-385` 的"运算符优先级总表"遗漏逗号，是一个直接的规范缺陷

### 证据 E：for-init / for-update 与逗号表达式的边界确实含糊
- `05-statements.md:169` 给出示例 `for (let i = 0, j = 10; i < j; ++i, --j)`，但未解释两种逗号的差异
- 解析器实现（`parser.cc:2000-2006` / `2038-2040`）：
  - **for-init** 若以 `let/const` 开头 → 走 `parseVariableDeclarationList()`（逗号是"多变量绑定分隔符"，语义不同——每个绑定都是独立变量声明，非求值）
  - 否则 → 走 `parseExpression()`（逗号是逗号表达式，左到右求值取最后）
  - **for-update** → 一律走 `parseExpression()`（逗号是逗号表达式）
- 两种"逗号"在语法上都能被 parser 正确区分（对 for-init 中带 let/const 的情形走了完全不同的 AST 路径），但规范**未说明这一区分**，用户阅读示例时无法理解：为什么 `let i = 0, j = 10` 里的逗号和 `++i, --j` 里的逗号不是同一种语法结构。

### 综合判断
- 候选问题描述的三要素全部被独立核验确认：
  1. EBNF/Parser 支持逗号表达式 ✓
  2. 04-expressions.md 完全未提及（类别、优先级表、示例皆缺失） ✓
  3. for 循环中两种逗号的边界未说明 ✓
- 严重度维持 **medium**：不会引发错误解析（Parser 已正确实现），但属于 spec-impl-mismatch 中"正式语法存在但规范文档缺位"的用户体验缺陷，且与运算符优先级总表遗漏叠加，会造成学习混淆和后续 checker/代码生成阶段无规范可依。不升级到 high，因为当前不产生错误行为。

### 建议补充（在原建议基础上）
原建议的三点已充分。额外值得指出的是：`docs/spec/chapters/04-expressions.md:363-385` 的 Operator Precedence 表应在第 0 位（或作为 "0. Comma" 新增条目）补上 `,`，明确其"最低优先级、左结合、顺序求值取最后"。
- 反对方: 尝试证伪但无法有效推翻的关键点：
1. 原报告称 `17-grammar-reference.md:276` 有 `Expression ::= AssignmentExpression (',' AssignmentExpression)*`，经查证完全属实（EBNF 第 276 行）。而且 parser 的 `parseExpression()` 函数（parser.cc 2415-2434 行）严格按 EBNF 实现，逐字节匹配：注释注明 "expression: assignmentExpression (COMMA assignmentExpression)*"，while 循环构造 `BinaryExpression(op=Comma)` 节点，说明逗号表达式是**已落地且工作的实现**，不是文档残留或死代码。
2. 04-expressions.md 全文 grep comma 零命中，且第 5-13 行列出的 7 大表达式类别中确实没有 Comma Expression 条目，正文也无任何运算符优先级表格或示例提到逗号运算符，证据确凿。
3. for-init 与 for-update 的歧义点同样被代码证实：EBNF 第 265-266 行写的是 `ForInit ::= ... | Expression`、`ForUpdate ::= Expression`；parser 的 parseForStatement（parser.cc 第 2005、2033、2040 行）对 init 的表达式分支和 condition/update 全部调用 `parseExpression()`，也就是**直接复用顶层带逗号的 Expression 规则**。因此 `for (let i = 0, j = 10; i < j; ++i, --j)` 中 for-update 的逗号是逗号表达式（从左到右求值，取最后值），而 for-init 中 `let i=0, j=10` 的逗号则是 VariableDeclarationList 的列表分隔符（完全不同语义）。EBNF 和 spec 正文均未就这一歧义或边界做任何澄清或消歧说明——这不属于"未来版本尚未补完章节"（因为 EBNF+parser 已经同时存在两种逗号语义了）。
4. 测试层面更支持报告：`ParseCommaExpression` 和 `ParseCommaExpressionInVariableDeclaration`（parser-test.cc 2398-2422）两个 unittest 用显式测试用例确认了逗号表达式的存在，但 lit 测试 `language/expressions/` 下无任何 comma 相关文件，7 大类别测试文件都覆盖了但唯独逗号没有，与 spec 正文漏写的情形完全对应。

因此，该问题从语法（EBNF）、实现（parser 产出 BinaryExpression op=Comma）、测试（unittest 已验证解析成功）三端均可确认存在，但规范正文 04-expressions.md 全文未提及逗号表达式运算符，也未在运算符优先级表（04-expressions.md 363-386）中列出逗号，导致规范正文与 EBNF/实现之间存在 spec-impl-mismatch。

### 31. 🟡 [中] var 关键字在声明章节被介绍，但 EBNF 和解析器均不支持  
**类别**: 规范-实现不一致 | **置信度**: 96%

**问题描述**  
声明章节（06-declarations.md）在变量声明分类和示例里给出了 `var`（函数作用域、遗留）形式，但语法参考 EBNF 的 LetOrConst 仅列出 `'let' | 'const'`，且解析器 parseVariableStatement 在关键字识别处只接受 LetKeyword / ConstKeyword，`var` 作为保留字无法进入语法。文档宣称支持的特性在文法和实现中都不存在。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/06-declarations.md:7 — `1. Variable Declarations: let, const, var`
    章节开头宣称 var 存在
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/06-declarations.md:73 — `### var Declarations; Declare variables with function scope`
    专节介绍 var
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:117 — `LetOrConst ::= 'let' | 'const'`
    EBNF 只含 let/const
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc:2182 — `case ast::SyntaxKind::LetKeyword: case ast::SyntaxKind::ConstKeyword:`
    解析器只处理 let/const

**潜在影响**  
用户按文档写 `var x = 1;` 将触发保留字错误；若未来真要引入 var 需再设计保留字迁移策略。

**修复建议**  
要么从 06-declarations.md 中删除 `var` 章节，要么在 EBNF 和 parser 中显式加入并给出明确的语义（目前声明章节暗示与 let 不同的函数作用域语义，若无实现计划应直接移除）。
**评审备注**
- 确认方: 经独立核验，问题 F004 真实存在且比原描述更"深入"一层：不只是 EBNF 和 parser 不支持，连 lexer 词表和 AST SyntaxKind 枚举里也根本没有 VarKeyword（utils.cc 的 getKeywordKind 全文 100+ 条关键字映射中没有 "var"；kinds.h 也搜不到 VarKeyword）。这意味着 `var x = 1;` 在当前实现里会把 `var` 当作普通标识符，然后 `x = 1;` 形成非法表达式语句（而不是"保留字错误"——原证据里的表述"作为保留字无法进入语法"严格说并不准确）。证据清单：

1. 06-declarations.md:7 声明分类列出 `var`；06-declarations.md:72-85 专门给出 ### `var` Declarations 小节和 5 行示例代码，明确宣称 var 是函数作用域"遗留"形式。
2. 17-grammar-reference.md:117 LetOrConst 文法只含 `'let' | 'const'`；VariableStatement 产物是 LetOrConst + VariableDeclarationList，无 var 分支。
3. parser.cc:2181-2184 parseDeclaration 在关键字开关处只接 LetKeyword / ConstKeyword 进入 parseVariableStatement，无 VarKeyword。
4. lexer/utils.cc:166-271 getKeywordKind 没有 "var" 条目，因此 lexer 不会把 var 识别为关键字；ast/kinds.h 同样没有 VarKeyword。
5. 02-lexical-structure.md:114 的关键字表格里 `var` 与 let/const 并列——但这与 lexer 实现也是不一致的，属于"spec 内部一致、spec 与实现不一致"的同一类问题的延伸。

严重度维持 medium：对用户的直接影响是按文档写 `var x = 1;` 必然得不到预期结果（诊断表现甚至不是清晰的"保留字"错误，而会掉到表达式语句兜底，报错信息更混乱），属于规范承诺与实现两层之间的显著差异。但由于当前声明章节把 var 标注为 "legacy, prefer let"，即便移除也不影响核心语法表达力，因此未到 high。

修复建议仍与原报告一致：首选在 06-declarations.md 中删除第 7 行的 `var` 枚举与整个 72-85 小节；若确实有引入计划，则需同步补上 VarKeyword（kinds.h）、lexer 词表、EBNF 扩展（`LetOrConstOrVar ::= 'let' | 'const' | 'var'` 之类）、parser 分支，以及后续 binder/checker 层的函数作用域语义与 let/const 区分——后者工作量不小，若无明确计划应直接移除文档条目。

相关文件绝对路径：
- 规范声明章节：/Users/bytedance/Develop/ZOM/docs/spec/chapters/06-declarations.md
- 规范文法参考：/Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md（行 116-117）
- 规范关键字表：/Users/bytedance/Develop/ZOM/docs/spec/chapters/02-lexical-structure.md（行 112-118、162-163）
- 解析器入口：/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc（parseDeclaration，行 2181-2184）
- 词法关键字表：/Users/bytedance/Develop/ZOM/products/zomlang/compiler/lexer/utils.cc（getKeywordKind，行 166-272）
- SyntaxKind 枚举：/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/kinds.h（行 57-84 附近）
- 反对方: 反驳方找到的可削弱/细化原问题的证据：

1. 【原证据对实现机制有轻微误读】原证据称"`var` 作为保留字无法进入语法"。实际核验结果：
   - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/kinds.h 完全不存在 VarKeyword 枚举值（只有 LetKeyword 第84行、ConstKeyword 第57行）；
   - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/lexer/utils.cc 第 166-271 行 getKeywordKind() 关键词映射表中没有 "var" 条目，查表落入最后一行返回 SyntaxKind::Identifier；
   - 因此源码 `var x = 1;` 中的 `var` 被词法层当作普通标识符，随后通过解析器表达式语句兜底路径报错，并不是"保留字识别出来后被语法拒绝"的路径（与 `if`、`try` 等真正的保留字路径不同）。
   这只是措辞细节问题，不影响总体结论的真实性。

2. 【文法与解析器三方是一致的，并非混乱】EBNF（LetOrConst = 'let' | 'const'）、ANTLR 形式文法（ZomParser.g4:777 variableStatement: (LET | CONST) ...）、C++ 递归下降解析器（parser.cc:2182-2184 switch 只有 Let/Const 分支）三方是互相一致的。即"实现轴"是统一的，矛盾只发生在 06-declarations.md 一端。这可用于反驳方细化问题覆盖面，而不是推翻结论。

3. 【02 章保留字总则提供缓冲】02-lexical-structure.md:162-163 明确声明"Some reserved words are reserved for future language design. If a reserved word has no grammar rule in the current parser, using it as syntax is a parse error..."，这在原则上允许"词法层声明、语法层暂不接入"。但该总则并不能合理解释 06 章为何以"当前语法教学"的口吻专设小节并给出可运行示例。

### 32. 🟡 [中] 关系运算符 `is`/`in`/`instanceof` 被文档介绍但未列入 EBNF 也未在 Parser 的优先级表注册  
**类别**: 语法设计 | **置信度**: 96%

**问题描述**  
EBNF 的关系表达式文法只把 `<`/`>`/`<=`/`>=` 和 `as/as?` 列进 RelationalExpression，但 04-expressions.md 的 "Type Check Operators" 专节介绍了 `is`/`in`/`instanceof` 三个二元类型检查运算符，并给出了示例。getBinaryOperatorPrecedence 的 switch 中没有处理 IsKeyword、InKeyword、InstanceofKeyword 任何一个，返回 kLowest。因此这三个运算符目前在表达式层要么无法作为二元运算符形成表达式，要么回退成某种语句形式，总之与 04 章宣称的"关系运算符"语义严重脱节。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/04-expressions.md:282 — `let isString = value is str; let hasProperty = "length" in object; let isInstance = obj instanceof MyClass;`
    关系类型检查运算符示例
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc:53 — `ast::OperatorPrecedence getBinaryOperatorPrecedence(ast::SyntaxKind tokenKind) { switch (...)`
    运算符优先级函数入口
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc:76 — `case LessThan: ... case AsKeyword: return kRelational;`
    kRelational 只覆盖 <,>,<=,>=,as，无 is/in/instanceof
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:296 — `RelationalExpression ::= ShiftExpression ((('<' | '>' | '<=' | '>=') ShiftExpression) | ('as' ('?' | '!')? TypeExpression))*`
    EBNF 也只写了比较和 as，未含 is/in/instanceof

**潜在影响**  
关系类型检查运算符（is/in/instanceof）无法形成二元表达式；`let x = a is B;` 这类简单语句按当前实现不可解析。

**修复建议**  
在 EBNF RelationalExpression 中加入 `| ('is'|'in'|'instanceof') ShiftExpression` 并同步在 getBinaryOperatorPrecedence 为三者分配 kRelational 优先级；或者在 Parser 里把它们当作专用形式（类似 as）分别处理。
**评审备注**
- 确认方: ## 核验结论（独立验证）

问题**完全真实**，是一处确凿的"文档/文法/实现三端不一致"。

### 独立核验的核心证据

**(A) 文档（04-expressions.md 第 279-285 行）**：确实单独用 "Type Check Operators" 一节正式介绍了 `is` / `in` / `instanceof` 三个二元运算符，并给出 `value is str`、`"length" in object`、`obj instanceof MyClass` 三个表达式位置（`let` 右值）的完整示例。说明作者意图将三者作为表达式层关系运算符。

**(B) EBNF（17-grammar-reference.md 第 296-297 行）**：`RelationalExpression` 的产生式只写了 `('<' | '>' | '<=' | '>=') ShiftExpression` 以及 `'as' ('?'|'!')? TypeExpression`，完全没有 `is` / `in` / `instanceof`。文档第 04 章与文法参考第 17 章互相矛盾。

**(C) 实现（parser.cc 第 53-96 行 `getBinaryOperatorPrecedence`）**：switch 中 return `kRelational` 的 case 只有 `LessThan`/`GreaterThan`/`LessThanEquals`/`GreaterThanEquals`/`AsKeyword` 共 5 个 token；`InKeyword`/`IsKeyword`/`InstanceOfKeyword` 全部 fall-through 到 default，返回 `kLowest`。

**(D) 二元表达式解析路径（parser.cc 第 2505-2604 行 `parseBinaryExpressionOrHigher` / `parseBinaryExpressionRest`）**：通用二元分支走 `getBinaryOperatorPrecedence` → 三关键字优先级为 `kLowest`，循环条件 `newPrecedence > precedence`（当 precedence=kLowest 时 0>0 为 false）直接 break。因此 `a is B` 会被解析为 `a`，`is` 被留在 token 流；后续若在语句上下文中就会报 "Declaration or statement expected"（ZOM2049）等错误——**完全无法按文档形成二元表达式**。`a in b`、`a instanceof B` 同理。

**(E) 独立交叉验证 —— 关键字在 parser 中的其它用途（反证）**：
- `InKeyword` 只在 `parseForStatement`（2010 行）中用作 for-in 分隔，以及 `isStartOfStatement`（271、367 行）作语句边界前瞻。
- `IsKeyword` 只在 `parsePattern`（3716 行）→ `parseIsPattern`（3809 行）路径中用作"模式层"的 `is T` 前缀。
- `InstanceOfKeyword`：parser.cc 全文 0 次引用 —— 这意味着 `instanceof` token 虽然被 lexer 产出，但 parser 层**完全没有任何处理路径**，是三端中最严重的"悬空"关键字。

**(F) 测试覆盖佐证**：
- 全项目 tests/ 下 `.zom` 文件 grep `is` 作为运算符的用例**零命中**；grep `instanceof` 零命中。
- parser unittest（237 个用例）中没有用例验证 `let x = a is B;` / `a in b` / `a instanceof X;`。
- 因此该不一致长期无人察觉，符合"真实 bug 尚未被发现"的特征。

### 与其它已确认问题的关联
- `in` / `is` 两关键字同时承担语句层（for-in）/ 模式层（is pattern）角色，这是"上下文关键字"设计；当 parser 优先级表未登记时，表达式上下文的解析就退化了。修复时需注意：通用 `parseBinaryExpressionRest` 路径里的 `is` / `instanceof` 很可能右侧需要 `parseType()` 而非 `parseBinaryExpressionOrHigher()`（类似 `as` 走专用分支 2568-2593 行）；而 `in` 右侧是普通表达式，但需防止与 `for (let x in ...)` 的歧义（目前歧义是通过 statement 层先匹配 for-in 来避免的，表达式层单独引入 `in` 作为二元运算符不会影响 for-in 的上下文）。

### 严重度评估
- 下调为 **medium**。理由：
  1. 三个运算符都是"文档中声明、实现中缺失"导致**不可用**——语法功能性缺失，不是崩溃/安全问题，严重度不如 memory-safety。
  2. 但它们是 04 章**专节介绍**的特性，而非边角注释，属于"用户按文档写的合法代码无法编译"类问题，对入门体验影响较大。
  3. `instanceof` 更是 lexer 已产出 token、parser 全无处理的"完全悬空"关键字，可能在某些 token 序列上产生奇怪的诊断（而非明确的 "unsupported operator"），进一步损害可预期性。
- 综合：**medium**（介于 high 与 low 之间，偏向 high 一侧，但低于"panic/崩溃/语义错误通过编译"类）。

### 相关文件（绝对路径）
- `/Users/bytedance/Develop/ZOM/docs/spec/chapters/04-expressions.md`（第 279-285 行，文档宣称处）
- `/Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md`（第 296-297 行，EBNF 缺失处）
- `/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc`（第 53-96 行，优先级 switch；第 2568-2593 行，`as` 专用分支，可作为 `is`/`instanceof` 的修复参考模板）
- `/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/kinds.h`（第 77、79、82 行，三关键字 SyntaxKind）
- `/Users/bytedance/Develop/ZOM/products/zomlang/compiler/lexer/utils.cc`（第 203 行，`instanceof` 的 lexer 映射，证明词法层已接入）
- 反对方: 【核心证伪证据链，共 5 条】

1. 四重权威来源完全一致，只有 04 章文本偏离
   - EBNF /docs/spec/chapters/17-grammar-reference.md:296-297：RelationalExpression 仅含 `<`/`>`/`<=`/`>=`+Shift 和 `as`+Type，**不含** is/in/instanceof。
   - ANTLR /docs/spec/ZomParser.g4:401-405：relationalExpression 文法与 EBNF 逐字对齐，**不含** is/in/instanceof。
   - Parser /products/zomlang/compiler/parser/parser.cc:53-96（优先级表）+2568-2593（AsKeyword 特殊分支）：**不含**三者。
   - AST：expression.h 与 ast-nodes.def 中 grep "IsExpression|InExpression|InstanceOf" 零命中，**无对应表达式节点**。
   四者一致说明"当前语法不含表达式级 is/in/instanceof"是**有意的设计状态**，不是遗漏。

2. 02 章保留字声明提供明确豁免
   /docs/spec/chapters/02-lexical-structure.md:162-163 明文："Some reserved words are reserved for future language design. If a reserved word has no grammar rule in the current parser, using it as syntax is a parse error rather than a supported construct." is/in/instanceof 均列于 148-152 行 Operator Keywords 列表但 EBNF 无规则，完全落入本条声明覆盖的正常范围。

3. 三关键字并非整体"缺失"，仅表达式级语义未落地
   - `in`：for-in 语句已完整实现（parser.cc:2010-2025 parseForStatement 分支）。
   - `is`：模式匹配 IsPattern 已完整实现（parser.cc:3716-3813 parseIsPattern），与 07 章模式匹配文法吻合。
   - `instanceof`：词法层已正确识别为 InstanceOfKeyword，不会误解析为标识符；只是表达式级路径未接入。
   问题将"表达式级语义未实现"笼统定性为"关键字与文档严重脱节"，属于定性夸大。

4. "优先级表漏三个 case"是误导性表象
   `is` 和 `instanceof` 的右操作数是 TypeExpression（如 `value is str`、`obj instanceof MyClass`），需要类似 `as` 的专门分支（parser.cc:2568 行的 else if(AsKeyword) 分支）调用 parseType()；即便在 getBinaryOperatorPrecedence 中加上三个 case，只要没有专用分支，右操作数就会被 parseBinaryExpressionOrHigher() 当作表达式解析而产生语义错误的 AST。更根本的缺失是 AST 没有对应节点。问题聚焦于"漏写 case"回避了"整套未来特性未实现"的现实。

5. 严重度绝非 high
   - 无静默错误：用户使用三者会触发明确的 parse error（在二元表达式循环中返回 kLowest，循环中断后上一层 parser 会对未消费的关键字产生诊断），不会生成错误 AST、不会退化为错误语义的语句形式。
   - Checker 阶段本身 0%（类被注释掉），讨论"类型检查运算符"的优先级归属属于超前问题，不影响任何实际用户路径。
   - 02 章保留字声明已提前给出用户层面的预期管理。

### 33. 🟡 [中] match 判别式强制括号与表达式-语句边界可读性冲突  
**类别**: 人类工效 | **置信度**: 96%

**问题描述**  
`match` 语句的判别式必须包在括号里 `match (expr) { ... }`，同时 `for-in`、`if`、`while`、C-style `for` 也要求括号。但函数调用 `f(args)`、元组 `(a, b)`、分组表达式 `(expr)` 共享同一对括号。这本身是 C 家族语言的惯例，不算歧义——但结合 match 可作为表达式（`let r = match (x) {...}`）使用时，`let r = match (1 + 2) * 3;` 会有读者瞬间误解为 "`match (1+2)` 的结果再乘 3"（合法）还是 "`match (1+2)*3`"（括号只到 2，表达式未闭合）。规范没有说明 `match` 作为表达式时的精确边界——尤其考虑到 match 的 body 本就是 `{...}`，理论上不需要括号，这强制括号在心理模型上与调用/分组冲突。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/05-statements.md:103 — `let result = match (operation) { when "add" => a + b ... default => 0 };`
    展示了 match 作为表达式的用法且判别式带括号
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:252 — `MatchStatement ::= 'match' '(' Expression ')' MatchBlock`
    EBNF 强制括号

**潜在影响**  
不影响机器解析，但对人类可读性有显著负担；与 Swift/Rust 等现代语言省略判别式括号的趋势背道而驰。

**修复建议**  
可选：去掉判别式括号，语法改为 `match expr { ... }`，与 Swift/Rust 对齐；或保留括号但在编码规范中禁止 `match` 表达式后紧跟运算符。
**评审备注**
- 确认方: 
【独立证据】

1. **规范层面真正的问题比候选描述更深：match 作为表达式在 EBNF 中完全缺失。**
   - 候选证据指向的 EBNF `17-grammar-reference.md:252` 是 `MatchStatement ::= 'match' '(' Expression ')' MatchBlock`——这是一个 Statement 级别的规则，根本不在 Expression 产生式链中。
   - 检查 `PrimaryExpression`（345-351 行）列出的 7 个变体（this/Identifier/Literal/ArrayLiteral/ObjectLiteral/FunctionExpression/ParenthesizedExpression），`match` 不在其中。`ConditionalExpression`、`AssignmentExpression`、`UnaryExpression`、`LeftHandSideExpression` 等整条表达式层级中，MatchStatement 都没有接入点。
   - 然而 `05-statements.md:103` 的示例写的是 `let result = match (operation) { ... };`，语义上把 match 当作右值表达式使用。这就构成了 **"示例声称支持、但文法未定义、解析器未实现" 的规范漏洞**，不是一个单纯的 ergonomics 问题。

2. **解析器实现确认了"match 表达式"当前不可用。**
   - `parser.cc:812` 把 MatchKeyword 只放在 `parseStatement` 的 switch 里（另一个在 868 行 `isStartOfStatement` 探路里）。
   - `parsePrimaryExpression()`（2887-2916 行）的 switch 覆盖 15 种 token kind，**没有 MatchKeyword 分支**。
   - 整条链路 `parseAssignmentExpressionOrHigher → parseBinaryExpressionOrHigher → parseUnaryExpressionOrHigher → parseUpdateExpression → parseLeftHandSideExpressionOrHigher → parseMemberExpressionOrHigher → parsePrimaryExpression` 没有一处接入 match。
   - AST 节点定义：`ast/statement.h:604` 中 `class MatchStatement final : public Statement`，`ast-nodes.def:129` 登记为 `AST_ELEMENT_NODE(MatchStatement, Statement)`——它继承自 Statement，不是 Expression。因此即使把 MatchKeyword 接入 PrimaryExpression，返回类型也不匹配，需要新增 `MatchExpression` 节点或重构继承链。
   - 现有 lit 测试 `statements/match.zom` 中没有任何 "match 作为表达式赋值给变量" 的用例（`grep let.*match` 零命中）。规范示例 vs 实现的差距未被测试覆盖。

3. **关于候选原论题——括号可读性——的判断。**
   - 候选描述的核心主张是："`match (expr) { }` 的括号在心理模型上与调用/分组冲突"，并举例 `let r = match (1 + 2) * 3;` 产生读者歧义。这一论断在当前阶段**不成立**，原因是 `match` 目前根本无法出现在表达式上下文中——当前读者永远不会遇到 `let r = match (...) * 3;` 这样的合法代码。
   - 即便未来实现 match-as-expression，"强制括号" 的实际歧义也很弱：从 `match (expr) { body }` 的完整结构看，`{` 紧随 `)` 之后的形态本身就消解了"是函数调用/分组"的疑虑——C 家族程序员对 `if (x) {`、`while (x) {`、`switch (x) {` 已经有一致的心理模型。`* 3` 出现在 `}` 之后时，读者的 parse-tree 已清晰知道乘号的左操作数是整个 match-expression。Swift/Rust 省略括号是风格差异，不是"解决一个歧义"——因为两者都不强制 `() {}` 相邻，所以真正的语法边界其实在 `{`，不在 `(`。
   - 候选描述"与 Swift/Rust 等现代语言省略判别式括号的趋势背道而驰"成立但权重低：ZOM 的 if/while/for 全部强制括号，它选择的是 C/Java/Kotlin 风格而非 Swift/Rust 风格，match 保持一致是设计决策，不是疏漏。

4. **综合评估。**
   - 候选问题标注的类别（ergonomics）和严重度（low）都**不准确**。
   - 真实的问题是 **spec / grammar / parser 三者不一致**：
     - spec 文字示例展示了 match-as-expression（05-statements.md:103）
     - grammar EBNF 把 match 只定义为 statement（17-grammar-reference.md:252）
     - parser 和 AST 完全按 statement-only 实现
   - 这是规范层面的 **medium 级缺陷**——要么在 spec 中明确 match 仅为语句、并移除 `let result = match (...)` 示例；要么在 grammar、AST、parser 中完整接入 match-expression 表达式产生式。目前的状态对"match 能否当表达式用"给出了自相矛盾的回答。
   - 至于"判别式括号的心理冲突"，属于 subjective ergonomics，只有当 match-as-expression 被正式支持后才是一个值得讨论的风格问题；且即使那时，括号可读性本身也仍是 low 级（ZOM 已全面采用强制括号风格）。

【关键引用文件】
- 规范示例展示 match-as-expression：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/05-statements.md:103`
- EBNF 把 match 仅定义为 Statement：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:252`
- Expression 产生式全链路无 match 接入（PrimaryExpression 为证）：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:345-351`
- 解析器只在 parseStatement 识别 MatchKeyword：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc:812-813, 868`
- parsePrimaryExpression 无 MatchKeyword 分支：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc:2887-2916`
- MatchStatement 继承 Statement 而非 Expression：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/statement.h:604`，`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/ast-nodes.def:129`
- 现有 lit 测试无 match-as-expression 用例：`/Users/bytedance/Develop/ZOM/products/zomlang/tests/language/statements/match.zom`

- 反对方: 

## 核心反驳证据链（按证伪强度排序）

### 证据 1：问题前置前提完全不成立——match 在当前规范和实现中**只能是语句，不能是表达式**

这是决定性的证伪。F015 的整个论证链条建立在 "match 可作为表达式使用" 之上，但：

**(a) EBNF 语法参考（最权威的规范文件）明确只定义了 MatchStatement：**
- 位置：`docs/spec/chapters/17-grammar-reference.md:231,252`
- `Statement ::= ... | MatchStatement | ...` — match 只出现在 Statement 备选列表中
- `Expression` 的完整语法链（`PrimaryExpression → MemberExpression → LeftHandSideExpression → CallExpression → ... → AssignmentExpression → ConditionalExpression → Expression`）全部 300-394 行**完全没有 MatchExpression 或任何 match 入口**
- `PrimaryExpression ::= 'this' | Identifier | Literal | ArrayLiteral | ObjectLiteral | FunctionExpression | '(' Expression ')'` —— 没有 match

**(b) AST 节点继承关系：**
- 位置：`products/zomlang/compiler/ast/statement.h:604`
- `class MatchStatement final : public Statement` —— 只继承 Statement，不继承 Expression，**不能放在任何需要 Expression 的语法位置**

**(c) 解析器调用路径：**
- 位置：`products/zomlang/compiler/parser/parser.cc:812-813`
- `parseMatchStatement()` 仅在 `parseStatement()` 的 switch-case `MatchKeyword` 分支中被调用
- `parseExpression()` 系列 18 个函数（`parseAssignmentExpressionOrHigher`、`parseBinaryExpressionOrHigher`、`parseUnaryExpressionOrHigher`、`parseLeftHandSideExpressionOrHigher`、`parseMemberExpressionOrHigher`、`parsePrimaryExpression` 等）中**零引用** MatchKeyword 或 parseMatchStatement
- `parseInitializer()`（parser.cc:2436-2437）走 `parseAssignmentExpressionOrHigher()`，不可能进入 match 分支

**(d) 测试侧佐证：**
- `parser-test.cc` 所有 match 测试都是顶层语句，没有 `let x = match(...)` 形式
- `language/statements/match.zom` 所有测试均为语句形式
- `products/zomlang/tests/` 全目录 grep `let.*match\|=.*match(` **零命中**

**(e) 文档示例只是未来规划：**
- `05-statements.md:103` 展示的 `let result = match(operation) {...}` 是超前示例，不代表当前实现。类似情况在该项目中并不罕见（`await`/`async` 也是文档提及但未实现）。

**结论：以当前规范和实现为准，match 不能作为表达式。F015 的 "结合 match 可作为表达式使用时..." 这一前置条件不成立。**

---

### 证据 2：即便假设 match 未来成为表达式，F015 构造的 "歧义" 也不是语法歧义

F015 构造的例子：`let r = match (1 + 2) * 3;`

按规范 EBNF `MatchStatement ::= 'match' '(' Expression ')' MatchBlock`：
- `match` 之后强制 `(` → 解析 Expression → 强制 `)` → 强制 `{`（MatchBlock 以 `'{'` 开头）
- 输入串 `match (1 + 2) * 3` 在解析完 `)` 后，下一个 token 是 `*`，不是 `{`
- **这直接是语法错误**，不会产生第二种合法解析树

F015 声称的"歧义"不是文法歧义，而是心理学上的"读者瞬间困惑"。这种读者困惑在 if/while/for 中同样存在（如 `if (a + b) * c`、`while (x) + y`），但 C 家族语言 50 年工程实践从未将其视为设计缺陷。将其列为语法设计问题属于**范畴错误**。

---

### 证据 3："match body 是 {...} 所以不需要括号"是错误前提

括号用于**界定判别式表达式边界**，而非区分 body。

反例如果去掉括号：
- `match a + b { when ... }` — 判别式是 `a` 还是 `a + b`？
- `match x.f(1, 2) { when ... }` — 判别式止于何处？
- `match new Foo<T>().bar() { when ... }` — 完全无法预测停止点

括号是 C 家族控制流结构（if/while/for/for-in）的统一约定，精确界定条件/判别式表达式的文法范围。去掉括号才会引入真正的歧义。这和函数调用括号的用途虽然都用 `()`，但读者通过关键字上下文（`match`/`if`/`while` vs 标识符）快速区分，**不存在"心理模型冲突"**，这是成熟语言社区的既有惯例。

---

### 证据 4：顺带的文档不一致（非 F015 直接相关，但指出报告中的证据有瑕疵）

F015 引用的证据 2（`parser-test.cc:1147` 和 `2744` 的 match 测试）使用了**无括号形式**：
- 第 1154 行：`match value { case 1 => "one"; ... }`
- 第 2751 行：`match x { 1 => true, _ => false }`

但规范 EBNF `MatchStatement ::= 'match' '(' Expression ')' MatchBlock` 和 parser 实现（`parser.cc:2123` `consumeExpectedToken(LeftParen)`）都强制括号。这说明：
- 要么测试用例实际上**没有测试到它断言的东西**（依赖 parser 错误恢复让 `result != none` 为真，但生成的 AST 是残缺的）
- 要么报告引用了过时的测试

这进一步表明 F015 在引用证据时未核验当前实现，而是基于过时或推测性的信息构建了问题。

---

## 最终判定理由

候选问题建立在三重不成立的前提上：
1. **事实前提不成立**：match 目前不是表达式（规范、AST、parser、测试四方面一致确认）
2. **逻辑前提不成立**：即使是表达式，所构造的例子也不是语法歧义，而是读者主观困惑
3. **设计前提不成立**："body 有 `{}` 所以判别式不需要括号"忽略了括号的真实职责——界定判别式表达式边界

该问题属于推测性的、基于未来未实现功能的主观 ergonomics 担忧，不构成本项目当前真实存在的设计缺陷。


### 34. 🟡 [中] CharacterLiteral 与单引号 StringLiteral 使用相同引号 `'...'`，存在字面量层面的根本歧义  
**类别**: 语法设计 | **置信度**: 95%

**问题描述**  
词法层用完全相同的单引号 `'...'` 同时表示 StringLiteral 与 CharacterLiteral，只靠"是否为单个字符"在 AST/语义层区分；EBNF 把两者并列（StringLiteral 已包含单引号形式）。这使得 `'a'` 在解析阶段无法确定是 1 字符字符串还是字符字面量——必须回退到类型上下文才能判定；在无类型上下文的表达式中会产生歧义。更严重的是，`'\n'` 这样的转义序列"看上去是一个字符"，但 `'\\'`（反斜杠本身）又会干扰字符串终止识别。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:50 — `StringLiteral ::= '"' DoubleStringCharacter* '"' | "'" SingleStringCharacter* "'"`
    字符串允许单引号
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:64 — `CharacterLiteral ::= "'" SingleStringCharacter "'"`
    字符字面量也使用单引号，语法与单字符单引号字符串完全同形
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/02-lexical-structure.md:236 — `'Single quoted'     // Single-quoted string`
    词法章节明确把 '...' 当作字符串
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/02-lexical-structure.md:273 — `'a'         // ASCII character`
    后续又把 'a' 当作字符

**潜在影响**  
相同的字面量 `'a'` 在不同类型上下文中有两种不同解释（char vs str），破坏类型推断稳定性；重载/模式匹配的 dispatch 结果不可预测。

**修复建议**  
把字符字面量改成独立的引号形式（如反引号 `` `c` `` 或 Rust 风格的 `'c'`+后缀限制），与字符串彻底分离；或者取消 CharacterLiteral，只通过 str 的长度 1 表示字符（Swift 做法）。
**评审备注**
- 确认方: ## 独立证据

### 规范层确证（候选问题主张属实）

- **EBNF 并列同形**：`docs/spec/chapters/17-grammar-reference.md:50` `StringLiteral ::= ... | "'" SingleStringCharacter* "'"` 与 `:64` `CharacterLiteral ::= "'" SingleStringCharacter "'"` 显然同形——前者的 Kleene 星当星内计数为 1 时与后者 1:1 重合。EBNF 中没有提供词法消歧说明。
- **词法章自相矛盾**：`02-lexical-structure.md:236` 把 `'Single quoted'` 当作字符串，`:273` 又把 `'a'` 当作字符；两段相邻正文（230-266 vs 268-278）没有给出区分原则。
- **Predefined Types 缺少 char**：`docs/spec/chapters/03-types.md:18-76` 列出的预定义类型里没有 `char`（或 `Character`、`rune`）；词汇章却声称 `'a'` 是 "character literal"，规范内部不一致。

### 实现层证据（通过规避来"化解"而非真正解决）

- `lexer.cc:424-428`：`'"'` 和 `'\''` 走同一 `lexString()` 分支，结果都是 `SyntaxKind::StringLiteral`；`SyntaxKind::CharacterLiteral`（kinds.h:32）是**悬空枚举项**，全 compiler 目录 grep 只在 kinds.h 本身命中一次。
- `ast/expression.h` 只有 StringLiteral/IntegerLiteral/FloatLiteral/BigIntLiteral/BooleanLiteral/NullLiteral，**没有 CharacterLiteral 表达式类**。
- `ast/type.h` 的 PredefinedTypeNode 子类含 Bool/Str/Unit/Null/I8/I16/I32/I64/U8/U16/U32/U64/F32/F64 共 14 种，**没有 Char 类型节点**。
- `tests/language/expressions/literals/character-literals.zom`（第 25、45、65 行）断言 `'a'` / `'\n'` / `'\u{41}'` 的 AST 节点**全部是 StringLiteral**——与 spec "character literal" 章节描述冲突。

### 候选问题陈述中的修正

1. 候选问题"在解析阶段无法确定……必须回退到类型上下文"在当前实现下**不成立**：当前实现根本不区分，总是 StringLiteral，因此无需回退；这是规范的缺陷而非当前实现的缺陷。
2. `'\n'` 与 `'\\'` 的转义终止干扰问题在 EBNF 层是真实的（EBNF 没有长度约束），但在 ANTLR CHAR_LITERAL 规则（SQUOTE … SQUOTE，括号内严格一个 content/escape）下已被词法化排除，不是实际 bug。

## 严重性调整依据

- 候选主张 **high**。
- 我下调为 **medium**：
  - 规范层确是硬伤（设计未定稿），但**实现和测试都已按"单引号即字符串"统一落地**，没有实际的二义性调用或诊断缺失；
  - 一旦未来真正引入 `char` 类型 + 独立字符字面量，再改动字面量引号形态会是一次破坏兼容性的语法修订；
  - 但就当前版本而言，它不影响编译通过与否，也不会导致 sanitizer 报错。
- 若站在"规范必须可实现"角度，也可接受 **high**：未来要落地 `char` 字面量时，必须先改规范，工作量涉及 spec 三章 + lexer + parser + 所有字面量测试。

## 建议（独立于候选问题）

优先选候选给出的第二条路径——**取消 CharacterLiteral，统一把单字符视为 `str`，用标准库函数/成员访问获取字符**（Swift 做法）。理由：
1. 与**当前实现和测试完全吻合**，迁移成本为 0；
2. 单字符本身在运行时尺寸收益（~4 字节 unicode scalars vs 堆字符串）可通过常量折叠 + `str` 长度 1 的值语义优化补齐；
3. 避免引入新的引号（反引号已被 template literal 占用）。

若坚持保留 `char` 类型作为一等公民，则应使用**后缀式**消歧：保留 `'...'` 全部为字符串，单独引入如 `'a'c`（Rust 后缀风格但用不同后缀区分 char 与字节），并在 EBNF 中彻底把 `'...'` 从 CharacterLiteral 中移除。

**参考文件（绝对路径）**
- 规范：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/02-lexical-structure.md`（230-278 行）
- EBNF：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md`（50-64 行）
- 词法实现：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/lexer/lexer.cc`（424-428 行）
- 解析器字面量分支：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc`（2921-2977 行）
- SyntaxKind 声明：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/kinds.h`（第 32 行，悬空 CharacterLiteral）
- 字面量表达式类：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/expression.h`（347-460 行，无 CharacterLiteral）
- 预定义类型类：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/type.h`（152-318 行，无 CharTypeNode）
- character-literals 测试：`/Users/bytedance/Develop/ZOM/products/zomlang/tests/language/expressions/literals/character-literals.zom`
- ANTLR 文法：`/Users/bytedance/Develop/ZOM/docs/spec/ZomLexer.g4`（261-270 行）、`/Users/bytedance/Develop/ZOM/docs/spec/ZomParser.g4`（171-176 行，characterLiteral 死规则）
- 反对方: 反驳证据（按强度排序）：

1. **类型系统无 char 类型 — 釜底抽薪级反驳**：
   - `docs/spec/chapters/03-types.md` 的 Predefined Types 表只列出 i8/i16/i32/i64/u8/u16/u32/u64/f32/f64/bool/str/null/unit/never/any，没有 `char` 关键字，也没有字符类型条目。
   - `products/zomlang/compiler/ast/kinds.h` 的 Type keywords 段（第 121-141 行）无 `CharKeyword`。
   - 没有字符类型，CharacterLiteral 在语义层就没有落脚点。连"字符字面量应该是什么类型"这个问题都不存在，因为类型系统根本没有 char。

2. **词法层实际只产出一种 token — 零歧义**：
   - `lexer.cc` 第 424-428 行：`case '"': case '\'':` 统一走 `lexString()`，然后 `formToken(ast::SyntaxKind::StringLiteral, str)`。单引号和双引号完全共用一条扫描路径，产出相同的 token 类型。
   - 全代码库 `SyntaxKind::CharacterLiteral` 在 lexer/parser/factory 三个关键文件中出现次数为 **0**（grep 验证）。词法器从未产出过 CharacterLiteral token。

3. **解析层统一构造成 StringLiteral — 无任何分支判断**：
   - `parser.cc` 第 2891、2939、2941 行：字面量解析只有 `SyntaxKind::StringLiteral` 分支，全部用 `createStringLiteral(value)` 构造 AST。
   - `ast-nodes.def` 只有 `AST_ELEMENT_NODE(StringLiteral, LiteralExpression)`，**没有 CharacterLiteral 的 AST 节点声明**。`ast/factory.h` 中无 `createCharacterLiteral`。
   - 解析阶段根本没有"判断是字符还是字符串"的逻辑，因为 token 类型已经确定是 StringLiteral。

4. **测试文件反向证实 — 文件名是 character-literals，但断言全是 StringLiteral**：
   - `tests/language/expressions/literals/character-literals.zom` 中 `let a = 'a';` 断言结果是 `"node": "StringLiteral"`, `"value": "a"`，`let b = '\n';` 和 `let c = '\u{41}';` 也是 StringLiteral。
   - 该测试是"Character Literals 在当前实现下就是字符串"这一设计的显式确认，而非遗漏。

5. **转义序列终止判断逻辑标准且正确**：
   - `lexString()` 在第 1184 行用 `quoteChar = ch()` 捕获开启用的引号字符，第 1197 行只在遇到未转义的匹配引号时终止。
   - 反斜杠分支（1202-1208 行）调用 `lexEscapeSequence()` 完整消费"反斜杠+后续字符/序列"对，然后更新 `start` 继续。`'\\'`（反斜杠本身）完全不会干扰终止识别，这是所有主流语言扫描器的标准实现。

6. **`CharacterLiteral` SyntaxKind 是死代码**：
   - 仅在 `kinds.h` 第 32 行声明为 token，无任何生产或消费路径。属于早期设计迭代中残留的占位符，不能作为"存在歧义"的证据。

7. **"必须回退到类型上下文"这一论断在实现中完全不成立**：
   - 声明中关于"`'a'` 在解析阶段无法确定是 1 字符字符串还是字符字面量——必须回退到类型上下文"的描述完全是对 EBNF 纸面推演的结果。实现中解析器从不、也无需回退到任何类型上下文，因为 lexer 已经给出唯一确定的 token 类型。

### 35. 🟡 [中] 规范的内存安全保证（界检查、null 安全、use-after-cleanup）均未实现  
**类别**: 安全性 | **置信度**: 95%

**问题描述**  
规范 14 章列出的编译期界检查、null 安全（未 unwrap 的可选值不得访问成员）、use-after-cleanup 检查在 checker、binder、任何 pass 中都找不到对应代码。

**证据**
  - docs/spec/chapters/14-memory-management.md:95 — `Compile-time bounds checking listed as guarantee`
    规范声明编译期界检查
  - docs/spec/chapters/14-memory-management.md:110 — `Null safety listed as compile-time guarantee`
    规范声明 null 安全
  - products/zomlang/compiler/: — `No bounds-check / null-safety / use-after-cleanup pass found`
    编译器中找不到相关实现

**潜在影响**  
Safety First 设计目标目前完全不可验证，所有内存安全保证仅停留在文档层面。

**修复建议**  
在 TypeChecker 中优先实现 null 安全（可选值成员访问前必须 unwrap），界检查留到 IR 阶段插入运行时检查并结合常量传播消除。
**评审备注**
- 确认方: 
**确认真实性：是真实问题**，有充足独立证据支持三类保证均未实现：

**(1) 规范声明层面（主张侧证据）**
- `docs/spec/chapters/14-memory-management.md:115-135` 明确以 `### Memory Safety` 为题，用代码示例 + 注释给出三项编译期保证：
  - 第 123 行 `array[10]` 标注 `// Compile error: index out of bounds`
  - 第 127 行 `optional.length` 标注 `// Compile error: optional not unwrapped`
  - 第 133 行 `resource.use()` 标注 `// Compile error: use after cleanup`
- `docs/spec/chapters/01-introduction.md:7` 将 `Safety First: ... memory safety guarantees` 列为第一条设计原则；第 16 行宣称 `Memory safety without garbage collection`。
- 因此这三项是**对外公开承诺的语言特性**，不是内部 TODO。

**(2) 实现缺失层面（独立核验证据）**
- `products/zomlang/compiler/checker/checker.h:24-36`：`TypeChecker` 类整体被注释掉，仅保留空壳。
- `products/zomlang/compiler/checker/checker.cc:26`：`namespace checker {}` 为空命名空间，零实现。
- `products/zomlang/compiler/basic/frontend.cc`：编译管道只暴露 `performParse` + `performBind`，**无 `performCheck`**。
- `products/zomlang/compiler/driver/` 全文无 `TypeChecker` / `typeCheck` / `semantic` 调用，driver 仅调度 parseSources → bindSources。
- `products/zomlang/compiler/diagnostics/diagnostics-sema.def`（37 行）：已注册的 15 条语义错误中，**没有任何一条**与 optional 未 unwrap、数组越界、use-after-cleanup 相关（连诊断 ID 都不存在）。
- 全 compiler 目录 `grep -i bounds/null.*safe/optional.*unwrap/use.*after.*cleanup/liveness` 零命中（仅命中 lexer 内部 buffer bounds、AST NodeList 的 C++ 断言 `ZC_REQUIRE`、类型参数的 upper/lower bounds —— 全部是实现端自校验，非 ZOM 语言级检查）。
- `tests/language/semantic/` 为空目录；`tests/unittests/compiler/checker/checker-test.cc` 全部为 TODO 占位，证实语义检查阶段整体未落地。

**(3) 对原结论的修正/补充**
- 原严重度 "low" 偏保守，我上调为 **medium**：这并非"未完成特性的常规缺口"——它属于**对外宣称的核心设计原则 + 文档已给出具体代码示例期望的编译行为，但实现端连类型检查骨架都没启动**。三项保证本身依赖的前置条件（TypeChecker、表达式类型推断、子类型规则、可选类型规范化 `T|null === T?`、模式匹配穷举性、deinit/liveness 分析）**全部处于 0% 状态**。
- 但标 medium 而非 high/critical 的原因：从项目提交历史（近期仍在 parser 语法层完善 do-while、strict equality、模块系统）可判断项目当前阶段明确处于"前端语法解析优先推进"期，IR/codegen 完全未启动，Safety First 作为目标而非当前承诺存在预期落差但尚可理解；且三类保证中界检查按原建议也确应在 IR 阶段落地，不是 checker 阶段能独立承担的。
- 原建议"在 TypeChecker 中优先实现 null 安全"合理但不完整：**null 安全至少还需要 `diagnostics-sema.def` 先定义 `OptionalNotUnwrapped`、`NullAssignedToNonNull` 等诊断码**，以及 `UnionTypeNode` 的规范化（`T|null` 与 `T?` 等价）。当前这些前置物都不存在。
- 原表述"任何 pass 中都找不到对应代码"成立：grep 全 compiler 零命中，parser/binder/checker/driver 四层均无相关 pass 名或逻辑。

**(4) 反证（counterEvidence）**
- 唯一的反证因素：本项目在多个方面明确标注了"未来保留"（并发、属性、throw 等，都有文字说明），但第 14 章 Memory Safety 一节**没有任何 "future / planned / v2" 之类的标注**——这反而让缺失显得更严重：文档将其作为已保证项列出而非规划项，这是规范性问题。
- 另一点细微反证：`ast/expression.h` 中已定义 `NonNullExpression`（`expr!`）和 `OptionalAs` / `ConditionalAs` 节点族（`?.` 成员访问 AST 侧已支持），表明**语法载体层已经就位，可以作为未来 null 安全检查的操作对象**；但这只是"能被检查的节点存在"，绝非"检查逻辑存在"。

**核验结论：真实问题。规范声明了三项编译期内存安全保证并给出了可编译示例，但实现端（TypeChecker 类被注释、driver 无 checker pass、诊断码未定义、测试目录为空）表明它们目前完全不可验证。建议严重度从 low 上调为 medium。**

- 反对方: 反驳 1：Use-after-cleanup 在规范层面缺少形式定义（示例中 cleanup() 是用户自定义方法，无 attribute 或关键字标识清理语义），严格说不构成规范保证。反驳 2：Null 安全和界检查理论上可融入 TypeChecker 的常规成员查找/常量折叠流程，不一定要有命名为 NullSafetyPass 或 BoundsCheckPass 的独立文件，但目前 TypeChecker 整体为空（0% 实现度），这一反驳无法落地。反驳 3：该问题是更广泛语义检查阶段未实现的子项，不应作为独立特性遗漏来计数，但这不改变实现缺失这一事实本身。

### 36. 🟡 [中] raises 子句在 spec 中接受 TypeList，但 parser 只读一个类型；同时 raises 的联合类型语义与 error 类型的定位存在张力  
**类别**: 规范-实现不一致 | **置信度**: 94%

**问题描述**  
两个问题：(1) EBNF 写 `RaisesClause ::= 'raises' TypeList`，但 parser 的 parseRaisesClause 直接返回 parseType()（单个类型）。spec 允许多错误 `raises E1 | E2`，但实现只接受一个；(2) raises 在 11-error-handling.md 中说明返回值是联合类型 `T | E1 | E2`，这意味着 error 类型本身是普通类型，与 union 结合表示可能的返回。但 `error` 关键字同时声明了第一类 error 类型（`error Name { fields }`），这使 raises 与普通 union 的边界不清：`fun f() -> T raises E1 | E2` 和 `fun f() -> T | E1 | E2` 有什么区别？如果没有区别，raises 是多余的语法糖；如果有，需要明确（如强制穷尽检查、自动 `?!` 传播等）。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:196 — `RaisesClause ::= 'raises' TypeList`
    TypeList = TypeExpression (',' TypeExpression)*。注意分隔符是逗号而非 `|`。
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/11-error-handling.md:65 — `fun readConfigFile() -> Config raises FileNotFoundError | ParseError {`
    示例使用 `|` 分隔多错误类型，与 EBNF 的逗号分隔符冲突！
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/03-types.md:9 — `Strong: No implicit conversions between incompatible types`
    强类型意味着 `T raises E` 返回值不能直接赋值给 `T` 类型的变量。这点符合预期，但 raises 与普通 union 的语义差异未明确。

**潜在影响**  
中：(1) 是 parser-spec 不一致的 bug；(2) 是语义设计的关键空缺——不明确 raises 的附加值将导致用户困惑（与 Java checked exception 类比或与 union type 混淆）。

**修复建议**  
立即修复两个问题：(1) 统一多错误分隔符——推荐使用 EBNF 中的逗号（`raises E1, E2`），在示例 11-error-handling.md:65 中修正为 `raises FileNotFoundError, ParseError`，并更新 parser 的 parseRaisesClause 支持 TypeList；(2) 在 11-error-handling.md 中明确 raises 的语义附加值：(a) `?!` 操作符只对 raises 类型的值生效，不对普通 union 生效；(b) 编译器可以在 match 时对 error cases 做特殊提示；(c) raises 类型在类型显示时会特殊渲染（`T raises E1,E2` 而非 `T | E1 | E2`）。
**评审备注**
- 确认方: ## 独立核验证据清单

### 问题 (1)：spec 内部 + spec-parser 不一致

**证据 1 — EBNF 用逗号分隔**：
- 17-grammar-reference.md:196 `RaisesClause ::= 'raises' TypeList`
- 17-grammar-reference.md:214 `TypeList ::= TypeExpression (',' TypeExpression)*`
→ EBNF 要求 `raises E1, E2`（逗号）。

**证据 2 — 11-error-handling.md 示例用 `|` 分隔**：
- 行 65：`fun readConfigFile() -> Config raises FileNotFoundError | ParseError {`
- 行 134：`fun readConfigFile() -> Config raises FileNotFoundError | ParseError {`
→ 与 EBNF 冲突。

**证据 3 — parser 实际实现**：
- parser.cc:565-577 `parseRaisesClause` 返回 `parseType()`。
- parser.cc:3136-3177 `parseType()` 顶层调用 `parseUnionTypeOrHigher()`，支持 `|` 联合。
- parser.cc:4338-4339 `parseRequiredReturnType` 中 `consumeExpectedToken(RaisesKeyword)` 后同样调 `parseType()`。
→ parser 接受 `raises E1 | E2`（解析成 UnionTypeNode 存入 errorType），**拒绝** `raises E1, E2`（逗号留在 token 流中产生后续错误）。

**证据 4 — 所有测试只用单错误，无多错误覆盖**：
- raises.zom（函数和闭包两份）仅 `raises Error`（单个 TypeReferenceNode）。
- function-types.zom、type-forms.zom 仅 `raises Err`（单个）。
- parser-test.cc 两个 ParseRaisesClause 用例：都只测 `raises ErrorType`、`raises Error`。
→ 没有覆盖 `raises E1 | E2` 也没有覆盖 `raises E1, E2`。

**证据 5 — AST 节点接受 UnionTypeNode**：
- ast/type.h:375-376 `ReturnTypeNode(type, Maybe<Own<TypeNode>> errorType)` — errorType 字段是任意 TypeNode（包含 UnionTypeNode），结构兼容。

### 问题 (2)：语义设计空缺

**证据 6 — AST 结构区分已存在（但未被语义层利用）**：
- 写法 A `fun f() -> T | E1 | E2`：parseType() 一次性读到 `T | E1 | E2` 赋给 ReturnTypeNode.type，errorType = none。
- 写法 B `fun f() -> T raises E1 | E2`：parseType() 遇到 `raises` 停下只读到 T 赋给 type，之后 parseType() 读 `E1 | E2` 成 UnionTypeNode 赋给 errorType。
→ AST 层结构可区分（见 raises.zom AST dump 的 returnType / errorType 双字段）。

**证据 7 — Checker 完全空白，语义附加值零实现**：
- checker/checker.h:24-36 TypeChecker 类整体注释掉。
- checker/checker.cc:26 空命名空间。
- driver 管线仅 parseSources() → bindSources()，无 checkSources()。
→ 无法验证 `?!`、`?:`、`!!` 对 raises 值 vs 普通 union 值的区别行为。

**证据 8 — 文档中 raises 的语义边界不明确**：
- 11-error-handling.md:3 "no implicit error control flow" — 与普通 union 的 match 处理在文档里看起来等价。
- 04-expressions.md 中 `?!`（错误传播）、`!!`（强制解包）、`?:`（错误默认）的定义没有说明仅对 raises 标注值生效，也未说明对普通 union 的行为。
- 03-types.md:178-202 Function Types 章节只有单行示例 `type SafeParser = (str) -> i32 raises ParseError;`，没有解释 raises 与普通返回联合的区别。
- 03-types.md:89-107 Union Types 章节也没有提及与 raises 的关系。

## 对原建议的修正

原建议"统一为逗号"与现有实现冲突较大。更合理的方向是：

1. **修 EBNF 而非改 parser**：将 17 章的 `RaisesClause ::= 'raises' TypeList` 改为 `RaisesClause ::= 'raises' TypeExpression`，与 parser 当前行为和 11 章示例（`|` 分隔）一致。`|` 作为多错误分隔符在语义上也更直观（错误间是"或"关系）。
2. **修复文档**：保留 11 章示例的 `|` 形式；同时将 11 章 "返回值类型实际上是联合" 的措辞修正为 "运行时值的集合等价于联合，但编译期保留错误类型元信息"。
3. **补充多错误测试**：新增 `raises A | B | C` 的 lit 测试（验证 AST 中 errorType 为 UnionTypeNode 而非扁平展开）。
4. **文档补语义附加值**：在 11 章明确 raises 与普通 union 的三点差异（`?!` 操作符绑定、match 穷举检查对 error 分支的特殊提示、类型渲染差异），后续 checker 实现时对号入座。

## 关键文件路径

- EBNF 语法：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md`（196, 214 行）
- 错误处理文档：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/11-error-handling.md`（65, 134 行）
- Parser 实现：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc`（565-577, 4316-4341, 3136-3177, 3208-3257 行）
- AST 类型节点：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/type.h`（372-389 行）
- Checker 空壳：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/checker/checker.h`（24-36 行）、`checker.cc`（26 行）
- 类型系统文档（函数类型）：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/03-types.md`（178-202 行）
- 单错误测试：`/Users/bytedance/Develop/ZOM/products/zomlang/tests/language/declarations/functions/function-definitions/raises.zom`
- 类型测试：`/Users/bytedance/Develop/ZOM/products/zomlang/tests/language/types/function-types.zom`（3 行）
- Parser unittest：`/Users/bytedance/Develop/ZOM/products/zomlang/tests/unittests/compiler/parser/parser-test.cc`（695, 3753 行附近）
- 反对方: 逐条反驳：(a) 称"parser 的 parseRaisesClause 直接返回 parseType()（单个类型）"是对 parseType() 能力的误读：parseType() 顶层即 parseUnionTypeOrHigher()（parser.cc:3137-3178），所以 `raises E1 | E2` 会完整解析为 UnionTypeNode{E1,E2}，不是单个 TypeReference。ReturnTypeNode.errorType 虽然是 Maybe<Own<TypeNode>>，但可以承载 UnionTypeNode，表示层面无损。所有测试用的单错误 `raises Error`、`raises Err` 也完全通过 lit。

(b) 11-error-handling.md 示例 `fun readConfigFile() -> Config raises FileNotFoundError | ParseError 与 17 章 EBNF `RaisesClause ::= 'raises' TypeList`（TypeList 用逗号分隔）确实存在分隔符冲突，但这是 spec 内部的 spec-spec-mismatch（11 章与 17 章互相打架），而非 spec-impl-mismatch。parser 实际上接受 `|`（与 11 章一致），真正偏离的是 17 章的 EBNF 写的是逗号 TypeList。因此应改为 `RaisesClause ::= 'raises' TypeExpression（或等价地：示例改用逗号）。

(c) 候选声称"11-error-handling.md 中说明返回值是联合类型 T | E1 | E2"——11 章全文搜索不存在这句话，属于陈述过度。11 章第 126 行只陈述了"Functions can return multiple error types by declaring them with raises"，没有直接陈述返回值等于普通 union 的断言。

(d) AST 层面 ReturnTypeNode 明确分离 type 与 errorType 两个字段，`-> T raises E1|E2` 会被表示为 ReturnType(type=T, errorType=Union{E1,E2})，而 `-> T | E1 | E2` 会被表示为 ReturnType(type=Union{T,E1,E2}, errorType=none)。结构不同，不存在"边界不清"在 AST 层；语义层 checker 还未实现，属于未来工作，而不是已声明为多余。

(e) `03-types.md 强类型一节（第9行）被拉来作证，但那是基本类型系统陈述，不构成 raises 与 union 的张力证据。

综上：候选核心证据链(a)(c)(d)(e)被证伪，只有分隔符不一致部分为真，但属于 spec 内部章节之间的冲突，且真实严重度下降。

### 37. 🟡 [中] 测试文件 error-handling-operators.zom 触发的是误报而非设计意图的错误  
**类别**: 人类工效 | **置信度**: 94%

**问题描述**  
let a = risky()?!; 本意测试 ?! 错误传播，但实际触发 Identifier expected（被误识别为三元 ?:）；let b = optional!!; 本意测试 !!，实际触发 reserved keyword（词法切分错误）。整个负面测试验证的是错误恢复而非特定语义。

**证据**
  - products/zomlang/tests/language/expressions/error-handling-operators.zom:1 — `let a = risky()?!; -- triggers Expected ':' not error-propagate`
    ?! 被误识别为三元运算符的 ? 和后续 !，产生非设计意图错误
  - products/zomlang/tests/language/expressions/error-handling-operators.zom:2 — `let b = optional!!; -- triggers Reserved keyword 'optional'`
    词法切分异常导致 optional 被当作关键字

**潜在影响**  
测试严重脆弱：任何 parser 小改动都可能改变错误码，使测试失败但不反映真正回归。

**修复建议**  
分离为多个独立文件，每个测试一个明确的语法/语义错误，并使用精确的 FileCheck 诊断匹配。
**评审备注**
- 确认方: ## 核验结论

问题 **真实**，且比原描述还多了一层 "spec 自身与实现相矛盾" 的证据链。

---

### 独立证据（按断言逐条）

**(1) `let a = risky()?!;` — 本意测 `?!` 错误传播，实际触发三元恢复错误**

- Lexer 层（`products/zomlang/compiler/lexer/lexer.cc:665-679`）处理 `?` 时只识别 `?.`、`??`、`??=` 三种复合，完全 **没有 `?! → ErrorPropagate` 分支**。因此源码中的 `?!` 始终被切为两个 token：`Question + Exclamation`（Kinds.h:197/189 有这两个单一符号 token）。
- Parser 的条件三元分支（`parser.cc:2474-2501` `parseConditionalExpressionRest`）看到 `Question` 就立即消费为三元问号，然后调 `parseAssignmentExpressionOrHigher()` 解析 whenTrue。
- 紧随其后的 `!` 走 prefix-unary 路径（`parser.cc:2660-2668` `parseSimpleUnaryExpression` 中的 Exclamation 分支 → `parseUpdateExpression`），但 operand 下一个 token 就是 `;` / EndOfStatement，于是在 `createIdentifier(false)` 里触发 **ZOM2011 "Identifier expected"**；随后三元等待 colon 未果触发 **ZOM2025 "Expected ':'"**。
- 因此诊断完全来源于 "三元问号 + 前缀 `!` 缺少 operand" 的组合，**与 "`?!` 是错误传播运算符" 的设计语义毫无关系**。真实的设计错误是：
  - Lexer 缺少 `charAt(1)=='!'` 分支以产出 `ErrorPropagate` token（kinds.h:221 已声明但 lexer 从未产出）；
  - Parser 的 `parseUpdateExpression` 与 `parseMemberExpressionRest` 均未处理 `ErrorPropagate` 后缀，即便 token 存在也不会被消费。

隔离验证：将 `risky()?!;` 单独编译，输出确实与测试文件前 3 条断言逐字一致，**没有出现任何 "error propagate" 相关诊断**。

---

**(2) `let b = optional!!;` — 本意测 `!!`，实际先触发 "Reserved keyword 'optional'"**

- `products/zomlang/compiler/lexer/utils.cc:218` 明确写死：
  ```
  if (text == "optional"_zcb) return ast::SyntaxKind::OptionalKeyword;
  ```
- Kinds.h:93 把 `OptionalKeyword` 放在 `AbstractKeyword..NullKeyword` 区间内，而 `FirstReservedWord/LastReservedWord` 恰好覆盖这一区间（`kinds.h:328-329`）。因此 `lexer::isReservedKeyword(OptionalKeyword) == true`。
- Parser 的 `createIdentifier(false)`（`parser.cc:1579-1586`）命中这一分支，发射 **ZOM2010 "Reserved keyword 'optional' cannot be used as an identifier"**，随后 `nextToken()` 跳过关键字，此时剩下 `!!`（即 `ErrorUnwrap` token）以及 `;`，错误恢复阶段再产生 Expected ',' / Variable declaration expected 等噪声。
- **反证 1**：规范文档 `docs/spec/chapters/02-lexical-structure.md` 的 §Keywords（第 108-163 行）六张关键字表共 80+ 关键字 **完全未列出 `optional`**。
- **反证 2（强）**：同一规范文件第 174 行（Null Literal 示例）写着 `let optional: i32? = null;` —— 规范本身把 `optional` 用作普通标识符，这与 lexer 将其列为保留字的实现 **直接矛盾**。
- **反证 3**：`parser.cc` 全文 0 处引用 `OptionalKeyword`，说明保留字的"未来语法"没有任何 parser 入口，按规范第 162-163 行的说明（未接入语法的保留字应为 "parse error"），这更像是一次**误加的保留字**而非有意设计。

故 `optional!!` 这一行的首个错误与 `!!` 的词法切分没有关系（`!!` 作为 `ErrorUnwrap` token 在 lexer.cc:418-421 已经正确实现），问题在于变量名先被保留字拦截。

---

**(3) "整个负面测试验证的是错误恢复而非特定语义"**

逐行分析当前 FileCheck 断言（第 7-32 行）锁死的是以下诊断：

| 断言内容 | 实际来源 | 与设计语义的关系 |
|---|---|---|
| `ZOM2011: Identifier expected` @3:18 | 三元 `?` 消费后，前缀 `!` 试图解析 operand，在 `;` 处失败 | **非设计**，属于三元误路径 |
| `ZOM2025: Expected ':'` @3:18 | 三元恢复 | **非设计**，同上 |
| `ZOM2010: Reserved keyword 'optional'` @4:9 | `optional` 被误标记保留字 | **非设计**，且与规范 02-lexical 章节示例冲突 |
| `ZOM2025: Expected ','` @4:17 | 保留字吞掉后 `!!` 处恢复噪声 | 纯恢复产物 |
| `ZOM2059: Variable declaration expected` @4:17 | 同上恢复级联 | 纯恢复产物 |

5 条断言里 **0 条**来自真正的错误处理运算符语义。

一旦修复任意以下实现缺口，这组断言都将整体失效但不反映回归：
- Lexer 补上 `?! → ErrorPropagate`；
- Parser 补上 `ErrorPropagate / ErrorUnwrap` 后缀表达式；
- 从保留字表移除 `optional`（与规范对齐）；
- 甚至仅是三元恢复策略稍作调整（例如更激进地同步到下一个语句）。

脆弱性评估：**比 low 更接近 medium**——当前该 lit 测试仍通过（`ctest` 验证 PASS），即现状暂无立即回归，但它**锁定了 bug 行为而不是正确行为**。随着 `?!` 语义真正落地（spec 04-expressions:254-266 已正式定义）和 `optional` 保留字争议解决，这个测试必然失败，届时维护者会浪费时间区分 "是真回归还是 bug 被修了"。

---

### 修正后的严重度评估

建议从 **low** 上调到 **medium**：
- 不是 "当前所有测试都仍过" 的纯 cosmetic 问题：它与规范 02 章 174 行 `let optional: i32? = null;` 示例**直接冲突**（按当前实现该示例会编不过），这已经超出 ergonomics 进入 "规范-实现不一致" 范畴。
- 测试脆弱性是真实的：错误操作符是后续 Checker 阶段必然会碰的核心语义，锁定错误路径诊断会在功能完善时产生假阴性。
- 但它还没有破坏已有正确的功能（`?!` 和 `optional` 作标识符目前都未被任何正向测试使用），所以不上升到 high。

---

### 建议（与候选建议一致，补充细节）

1. **分离成多个独立文件**，每个文件聚焦一个语义：
   - `error-propagate-operator.zom`：在 `?!` 真正实现前，若仍希望保留负面占位，应使用 `let x = noErrorProp()? ! y;`（显式插入空格）这种三元/二元歧义形式，并 FileCheck 断言明确对应到 ErrorPropagate 语义诊断而非 Identifier expected。
   - `force-unwrap-operator.zom`：把变量名改为非保留字（如 `let b = value!!;`）后，如果 `!!` 尚未在 checker 层实现则断言对应的 "unsupported" 或语义诊断；若语法已支持则改为正向 AST dump。
   - `reserved-optional-keyword.zom`：单独测试 "optional 作标识符"，并在 `optional` 从保留字表移除后改为正向测试（与规范 02-lexical §Null Literal 示例对齐）。
2. **修复两项实现缺口**：
   - lexer.cc 中 `case '?':` 补充 `charAt(1)=='!'` 分支，产出 `ErrorPropagate` token；
   - parser.cc `parseUpdateExpression` 或 `parseMemberExpressionRest` 增加 `ErrorPropagate` / `ErrorUnwrap` 后缀路径；
   - lexer/utils.cc 移除 `optional` 的保留字映射（除非它在规范中有未被我发现的语法入口），或至少把它从 `ReservedKeyword` 区间移到 context-sensitive 识别。
3. FileCheck 使用 `CHECK-DAG` / `CHECK-NEXT` 与**精确的诊断 ID**（而不仅仅是字面量文本）绑定，降低恢复噪声变动带来的脆弱性。

---

### 关键证据文件（绝对路径）

- 测试目标：`/Users/bytedance/Develop/ZOM/products/zomlang/tests/language/expressions/error-handling-operators.zom`
- Lexer 主实现（`?!` 未实现、`!!` 已实现）：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/lexer/lexer.cc` 409-423 行、665-679 行
- Lexer 误加 `optional` 保留字：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/lexer/utils.cc:218`
- 保留字判定区间：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/kinds.h:326-329`
- 三元误路径（`?` 消费导致非设计错误）：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc:2474-2501`
- Identifier 处保留字报错：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc:1579-1586`
- 规范与自身示例冲突证据：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/02-lexical-structure.md:108-163`（关键字清单无 `optional`）、同文件 `:174`（示例把 `optional` 作变量名）
- 规范中 `?!` / `!!` 的正式语义：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/04-expressions.md:254-266`
- 反对方: 1. 关于行 2 `let b = optional!!;` 原解释"词法切分错误（!! 误切导致 optional 变成关键字）"是不成立的。经过核验：
   - lexer/lexer.cc:418-420 显式把连续两个 ! 组合成 ErrorUnwrap token。
   - lexer/utils.cc:218 独立地把标识符形态的 "optional" 词匹配为 OptionalKeyword，与后续字符无关。
   - 因此 `optional!!` 的 token 流是 干净的 `[OptionalKeyword, ErrorUnwrap, Semicolon]`，不存在"切分错乱"。ZOM2010（Reserved keyword）是对 optional 关键字状态的直接报出，与 !! 无关。

2. `optional` 并非"文档未列出所以不该是关键字"——02-lexical-structure.md:162-163 明确写了"Some reserved words are reserved for future language design"，lexer 注册的 60+ 关键字中有大量（bigint/object/symbol/undefined/const/var 等）也同样未被 spec 的 keyword 分组清单显式列出，只作为 Future reserved 保留。optional 作为关键字与这批保留字处于同一状态，属一致行为。

3. 因此"第二条触发 reserved keyword 是词法切分错误"的论断属于误判。真正的问题是：即便把 optional 替换成非关键字标识符（如 value），parser.cc 的 parseUpdateExpression 仅处理 ++/-- 后缀，完全未接 ErrorUnwrap（!!），导致 value!! 也只会是 Exclamation token 的前缀路径或 ErrorUnwrap 未识别，仍然报无关错误，不会触发任何与 force-unwrap 语义相关的诊断。于是该测试行"未验证到 !! 语义"的结论仍然对，但原因是 parser 没实现，不是 lexer bug。

4. 关于行 1 `let a = risky()?!;`：断言"?! 被误识别为三元"是正确的（lexer 的 `?` 分支只处理 `?.`/`??`/`??=`，完全未前瞻 `!`；parseConditionalExpressionRest 吃掉 `?`，并把后续的 `!` 当前缀运算符，随后缺 operand 报 ZOM2011 Identifier expected，再补 ZOM2025 Expected ':'）。这一点与原证据一致。

5. AST kinds.h 同时定义了 ErrorPropagate(?!)、ErrorUnwrap(!!)、ErrorDefault(?:) 三种 token 形态，parser 还写了 ErrorDefault 的相邻识别分支；唯独 ?! / !! 没有对应解析路径。属于"AST/factory 先声明、parser 未落地"的状态（与上下文给出的 parse 对照清单一致："后缀 ?! spec 有，parser 有 token kind 但没实现"）。

6. 整份文件命名为 error-handling-operators.zom，但 FileCheck 断言里没有一个诊断与 ErrorPropagate / ForceUnwrap / Error-Handling 语义挂钩——断言的是 Identifier expected、Expected ':'、Reserved keyword、Expected ',' 等通用解析错误。因此"验证的是错误恢复而非特定语义"成立。

7. 反驳点：该文件本身作为负面测试（RUN: !）只要求"编译失败并产生这些诊断串"，从 lit 运行的角度它是稳定通过的，因此从"测试正确"角度它没有 bug；问题出在"测试意图与实际验证内容脱节"，属于 ergonomic/可维护性缺陷（重命名或改文件名会误导后续维护者），而不是编译器功能 bug，故严重度 low 是合理的。

### 38. 🟡 [中] 隐式转换规则与规范矛盾：Spec 声称 Strong/No implicit，代码硬编码 i32→f32 宽化  
**类别**: 类型系统 | **置信度**: 94%

**问题描述**  
03-types.md:10 明确写「No implicit conversions between incompatible types」，但 isAssignableFrom 第 124-127 行硬编码了 i32 可赋给 f32，且注释里写着「Built-in type compatibility rules」。更糟的是数值提升矩阵只覆盖了 1 条，其它整数类型、f32→f64 全部缺失。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/03-types.md:10 — `- **Strong**: No implicit conversions between incompatible types`
    规范声明：非兼容类型间没有隐式转换。
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/type-symbol.cc:123 — `// Built-in type compatibility rules
if (getKind() == SymbolKind::Type && other.getKind() == SymbolKind::Type) {
  // Allow numeric type widening (i32 -> f32)
  if (getName() == "f32"_zc && other.getName() == "i32"_zc) { return true; }
}`
    代码中存在显式的 i32→f32 隐式宽化，并且以「Built-in type compatibility rules」自居，说明并非临时 hack 而是设计。这和 Strong 的定义冲突 (整数与浮点显然是不同类型)。
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/type-symbol.cc:168 — `bool TypeSymbol::isNumeric() const {
  auto name = getName();
  return name == "i32"_zc || name == "f32"_zc || name == "double"_zc;
}`
    佐证：numeric 的判定也只写了 i32/f32，且错误地把 ZOM 中不存在的类型名 "double" 写进判断 (规范中是 f64，见 03-types.md:39)。这表明整个数值语义链都未系统化设计。

**潜在影响**  
要么违反 Strong 承诺，要么未来实现完整数值矩阵时要做破坏性变更。另外 "double" 这样的错误字面量说明当前实现没有被任何类型体系测试验证过。

**修复建议**  
二选一：(A) 若坚持「严格 Strong」，删掉 i32→f32 隐式宽化，所有跨类型赋值必须显式 as 转换，并在规范中补充数值转换章节；(B) 若允许「无损宽化」(安全子集)，补充完整的提升链 i8→i16→i32→i64→f32→f64、u8→u16→u32→u64→f64，并单独写一章 Numeric Promotion Rules，把 Strong 的定义修正为「No implicit narrowing conversions」。
**评审备注**
- 确认方: 证据全部核实：
- 03-types.md:10 确实写了 "Strong: No implicit conversions between incompatible types"
- type-symbol.cc:123-127 确实在 isAssignableFrom 内写了 "Built-in type compatibility rules" 并硬编码 f32 接受 i32，注释"Numeric type widening"——这与 "No implicit conversions between incompatible types" 在字面上矛盾（i32 与 f32 显然不是 compatible types）
- type-symbol.cc:170 的 isNumeric() 确实把 ZOM 中不存在的 "double" 写进判断，而规范 (03-types.md:39) 及 AST 层 (ast/type.h F64 PredefinedTypeNode) 统一使用 "f64"，这是纯 bug，并且该函数在全项目 grep 零调用——佐证数值语义链未被任何测试或阶段使用
- BuiltInTypeSymbol::create* 仅有 i32/f32/str/bool/unit 五种，与规范列的 i8/i16/i32/i64/u8..u64/f32/f64/str/bool/null/unit/never/any 相比，符号层只落地了最小集，"完整提升矩阵不存在"在当前代码中无可非议，但一旦补齐其它内置类型符号，现有 isAssignableFrom 的硬编码设计必然要么违反 Strong 承诺，要么需要破坏性重写

综上：这是真实的设计矛盾，但严重度应从 high 调整为 medium——理由是代码还只是骨架，未接入编译管线，不影响当前用户；但它确实预示了后续两个方向的破坏性决策风险：要么走严格 Strong（删除该硬编码，要求显式 as 转换），要么走无损宽化（补完整矩阵，并修改 spec Strong 定义为 "No implicit narrowing"）。两种方向都已超过单文件修复级别，属于需要 Spec-Writing Committee 明确结论的设计决策。建议在做出决策前，先将 type-symbol.cc:123-127 改为 TODO/注释掉，避免错误承诺随着后续接入 Checker 被"默认继承"。

另：isNumeric 中的 "double" 字面量是独立的拼写错误，严重度 low，可以单独修复。

相关文件：
- 规范：/Users/bytedance/Develop/ZOM/docs/spec/chapters/03-types.md:10,39
- 矛盾代码：/Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/type-symbol.cc:116-130 (isAssignableFrom), 168-171 (isNumeric), 189-217 (BuiltInTypeSymbol::create* 工厂)
- 佐证 Checker 未接入：/Users/bytedance/Develop/ZOM/products/zomlang/compiler/checker/checker.{h,cc}，/Users/bytedance/Develop/ZOM/products/zomlang/compiler/driver/driver.cc（管线中无 checkSources 调用）
- 反对方: 【核心反驳 1 — 代码阶段误判：骨架代码 ≠ 设计决策】
isAssignableFrom 所在的 TypeChecker 阶段整体未接入编译管线：checker.h 第 24-36 行 TypeChecker 类被整段注释，checker.cc 是空命名空间（第 26 行 namespace checker {} 为空），driver/driver.cc 中无任何 checkSources/typeCheck 调用。isAssignableFrom、isSubtypeOf、isNumeric 在编译器管线内零调用，仅在 type-symbol-test.cc 的单元测试里被"自测接口表面"式地部分验证。更关键的是：规范声明了 14 种预定义类型（i8/i16/i32/i64/u8/u16/u32/u64/f32/f64/bool/str/unit/null），但 BuiltInTypeSymbol 的工厂方法只实现了 5 个（createI32/F32/Str/Bool/Unit）——连 I8/I64/U 系列/F64 都还没有工厂函数。在这种完成度下，"只有一条 i32→f32 硬编码"是开发进度问题，不是设计矛盾。

【核心反驳 2 — 规范措辞误读："Strong" 不排除数值宽化】
"No implicit conversions between incompatible types" 的关键限定词是 **between incompatible types**。规范从未定义"i32 和 f32 是 incompatible types"。在 Java（JLS 5.1.2 Widening Primitive Conversion）和 C#（C# Spec 6.1.2 Implicit Numeric Conversions）等公认强类型语言中，int→float 都是标准的隐式宽化转换；"Strong typing" 在类型系统文献中没有统一精确的定义，更不是"完全零隐式转换"的同义词。如果 ZOM 最终设计将"同属 numeric 家族的宽化"视为 compatible，本条款就不构成任何矛盾。报告者跳过了"判断兼容性边界"的必要步骤，直接从措辞的朴素阅读得出矛盾结论，属于不充分的解读。

【核心反驳 3 — 自身证据矛盾：作者连测试都没写，说明这是占位示例】
在 type-symbol-test.cc 的 TypeSymbol_AssignabilityChecking 用例（第 611-635 行）中，作者写了 3 条断言：int←int（true）、int←f32（false，narrowing 不允许）、string←int（false）。但 **完全没有写 f32←i32（true）的断言**。也就是说，代码中那条 i32→f32 的规则，甚至连作者自己都没有用测试锁死其行为。这恰恰说明它是作者在搭建 API 时随手放的示例占位（演示"如何在这里扩展兼容性规则"），而非经过确认的设计决策。

【次要反驳 4 — "double" typo 无实际影响，是实现 bug 非设计矛盾】
isNumeric() 中的 `name == "double"_zc` 分支永不触发，因为 ZOM 中不存在创建名为 "double" 的类型的路径（所有 BuiltInTypeSymbol 工厂产生的是 f32 不是 double；f64 的工厂甚至还没实现）。这是一个无副作用的纯 typo，属于低优先级实现瑕疵，不应被提升为"设计与规范矛盾"的证据链的一环。

### 39. 🟡 [中] 函数子类型 (参数逆变 / 返回协变) 未实现，重载决议无依据  
**类别**: 语义与组合性 | **置信度**: 94%

**问题描述**  
FunctionTypeSymbol::isMoreSpecificThan() 返回常量 false；TypeSymbol 没有对应 Function 的子类型规则 (参数按逆变、返回按协变)。这影响高阶函数、方法重写 (override)、函数重载决议三块核心能力。

**证据**
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/type-symbol.cc:328 — `bool FunctionTypeSymbol::isMoreSpecificThan(const FunctionTypeSymbol& other) const {
  // TODO: Implement specificity comparison logic
  return false;
}`
    关键比较函数是 TODO。
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/type-symbol.h:242 — `enum class Variance { Invariant, Covariant, Contravariant };`
    虽然 TypeParameterSymbol 有独立 Variance 枚举，但函数参数/返回值的子类型规则从来没有与之协作的代码路径。
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/12-generics.md:61 — `interface Functor<T> {
    fun map<U>(transform: (T) -> U) -> Functor<U>;`
    Monad/Functor 示例依赖函数参数的子类型一致性，否则无法安全地通过 transform 调用链传递闭包。

**潜在影响**  
高阶函数传递 (f(g) 其中 g 是父类参数 → 要求可赋给需要子类参数的函数类型)、override 签名校验 (返回类型可协变、参数不可变)、同函数名多候选选择，全部会产生错误或漏检。

**修复建议**  
在 TypeSymbol::isSubtypeOf 增加 FunctionType 分支：`(P1)->R1 <: (P2)->R2 当且仅当 P2 <: P1 ∧ R1 <: R2`。重载决议用 isMoreSpecificThan 逐条比较：形参长度优先、再逐条参数逆变比较。
**评审备注**
- 确认方: ## 独立核验结果

### 一、直接核验确认的点

1. **`FunctionTypeSymbol::isMoreSpecificThan` 确实是常量 TODO 返回 false**。文件 `symbol/type-symbol.cc:328-331` 与原证据完全一致。

2. **`TypeSymbol::isSubtypeOf` 确实没有 Function 分支**。`type-symbol.cc:79-114` 仅做：
   - 指针相等 / 名称相等（名义）
   - 沿 getSupertypes() 递归
   - 当双方 SymbolKind==Class 时查 superclass+interfaces
   - 全程没有 `isFunction()` 判断、没有 `dynamic_cast<const FunctionTypeSymbol&>` 分支、没有对参数/返回类型做逆变/协变递归。
   这意味着形如 `(Cat)->Cat <: (Animal)->Animal` 这样的判断永远为 false（名称不同、supertypes 为空）。

3. **`TypeSymbol::isAssignableFrom` 同样没有 Function 分支**（`cc:116-130`），除了子类型委托外只硬编码了 `i32→f32` 一条宽化。高阶函数传参的赋值兼容性（`Consumer<Animal>` 形参赋 `Consumer<Cat>` 实参等）无规则可循。

4. **TypeParameterSymbol::Variance 存在但零调用者**。除自身 getter/setter (`type-symbol.cc:350-352`) 外，全代码库 `getVariance/setVariance` 零其它引用。Variance 没有参与任何子类型判定或类型实例化过程，只是"预留数据"。

5. **FunctionTypeSymbol 的分类接口不完整**（额外发现）：
   - getKind() 未 override → 返回继承自 TypeSymbol 的 `SymbolKind::Type`，与 BuiltInTypeSymbol 等混淆。
   - 构造函数没有设置 `SymbolFlags::Function`，因此 `TypeSymbol::isFunction()` 对函数类型实例返回 false（`isFunction()=hasFlag(SymbolFlags::Function)`，见 `cc:63`）。
   - 无 `static bool classof(zc::Maybe<const Symbol&>)` 方法，zc::cast/ dyn_cast 不支持。

6. **全项目范围内重载决议 / override 校验 / 高阶函数调用实参形参匹配，没有任何实现入口**：
   - checker 全空、driver 不调用 check。
   - `isMoreSpecificThan` 也零调用者（grep 只命中定义与声明）。
   - Scope 的 "Symbol overloads"（`scope.cc:203`）注释只引用了 `operator[]` 语法糖，不是函数重载集。

### 二、对问题真实性的判定

问题陈述 **属实**。但需要修正上下文：它不是"某块关键比较函数漏写"那么局部——而是 **Checker 阶段整体缺位 + 类型符号子类型 API 设计正确但函数类型分支未接入 + 分类/创建辅助都不完整**。原标题"函数子类型(参数逆变/返回协变)未实现，重载决议无依据"是准确的，但严重度需要结合"整段语义层没开发"这一大背景。

### 三、严重度评估（修正）

原严重度：**medium**。
我的评估：**medium**（与原评估一致，但理由不同）。

- 下调理由：该缺陷是"语义检查阶段尚未启动"的一部分。当前任何类型不兼容错误都不会被报出，函数子型缺失不会比其它子类型缺口（如 union/null/interface 规则）更糟。从"在当前已交付能力中造成故障"的角度，影响为 0。
- 维持 medium 的理由：一旦开始实现 Checker，这是优先级最高的缺口之一——Functor/Monad、override 签名校验、方法重载是设计目标中明确的能力（见 spec 12 章示例、08 章 `override` 修饰符、SymbolFlags::Override 位），它们都依赖 `(P1)->R1 <: (P2)->R2 iff P2<:P1 && R1<:R2` 这一基本规则；漏掉会导致：
  - override 返回类型协变 / 参数不变 的检查缺失；
  - 高阶函数形参绑定出错（反向也漏检）；
  - 重载决议的"更具体函数"选择恒定失败（恒 false 可能退化为歧义报 Ambiguous，或取首候选，行为未定）。
- 不升级为 high：不是当前运行即可观察到的 bug，且只是众多 Checker 缺口之一（表达式推断、泛型实例化、trait 一致性、穷举性同样 0%），升级为 high 会与其它语义缺口失去比例感。

### 四、补充建议（超原文）

除建议中提出的 `isSubtypeOf` 增加 Function 分支和 `isMoreSpecificThan` 实现外，还需同步补齐：
1. `FunctionTypeSymbol` 构造器设置 `SymbolFlags::Function`，保证 `isFunction()` 语义正确；
2. 增加 `static bool classof(zc::Maybe<const Symbol&>)` 以支持 zc::cast 安全下转；
3. 可选：在 `SymbolKind` 枚举中增加 `FunctionType`，与 BuiltIn/Class/Interface 并列，使 `getKind()` 可用于 dispatcher；
4. `isSubtypeOf` 的 Function 分支需要处理 arity 一致性、可变参数 (`isVariadic()`) 和 `raises` 子类型（联合型错误集的超集关系）。

### 关键文件清单

- `/Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/type-symbol.cc` （`isSubtypeOf` L79, `isAssignableFrom` L116, `isMoreSpecificThan` L328）
- `/Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/type-symbol.h` （`TypeSymbol` L42, `FunctionTypeSymbol` L197, `Variance` L242）
- `/Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/symbol.h` （`SymbolKind` 枚举 L49，缺少 FunctionType 条）
- `/Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/symbol-flags.h` （`Covariant/Contravariant` L133, `Function` L80, `Override` L114）
- `/Users/bytedance/Develop/ZOM/products/zomlang/compiler/checker/checker.cc` （空命名空间 L26）
- `/Users/bytedance/Develop/ZOM/docs/spec/chapters/03-types.md` （类型系统概述，函数类型节 L178）
- `/Users/bytedance/Develop/ZOM/docs/spec/chapters/12-generics.md` （Functor/Monad 示例 L61）
- 反对方: 【核心反证一】isMoreSpecificThan 没有任何调用点，函数永远不会执行。全项目 grep（含 .h/.cc）只找到声明（type-symbol.h:221）和定义（type-symbol.cc:328）两处，binder/checker/driver 中零引用。原因是 TypeChecker 类整体被注释掉（checker.h:24-36），Driver 编译管线只有 parseSources() → bindSources() 两段，根本没有 typeCheck 阶段（见 driver.cc 全文，仅有 114-141 行 parse 和 143-181 行 bind，无任何 checker 调用）。在这种语境下 "返回 false" 不是 bug 而是"预留接口 + TODO 占位"的标准工程实践，不会对当前任何功能产生影响。

【核心反证二】Variance 枚举与函数子类型是两个不同层面的独立概念，声称"没有与之协作的代码路径"属于类型论概念误读。TypeParameterSymbol::Variance（Invariant/Covariant/Contravariant）是**声明点变体（declaration-site variance）**，用于 `interface List<out T>` 这类泛型参数标注；而函数参数逆变/返回协变是**函数类型间的固有子类型规则（use-site）**，是类型论中公理化的（A'<:A, B<:B' ⟹ (A→B) <: (A'→B')），不需要任何用户标注枚举参与。二者不需要也不应该"协作"——就像 C# 里 `delegate TResult Func<in T, out TResult>(T arg)` 的声明点变体和 C# 本身的委托子类型规则是两个正交特性。把二者强行绑定并暗示缺了"协作路径"是对二者关系的误解。

【核心反证三】"影响高阶函数/方法重写/重载决议"的说法不成立——这三块的上游依赖全部不存在：(a) 高阶函数需要表达式类型推断和实参形参兼容检查，全项目 grep 找不到 inferType/checkExpression/solve/unify；(b) 方法重写需要 ClassSymbol 的超类/接口已解析写入，但 Binder 对 HeritageClause 只做 AST 遍历，并未将 extends/implements 解析结果写入 ClassSymbol::Impl::interfaces 和 superclass（从 ClassSymbol::getInterfaces 在类型系统分析中被明确"未填充"可证）；(c) 重载决议需要调用点候选构建，CallExpression 的类型检查路径在 Checker 空壳的前提下不可能存在。三块功能在当前代码库中均不可用，不可用的根因是 Checker 整体未开工，而不是 isMoreSpecificThan 这一个函数的 TODO。

【核心反证四】Functor 示例不能作为当前缺陷的证据。docs/spec/chapters/12-generics.md 是语言设计目标规范（描述完整语言形态），不是当前 v1 实现验收清单。Functor/Monad 示例同时依赖泛型约束求解、关联类型绑定、高阶类型、trait 一致性检查等十几项同样未实现的机制。选择性只挑"函数子类型缺失"而忽略同等缺失的所有上游依赖，属于摘取证据。

【辅助反证五】同一 type-symbol.cc 中同级 TODO 大量存在却未被列入：TypeSymbol::getSubtypes() 返回空数组（140 行 TODO）、isSubtypeOf() 不处理联合/交叉/可选/函数类型、isAssignableFrom() 只硬编码 i32→f32 一条规则（无完整数值提升矩阵）。如果 isMoreSpecificThan TODO 算 medium 设计问题，上述每一项也应同级。这是"对整块未开发区域逐条挑刺"模式，而非发现独立缺陷。

【辅助反证六】代码中 TODO 注释本身就是"已知待填充点"的明确声明（"// TODO: Implement specificity comparison logic"）。这不是遗漏或误实现，而是开发里程碑中的预留位，语义上等同于"此处将来填逻辑"的 Pimpl 空骨架，不应被定性为"设计问题"。

### 40. 🟡 [中] 关键字策略总体完善，但 `type` 与 `alias` 功能重叠，以及 `_` 作为通配符的标识符合法性冲突  
**类别**: 演进性 | **置信度**: 92%

**问题描述**  
关键字表分 6 组共约 80+ 个保留字，策略合理。但有两个具体问题：(1) `type` 和 `alias` 都在保留字中，EBNF 只用了 `alias Name = Type;`，`type` 关键字的未来用途不明确（可能做 type class / associated type / 类型声明），需防止两者演变为冗余或冲突机制；(2) `_` 在 Pattern 中作为 WildcardPattern，但 IdentifierStartChar 也允许 `_` 开头（`_foo` 是合法标识符）。那么裸 `_` 作为标识符是否合法？如果是合法标识符，它在 pattern 中的 wildcard 语义就会与标识符冲突。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/02-lexical-structure.md:115 — `type        module      namespace   package     constructor`
    `type` 是保留字，但 EBNF 只用了 `alias` 声明类型别名
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:163 — `AliasDeclaration ::= 'alias' BindingIdentifier TypeParameters? '=' TypeExpression ';'`
    只用 alias 做类型别名。type 关键字无语法入口。
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/02-lexical-structure.md:74 — `IdentifierStartChar ::= UnicodeIDStart | '$' | '_'`
    `_` 是合法标识符首字符，意味着 `_` 本身（单独一个下划线）也是合法标识符。
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:382 — `WildcardPattern ::= '_' TypeAnnotation?`
    pattern 中 `_` 是通配符。此时需要 parser 层面的消歧：`let _ = foo();` 是 wildcard binding（丢弃值）还是标识符命名为 `_`？

**潜在影响**  
中：`type` 关键字的用途不明确会让用户困惑（'什么时候用 type？什么时候用 alias？'）；`_` 的消歧如果处理不当会导致奇怪的编译错误或未使用变量警告错误。

**修复建议**  
(1) 明确 `type` 关键字的定位：推荐将 `type` 保留用于未来的关联类型绑定语法（如 `type Item = T;` 在 interface/extension 中），并在 06-declarations.md 中注明 'alias 用于顶层类型别名，type 用于上下文中的类型成员声明'；(2) 明确 `_` 的语义规则：在 pattern 中 `_` 永远是通配符（不引入绑定），单独使用 `_` 作为标识符时发出专门警告 'underscore identifier used outside pattern' 但不报错（兼容 ML 风格），或直接将裸 `_` 从合法标识符中排除。
**评审备注**
- 确认方: 两个子问题都真实但需要重新定性：
(1) type 关键字不是"与 alias 功能重叠"，而是"设计上预留为关联类型、但文法 EBNF (17-grammar-reference.md:137-141 InterfaceElement / ClassElement) 与解析器 parseInterfaceElement()/parseClassElement() 都遗漏了语法入口，导致接口章节示例无法编译"——这属于"规范内部自相矛盾（示例 vs 文法）+ 实现与示例脱节"，阻塞性比用户困惑更具体。建议：在 EBNF 的 InterfaceElement 和 ClassElement 中加入 type AssocType (= Type | : Constraint)? 的规则，并在 parser 中补充 AssociatedTypeDeclaration AST 节点与解析路径；同时在 06-declarations.md 或 09-interfaces.md 明确 "alias 用于顶层类型别名，type 用于 interface/class/struct 内部的关联/成员类型声明" 的划分。
(2) 裸 _ 的语义冲突真实存在：a) lexer 层不产出 SyntaxKind::Underscore，与 kinds.h 定义不一致（死代码）；b) VariableDeclaration 的绑定名路径 parseBindingIdentifier() 未排除 _，会让 let _ = x; 生成名为 "_" 的标识符绑定而非"丢弃绑定"的 WildcardPattern 语义，语义检查阶段一旦加 unused 警告就会误报；c) 规范文本未写任何消歧规则。建议：在 lexer 扫描标识符时若字面量恰好是 "_" 单独一个字符，则产生 Underscore token（同时把它加入 Reserved keyword 判定 or 在非 pattern 上下文中接受但发专门警告），从而让 parsePattern 的 case Underscore 分支可达；或在 parseBindingIdentifier 中对裸 _ 做与 parsePattern 一致的识别（对 VariableDeclaration 走 Wildcard 绑定路径）。
关键文件：
- docs/spec/chapters/02-lexical-structure.md:74 (IdentifierStartChar), 107-163 (关键字表)
- docs/spec/chapters/09-interfaces.md:97-120 (关联类型示例，type 用途)
- docs/spec/chapters/17-grammar-reference.md:133-147 (InterfaceElement/ClassElement EBNF 缺 type 语法), 163 (AliasDeclaration), 382-383 (WildcardPattern/IdentifierPattern)
- products/zomlang/compiler/lexer/utils.cc:166-272 (getKeywordKind 无 "_" 条目)
- products/zomlang/compiler/ast/kinds.h:228 (Underscore 枚举值定义)
- products/zomlang/compiler/parser/parser.cc:1636-1639 (parseBindingIdentifier 未排除 _), 3649 (Underscore case 不可达), 3651-3706 (Identifier 分支内的 _ 消歧), 4687-4714 (parseInterfaceElement 不接受 type), 4827 (scanStartOfDeclaration 预识别 TypeKeyword)
- 反对方: 子问题1反驳证据：
1. `alias` 是顶层独立声明（AliasDeclaration ::= 'alias' BindingIdentifier ...），`type` 接口/类作用域内关联类型（09-interfaces.md:101/108 明显示例 `type Iterator: Iterator<T>` 和 `type Iterator = ArrayListIterator<T>`）——作用域和用途完全不同，不存在重叠。
2. grammar-reference 的 InterfaceElement(137-139) 和 parser.parseInterfaceElement(4701-4704 仅解析 let/const/fun) 暂未接入 type 入口，这是实现进度滞后，不是设计冲突。
3. scanStartOfDeclaration 第 4827 已预留 TypeKeyword 为声明起始，说明 parser 已考虑 type 作声明入口。

子问题2反驳证据：
1. kinds.h 有独立 Underscore SyntaxKind，lexer 词法层面有区分。
2. parser.parsePattern 第 3649-3662 行做双重消歧：Underscore token → WildcardPattern；以及 value=="_" Identifier + 终止符前瞻 -> WildcardPattern。
3. `let _ = foo()` 的 `_` 在变量声明位置被解析为 Identifier，这是有意设计——AST 层用 Identifier 表示，语义阶段根据名字 "_" 特殊处理丢弃绑定。Rust/Swift/Go 等主流语言都是这个惯例。

综上，两个子问题都是对设计意图的误读，是实现进度问题或已知的已知消歧策略的不知情下不存在所谓"设计冲突"。

### 41. 🟡 [中] C/FFI 互操作是设计目标之一，但完全无规范语法位置和实现路线  
**类别**: 人类工效 | **置信度**: 92%

**问题描述**  
设计原则明确列出 'Seamless integration with existing C/C++ codebases'。但实际情况：(1) spec 中没有 extern/foreign/import C 等语法入口；(2) SymbolFlags 虽然有 Extern(57) 和 Builtin(52) 位，但 AST 层没有 ExternDeclaration 或 ForeignDeclaration 节点；(3) 保留字列表中 `declare`、`intrinsic`、`immediate` 与 FFI 相关，但未接入语法；(4) 没有 C ABI 兼容的类型映射规范（C int → i32？指针如何表示？）。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/01-introduction.md:10 — `Interoperability: Seamless integration with existing C/C++ codebases`
    核心设计目标之一
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/symbol-flags.h:148 — `Extern = 1ULL << 57,     // External linkage`
    SymbolFlags 中有 Extern 位，但没有声明语法。
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/02-lexical-structure.md:117 — `init        deinit      get         set         accessor / declare`
    declare 在关键字列表里，但 EBNF 中完全没有 DeclareDeclaration。它通常用于外部类型/函数声明（TypeScript 风格）。
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/02-lexical-structure.md:143 — `immediate   intrinsic   global      unique      out`
    intrinsic/immediate 暗示内建函数/编译器内置，但无对应语法或 ABI。

**潜在影响**  
高：作为核心设计目标，没有明确的 FFI 接口设计就无法启动生态系统集成；标准库（字符串、IO、内存）也依赖 FFI 与 OS 交互，不先定义 FFI 则实现运行时时会遇到瓶颈。

**修复建议**  
在 17 章语法中立即添加外部声明的扩展占位，并在 docs/plans/ 中新增 FFI 设计文档：(1) 语法：`@extern("c") fun printf(fmt: str, ...): i32;` 或 `declare fun malloc(size: usize): raw_pointer;`；(2) 在 AST 中新增 `ExternFunctionDeclaration` 节点；(3) 定义最小 C ABI 类型映射表：i8/u8 ↔ char, i32 ↔ int, i64 ↔ long long, f64 ↔ double, str ↔ const char*（仅在边界处）；(4) 引入 `@packed`、`@aligned(N)` 属性的保留语法（即使当前 attributes 整体未启用，也为 FFI 必备的 struct 布局保留标记）。
**评审备注**
- 确认方: 【独立核验结论：真实存在，但严重度应下调至 medium，类别应为 process/spec 而非 ergonomics】

一、原问题 4 项主张的独立逐项验证：

(1) 设计目标确实写出 "Seamless integration with existing C/C++ codebases"（`docs/spec/chapters/01-introduction.md:10`），spec 全文（17 章 + OVERVIEW + SPEC.md + design/ + plans/）中 `extern`/`foreign`/`FFI`/`cdecl`/`C ABI` 等术语 **0 次出现**。—**证实**。

(2) SymbolFlags：`Extern(1ULL << 57)`、`Builtin(1ULL << 52)` 存在于 `symbol-flags.h:143,148`，并被纳入 `MetaFlags` mask。但：
   - AST `ast-nodes.def` / `kinds.h` 中 **无** ExternDeclaration / ForeignDeclaration 节点；
   - 整个 `binder.cc`/`symbol.cc`/`value-symbol.cc`/`type-symbol.cc` **没有任何** `SymbolFlags::Extern` 设置或读取点；
   - `parser.cc` 对 `declare fun externalFunc(...)` 走的是 `ParseDeclareStatementReportsError` 报错恢复路径（parser-test.cc:3283-3295），并未构建特殊 AST。
   —**基本证实**，但注意 Builtin 已被 `BuiltInTypeSymbol::createI32/Str/...` 使用，只是用途是内建类型而非 FFI。

(3) 保留字 `declare`（Declaration Keywords）、`intrinsic`、`immediate`（Modifier Keywords）在 `02-lexical-structure.md:117,143` 列出；对应 `kinds.h:74` ImmediateKeyword、`kinds.h:81` IntrinsicKeyword、`utils.cc:198,205` 词法识别均已接入；但 `parser.cc` 中 **无** 这三者的任何语法分支——`isModifier()` 列表（parser.cc:1055-1062）只含 abstract/export/public/private/protected/static/readonly/mutating/override，**不含** immediate/intrinsic/declare。parser 唯一对 declare 的引用是在诊断建议词列表和 "Cannot be declared here" 的保护性短路（parser.cc:1794, 1870-1872），不构成语法接入。—**证实**。

(4) C ABI 类型映射：
   - `03-types.md` 的 Predefined Types 表只给 i8/i16/i32/i64/u8/u16/u32/u64/f32/f64/bool/str 等位宽，**未写** "C int 对应 i32 还是 i64（平台相关?）"、"char 对应 i8 还是 u8" 等互操作必需说明；
   - `14-memory-management.md` 中 `allocate<i32>(1000)` 示例把返回值绑到 `let ptr`，`ptr[i]` 直接下标访问——但 `ptr` 的语法类型**未说明**（ArraySlice? RawPointer? i32[]?），表明"指针类类型的名字和 ABI" 的确没有规范；
   - `pointer`/`raw_pointer`/`Ptr`/`unsafe` 既不在 AST 类型节点，也不在类型预定义里。
   —**证实**。

二、补充的独立佐证：
- 第 16 章 `06-attributes-and-annotations.md` 明确声明整个 `@` 属性体系"reserved for future"。因此 `@extern("c")` 这类 FFI 属性目前**连语法容器都不存在**，原问题建议中提到的 `@packed`/`@aligned(N)` 也是同样情况。
- `products/zomlang/runtime/` 目录**仅含空的 1 行 CMakeLists.txt**（没有任何运行时代码），佐证：与 libc / OS 交互的 FFI 层目前尚未开工，整体开发阶段还在前端语法。
- 类型系统的 Checker 阶段**整体为 0% 占位**（checker.h 被注释，checker.cc 为空命名空间，驱动管线无 checkSources 调用），意味着"没有 FFI 类型映射检查"是"没有任何类型检查"的子集，不是孤立缺失。

三、与原问题的分歧点（为什么严重度下调 + 类别调整）：
- 原严重度 **high** → 建议 **medium**。理由：
  1. 设计目标中 C/C++ 互操作排第 4 位（在 Safety / Performance / Expressiveness 之后）；
  2. 当前开发里程碑仍是"前端语法 + 绑定器"，Checker/IRGen/Backend 都未启动，FFI 属于 codegen 之后才需要的特性，当前阶段的空缺是**可预期的**；
  3. 标准库/运行时未开工（runtime 目录为空），因此"无法启动生态集成"和"标准库依赖 FFI"的**冲击尚未兑现**，属于将来必须解决但**当前还没阻塞**的问题。
  4. declare/intrinsic/immediate 与 async/await/yield/try/catch 等 ~20 个关键字同属"未来保留"，项目已在第 162-163 行显式声明。这是 FFI 语法位**与其他未来特性平等的预留**，不应单独升格。
- 原类别 **ergonomics** → 实际应归为 **spec-process / missing-roadmap**。因为问题不在"语法用户体验差"，而在"核心设计目标没有规范章节和实现计划文档占位"。

四、对原建议的调整：
- 立即在 17 章加 EBNF 扩展占位**没有必要**（与第 15/16 章"全章未来保留"的处理风格不一致）；更一致的做法是**新增 18 章或在 plans/ 下新增 `2026-MM-DD-zom-vx-ffi-roadmap.md` 与其他未来能力（并发、属性）并列处理**。
- 新增 AST 节点 / ExternFunctionDeclaration 在 **Checker 没跑起来之前没有落地价值**（所有语义阶段都是空的），优先级低于类型检查主链路。
- C ABI 最小类型映射表值得**早期决定**（i32 ↔ int 是否默认、long 平台差异、指针表示、str ↔ const char* 边界），因为它会反向约束预定义类型的位宽、值类型布局、`allocate<T>` 返回类型等**已经写入 spec 的内容**。这是唯一的"高优先级但不改严重度"项。

【关键文件绝对路径】
- 设计目标：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/01-introduction.md`（第 10 行）
- 关键字保留 + 未来保留字总说明：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/02-lexical-structure.md`（第 117、143、162-163 行）
- 属性/注解全章未来保留（FFI 属性前置条件）：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/16-attributes-and-annotations.md`
- 17 章文法总览（确认无 extern/foreign 产生式）：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md`
- Extern/Builtin 标志定义：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/symbol-flags.h`（第 143、148、204 行）
- declare 报错测试：`/Users/bytedance/Develop/ZOM/products/zomlang/tests/unittests/compiler/parser/parser-test.cc`（第 3283-3295 行）
- 修饰符白名单（不含 immediate/intrinsic/declare）：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc`（第 1055-1062 行）
- 词法接入 intrinsic/immediate：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/lexer/utils.cc`（第 198、205 行）
- 运行时目录占位：`/Users/bytedance/Develop/ZOM/products/zomlang/runtime/CMakeLists.txt`
- 反对方: 证伪/缓和的论点如下：

1. 【对"没有语法入口"的缓和】—— 候选问题原描述说"没有 extern/foreign/import C 等语法入口"，这是准确的事实陈述，但忽略了关键背景：规范文档有一段全局免责声明（`02-lexical-structure.md:162-163`）："Some reserved words are reserved for future language design. If a reserved word has no grammar rule in the current parser, using it as syntax is a parse error rather than a supported construct." 这意味着 declare/intrinsic/immediate 在当前版本中被明确视为"未来保留"而非缺陷——项目并没有假装它们已接入，而是明说它们暂未接入。同理，整个 v1 规范有大量"v1 不支持/明确排除"清单（并发、async/await、属性注解、try-catch、namespace、通配导入等），FFI 也应被归入同一类：属于 v1 之后的设计领域，而非"承诺了但没交付"的矛盾。

2. 【对"SymbolFlags Extern/Builtin 没有声明语法"的反证】—— 这是对 flags 用途的误读：
   - `Builtin = 1ULL << 52` 已被实际使用：`type-symbol.cc:191-215` 中 `BuiltInTypeSymbol::createI32/F32/Str/Bool/Unit` 都会设置 Builtin 位，`value-symbol.h:223-224` 中 `FunctionSymbol::isBuiltin()` 会查这个位。它的语义是"编译器内置的符号"（prelude、运行时内置），而非"FFI 的 C 函数"。把 Builtin 解读为 FFI 证据属于上下文误读。
   - `Extern = 1ULL << 57` 被归入 `MetaFlags`（`symbol-flags.h:204`：`MetaFlags = Macro | Template | Implicit | Explicit | Deferred | Forward | Extern | Export`）。与其并列的 `Deferred`、`Forward`、`Export` 都没有"语法声明入口"——它们是**链接器/代码生成阶段的元信息位**，不需要一一对应语法节点。特别是 `Export`（1ULL<<58）完全没有"export 函数对应设置 Export 位"的代码路径，但没人会因此提出"Export 没有声明语法"的问题。因为当前整个代码生成/链接阶段都不存在，Extern 位留待后端填入是完全合理的前置建模，不是"悬空未用的矛盾"。

3. 【对"declare/intrinsic/immediate 与 FFI 相关"的反证】—— 这是最核心的误读：
   - `intrinsic` / `immediate` 出现在 `02-lexical-structure.md:143` 的 **Modifier Keywords** 组，与 `global`、`unique`、`out` 并列。这组单词对应的语义更可能是"内建函数（编译器 magic）/ 编译期立即求值 / 全局变量 / 唯一性类型 / 输出参数"等语言内建的语义修饰，而不是"与 C 语言互操作"。
   - `declare` 出现在 Declaration Keywords 组（与 class/struct/fun/let 同组），对应 TypeScript 风格的"声明一个存在于别处的类型/函数"，这的确与 FFI 有关，但 parser-test.cc `ParseDeclareStatementReportsError` 已明确用例为"declare fun externalFunc(x: i32) -> str;"——parser 团队已经知道该语法是用户可能会尝试的形式，**故意让它报错**，而非漏接。这属于"有意推迟、明确不支持"的设计决策，可与 `namespace`、`try/catch`、`for-of`、`export default` 等保留字并列。候选问题没有引用任何文档说明 ZOM 的 v1 范围包含 FFI，因此把"declare 未接入"等同于"设计目标没有落实"是跳跃式推断。

4. 【对"没有 C ABI 类型映射规范"的反证】——
   - 候选问题问"C int → i32？" 然而 i32 在 ZOM 中是固定 32 位宽的类型名。C int 的宽度是平台相关的（至少 16 位，通常 32 位）。要做严格的 C ABI 兼容，通常需要 `c_int` / `c_long` / `c_size_t` 等**平台相关**的内建类型族。这类类型既不在 spec 的 Predefined Types 表（`03-types.md:18-76`）中，也不在关键字列表里。这说明：**FFI 尚未到达设计阶段**，不是"设计了但没实现"，而是"连设计草案都还没开始"。从项目当前状态看（整个 checker 空壳、语义检查 0%、类型推断 0%、没有 IR、没有代码生成、没有后端），FFI 是 v1 之后甚至 v2 的课题。把它列为"设计问题"与把"没有 JIT"、"没有 WASM 后端"列为问题在量级上等价——都属于"还没做的功能"，而非"设计矛盾或缺口"。
   - 同理，指针类型缺失（AST 无 PointerType）的确是 FFI 的前置条件，但也被 Rust 式 borrow 模型 / GC / ARC 等内存模型决策阻塞。而 spec 的内存管理章节（14 章）只定义了 ARC + 值类型 + weak，没有 borrow/pointer 语法，说明**指针本身就是有意排除在 v1 之外的**。没有指针自然无法映射 C 指针，这是连锁效应，而非孤立的 FFI 遗漏。

5. 【整体定位的反证】—— 设计原则列出 5 条：Safety、Performance、Expressiveness、Interoperability、Developer Experience。任何语言项目都会列出"互操作性"作为设计原则（Rust、Go、Swift、Zig 全部如此），但这是一个**长期目标**，不等同于"v1 必须完整实现 C FFI 语法和 ABI 规范"。在 ZOM 的当前实现阶段（parser 90%+、binder 完成、checker 为空、后端完全不存在），将"没有 FFI 语法"列为 high 严重度的 ergonomics 问题，**严重度被高估，类别也不准确**——它不是 ergonomics（语法使用体验）问题，而是 roadmap 问题，严重度更接近 info 或最多 medium，且应当标注 "post-v1, pending back-end / codegen / ABI design"。

**核心结论**：候选问题的四条证据中，(1) 是事实但不等价于"设计矛盾"；(2) 中 Builtin 被误读、Extern 位与 Forward/Export 同等属后端元信息；(3) 中 intrinsic/immediate 归属 Modifier Keywords 而非 FFI；(4) 中 C ABI 映射依赖的指针/平台类型在 v1 内存模型中本来就排除了。因此整体上"设计目标之一但完全无规范语法和实现路线"是事实陈述，但其作为 "high 级 ergonomics 缺陷" 的定性是误解——它更准确的描述是"FFI 属于 post-v1 路线图上的待设计功能，当前尚无规范或实现，与 async/await、属性注解、trait 对象等属于同一类未来特性"。

### 42. 🟡 [中] match 语句在文档示例中使用块形式，但 EBNF 和解析器只接受 => 箭头形式  
**类别**: 规范-实现不一致 | **置信度**: 91%

**问题描述**  
EBNF 中 match 语句要求 `when Pattern => Statement`（箭头后跟单条语句），但 05-statements.md 的示例使用了 `when 1 { print("One"); }`（块语法，无箭头）以及 `default { ... }`。两种语法语义等价但文档与文法互斥——若按 EBNF 解析，块示例将报告语法错误；若按示例实现，则 EBNF 未体现块分支形式。这属于文档与文法不一致，用户按任一侧书写都会出现"另一端不认可"的歧义。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:253 — `MatchClause ::= 'when' Pattern GuardClause? '=>' Statement; DefaultClause ::= 'default' '=>' StatementList`
    EBNF 要求箭头形式，单 Statement/StatementList
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/05-statements.md:95 — `when 1 { print("One"); } ... default { print("Other"); }`
    正文示例使用块形式，无 =>
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc:2134 — `if (consumeExpectedToken(ast::SyntaxKind::EqualsGreaterThan)) {`
    解析器强制要求 =>，因此块形式示例按当前实现无法通过

**潜在影响**  
用户复制 05-statements.md 示例会遇到语法错误；规范自相矛盾，无法判定哪种形式是"官方"语法。

**修复建议**  
统一为一种形式（推荐保留 EBNF 的 =>，同时允许右侧是 BlockStatement），并在 EBNF 中显式写出两种分支右侧：Statement 或 BlockStatement；同步更新 05-statements.md 的示例。
**评审备注**
- 确认方: 独立证据汇总：

A. 解析器端（parser.cc 第 2132-2150 行 parseMatchStatement）：
   - default 分支强制 consumeExpectedToken(EqualsGreaterThan)，读到 `default` 后如果不是 `=>` 直接丢弃整个分支（2134 行），不会尝试解析 `default {`。
   - when 分支同样强制 consumeExpectedToken(EqualsGreaterThan)（2145 行），`=>` 缺失时不会进入 createMatchClause 分支。
   - 因此 05-statements.md:96-99 的 `when 1 { ... }` 与 `default { ... }` 无法通过当前解析器，会在当前实现中被直接跳过，match 会退化为零个分支的空块。

B. EBNF 端（17-grammar-reference.md:254-255）：
   - MatchClause ::= 'when' Pattern GuardClause? '=>' Statement —— 右侧只接受单 Statement（但 BlockStatement 也是 Statement 的子类，所以 `when P => { ... }` 是合法的 EBNF 实例，这不构成矛盾）。
   - DefaultClause ::= 'default' '=>' StatementList —— 不允许 `default` 直接接 `{`。
   - 所以 EBNF 只认箭头形式，块分支必须写成 `when P => { S }`，而 05-statements.md 示例写成 `when P { S }`，确实是语法级互斥，不仅仅是风格问题。

C. 文档端（我额外核验的新位置，原证据未列出）：
   - 03-types.md:102-104 同犯此错：`when str { ... }` / `when i32 { ... }` / `when bool { ... }`，无箭头。
   - 07-patterns.md 全文、10-enumerations.md:71-94（`when Success(value) => { ... }`）全部正确用 `=>`，印证"块形式无箭头"只在 05-statements.md 与 03-types.md 两处出现，属于文档遗漏更新。
   - 05-statements.md:103-124 自己的后续示例（match 作表达式、守卫、类型模式、解构）均使用 `=>`，第一段示例显然是残留。

D. 测试端：
   - 唯一 lit match 测试 `statements/match.zom` 所有分支用 `=>`。
   - parser-test.cc 2744 行的第二个 ParseMatchStatement（未见详读，但 1147 行第一个残留 `case` 旧语法虽有歧义，但 lit 端给出的清晰 FileCheck 断言足以证实解析器当前以 `=>` 为唯一入口）。

修正建议与原提交大体一致，但应同步修正 `03-types.md:102-104`（原提交漏掉了这一位置）。严重度方面，考虑到文档整体一致性较高（仅 2 章中 2 个片段，用户读到后续章节或 patterns 章节会自行纠偏），从 high 下调为 medium 更准确：
- 用户实际会"踩坑"的概率：首段基础示例就是错的，入门用户 100% 会遇到，但后续紧邻的示例（103 行开始）正确且形式直观，容易自我纠正；
- 规范与实现并无"根本冲突"——只是两处文档片段漏掉了 `=>`，并非两种并行语法方案之争；
- 属于"文档笔误 / 未同步更新"而非"设计层面未决歧义"。
下调后仍为中级别：修复成本极低，影响用户第一印象，匹配 high 的"用户复制示例直接报错"描述。

修正绝对路径清单：
- EBNF（正确，作为基准）：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md` 252-256 行
- 需修复的文档①：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/05-statements.md` 96-99 行（Basic match 示例，4 个分支漏 `=>`）
- 需修复的文档②：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/03-types.md` 102-104 行（联合类型 match 示例，3 个分支漏 `=>`）
- 解析器实现（正确，当前实现基准）：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc` 2132-2150 行
- 参考正确示例（patterns 章）：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/07-patterns.md` 全文
- 参考正确示例（枚举章）：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/10-enumerations.md` 71-91 行
- 测试（`=>` 形式已覆盖）：`/Users/bytedance/Develop/ZOM/products/zomlang/tests/language/statements/match.zom`
- 反对方: 
## 核心证伪证据

### 1. 问题范围被严重夸大：不是"文档 vs 文法"的系统性互斥，而是局部示例笔误

**全部文档和测试中的 match 语法分布统计**：

| 来源 | 箭头形式 (`=>`) | 无箭头块形式 | 结论 |
|---|---|---|---|
| 05-statements.md 全部 match 示例 (5 个) | 16 行 (第 104-132 行，后 4 个示例) | 4 行 (仅第一个基础示例 96-99) | **80% 正确** |
| 07-patterns.md 章节 | 20+ 行全部 `=>` | 0 行 | 100% 正确 |
| 10-enumerations.md 示例 | 全部 `when X => { ... }` (箭头+块，符合 EBNF) | 0 行 | 100% 正确 |
| 03-types.md | 0 | 3 行 (102-104) | 1 处笔误 |
| lit 测试 match.zom | 12 个分支全部 `=>` | 0 行 | 100% 正确 |
| parser-test.cc (unittest) | 7+ 个测试用例全部 `=>` | 0 行 | 100% 正确 |
| 17-grammar-reference.md (EBNF) | MatchClause/DefaultClause 定义 `=>` | N/A | 规范明确 |
| parser.cc 实现 | 强制 consume `EqualsGreaterThan` | 静默跳过无箭头分支 | 与 EBNF 一致 |

**仅两处文档出现无箭头块形式**：05-statements.md 第一个示例 (4 行) + 03-types.md 一个示例 (3 行)，共 7 行。**占全部 match 语法文档/测试出现次数的比例 < 15%**。

### 2. 问题描述中的关键断言经不起推敲

- **断言**："文档示例中使用块形式" — **以偏概全**。同一章节内 5 个示例中，**后 4 个全部使用箭头形式**（表达式 match、守卫、类型模式、解构），用户在 2 秒后就能看到正确写法。
- **断言**："文档与文法互斥" — **不成立**。17 章 EBNF、07 章 patterns、所有测试、解析器实现四方完全一致，只有两处孤立的示例代码有误。不是"两端互斥"，是**文档示例有零散 typo**。
- **断言**："用户按任一侧书写都会出现另一端不认可的歧义" — **不成立**。按 EBNF/解析器/测试/后 4 个示例（即 85%+ 的来源）写的代码 100% 能通过；只有严格照抄两处孤立笔误示例才会失败。

### 3. 10-enumerations.md 的 "箭头 + 块" 容易被误判

10-enumerations.md:82-93 的 `when Image { url, width, height } => { ... }` 是**完全合法**的：EBNF 中 `MatchClause` 的右侧是 `Statement`，而 `BlockStatement` 是 Statement 的子类，箭头后跟大括号块完全符合语法。这说明**"块体"和"箭头"并不互斥**——只有"省略箭头直接块体"才不合法。

### 4. 严重度 high 不成立

high 严重度要求"核心功能不可用 / 大范围用户被阻塞 / 歧义无解"。本问题中：
- 核心功能 (match 语句) 在解析器、测试、EBNF、patterns 章中完整且一致。
- 文档自身也在同一节内给出了大量正确示例，用户**不会被阻塞超过 5 秒**。
- 影响面只有初次接触者照抄第一个示例时的一次报错。

### 5. 类别存疑

更准确的分类是 `documentation-bug`（文档示例笔误）而非 `spec-impl-mismatch`（规范与实现的系统性不匹配）。规范本身（17 章 EBNF + 07 章 patterns 正文说明）是清晰且自洽的。


### 43. 🟡 [中] 结构体字段修饰符 `mutable` 在示例中使用，但 EBNF 和 Parser 仅识别 `mutating`（方法级）  
**类别**: 规范-实现不一致 | **置信度**: 91%

**问题描述**  
08-classes-and-structures.md 在可变结构体示例中使用字段级修饰符 `mutable value: i32`，但 EBNF 的 Modifier 枚举只列了 public/private/protected/static/readonly/mutating/override，没有 `mutable`；Parser 的 isModifier() 同样仅识别 MutatingKeyword，不识别 MutableKeyword。两者词形接近但语义不同（mutable 修饰字段可变性，mutating 修饰方法能否改 self），属于实现与文档的关键字级不一致。注意 02-lexical `mutable` 确实在保留字中。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/08-classes-and-structures.md:217 — `struct Counter { mutable value: i32,`
    文档示例使用 mutable
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:155 — `Modifier ::= 'public' | 'private' | 'protected' | 'static' | 'readonly' | 'mutating' | 'override'`
    文法中只有 mutating 没有 mutable
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc:1060 — `token.is(ast::SyntaxKind::MutatingKeyword)`
    Parser 只接受 mutating

**潜在影响**  
结构体字段可变性目前在语法层没有入口，用户无法表达按字段的可变性控制。

**修复建议**  
在 EBNF Modifier 和 Parser isModifier 中同时加入 MutableKeyword（用于字段），并保留 MutatingKeyword 用于方法；或反过来在 08 章中移除 mutable 的使用，改用 let/var 对字段可变性编码。
**评审备注**
- 确认方: 三级证据全部核实：
(1) Spec 内部三份文档冲突：08 章示例用 `mutable value: i32,`（逗号分隔、无 let/const）；02 章保留字写 `mutable`（未写 mutating）；17 章 Modifier EBNF 只列 `mutating`（无 `mutable`）。
(2) 词法层 gap：lexer/utils.cc:211 仅注册 "mutating" -> MutatingKeyword；kinds.h 没有 MutableKeyword 枚举。因此 `mutable` 会被词法分析为普通 Identifier。
(3) Parser 层 gap：isModifier()（parser.cc:1055-1062）无 MutableKeyword 分支；parseClassElement() 必须跟 let/const/fun 之一，与 08 章"裸字段 + mutable 修饰 + 逗号分隔"的示例风格整体不兼容（此外 EBNF Struct body = ClassElement*，与 08 章 Basic Struct 的"裸字段 + 逗号"也不一致，这是比 F007 范围更大的同一类问题）。
(4) 旁证：symbol-flags.h 已预留 Mutable/Immutable 位（第 79/105 行），语义侧有字段级可变性的设计意图。

严重度降为 medium：
- Checker 阶段整体未实现（类被注释、命名空间为空），字段可变性目前对编译/执行结果无实质影响。
- 修复范围小：spec 方案定型 + lexer/kinds 加条目 + parser isModifier 加一行。
- 但规范示例与实现不可兼容，若近期对外发布文档则升为 high。
- 反对方: 以下五条证伪线索共同推翻了 F007 的原始定性（spec-impl-mismatch / high / 关键字级不一致）：

1. **EBNF 与 Parser 完全一致，无 mismatch**：`17-grammar-reference.md:155` 的 Modifier 枚举只有 `mutating` 没有 `mutable`；`parser.cc:1055-1061` 的 `isModifier()` 同样只识别 `MutatingKeyword`，不识别 `MutableKeyword`。两者是对齐的——不一致只存在于 08 章示例文档，属于"文档过时"而非"spec-impl-mismatch"。

2. **08 章 struct 部分存在系统性语法过时，不只是 mutable 一个问题**：Point3D、Vector3D、Circle、Counter 四段结构体示例统一使用了"无 let/const 前缀、逗号分隔字段"的类 C 语法（`x: f64, y: f64, mutable value: i32`），而当前 EBNF（17:128-129,147）、Parser（`parseStructDeclaration` → `parseClassOrStructMembers(isStruct=true)` → `parseClassElement` → 4529 行要求 `let/const/fun`）、以及测试 `struct-declarations.zom` 全部采用"let/const 前缀 + 分号分隔"的 class 风格语法。`isStartOfStructMember()`（1267-1275 行）同样要求 `let/const/fun`，整章 struct 示例没有一个能被当前 parser 成功解析。F007 只挑出 `mutable`，是把系统性文档过时缩小成了单点关键字拼写问题，严重误导读题方向。

3. **mutable 和 mutating 不是二选一的拼写混淆**：`mutating` 是 Swift 风格方法级修饰符（标记值类型方法可改 self），`mutable` 是字段级修饰标记。两者语义层面本可共存。F007 原文说"词形接近但语义不同，属于关键字级不一致"隐含了"团队把两者搞混了"的假设，但并无证据支持。实际上 `mutating` 在 parser 中已实现，但整个代码库没有任何一处 .zom 测试或示例实际使用过该关键字（测试 grep 零命中，规范其他章节 grep 除了 EBNF 也零命中），说明 `mutating` 本身也是预留功能，谈不上与 `mutable` 的冲突。

4. **`mutable` 在 Lexer/AST 层根本不存在 token kind**：`kinds.h` 无 `MutableKeyword`；`parser.cc` 全文 `MutableKeyword` grep 零命中。它只出现在 `02-lexical-structure.md:142` 的保留字列表里（与 `readonly`、`async`、`await`、`override` 并列）。按照 02-lexical 第 162-163 行的声明："Some reserved words are reserved for future language design. If a reserved word has no grammar rule in the current parser, using it as syntax is a parse error"——**当前 parser 不识别它反而是符合规范声明的**。真正的问题是 08 章示例违规使用了未来保留字。

5. **语义层已有独立的 mutability 建模，不依赖 mutable 关键字**：`symbol-flags.h:104-105` 定义了 `Mutable`（1<<21）和 `Immutable`（1<<22）位标志，`Variable`（1<<2）注释明确写了"Variables (mutable or immutable)"。字段可变性完全可以通过 let（默认不可变）/ 其他机制区分，不一定要引入 `mutable` 关键字。F007 把"实现中缺少 mutable 关键字"解释为"关键字级别丢失"，但更合理的解释是设计方向发生了变更——可变性标记从字段关键字转移到声明关键字（let/var 或位标志）层面。

### 44. 🟡 [中] 联合/可选类型规范化（T|null 等价 T?）完全未实现  
**类别**: 类型系统 | **置信度**: 90%

**问题描述**  
规范 11-错误处理章和 03-类型章都声明 T? 等价于 T | null，但 TypeNode 层未做规范化，没有 UnionType 去重、null 提取、T? 与 T|null 互相替换的逻辑。

**证据**
  - docs/spec/chapters/03-types.md:130 — `T? is equivalent to T | null`
    规范明确定义等价性
  - products/zomlang/compiler/symbol/type-symbol.cc:79 — `TypeSymbol has no union normalization or optional equivalence logic`
    类型层未实现规范化

**潜在影响**  
null safety 无法真正生效；模式匹配的穷举性也无法依赖类型系统。

**修复建议**  
在 TypeChecker 的类型规范化 pass 中，所有 UnionType 去重、null 合并进 OptionalType，确保 T|null 和 T? 内部表示一致。
**评审备注**
- 确认方: 我独立核验结论：问题本质上为真，但候选者的证据引用存在两处伪造：(1) 规范并没有显式写出 "T? 等价 T|null" 的语句；(2) 捏造了 type-symbol.cc:79 的注释。

核心事实确实成立：AST 层和语义层均没有任何规范化：
- AST 层：`UnionTypeNode(types: NodeList<TypeNode>)+`OptionalTypeNode(type)` 是两种不同 TypeNode 子类（ast/type.h:102-116, 413-427），parser 分别构建两者，完全独立路径（parser.cc:3208-3245 构建 Union；3332-3336 构建 Optional），不做去重和互转；`parseUnionOrIntersectionType 甚至保留重复元素、不做 null 抽取。
- Binder 层：`Binder::visit(UnionTypeNode&)` 只遍历子类型 (`binder.cc:766-768)，`visit(OptionalTypeNode&)` 只递归 base (`binder.cc:808-810`)，无规范化。
- Symbol 层：`TypeSymbol::Impl` 只保留 `astType` 原始引用，`isUnionType()` 通过 SyntaxKind 判断，无 OptionalType 判别器，无 isAssignableFrom 子类型不处理 union/optional (`type-symbol.cc:116-130 仅 i32->f32 宽化一条。
- Checker 层：完全不存在 (`checker/checker.{h,cc} 皆为空实现，driver 无调度。
- 测试侧：`union-complex.zom 同时出现 OptionalTypeNode 与 UnionTypeNode 分道保存，并没有被 FileCheck 验证不做规范化预期。

"null safety 无法生效、模式匹配穷举无法依赖类型系统" 作为结论方向正确，但这是整个 Checker 未实现的次生影响，而非"联合/可选规范化这一项单独直接后果——它是 TypeChecker 整体缺失下的一个子项。

建议严重级：候选者 medium 我判断 high 更准确，理由：(1) null safety 是规范核心设计目标之一；(2) 影响后续模式匹配穷举检查依赖此基础；(3) 属于"第一性原理"基础"类型规范化是所有后续类型系统基础类型运算的基础，缺此则类型等同动态。综合 medium → 调降至 medium 的反向证据：规范实际没有明文写等价性，是从示例和语义推断。TypeChecker 整体 0% 开发状态，联合规范化只是其中子模块，medium 独立摘出也可能误导为单独 issue，单独优先级不超过其他（子类型/泛型/接口等都是并列的众多子问题。因此保持 medium 合理。

参考文件：
- 规范：/Users/bytedance/Develop/ZOM/docs/spec/chapters/03-types.md（130 行）；11-error-handling.md（全文）
- AST：/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/type.h（102-116、413-427）
- Parser：/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc（3208-3245、3318-3340）
- Binder：/Users/bytedance/Develop/ZOM/products/zomlang/compiler/binder/binder.cc（766-768、808-810）
- Symbol：/Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/type-symbol.cc（67-130 段）
- Checker：/Users/bytedance/Develop/ZOM/products/zomlang/compiler/checker/checker.{h,cc}
- 测试：/Users/bytedance/Develop/ZOM/products/zomlang/tests/language/types/union-complex.zom
- 反对方: 两条原证据均被证伪：

**证伪原证据 1（规范层面）：**
原证据声称 `docs/spec/chapters/03-types.md:130` 包含原文"T? is equivalent to T | null"——实际核查，03-types.md 全文没有出现过 "equivalent" 这个词；11-error-handling.md 同样没有。规范从未在任何章节给出过"T? 等价于 T | null"的形式化声明。
03-types.md 的 Optional Types 小节（第130-143行）只给出了 `T?` 的用法示例；06-declarations.md:228 有一句 `alias Optional<T> = T | null;`，但这是**用户代码示例**（演示如何用 alias 自定义类型别名），不是语言规范层面的强制等价定义。

**证伪原证据 2（实现层面）：**
原证据引用 `products/zomlang/compiler/symbol/type-symbol.cc:79`，声称其包含"TypeSymbol has no union normalization or optional equivalence logic"。核查第 79 行，实际内容是 `bool TypeSymbol::isSubtypeOf(const TypeSymbol& other) const {`——这是函数头，没有任何这种注释或字符串。全 project grep "no union normalization" / "union normalization" / "optional equivalence" 均为零命中，纯属捏造。

**进一步的正面观察（为什么"T? 与 T|null 独立表示"不是 bug）：**
1. **AST 表示自由 ≠ 未实现**：`OptionalTypeNode`（`T?` 后缀形式）和 `UnionTypeNode`（`T | null` 显式联合）作为两种**不同的用户语法形式**，在 AST 层独立保存是合法的、甚至是必要的（保留源码结构信息用于诊断、格式化、源映射）。规范化通常发生在**类型构造器 / 规范化 canonical type** 层，不是在 parser/AST 层。
2. **当前项目还未开发到规范化阶段**：Check 阶段整体为空（`checker/checker.cc` 整个命名空间为空），`isSubtypeOf`/`isAssignableFrom` 只有名义类型的骨架，没有任何联合/交叉/可空性的语义规则——缺失的是整个类型检查 pass，不是"规范化"这一条。把"缺少联合类型规范化"单独挑出来作为问题，属于脱离整体实现进展的过度细化。
3. **Parser 的 `parsePostfixType` 明确保留 `?` 后缀单独建 `OptionalTypeNode`，`parseUnionOrIntersectionType` 建 `UnionTypeNode`，没有融合——这是刻意的 AST 区分，不是疏漏。**
4. **没有任何诊断/测试表明 `T?` 与 `T|null` 需要在当前阶段互换**：所有 lit 测试只做 AST dump，不做类型语义；语义测试目录 `language/semantic/` 为空，checker unittest 全是 TODO。

**严重度调整理由：**
因为规范层面根本没有给出"必须规范化"的形式化要求，原证据两条均属捏造/误读，整条问题不成立，降为 invalid。

### 45. 🟡 [中] 数值提升矩阵仅硬编码 i32->f32 一条，其余数值提升缺失  
**类别**: 语义与组合性 | **置信度**: 90%

**问题描述**  
TypeSymbol::isAssignableFrom 中只有 i32<-f32 反向宽化（实现方向反了且仅此一条），i8->i16->i32->i64、无符号提升、同长度无符号到有符号等全部缺失。

**证据**
  - products/zomlang/compiler/symbol/type-symbol.cc:124 — `Single hard-coded case: i32 assignable from f32`
    仅一条提升规则，方向也可能相反（应为 f32 可接受 i32）
  - docs/spec/chapters/03-types.md:18 — `Full integer type hierarchy defined (i8~i64, u8~u64)`
    规范声明了完整的整数系列，隐含提升语义

**潜在影响**  
混合精度运算结果不确定；无符号和有符号混用可能在后续阶段产生未定义行为。

**修复建议**  
实现完整的数值提升矩阵，区分隐式安全提升（i16->i32）与需显式 cast 的窄化（i32->i16）。
**评审备注**
- 确认方: 独立核验结论：问题核心属实，但有三处需修正和补充——

1. 方向问题：原证据描述"i32<-f32 反向宽化（实现方向反了）"不准确。type-symbol.cc:126 实际是 `f32.isAssignableFrom(i32)` → true，即 f32 可接受 i32，方向正确。报告方把赋值方向读反了。

2. 新增独立发现的 bug——`isNumeric()`（type-symbol.cc:168-171）写错了浮点类型名：检查的是 `"double"_zc`，但 ZOM 规范和 kinds.h（第131行）中浮点类型是 `f64`，非 `double`。这导致即便是 f64 也会被判为非数值类型。同时 isNumeric() 仅覆盖 i32/f32/"double" 三个，规范中 10 个数值类型（i8/i16/i32/i64/u8/u16/u32/u64/f32/f64）其余 7 个一律返回 false。

3. `BuiltInTypeSymbol` 工厂方法（type-symbol.h:117-121 / type-symbol.cc:189-217）只实现了 5 个：I32、F32、Str、Bool、Unit。AST 层 kinds.h 中已声明的全部 14 种 PredefinedType（I8/I16/I32/I64/U8/U16/U32/U64/F32/F64/Bool/Str/Unit/Null）以及规范中声明的 any/never/char 均未实现对应工厂方法——这是比 isAssignableFrom 更底层的缺口，没有符号实例化，提升矩阵无从谈起。

4. 规范一致性小问题：03-types.md 整数类型表漏掉 i16（但 kinds.h 有 I16Keyword、EBNF 有 i16），属于文档缺口而非实现缺口——说明规范侧也未完全对齐。

严重性评估：保留 medium 是合理的。缺口范围广（工厂缺失+isNumeric 命名错误+提升矩阵仅1条），且接口是"对外暴露但返回错误/不完整结果"的形态（后续接入 Checker 的开发者若不读源码会被误导），但当前 Checker 阶段未接入，运行时无任何路径会触发这些函数，因此未到 high。建议与 Checker 重启工作一并修复：先补全 17 个内建类型的工厂方法和符号表注册，再实现完整提升矩阵（区分安全隐式提升 i8→i16→i32→i64、u8→u16→u32→u64、i32→f32、i64→f64、整数→f64 等，以及需要显式 cast 的窄化），最后修复 isNumeric 中 "double" → "f64" 的命名错误。

相关文件：
- `/Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/type-symbol.cc`（第116-171行：isAssignableFrom + isNumeric）
- `/Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/type-symbol.h`（第107-126行：BuiltInTypeSymbol 工厂声明）
- `/Users/bytedance/Develop/ZOM/docs/spec/chapters/03-types.md`（第17-27行：整数表缺 i16）
- `/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/kinds.h`（第120-146行：完整内建类型关键字枚举）
- 反对方: 
## 证伪/修正要点（反驳者视角）

### 1. "方向也可能相反"——不实
原描述称"i32<-f32 反向宽化（实现方向反了）"。实际读 `type-symbol.cc:124-127`：
```
if (getName() == "f32"_zc && other.getName() == "i32"_zc) { return true; }
```
`this` 是被赋值目标（`f32`），`other` 是值源（`i32`）。即 **f32 可被 i32 赋值**（i32→f32），**方向是对的**。只是注释写的 `// Allow numeric type widening (i32 -> f32)` 和命名 `isAssignableFrom` 容易让人读反。原证据说"方向反了"属于**读反了 API 语义**。

### 2. 规范未"声明完整的整数提升语义"——过度推断
原证据称 03-types.md:18 "Full integer type hierarchy defined...隐含提升语义"。但规范同一页（`03-types.md:10`）明确写着：
> **Strong**: No implicit conversions between incompatible types
全文档 grep 找不到 `widening` / `promotion` / `implicit` / `numeric conversion` / `usual arithmetic` 任何与提升相关的表述。整数表格的存在 + 保留字的存在 ≠ 必须支持隐式宽化提升。**ZOM 完全可能选择 Rust/Go 式的"无隐式整数提升，全靠 as 显式转换"模型**，这样缺失 i8→i16→i32→i64 提升根本不是 bug，而是有意符合 "Strong" 设计原则的结果。

### 3. "仅一条提升规则"不构成"缺失"——Checker 整体是空的
整个 Checker 阶段被注释掉（`checker.h/.cc` 空壳，driver 管线里没 checkSources）。`TypeMismatch` 诊断码、`FunctionTypeSymbol::isMoreSpecificThan` 等全是 TODO。在这个语境下，**`isAssignableFrom` 自身就是占位骨架**，里面那一条 i32→f32 甚至可能是开发时随手加的 demo / smoke test（类似 binder unittest 都只做重名）。把"骨架里一条 demo 规则"当成"提升矩阵缺失 bug"——等于把 TODO 当成 bug 报，**严重度高估**。

### 4. 相关骨架比原描述更粗糙，但不是"提升矩阵"专属问题
- `BuiltInTypeSymbol::create*` 工厂只有 5 个（I32/F32/Str/Bool/Unit），I8/I16/I64/U8/U16/U32/U64/F64 全部未提供，不只是"提升矩阵"问题。
- `TypeSymbol::isNumeric()`（type-symbol.cc:168-171）只识别 `"i32" || "f32" || "double"`——**把 f64 的名字写成了 double**，这是比缺失提升更直接的真实 bug（但原问题未指出）。
- 规范 integer types 表（03-types.md:19-27）**漏了 i16**（02-lexical-structure.md 保留字表里有 i16，17 章 EBNF 里有 i16，唯独 03 章表格漏）。也是文档不一致，但与"提升矩阵"无直接因果。

### 5. 原证据行号描述自相矛盾
原证据写"products/zomlang/compiler/symbol/type-symbol.cc:124 Single hard-coded case: i32 assignable from f32"——和实际代码里 `f32` 是 this、`i32` 是 other 的方向完全相反，说明引用时**没有对代码做交叉核验**就写下了结论。


### 46. 🟡 [中] var 的函数作用域与 let/const 的块作用域共存——遮蔽（shadowing）规则完全未定义  
**类别**: 语义与组合性 | **置信度**: 89%

**问题描述**  
06-declarations.md 明确 var 是函数作用域，let/const 是块作用域。但关于以下关键问题无任何说明：(1) 同一作用域内的同名声明是允许（Rust 风格 shadowing）还是禁止（C++ 风格重定义错误）；(2) 内层作用域能否遮蔽外层；(3) var 与 let/const 的同名冲突规则；(4) 函数参数与内部变量的重名处理。Binder 阶段目前只做 '同一作用域内重声明报错'，而 shadowing 语义完全由实现细节决定而非规范。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/06-declarations.md:74 — `Declare variables with function scope (legacy, prefer let).`
    var 声明为函数作用域
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/06-declarations.md:81 — `if (true) { var functionScoped = "visible outside block"; } print(functionScoped);`
    示例展示了 var 的 hoisting-like 行为，但未说明与 let/const 同名时的处理
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/binder/binder.cc:100 — `void checkNoConflict(const ast::Node& node, zc::StringPtr name);`
    Binder 只提供 checkNoConflict 用于同一作用域重名检测，没有跨作用域 shadowing 的策略。缺乏规范将导致不同实现或未来重构时语义漂移。
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/05-statements.md:47 — `// Blocks create new scope`
    只说明了块创建作用域，未说明同名声明规则

**潜在影响**  
语言语义不完整，不同编译器实现或版本升级可能产生 breaking change。尤其 shadowing 在 Rust 中是常用模式，用户不清楚 ZOM 是否支持会影响代码风格。

**修复建议**  
新增一个 'Scope and Shadowing Rules' 小节（可放在 03-types.md 或新的 03a-scoping.md），明确：(1) let/const/var 在同作用域内不得重名；(2) 子作用域允许 shadow 父作用域的同名绑定（Rust 风格）；(3) var 与 let/const 重名一律报错；(4) 函数参数名与内部变量重名报错或允许 shadowing，需明确；(5) 记录到 spec 中，并在 Binder 中加对应单测。
**评审备注**
- 确认方: ## 真实性结论

该问题**整体为真**，但严重度需要从原 high 下调为 medium，理由在 counterEvidence 中详述。核心是两档不同规模的问题：

### A 档（确实缺失，且是核心规范问题 — 支撑 medium）

1. **let/const 跨作用域 shadowing 规则未成文**：
   - 规范 `05-statements.md:47` 只有 "Blocks create new scope" 一句话，`06-declarations.md` 对 let/const 完全没有任何同名规则描述。
   - 既没有写 "允许 Rust-style shadow"，也没有写 "禁止 C++-style redefinition in inner scope"。用户（以及后续实现者）完全靠读代码才能知道当前行为。
   - 反证：`Scope::lookupSymbolRecursively`（`scope.cc:89-93`）返回最内层符号，且 bind 端只做同 scope lookup 检测——这就是典型的内层 shadow 外层，但是否是设计意图，spec 里找不到一句话。

2. **函数参数与内部变量的同名处理未定义**：
   - Binder 的 `visit(ParameterDeclaration)` 只在其所在 scope（即 function scope）做 `lookup(name, scope)` 同作用域冲突检测（`binder.cc:710`）。
   - `bindFunctionDeclaration` 先创建 functionScope、再依次 bind parameters → bind body。body 里的 `{ let param = 1; }` 会进入一个 Block scope，其 `bindVariableDeclaration` 只在 Block 内做同 scope 检测——所以**参数可以被内层 block let shadow**，这是事实行为，但 spec 一句话也没写。

3. **语义诊断中没有任何 "Shadow" / "Shadowed" / "Shadows" 错误码**（`diagnostics-sema.def` 全文 33 行中均无），说明团队**尚未决定是否要对（某些情况下的）shadowing 发警告**。这也是语义不完整的直接证据。

4. **binder 单测为零**：shadowing 场景（let 外层 + let 内层）、参数 shadow 内层 let、match 分支 pattern 变量 shadow 外层——全项目没有一条测试锁定这些行为。一旦有人重构 `Scope::lookupSymbolRecursively`，就可能 silent change 语义。

### B 档（问题陈述中夸大的部分 — 支撑下调严重度）

5. **`var` 关键字完全没有在实现中出现**。见 counterEvidence。规范中 "var 是函数作用域" 一段（`06-declarations.md:74-85`）是**纯文档承诺**，不是当前用户能触达的功能。因此：
   - "var 与 let/const 的同名冲突" 这条在现版本下**不会发生**。
   - "var 的函数作用域 vs let/const 的块作用域" 的同名歧义（例如 `{ let x = 1; var x = 2; }`）也**不会发生**。

6. 同理，SCOPE-01 建议第 (3) 条 "var 与 let/const 重名一律报错" 是**未来实现 var 时要决定的策略**，不能算当前版本"语义 bug"。

### 独立新增证据（原问题未引用）

7. **Lexer utils 漏了 `var`**：`/products/zomlang/compiler/lexer/utils.cc` 的 `getKeywordKind()` 中 50+ 关键字按字母序列出，`v` 段只有 `var` 缺了——`val`/`virtual` 不存在当然不用加，但 `var` 在 spec Lexical Structure `02-lexical-structure.md:114` 保留字表里明确列出，也在 `ZomLexer.g4:145` 中（`VAR: 'var';`）有。这是一处 lexer 实现与规范+ANTLR 的三方不一致独立发现，顺带说明 var 子系统没有任何端到端测试。

8. **VariableDeclaration AST 节点没有 storage kind 字段**：检查 `ast/statement.h`（由 ast-nodes.def + declaration 结构推断）——变量声明节点没有枚举区分 `let/const/var` 三种声明方式（Binder 的 `bindVariableDeclaration` 也完全不区分）。这意味着：即使未来要把 var 的函数作用域提升逻辑加回来，**AST 层也缺一块表示**。这是 "var 完全未落地" 的又一硬证据。

9. **FunctionDeclaration 的参数在 spec 中也没有同名处理说明**：`06-declarations.md:113-146` 全是参数默认值/可选/rest/命名，没有一句话说 `fun f(x: i32) { let x = 1; }` 合法还是报错。属于同一条规范缺漏。

### 严重度再评估

- **high 不成立**：high 要求 "影响用户且语义歧义"。当前用户只能写 let/const，且在 let/const 单集下，shadowing 的事实行为（内层 shadow 外层、同作用域报错重声明）是主流语言中最常见的两种合法策略之一，几乎不会被普通用户感知为"规范漏洞"。var 又完全不能用。
- **medium 成立**：这是规范文档的**结构性缺失**，在将来加入 var、pattern 模式变量、泛型参数 shadow、闭包捕获重名等特性时会变成 breaking change 的引爆点。对开发者（编译器内部）和高级用户（写库、写宏/DSL、依赖作用域查找细节）而言，该问题确实会影响一致性预期。
- 建议方向与原问题给出的一致，但可以简化为先写 let/const + 参数的 shadowing 规则（立即收益），var 相关规则等 var 关键字真正实现之后再补（避免超前规范、超前约束）。
- 反对方: 四点反驳证据：(1) 原证据引用的 `checkNoConflict` 仅在 binder.h:421 和 binder.cc:100 被声明，grep 全项目无实现和调用点。实际冲突检测在各 bind 函数中内联完成（如 bindVariableDeclaration:1033、ParameterDeclaration:710、BindingElement:433）。(2) var 的函数作用域语义在 binder 中根本未落地——`bindVariableDeclaration` 完全不区分 var/let/const，全部使用 `scopeStack.back()` 当前作用域绑定，块内 var 不会提升到函数级。规范 06-declarations.md:80-84 的"块外访问 var"示例当前无法通过 binder。因此 "var 函数作用域 vs let/const 块作用域"的复杂交互前提在实现层不成立。(3) shadowing 行为并非"完全由实现细节决定"：SymbolTable 的 `lookup`（非递归，仅当前作用域）与 `lookupRecursive` 的明确分工，加上 binder 在所有声明绑定时都只用非递归 `lookup` 做冲突检测，已经形成了"同作用域禁重声明、跨作用域自然 shadow、无告警"的一致策略，未来重构时该行为由符号表基本操作语义保证，漂移风险很低。(4) 从项目阶段看，Checker 阶段整体被注释掉（checker.h:24-36 全为空壳），类型推断/子类型/接口一致性等全部语义规则都未落地。shadowing 规则的缺失只是规范完备性缺口的普通一员，并不具备单独拔高严重度的理由。

### 47. 🟡 [中] 内存模型仅有值/引用二分和 weak 关键字，缺少 use-after-cleanup / 循环检测 / move 语义  
**类别**: 安全性 | **置信度**: 88%

**问题描述**  
14-memory-management.md:115-135 宣称编译期支持 Bounds checking / Null safety / Use after free prevention，但符号系统、Binder、Checker 均没有任何对应建模。`weak` 修饰符仅在 spec 示例中出现，AST 里没有 WeakModifier 节点；也没有任何「释放」语义的抽象可供 use-after-cleanup 检查挂接。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/14-memory-management.md:119 — `// Bounds checking
let value = array[10]; // Compile error: index out of bounds

// Null safety
let optional: str? = getString();
let length = optional.length; // Compile error: optional not unwrapped`
    spec 列举了三种编译期保证。
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/symbol-flags.h:62 — `Unsafe = 1ULL << 62,        // Unsafe operations`
    Unsafe flag 已存在，但没有任何语句/表达式可以挂上它；没有 `unsafe {}` 块的语法 (05-statements 与 EBNF 均未列出)。
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/14-memory-management.md:63 — `class Child {
    weak parent: Parent?, // Weak reference`
    `weak` 在 spec 中出现为字段修饰符，但 parser / AST 的 ParameterDeclaration / PropertyDeclaration 修饰符列表中没有 Weak。
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/14-memory-management.md:85 — `let ptr = allocate<i32>(1000);
...
deallocate(ptr);
...
resource.cleanup();
resource.use(); // Compile error: use after cleanup`
    spec 第 132-133 行要求的 use-after-cleanup 检查需要线性类型 / borrow 语义支撑，但当前系统里没有任何借用/移动/生命周期的表示。这是 spec 超前于实现架构的典型例子，若强行按 ARC+值模型去实现会严重失真。

**潜在影响**  
内存管理章节目前只能作为产品愿景。真正开发后端时，这部分的设计取舍会决定语言复杂度 (Rust 式 borrow vs. Swift 式 ARC+Lint vs. GC)。当前阶段没有任何机制防止空可选解引用、数组越界、循环引用泄漏。

**修复建议**  
先做最低可落地的子集合：(1) Null safety (通过 Checker 的联合类型规范化 + 可选链语法检查即可实现，无需 borrow)；(2) Array bounds checking (静态可判定的字面量索引，其余退化到运行时检查)；(3) 重新评估 use-after-cleanup：若不做 borrow checker，就降级为命名约定 + 静态 lint (cleanup 后显式 move 变量到 dead state)，不要在 spec 里写能误导用户的承诺。
**评审备注**
- 确认方: 
## 独立验证结果（全部交叉确认）

### A. weak 修饰符的缺失 — 4 层证据

| 层面 | 状态 | 证据 |
|---|---|---|
| 词法层（token） | ❌ 缺失 | `kinds.h` 中无 `WeakKeyword`；`token.h` 无 Weak 相关定义 |
| 保留字列表 | ❌ 缺失 | `02-lexical-structure.md` 全文 grep weak 返回 0 次 |
| 语法参考 EBNF | ❌ 缺失 | `17-grammar-reference.md` 全文 grep weak 返回 0 次 |
| 修饰符识别 | ❌ 缺失 | `parser.cc:1055-1062` 的 `isModifier()` 支持 9 种：Abstract/Export/Public/Private/Protected/Static/Readonly/Mutating/Override，无 Weak |
| AST 节点 | ❌ 缺失 | 全局搜索 WeakModifier 零命中；修饰符以 `Vector<SyntaxKind>` 存储，而 SyntaxKind 没有 Weak |
| 符号层 | ❌ 缺失 | `symbol-flags.h` 有 63 种标志，但零种表示 Weak/弱引用 |

结论：`weak` 只在 14-memory-management.md 的两段示例代码（第 67 行 `weak this`、第 73 行 `weak parent: Parent?`）中出现，**从词法到语法到语义到符号，整条实现链上没有任何建模**。

### B. 三种编译期内存安全保证的实现挂点 — 3 层证据

**(1) Bounds checking / Null safety**
- 挂点路径上没有架构性障碍：Parser 已产出 `ElementAccessExpression` 和 `PropertyAccessExpression`，Binder 已绑定作用域；只需在 Checker 阶段对类型做判断即可。**当前唯一障碍是 Checker 为空（见下）。**

**(2) Use-after-cleanup**
- `kinds.h` 中搜索 allocate/deallocate/cleanup/borrow/move/lifetime/ownership 全部 0 次命中。**连语法节点都不存在**，无法在 AST 层面表达 `resource.cleanup()` 之后 `resource.use()` 的"死亡状态"转换。
- 除非引入 borrow checker 式的数据流分析，或至少给 SymbolFlags 增加一个 "Consumed/MovedFrom" 位，否则 spec 第 132-133 行承诺的编译期报错没有挂载位置。

### C. Unsafe flag vs. unsafe 语法

- `symbol-flags.h:155` 定义了 `Unsafe = 1ULL << 62`。
- 但 `05-statements.md` / `17-grammar-reference.md` / `02-lexical-structure.md` 三处搜索 unsafe 全部 0 次命中。
- Parser 搜索 Unsafe 0 次命中。**确实是"flag 存在，但没有任何语法路径能把它挂到符号上"。**

### D. Checker / 语义检查阶段状态

- `checker.cc:26` 是**完全空的命名空间** `namespace checker {}`，无一行实现。
- `driver.cc` 编译管道只有两步：`parseSources()` → `bindSources()`，**无 checkSources/typeCheck 调用**。
- 结论：Null safety、Bounds checking 即使不需要特殊架构，**也因为 Checker 阶段完全未启动而不能落地**。这是更大的上下文 — 整个语义阶段还没开工。

### E. 严重度评估微调的理由

原严重度 **medium** 维持不变：
1. **不调高到 high**：项目在设计目标（安全优先）和开发阶段（前端语法/绑定完成，语义即将启动）之间存在"规格先行"是正常现象。Null safety 和静态 bounds checking 两项都能在 Checker 启动后以较低成本补上，不会引发架构返工。
2. **不调低到 low**：use-after-cleanup 确实触及 ARC vs borrow checker 的根本性路线选择；`weak` 未列入保留字会造成未来破坏性变更；Unsafe flag 超前定义也会误导后续实现者。三项合起来构成真实且需要处理的设计债务。

### 关键文件路径

- 内存管理规范（问题发源地）：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/14-memory-management.md`（第 63-78 行 weak、第 115-135 行三项安全保证）
- 修饰符识别（9 种已支持的完整列表）：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc:1055-1062`
- 符号标志（含 Unsafe）：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/symbol-flags.h:155`
- 词法保留字（无 weak）：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/02-lexical-structure.md`（全文 0 次命中）
- Checker 空壳：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/checker/checker.cc:26`
- Driver 管道（仅 parse+bind）：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/driver/driver.cc:114-143`
- AST 节点种类（无 WeakKeyword、无 allocate/deallocate 等）：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/kinds.h`

- 反对方: 该问题有相当部分可以被证伪或降级，核心反驳点如下：

1. **Checker 为空是全项目的阶段问题，并非内存模型特有。** checker.h 中 TypeChecker 类完全被注释，checker.cc 为空命名空间，driver 管线也没有 checkSources() 调用——这意味着类型推断、运算符合法性、接口一致性检查、模式穷举性、泛型约束求解等 ALL 语义特性都未实现。单独把内存安全三件套（bounds/null/use-after-cleanup）的缺失拎出来定性为「设计问题」是选择性举证。

2. **null safety 的前置建模实际上已经存在。** AST 层有 OptionalTypeNode（T?）、NonNullExpression（后缀 !!）、NullLiteral、optional chaining `?.`、null coalescing `??`；binder 也已经有 visit(NonNullExpression) / visit(OptionalTypeNode) 等路径。这些正是 null safety 检查所需的全部语法糖载体，只是 Checker 还没写判空规则。断言「没有任何对应建模」不准确。

3. **「use-after-cleanup 需要线性类型 / borrow 语义」是错误判断。** 类似 Swift 的「流敏感 definite initialization + consumed state tracking」完全可以在 ARC+值模型上实现 `resource.cleanup(); resource.use()` 的检查，无需 borrow checker。原问题此处把 Rust 设计范式当作唯一可行路径，属于范式误读。

4. **allocate<T>() / deallocate() 是标准库函数，不应是关键字。** 它们不需要在 parser/kinds.h 中以关键词形式出现（类似 Swift 的 `UnsafeMutableRawPointer.allocate(byteCount:alignment:)` 是库 API）。因此没有 AllocateKeyword 并不能作为「释放语义无抽象」的证据。

5. **「缺少 move 语义、循环检测」在 ARC 模型里不是缺陷而是设计取舍。** Swift（ARC + 值类型）没有显式 move，也没有编译期引用环检测，只靠 weak/unowned 打破环——这恰恰是 spec 14 章的模型（ARC + weak）。用 Rust 的标准去评判是错位。

6. **Unsafe flag 不一定面向用户语法。** `Unsafe = 1ULL << 62` 可能预留给编译器内置的不安全操作标记（如内建指针运算 intrinsic），不一定对应 `unsafe {}` 块语法。将其解读为「必须有用户入口」是臆测。

### 48. 🟡 [中] 值类型 vs 引用类型的划分明确，但拷贝/移动/赋值语义细节缺失  
**类别**: 语义与组合性 | **置信度**: 85%

**问题描述**  
14-memory-management.md 明确 struct = 值类型（栈分配 + 拷贝语义）、class = 引用类型（ARC）。但以下关键细节未说明：(1) 值类型的赋值是深拷贝还是浅拷贝（对含有引用类型字段的 struct）；(2) 赋值操作的可重载性；(3) 参数传递是否与赋值语义一致；(4) 值类型是否支持隐式 move（Rust 式所有权转移）还是始终 Copy；(5) 大结构体的返回值优化（NRVO）是规范保证还是 QoI；(6) = 在 class 上是引用赋值还是值拷贝（C# 风格 vs C++ 风格）。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/14-memory-management.md:15 — `let p = Point(1.0, 2.0); // Allocated on stack / return p; // Copied to caller`
    声明了返回值拷贝，但未说明赋值 `let p2 = p` 的语义，也未说明含引用字段时的拷贝深度
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/14-memory-management.md:50 — `let shared = resource; // Reference count = 2`
    此例说明对 class 的赋值是共享引用（RC +1）。但未说明对 struct 的赋值如果含有 class 字段时的行为：整体拷贝还是字段级 RC +1？
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/03-types.md:11 — `Nominal: Types are distinguished by name, not just structure`
    名义类型意味着没有匿名 struct 兼容，但未说明相同 struct 名称下的赋值一致性检查如何在各场景下落地。

**潜在影响**  
语义模糊将导致 ABI 不稳定和跨编译器差异：特别是含引用类型字段的值类型拷贝、大值类型的 move elision 等，用户无法预测性能特征。

**修复建议**  
在 14-memory-management.md 中补充：(1) 定义 Copy 语义（POD 类型按位拷贝、含引用字段的 struct 执行逐字段 clone 并为引用类型字段 RC+1）；(2) 定义赋值与参数传递语义一致；(3) 明确 class 的 `=` 始终是引用重新绑定（RC 增减）；(4) 标记 NRVO/NRVO-move 为 QoI，但推荐的最小语义是单对象构造 + 零拷贝；(5) 可选：预留 `move` 关键字或 `@nodiscard` 属性，为未来所有权演进留出空间。
**评审备注**
- 确认方: ## 独立核验清单（6 子项逐项结论）

| # | 子问题 | 核验结论 | 原始证据对应 |
|---|---|---|---|
| (1) | 含引用字段的 struct 是深拷贝还是浅拷贝 | **完全缺失**。全 spec 无任何涉及「struct 内部包含 class/ARC 字段时的拷贝行为」的文本、示例或规则。 | 14:15-16 仅演示纯 POD struct 的栈分配+拷贝 |
| (2) | 赋值 (=) 操作符是否可重载 | **无语法入口，但更偏向特性未纳入**。17 章 EBNF 无 operator overloading 产生式；全 spec grep "operator/overload" 零命中。更像有意不提供（Swift 风格）而非语义模糊。 | — |
| (3) | 参数传递与赋值语义是否一致 | **部分说明 + 关键缺口**：08:147 对 struct 绑定了两者；但对 class，仅在 14:50 展示初始化（let）场景的 RC+1，**未描述赋值（a = b，覆盖旧值）场景下旧值 RC 是否递减**；14:52 `processResource(shared)` 也未说明函数传参时 RC 是否再次+1。 | 08:147 覆盖 struct；class 场景缺规范 |
| (4) | 值类型是否支持隐式 move vs 始终 Copy | **完全缺失**。spec 一律用 "copy/copied" 描述 struct 语义，没有任何 "move/transfer/ownership" 词汇；无 Copy/Clone trait 机制；无 move-only 类型概念。 | 08:147 + 14:16 都写 "copied" |
| (5) | NRVO 是规范保证还是 QoI | **完全缺失**。grep "NRVO/RVO/copy elision/return optim" 全 spec 零命中。14:16 `return p; // Copied to caller` 给出了最低语义（拷贝），但完全没有讨论「拷贝消除」的规范性地位。 | — |
| (6) | class 的 `=` 是引用重新绑定还是值拷贝 | **有示例 + 缺规范陈述**。14:50 和 14:27 的两个示例强烈暗示 C# 风格的引用绑定；但「赋值给已存在变量时，旧值 RC 递减 + 新值 RC 递增」这一闭环规则从未被写出。 | 14:50 / 14:27 仅为示例 |

## 额外发现

**(a) 子问题(1)的严重性被低估**。该缺口不仅是语义模糊——在 ARC 模型下，如果 struct 拷贝采用「按位复制」而非「逐字段 clone+RC+1」，会导致引用计数丢失（同一对象被两个 struct 各自持有，但 RC 未递增），最终引发 use-after-free。这不是性能特征不可预测的问题，而是**内存安全漏洞**的潜在来源。两个实现团队若分别选择「按位拷贝」vs「逐字段 clone」，ABI 完全不兼容且一方存在内存安全隐患。

**(b) 第(4)子项（move vs always copy）间接影响子问题(5)。** 如果 ZOM 选择「struct 始终 Copy」模型（类似 C# struct），NRVO 就是纯 QoI；如果支持 move 语义（类似 Rust），则 "guaranteed copy elision / NRVO" 可能成为规范保证。两者选择差异会直接传递给 (5) 的答案——这意味着 (4) 和 (5) 是联动的，不能独立制定。

**(c) 还存在一个原问题未提及的关联缺口：struct 默认相等性 (`==`) 的语义**——是按位相等还是逐字段相等？对含引用字段的 struct 来说，这与拷贝语义耦合（共享引用 vs 独立克隆会产生不同的相等结果）。04-expressions.md:233-244 只罗列了比较运算符，未给任何语义定义。

## 文件路径（独立证据）

- **08 章 struct 拷贝/传参声明**：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/08-classes-and-structures.md:147`
- **14 章核心内存模型（含 3 段关键示例）**：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/14-memory-management.md:14-17, 48-57, 25-29`
- **04 章赋值表达式**：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/04-expressions.md:307-336`（仅有语法示例，无语义规则）
- **04 章比较运算符**：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/04-expressions.md:233-244`（罗列运算符，无语义）
- **17 章 EBNF 语法参考**：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md`（grep 确认无 operator overloading / move / copy 相关产生式）
- **16 章属性（完全保留给未来）**：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/16-attributes-and-annotations.md:1-10` —— 原建议中提到的 `@nodiscard` 属性依赖的章节尚未展开

## 建议修订

将严重度从 **high** 调整为 **medium**。理由：
1. 6 子项中 (2) 更像设计取舍、(3)(6) 有部分说明，并非全部"完全缺失"。
2. 对处于 parser/binder 完成、checker 尚未启动阶段的项目而言，这种规范不完备度在预期范围内。
3. Swift/Kotlin/Java 等 ARC 语言的先例，使得合理的默认收敛路径较清晰——多数实现者会自然选择「逐字段 clone + RC+1」「赋值与传参语义一致」「NRVO 为 QoI」。
4. 但子项(1) 含引用字段 struct 的拷贝深度 + 子项(4) move vs copy 决策，仍属于**高优先级的 medium**——应在 checker 实现前确定，否则代码中写入的临时决策会成为事实上的规范。
- 反对方: 候选问题把 6 个子点并列当作"同样缺失的细节"，但其中 4 点要么已被现有规范直接/间接回答，要么属于把"未定义未来功能"误写为"文档缺失"，具体如下：

## (3) 参数传递与赋值语义一致性 —— 规范明确回答
`docs/spec/chapters/08-classes-and-structures.md:147` 原文 **"Structures are value types that are copied when assigned or passed as parameters."**
这句话把赋值与传参在同一句里并列声明，语义一致是显式保证，不是未说明。

## (4) 值类型是否支持 Rust 式 move —— 规范明确回答为"不支持（始终 Copy）"
- `08-classes-and-structures.md:147` 只写了 copy，没有任何 move / ownership transfer 字眼。
- `14-memory-management.md` 全篇没有 borrow / ownership / `&T` / move 语义，内存模型是 **ARC + 值拷贝** 的 Swift 风格。
- AST 层也没有 `Ref` / `MutRef` / `Lifetime` / `move` 节点（Parser 对照总结已确认）。
因此"是隐式 move 还是始终 Copy"并不含糊——后者是唯一答案。VAL-01 把这点列进"缺失细节"，属于把"不存在的能力"当成了"未说明的分支"。

## (6) `=` 在 class 上是引用赋值还是值拷贝 —— 规范明确回答为 C# 式引用赋值
- `14-memory-management.md:49-50` 的 **`let resource = Resource("test"); // RC = 1` 与紧接一行 `let shared = resource; // Reference count = 2`**，是对"class 赋值 = 共享引用 + RC +1"的教科书式说明。
- `04-expressions.md:238-239` 另给了专用 `===` / `!==` reference-equality 运算符，与 Swift / C# 完全一致，进一步印证 class 上 `=` 不涉及对象内容拷贝。
VAL-01 却仍把 (6) 标为"未说明"，这是直接忽略了第 50 行最关键的证据。

## (2) 赋值操作的可重载性 —— 不是缺失，是"当前设计中不存在"
- 全规范没有 `operator` 关键字、没有 `Assign`/`Copyable`/`Clone`/`Deref`/`Index` 等 trait、没有 Kotlin 风格 `fun plus()` 运算符约定、没有独立 `impl` 块（语法里只有 `implements Interface` 路径）。
- 当前唯一暴露的接口示例是 `Comparable<T>`（基于命名方法 `compareTo`，不是运算符重载）。
- EBNF 中 `AssignmentOperator ::= '=' | ...` 是语法层级，完全没有"用户自定义运算符"分支。
因此"赋值可不可重载"的答案当前就是 **不可重载（设计尚未引入运算符重载机制整体）**，而不是"细节缺失"。把它列成语义缺陷，等于在规范未进入"运算符重载设计"阶段之前就强要求文档给出可重载性矩阵。

## 对原证据的进一步反驳
- 引用 `14-memory-management.md:15` 的注释 "Copied to caller" 是 return-value 语义，然后 VAL-01 追问 `let p2 = p`——但同一章 (147 行) 已把"assigned or passed as parameters"并列说明，赋值和返回值同属"值拷贝语义"，逻辑上直接推出 `let p2 = p` 就是拷贝。不能用一条证据的上下文分离来制造缺失感。
- 引用 `03-types.md:11` "Nominal: Types are distinguished by name, not just structure"，然后追问"相同 struct 名称下的赋值一致性检查在各场景如何落地"——这是把名义类型的**名称同一性判定**（作用域解析 + 模块路径 + 泛型实参匹配）硬挪进"拷贝/赋值语义"话题。实际上名义类型只要求"同一声明"即可，与 (1)(2)(3)(4)(5)(6) 的赋值-拷贝矩阵不在同一层；如果这也算"语义细节缺失"，那么类型系统里几乎每一条都可以用同样话术扩展成"未说明各场景落地"。

## 残留的真实缺口（非"整体为伪"，但被 VAL-01 过度放大）
1. **(1) 含 class 字段的 struct 赋值——没有现成示例**：规范没写过"含引用字段时逐字段 RC +1，其余字段 bitwise 拷贝"这种句子。但在 ARC + 值拷贝的组合模型下，它是组合逻辑的必然推论（struct 语义是逐字段拷贝 + 每个字段按自身类型语义处理），更像"缺乏显式示例"而非"语义未定义"。
2. **(5) NRVO 是规范保证还是 QoI**：`14-memory-management.md:16` "// Copied to caller" 给出的是抽象机语义，没有引入"as-if"模型或"copy elision 必需"声明——但当前 V1 规范连抽象机 / 执行模型章节都没有（SPEC TOC 也无对应项），NRVO 作为执行模型的子集，在这一级别谈"规范保证 vs QoI"是提前量过大的要求，与第 15 章并发、第 16 章属性同样是"未来层"。

## 严重度下调理由
- 原始 6 点中 4 点被证伪或误分类，剩余 2 点中 1 点可由现有规则组合推出、1 点属于执行模型层面的超前要求。
- 真正的规范空白只有"嵌套类型复合拷贝"缺一个显式示例，但不会影响用户理解核心的值/引用划分，因此严重度应从 **high** 下调为 **medium**（中等：确有文档补强空间，但不构成核心语义歧义）。

### 49. 🟡 [中] 可选链的短路范围未被规范定义，存在整条链 vs 单段的语义歧义  
**类别**: 语义与组合性 | **置信度**: 85%

**问题描述**  
可选链 `?.` 与点/下标/调用并列（EBNF 的 OptionalChain 中 `?. (Identifier | '[' Expression ']' | Arguments)`，后跟零或多个普通链），但 EBNF 同时允许 OptionalChain 后接任意多的普通链段（Arguments/[exp]/.Id），再加上 OptionalChain 本身可以出现多次（`OptionalChain+`）。这导致对 `a?.b.c?.d` 这样的长链，"短路范围"未在规范中给出语义说明：首个 `?.` 判断 a 为 null 后，后续的 `.b`、`.c`、`?.d` 是否全部短路为 null？还是仅单个 OptionalChain 段短路？这是典型的语义级歧义，虽然语法覆盖了，但缺少短路语义会让读者对 `a?.b.c` 在 `a == null` 时是否访问 `.b` 和 `.c` 争论不休（对比 TS/JS 的整条链短路 vs Swift 的单个段短路语义不同）。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:337 — `OptionalExpression ::= (MemberExpression | CallExpression) OptionalChain+; OptionalChain ::= '?.' (Identifier | '[' Expression ']' | Arguments) (Arguments | '[' Expression ']' | '.' Identifier)*`
    单个 OptionalChain 允许后接多个普通 chain 段
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/04-expressions.md:138 — `let street = user?.address?.street;`
    示例展示了两次 ?. 但未说明短路范围

**潜在影响**  
人类对 `a?.b.c?.d` 的行为无法从规范中得到结论；如果实现把短路范围限制到单个 OptionalChain，则 `a?.b.c` 在 a 为 null 时仍会因后续 `.b` 崩溃，与用户直觉相反。

**修复建议**  
在 04-expressions.md 的可选链章节明确写出短路规则：一旦首个 ?. 的前置对象为 null，整个 OptionalExpression 的所有后续段全部短路为 null（与 TS 语义一致）；或用语法强制所有 chain 段均为 ?. 形式。
**评审备注**
- 确认方: 问题确实成立，但需要区分两个层次：

【规范层面的真实性 — 真实存在】
- 证据 A（规范沉默）：docs/spec/chapters/04-expressions.md 第 134–148 行的「Optional Chaining」小节只给出了 6 行示例代码（user?.address?.street 等），完全没有文字说明「短路范围」「求值顺序」「a==null 时后续段是否求值」。这是问题描述直接引用的，我独立核实无误。
- 证据 B（EBNF 歧义）：docs/spec/chapters/17-grammar-reference.md 第 337–339 行
    OptionalExpression ::= (MemberExpression | CallExpression) OptionalChain+
    OptionalChain    ::= '?.' (Identifier | '[' Expression ']' | Arguments)
                       (Arguments | '[' Expression ']' | '.' Identifier)*
  这条文法确实把「单个 ?. 后的普通 chain 段后缀 (Arguments | '[' ']' | '.' Id)*」与「后面再跟的另一个 OptionalChain（即另一个 ?.）」并列，两者之间没有显式的优先级/分组边界说明。对 a?.b.c?.d 这样的输入，既可以读成 ((a?.b).c)?.d（三个独立段各管各，即 Swift 式单个段短路），也可以读成整个 OptionalExpression 一旦在 a 处 null 就整条短路（TS 式）。规范没有给出消除歧义的语义规则。

【实现层面 — 有确定行为，不是未知】
- 证据 C（parser 明确实现了「整条链短路」）：
  * factory.cc:306/317/330 在 isOptionalChain=true 时给 CallExpression/PropertyAccess/ElementAccess 设置 NodeFlags::OptionalChain 位。
  * parser.cc:4407/4453/2794-2795 三个关键位置都使用相同模式：
      const bool isOptionalChain = questionDot || tryReparseOptionalChain(*expression);
    即只要左操作数（expression）已经在一个可选链中（flags 含 OptionalChain 或经过 NonNull 包装后能找到 OptionalChain 标记的左边界），新加入的这一段无论本身是 .Id / [expr] / (args)，都会被自动标记为 OptionalChain=true。这就是 TypeScript 语义的「整条链短路」。
  * 配套的 findOptionalChainBoundary（162-166 行）和 markNonNullOptionalChain（168-175 行）处理了 a?.b!!.c 这种 NonNull 包裹后仍要保留链传播的情形。
- 证据 D（AST dump 印证）：tests/language/expressions/calls-members-new-optional.zom 第 5 行
    let z = obj?.method(1)[0].field;
  期望 AST 中最外层 PropertyAccessExpression(field) 虽然自身 questionDot=false（CHECK 168 行），但由实现传递规则可知其 isOptionalChain 标志必然是 true（因为内层的 CallExpression 已经设为 OptionalChain 链的起点）。这验证了「? 之后的 [0]、.field 都同属一个短路范围」。

【结论】
- 问题真实，但不是「规范+实现都没确定所以行为未定义」，而是「规范文档缺少语义描述，但实现已经做了明确选择（TS 式整条链短路）」。
- 因此原严重度 medium 我认为可以维持甚至下调（因为实现已经锁死了语义，不会有实现冲突），但规范文档的缺失仍是真实问题——当另一个实现者（比如写解释器后端 / 代码生成器）只看规范不看 parser 时，很可能实现出错误的单段短路语义。建议保留 medium，标注为「文档缺失类」而非「实现未定类」。

影响范围：仅 docs；不涉及语法/parser 变更。
相关文件：
- /Users/bytedance/Develop/ZOM/docs/spec/chapters/04-expressions.md:134-148（缺语义说明）
- /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:337-339（EBNF 分组边界未用注释说明）
- /Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc:162-175, 2775-2800, 3964-4015, 4400-4457（实际语义：整条链传播 OptionalChain 标志）
- /Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/factory.cc:300-334（AST 标记方式）
- /Users/bytedance/Develop/ZOM/products/zomlang/tests/language/expressions/calls-members-new-optional.zom:5,149,162,168（行为测试，但未覆盖 isOptionalChain 标志传播的断言）
- 反对方: 反证 1 — 问题的对比基础存在事实错误。问题声称"对比 TS/JS 的整条链短路 vs Swift 的单个段短路语义不同"，但实际上 Swift 的可选链（`a?.b.c`）同样是整条链短路：当 `a == nil` 时 `.b` 和 `.c` 均不执行，结果为 `nil`。TS/JS、Swift、Kotlin、C# 6.0+、Dart 等所有主流可选链实现都是整条链短路，这是事实上的行业标准。问题陈述中"Swift 单个段短路"的前提不成立，因此对 ZOM 歧义的担心被过度放大。

反证 2 — 解析器实现已经明确定义了语义，不存在实际歧义。`parser.cc` 中 `parsePropertyAccessExpressionRest` 第 4407 行：`const bool isOptionalChain = questionDot || tryReparseOptionalChain(*expression);`。`tryReparseOptionalChain` 会检查左操作数是否带有 `NodeFlags::OptionalChain` 标志，并通过 `markNonNullOptionalChain` 递归传播标志。其直接后果是：一旦链中任何一段引入了可选链标志，后续所有 `.x` / `[x]` / `()` 段的 `isOptionalChain` 都为 true。对 `a?.b.c?.d` 的完整展开：
- `.b`：questionDot=true → isOptionalChain=true
- `.c`：questionDot=false，左操作数 `.b` 带 OptionalChain 标志 → tryReparseOptionalChain 返回 true → isOptionalChain=true
- `?.d`：questionDot=true，左操作数 `.c` 带 OptionalChain 标志 → isOptionalChain=true
AST 上三个节点全部被纳入同一短路域。语义由实现唯一确定：整条链短路。

反证 3 — EBNF 结构与 TypeScript 规范完全同构。TS 规范中 OptionalChain 的定义也是 `?.` 前缀段后跟零或多个普通链段，允许多个 OptionalChain 段连续。采用相同语法骨架 + 相同 AST 实现方式，在没有任何显式反证的前提下，读者默认应理解为与 TS 相同的整条链短路语义。语法结构本身并不承载"独立段"的暗示。

反证 4 — 问题引用的示例 `user?.address?.street` 等在两种语义下结果完全一致（每段都单独带 `?.`），因此"读者对示例会争论不休"的实际场景不存在。只有在 `a?.b.c` 这种"可选引入后接纯普通链"时才可能有疑问，但这种用法在工程中并不常见，且解析器已给出确定答案。

反证 5 — 严重度分级不当。该问题本质是"语义章节缺少显式短路范围文字说明"，属于文档完善问题，而非"语义级歧义导致多种合法实现"。medium 级别的歧义意味着不同实现团队可能产出不兼容的实现，但本项目中（1）解析器已给出唯一确定语义；（2）业界惯例也指向唯一语义——因此不可能产生不兼容实现。严重度应为 low 或 info。

### 50. 🟢 [低] interface 可以使用 implements 继承，但规范只允许 extends  
**类别**: 规范-实现不一致 | **置信度**: 98%

**问题描述**  
parser 的 heritage 机制对 class/interface/struct 通用，因此 interface I1 implements I2 可被接受，但 spec 的 InterfaceHeritage 只给了 extends。

**证据**
  - docs/spec/chapters/09-interfaces.md:53 — `Interface inheritance uses extends only`
    规范只声明了 extends
  - products/zomlang/compiler/parser/parser.cc:4350 — `parseHeritageClause accepts both Extends and Implements tokens regardless of declaration kind`
    parser heritage 机制对所有声明通用，导致 interface 也能使用 implements

**潜在影响**  
轻微的语法蔓延：用户可以写出 interface I implements J，虽语义接近 extends，但与规范不一致。

**修复建议**  
在 parser 中对 InterfaceDeclaration 的 heritage 子句限定仅允许 extends，或在规范中明确 interface 也可 implements（含义为 trait 实现）。
**评审备注**
- 确认方: 独立核验结论 —— 候选问题 PAR-SPC-008 属实。

## 核验证据（独立于原证据）

**规范侧（仅允许 extends）：**
- `docs/spec/chapters/17-grammar-reference.md:134-135`
  ```
  InterfaceDeclaration ::= 'interface' BindingIdentifier TypeParameters? InterfaceHeritage? '{' InterfaceBody '}'
  InterfaceHeritage     ::= 'extends' InterfaceTypeList
  ```
  InterfaceHeritage 规则只接受一个字面词 `extends`，没有备选。
- `docs/spec/chapters/17-grammar-reference.md:131-132`（用于 class/struct）
  ```
  HeritageClause ::= ('extends' | 'implements') ExpressionWithTypeArguments (',' ExpressionWithTypeArguments)*
  ```
  显然 class/struct 用 HeritageClause（含 implements），interface 用 InterfaceHeritage（仅 extends）——两条独立语法规则。
- `docs/spec/chapters/09-interfaces.md:91` 示例：`interface ReadWriteStream extends ReadableStream, WritableStream { ... }`，规范所有接口继承示例均使用 extends，没有一处出现 interface implements。
- 对照 ANTLR 文法 `docs/spec/ZomParser.g4` 中也没有 interface 与 implements 的组合。

**实现侧（parser 同时接受 extends 和 implements）：**
- `products/zomlang/compiler/parser/parser.cc:2284`（parseInterfaceDeclaration）：
  ```cpp
  zc::Maybe<zc::Vector<zc::Own<ast::HeritageClause>>> heritageClauses = parseHeritageClauses();
  ```
  直接调用了和 class（2262 行）、struct（2302 行）**完全相同**的 parseHeritageClauses()。
- `products/zomlang/compiler/parser/parser.cc:4802-4813`（parseHeritageClause）：
  ```cpp
  ast::SyntaxKind token = currentKind();   // 同时接受 ExtendsKeyword 或 ImplementsKeyword
  nextToken();
  ...
  return finishNode(ast::factory::createHeritageClause(token, zc::mv(types)), loc);
  ```
  两个关键字处理完全对称，**没有任何根据声明种类的分支**。
- `products/zomlang/compiler/parser/parser.cc:1299-1309`（isHeritageClause / isHeritageClauseExtendsOrImplementsKeyword）：两种关键字都被视作 heritage clause 起始。

**测试侧（缺少对该不一致的负面用例）：**
- parser-test.cc 的两个 `ParseInterfaceDeclaration` 用例（874 行、2834 行）都只测试了最简 `interface X { ... }` 无继承形式，未覆盖继承，更未覆盖 interface+implements 的报错。
- lit `language/interfaces/interface-inheritance.zom` 只测试 extends 的正面用例；`errors/` 下 8 个 interface-invalid-*.zom 也均**没有** `interface I implements J` 的诊断用例。

## 后果分析
- 诊断后果：`interface I1 implements I2 { }` 会无报错地生成一个带 `token=ImplementsKeyword` 的 HeritageClause。
- 语义后果：当前 checker/sema 阶段基本为空（见类型系统评估），所以不会产生二次检查。等后续接入接口一致性检查时，若只按 `extends` 处理 heritage，要么 silently 把 implements 当 extends（语义漂移），要么需要额外区分。
- 与其它 spec-impl-mismatch 的关系：parser 层面通用的 heritage 实现同时也放过了"class/struct 既 extends 多个基类"这类更严重的语法问题，但本问题只针对 interface，范围收敛。

## 严重度评估：low（原评估合理）
- **不影响正确程序：** 不会让符合规范的程序编译失败。
- **影响范围小：** 只涉及 interface 继承关键字选择的一致性。
- **无语义破坏：** 当前 sema 未启用，即使误接受 implements 也不会产生后续错误类型推导；未来修复成本低（在 parseInterfaceDeclaration 中对 heritage 种类做检查 + 发射诊断，或改用独立 parseInterfaceHeritage）。
- **建议方向** 与原建议一致：优先在 parser 中加限定，成本远低于修改规范把 interface implements 升格为正式特性。无需改动 EBNF。

## 补充
- 候选问题的原证据 "09-interfaces.md:53 Interface inheritance uses extends only" 的行号引用不准确（实际是类示例代码块结尾）。真正权威的出处是 `17-grammar-reference.md:134-135` 的 EBNF 文法定义。建议在 issue 中修正引用。

相关文件：
- `docs/spec/chapters/17-grammar-reference.md` 第 131-136 行
- `docs/spec/chapters/09-interfaces.md` 第 77-95 行（Interface Inheritance 节）
- `products/zomlang/compiler/parser/parser.cc` 第 2275-2297 行、第 4793-4813 行、第 1299-1309 行
- `products/zomlang/tests/language/interfaces/interface-inheritance.zom`
- `products/zomlang/tests/unittests/compiler/parser/parser-test.cc` 第 874 行、第 2834 行
- 反对方: 作为怀疑者，我尝试了以下反驳路径均未能证伪：

1. "也许规范本意就是允许 interface 用 implements，只是文档没写全？" — 三份独立规范源联合证伪：
   - EBNF（17-grammar-reference.md:134-135）明确定义 InterfaceDeclaration 使用独立的 InterfaceHeritage 产生式（而非通用 HeritageClauses），且 InterfaceHeritage 仅允许 'extends'
   - ANTLR 文法（ZomParser.g4:736）定义 interfaceHeritage: EXTENDS interfaceTypeList — 仅 EXTENDS
   - 如果设计者有意允许 implements，会直接复用 HeritageClauses 而非新建一个受限的 InterfaceHeritage

2. "也许这是 parser 层宽松解析、留给语义阶段再检查的策略？" — 与项目内其他不支持语法的处理方式不一致：namespace、declare、try/catch/finally、for-of、export default 等都在 parser 层直接产生诊断。诊断码定义文件中没有任何对应 "interface cannot use implements" 的错误码。ANTLR 文法把此限制放在语法层，表明设计意图就是 parser 级拒绝。

3. "也许 implements 和 extends 在 interface 上下文中是同义词？" — 规范未给出任何语义说明；主流语言（Java/TypeScript/C#/Kotlin）均严格区分 interface-to-interface 用 extends，class-to-interface 用 implements，无混用语义先例。

4. 全项目测试 grep 搜索 "interface.*implements" 零命中，不存在 "该语法被测试批准但文档滞后" 的证据。

另外注意：struct 声明存在类似但方向相反的宽松度问题 — ANTLR 文法 structHeritage 仅 IMPLEMENTS，但 EBNF 和实际 parser 均允许 extends+implements 两种。

### 51. 🟢 [低] 前导联合/交叉类型 | T 和 & T 在 parser 中存在但规范未声明  
**类别**: 规范-实现不一致 | **置信度**: 98%

**问题描述**  
parser 的 parseUnionOrIntersectionType 通过 hasLeadingOperator 支持 | T、& T 这种单目前导运算符形式，但规范的类型文法中 Union/Intersection 都是双目，没有前导形式。

**证据**
  - docs/spec/chapters/17-grammar-reference.md:166 — `UnionType and IntersectionType defined as binary operators only`
    规范类型文法未声明前导运算符
  - products/zomlang/compiler/parser/parser.cc:1850 — `parseUnionOrIntersectionType handles leading operator (| or &) before first type`
    parser 已支持前导运算符，超前于规范

**潜在影响**  
轻微语法蔓延，可能产生 | A | B 或 & A & B 这种不直观的写法。

**修复建议**  
在规范中补充 leading operator 语法和语义（如 TS 风格的 discriminated union 辅助写法），或在 parser 禁用。
**评审备注**
- 确认方: ## 独立核实证据

### 1. Parser 端确实支持 leading operator（已实证）

parser.cc:3208-3245 parseUnionOrIntersectionType 函数骨架：
- 第 3213 行 parseOptional(operatorToken) 先尝试吃掉一个前导的 | 或 &
- 第 3223 行 if (expectToken(operatorToken) || hasLeadingOperator) 把「有前导运算符」也当作进入 union/intersection 构建循环的充分条件
- 即使循环只 add 一个元素，也会走 createUnionType/createIntersectionType

实测（build-debug/zomc --dump-ast 跑临时源文件）全部通过：
- alias X = | A | B;  ->  UnionTypeNode [A, B]
- alias Y = & A & B;  ->  IntersectionTypeNode [A, B]
- alias Z = | A;      ->  UnionTypeNode [A]  （单成员）

### 2. 规范的三个层面均未声明前导形式

(a) 最权威 EBNF（17-grammar-reference.md:167-168）：
  UnionType        ::= IntersectionType ('|' IntersectionType)*
  IntersectionType ::= PostfixType      ('&' PostfixType)*
  两个产生式第一个符号都是非终结符（IntersectionType / PostfixType），绝不可能以 | 或 & 打头，与 parser 实现直接冲突。

(b) 类型语义章节（03-types.md:89-128 的 Union Types / Intersection Types）：所有示例（str | i32、Named & Aged、str | i32 | bool）全为双目，整章无 |T / &T 写法，语义描述中也只讲「若干类型之一」「满足多个约束」，无前导语义说明。

(c) 全文搜索（含 ZomParser.g4 / 02/03/17 章）：「leading」出现在 parser 实现注释以外的地方为零命中；「前置」「单目 union/intersection」等同义词也为零。

### 3. 原报告所述「轻微语法蔓延」已被实证

| A 这种单成员前导写法被解析为一个只含一个元素的 UnionTypeNode，产生了毫无语义必要的包装节点。若后续 Checker 阶段不做联合/交叉的扁平化与去重规范化，会出现：子类型判定中 T <: T 与 T <: Union([T]) 等价性未明、联合去重规范化遗漏、match 穷举性检查出错等问题。

### 4. 对照原证据核验

- docs/spec/chapters/17-grammar-reference.md:166 段证据属实：Union/Intersection 仅双目，无前导。
- products/zomlang/compiler/parser/parser.cc:1850 行号略有偏差（原证据指向注释行），实际语法行为位于 3208-3245 段，核心结论正确。
- 影响评估属实：语法蔓延 + 不直观写法确实存在。

### 5. 严重度维持 low 的理由

- 属纯语法层面的 spec-impl 不一致，不会破坏任何已声明功能的正确性（只是多接受了一些未声明的写法）。
- AST 表示一致，语义阶段只要加一个规范化就能兜底，修复成本极低。
- 没有任何测试或示例依赖此语法，用户也尚未暴露。
- 反对方: 证伪尝试全部失败：
1. 功能有效性测试：用 zomc 实际编译 `let x: | i32 = 42;`、`let y: & A = z;`、`let x: | i32 | str = 42;`、`let x: | & A & B = 42;`、`alias X = | i32; alias Y = & A;` 全部无诊断错误，AST 正常产出 UnionTypeNode/IntersectionTypeNode，功能真实可用，非死代码。
2. 尾随 bar 副作用假说：leading operator 路径在 parser.cc:3213 `parseOptional(operatorToken)` 独立进入，与 trailing bar（A |; 触发 ZOM2069 Type expected）的错误恢复路径完全无关。
3. 规范隐含假设：EBNF 17:167-168 明确写 UnionType/IntersectionType 为双目形式（`IntersectionType ('|' IntersectionType)*`），03 章文本与示例全部双目；全规范 grep "leading/prefix" 无匹配。没有任何地方暗示前导形式。
4. 错误恢复假说：输出 AST 干净，无诊断，单元素 Union/Intersection 语义等价于裸类型，但这是 parser 的积极选择而非恢复。
5. 跨语言约定假说：规范 02:162-163 精神是"未列入文法即不支持"，不能推定用户了解 TypeScript 等其他语言的前导运算符约定。

### 52. 🟢 [低] typeof 运算符有两种入口（类型层 vs 表达式层），表达力和语义均不同  
**类别**: 语义与组合性 | **置信度**: 97%

**问题描述**  
`typeof` 运算符在同一语法中有两种入口：(A) 表达式级 UnaryExpression `'typeof' UnaryExpression`（运行时取值，返回一个值——推测是类型描述字符串或类型对象），Parser 中 parseTypeOfExpression 对应这一条；(B) 类型级 TypeQuery `'typeof' Identifier ('.' Identifier)*`（编译期取类型）。两者的关键字相同且都可以出现在变量声明右侧：`let x = typeof expr;` 与 `type T = typeof expr.field;`。在需要歧义消解的复杂位置（如 `let z: typeof a = typeof b;`）会让读代码的人瞬间难以判断每个 typeof 是哪一层。对编译器而言更糟：类型层的 typeof 语法故意限制为标识符点链，无法表达 `typeof (a + b)`；而表达式层 typeof 可接受任何 UnaryExpression，因此在 `typeof (x+1)` 语境下，若上下文是类型表达式，会直接报错，无法智能切换到表达式层。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:185 — `TypeQuery ::= 'typeof' TypeQueryExpression; TypeQueryExpression ::= Identifier ('.' Identifier)*`
    类型层 typeof 受限
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:305 — `UnaryExpression ::= ... | ('+' | '-' | '!' | '~' | 'typeof') UnaryExpression`
    表达式层 typeof 接受任意 UnaryExpression
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc:2707 — `auto typeofExpr = ast::factory::createTypeOfExpression(parseSimpleUnaryExpression());`
    表达式层实现存在

**潜在影响**  
同一关键字两种语义；类型层 typeof 的表达力受限（无法取复杂表达式类型），而表达式层 typeof 的返回类型未在 spec 中定义。

**修复建议**  
要么把类型层 typeof 扩展到任意表达式并与表达式层做关键字层面的区分（例如编译期取类型用 `type.typeof`，运行时用 `typeof`），要么取消表达式层 typeof，强制用户走类型查询函数。
**评审备注**
- 确认方: # 独立核验结论（确认者视角）

## 核验结果：真实问题，但严重度需下调

### 事实核验（全部确认属实）

**证据 1 — 语法规范两种入口**

- `/Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:185-186`：
  `TypeQuery ::= 'typeof' TypeQueryExpression; TypeQueryExpression ::= Identifier ('.' Identifier)*`
  —— 类型层 typeof，**仅接受标识符点链**。
- 同文件 `:305`：`UnaryExpression ::= ... | ('+' | '-' | '!' | '~' | 'typeof') UnaryExpression`
  —— 表达式层 typeof，**接受任意 UnaryExpression**，包括括号、函数调用等。
- 两条规范都在同一份 EBNF 中同时存在，属**规范层显式设计**，不是实现缺陷。

**证据 2 — 解析器实现对称一致**

- `parser.cc:520-563` `parseTypeQuery()` + `parseTypeQueryExpression()`：严格按 `Identifier ('.' Identifier)*` 解析，遇到非 Identifier 直接失败（`parseIdentifierName` 不接受括号），因此 `typeof (a + b)` 在类型层语境下**不可能回退到表达式层**，直接报错——原描述这一点完全准确。
- `parser.cc:2694-2709` `parseTypeOfExpression()`：调用 `parseSimpleUnaryExpression()`，支持任意一元表达式（含括号提升到更高优先级），确实能 parse `typeof (x + 1)`。
- `parser.cc:2663-2664`：`parseSimpleUnaryExpression` 分支里 `TypeOfKeyword → parseTypeOfExpression()`；`parser.cc:3308-3310`：`parseAtomType` 里 `TypeOfKeyword → parseTypeQuery()`。**两条路径是通过调用入口分层选择，parser 内部不会互相混淆**。

**证据 3 — AST 节点分属两个层次**

- 表达式节点：`/ast/expression.h:607` `class TypeOfExpression final : public UnaryExpression`，派生自 Expression。
- 类型节点：`/ast/type.h:430` `class TypeQueryNode final : public TypeNode`，派生自 TypeNode。
- 两者**无共同基类**，语义层必须走完全不同的类型推导路径。

**证据 4 — 测试与文档实例**

- `tests/language/types/type-query.zom:4`：`let y: typeof x = 20;` → 产出 `TypeQueryNode`。
- `tests/language/types/type-forms.zom:11`：`typeof foo.bar`（类型层点链）。
- `tests/language/expressions/unary-and-cast.zom:6`：`let d = typeof a;` → 产出 `TypeOfExpression`（由 FileCheck 第 103 行证实）。
- `04-expressions.md:208-218` 有 "Type Operators" 小节，写了 `let typeString = typeof myVariable;`（表达式级），但**没有说明返回类型**，只有变量名暗示是字符串；而 `03-types.md:229-242` 的 "Type Queries" 小节同样用 `typeof` 关键字演示类型查询。

### 语义差异对比

| 维度 | 类型层 `typeof` | 表达式层 `typeof` |
|---|---|---|
| 语法入口 | TypeQuery | UnaryExpression |
| 操作数 | Identifier ('.' Identifier)* | 任意 UnaryExpression |
| 结果 | TypeNode（编译期类型） | Expression（运行时值） |
| 返回值类型 | 作为类型注解使用 | 规范未定义（示例暗示 `str`） |
| 求值时机 | 编译期 | 运行时 |
| 典型位置 | `:` 后、`=` 后（type alias RHS） | `let x = ...` 的 RHS |

### 关于严重度：从 medium 降为 **low** 的理由

原严重度是 medium，独立评估后应下调为 low，理由：

1. **语法层面无实际歧义**。解析器是分层递归下降，类型语境和表达式语境走完全不同的 parser 入口函数，互相不可达。`let z: typeof a = typeof b;` 这种情况，第一个 `typeof a` 在 `TypeAnnotation` 内部（由 `':' TypeExpression` 启动），第二个在 `AssignmentExpression` 内部；二者在文法里根本不会相互干扰，编译器绝不会混淆——对编译器不是问题。

2. **对人类的可读性问题真实，但不是严重语义缺陷**。`typeof` 两义是 TypeScript 等语言中**长期存在且被广泛接受的现实设计**（TS 里同样在类型层和值层都用 `typeof`），属于"学过一次就再也不会搞错"的惯例，不算高优先级问题。ZOM 作为现代语言，虽然可以做得更好，但不构成阻塞性设计缺陷。

3. **表达力受限（不能 `typeof (a+b)`）在当前阶段不造成真实影响**：Checker 阶段还是空壳（`checker.cc` 整个命名空间为空），`TypeQueryNode` 的语义求解压根没有实现，所以"无法表达复杂表达式类型"不是现在用户能碰到的痛点；即便支持了，也需要先实现编译期表达式求值，这是大规模工作，不能仅靠在 `TypeQueryExpression` 里把范围放宽到 Expression 就解决。

4. **表达式级 typeof 返回类型未定义** 确实应该补文档，但属于**规范文档缺失**而非语义矛盾，严重度较低。

### 我自己的补充发现（原描述未提及）

- `04-expressions.md` 第 369 行运算符优先级表的 "Prefix" 条目里把 `typeof` 和 `+ - ! ~ ++ --` 并列，而 `++` / `--` 在 EBNF 中实际是独立的 `UpdateExpression` 类（高于 Prefix 的普通 unary），这里列表的写法让读者容易误以为优先级相同——这是与本问题**间接相关的规范文档不一致**（列表里未把 `++/--` 与 `typeof` 分层写）。
- AST 中 `TypeQueryNode` 虽然只解析点链，但其字段类型是 `zc::Own<Expression>`（与 `TypeOfExpression` 一样），**从数据结构上已经给未来扩展留了口子**，如果放宽到任意表达式，AST 层面不需要改，只改 parser 即可，缓解了"扩展成本高"的担忧。

### 对原建议的补充

原建议"做关键字层面区分（`type.typeof` vs `typeof`）"方向正确，但 `type.typeof` 需要引入命名空间表达式/路径语法，当前语法没有；更轻量的替代方案是：
1. 短期：在 `03-types.md` 的 Type Queries 小节加一句话，明确说明 `typeof` 在类型语境和值语境下的差别；
2. 中期：把类型层 `TypeQueryExpression` 扩展为完整的表达式（前提是实现 Checker 的编译期类型计算）；
3. 长期：若确实需要区分，可考虑 `$typeof` / `compi` 类关键字而非 `type.typeof` 这种复合形式。

### 关键文件清单

- 语法参考（两种入口）：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:185-186, 305`
- 类型层语义描述：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/03-types.md:229-242`
- 表达式层语义描述：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/04-expressions.md:208-218, 369`
- 解析器类型层：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc:520-563, 3308-3310`
- 解析器表达式层：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc:2663-2664, 2694-2709`
- 表达式 AST：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/expression.h:607-612`
- 类型 AST：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/type.h:429-444`
- 类型层测试：`/Users/bytedance/Develop/ZOM/products/zomlang/tests/language/types/type-query.zom`
- 表达式层测试：`/Users/bytedance/Develop/ZOM/products/zomlang/tests/language/expressions/unary-and-cast.zom`
- 反对方: 反驳证据链（共六点）：

1. **两种入口均为规范显式定义，绝非歧义或实现溢出。**
   - EBNF 185-186 行明确定义 TypeQuery：`'typeof' Identifier ('.' Identifier)*` 用于类型位置；
   - EBNF 305 行明确将 `typeof` 列入 UnaryExpression 的运算符集合，用于表达式位置；
   - 规范正文 04-expressions.md 的一元运算符清单、03-types.md 的"类型查询"小节也分别描述了两层语义；
   - 给定上下文的 section 3.2 表格同时确认了两个层级。这不是"解析器超出规范"或"文档遗漏"——上下文里 section 8.3 声称"表达式级 typeof 是 parser 多出来的语法"是自相矛盾的（与同一文档的 3.2 节和 EBNF 冲突）。

2. **解析器上下文严格分离，编译期零歧义。**
   - TypeQuery 仅在 `parseSimpleType()` 的 switch-case (3308-3310 行) 被触发，调用链为 `parseType() → parseUnionOrIntersectionType() → parseSimpleType() → parseTypeQuery()`，全程处于**类型位置**；
   - TypeOfExpression 仅在 `parseSimpleUnaryExpression()` 的 switch-case (2663-2664 行) 被触发，调用链处于**表达式位置**；
   - 两条路径互不相交，`typeof` 关键字**不可能**在能同时是 type 和 expression 的位置被解析，不存在 parser 选错误分支的可能。

3. **阅读者消歧有明确边界符，不存在"瞬间难以判断"。**
   - `let z: typeof a = typeof b;`：`:` 始终是类型注解左边界，`=` 始终是表达式/初始化器左边界；
   - `type T = typeof expr.field;`：`type` 关键字后，`=` 右侧永远是类型表达式；
   - `fun f(p: typeof a) -> typeof b {}`：参数 `:` 后、返回 `->` 后都是类型位置；
   - 这是 TypeScript 等广泛使用语言的标准模式，并未被视作设计缺陷。

4. **TypeQuery 限制为标识符点链是语言设计的标准决策，而非缺陷。**
   - 编译期 `typeof` 只能作用于命名实体，对 `typeof (a + b)` 报错是**正确行为**——编译器无法在编译期求任意表达式的类型；
   - 该限制与 TypeScript 中 `type T = typeof (expr)` 非法完全一致；
   - 所谓"无法智能切换到表达式层"毫无意义：用户明确处于类型位置（如 `type T = ...`、`let x: ...`、`param: ...`），切换到表达式层语义在逻辑上不可能。若用户需要运行时语义，直接在表达式位置写 `typeof (a + b)` 即可，完全合法。

5. **"语义不同"是未实现阶段的臆测。**
   - Checker 整体是空壳（checker.cc 为空命名空间），两种 typeof 的类型规则、返回值、类型层级行为**均未定义**；
   - 声称表达式级 typeof "返回类型描述字符串或类型对象"完全是推测（描述原文中也写了"推测"二字）。将未实现的功能贴上"语义不同"的 medium 严重度标签，为时过早。

6. **与 TypeScript 的对照说明该模式是成熟可行的。**
   - TypeScript 有完全相同的设计：类型位置 `typeof` 限 `IdentifierName ('.' IdentifierName)*`，表达式位置 `typeof` 接受任意表达式并返回字符串；
   - TS 在工业界被大规模使用，这一设计从未被标记为语义问题。若 ZOM 照抄该模式却被定为 medium 语义问题，则标准本身也要被证伪。

综上：描述中的**部分事实观察为真**（确实有两个入口、表达力不同），但将其定性为"语义问题/设计缺陷"是误判——这是规范显式规定的、解析器严格执行的、与主流工业界一致的设计选择，并不存在歧义、混乱或不可实现性。


### 53. 🟢 [低] 逗号同时分隔多变量声明和逗号表达式，存在前向演进的语法歧义隐患  
**类别**: 演进性 | **置信度**: 97%

**问题描述**  
表达式的顶级产生式是 `Expression ::= AssignmentExpression (',' AssignmentExpression)*`，即任何表达式内部都支持逗号分隔的序列表达式；而同时 `VariableDeclarationList ::= VariableDeclaration (',' VariableDeclaration)*` 也是逗号分隔。在 `let (a, b) = x, y;` 这种解构加多变量声明的边缘场景，会出现 `let` 之后逗号到底属于 "变量列表分隔" 还是 "表达式序列" 的歧义——虽然当前 `let` 的 init 是 `Initializer = '=' AssignmentExpression`，但未来一旦把赋值的右侧放宽为 Expression（带逗号），两者的语法边界会变得模糊。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:276 — `Expression ::= AssignmentExpression (',' AssignmentExpression)*`
    表达式级支持逗号序列
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:118 — `VariableDeclarationList ::= VariableDeclaration (',' VariableDeclaration)*`
    变量声明列表也使用逗号分隔

**潜在影响**  
未来扩展逗号表达式或多绑定列表时极易引入真正的语法歧义，属于前向演进隐患。

**修复建议**  
将顶级 Expression 从 EBNF 改为不含逗号的 AssignmentExpression，把逗号表达式限制为显式括号内（像 Go 那样），或完全删除逗号序列表达式的支持。
**评审备注**
- 确认方: 这是一个真实的前向演进隐患（evolution hazard），但严重度需下调。分析如下：

**一、与原报告一致的事实：**
- 第 17 章 276 行：`Expression ::= AssignmentExpression (',' AssignmentExpression)*` — 顶级表达式支持逗号序列，parser.cc:2415-2434 也确实如此实现（parseExpression 在 parseAssignmentExpressionOrHigher 之后循环消费 Comma）。
- 第 17 章 118-120 行：`VariableDeclarationList ::= VariableDeclaration (',' VariableDeclaration)*`，且 `Initializer ::= '=' AssignmentExpression`。parser.cc:2436-2438 实现为 `return parseAssignmentExpressionOrHigher()`，明确不包含逗号。

**二、核心机制——为什么"现在安全"：**
解析层级是：
`parseVariableStatement → parseVariableDeclarationList (逗号切 VariableDeclaration) → parseVariableDeclaration → parseInitializer (= AssignmentExpression, 不含逗号)`
变量列表级的逗号在 parseDelimitedList 层消费（ParsingContext::VariableDeclarations，终止条件是 `canParseSemicolon() || InKeyword || Arrow`，parser.cc:262-272），所以每个 VariableDeclaration 的 init 天然不可能吃掉属于下一个绑定的逗号。

**三、为什么"它依然是演进隐患"：**
如果未来把 `Initializer ::= '=' AssignmentExpression` 放宽为 `'=' Expression`（即支持 `let a = 1, 2;` 这种「a = (1,2)」的逗号表达式），那么 `let a = 1, b = 2` 的第二个逗号就会出现结构歧义：
- Interpretation A：两绑定 `a=1`、`b=2`（列表分隔）
- Interpretation B：一绑定 `a=(1, b=2)`（逗号表达式）
当前仅靠"init 右边界止于赋值表达式级"来避免。这道防线在 spec 上其实是**隐式的、没有文档说明**的设计约束（"Initializer 的右侧不得放宽到 Expression 级"是没有注释的隐含前提）。一旦未来有人不了解这个约束就去改语法，歧义立即产生。

**四、类似历史案例：**
JavaScript 中同样存在 `let a = 1, b = 2` 与 `let a = (1, b) = 2` 的脆弱边界，TC39 在规范中显式规定 Initializer 只到 AssignmentExpression（和 ZOM 目前一致），这是有意的防歧义锁，不是偶然。Go 则彻底删除顶级逗号表达式以避免。

**五、调整建议（补充原建议）：**
- 原建议「把 Initializer 保持为 AssignmentExpression 级」本身就是当前已做的事，但缺乏显式文档锁定——建议在 grammar reference 的 `Initializer` 产生式旁加注释 `(* intentionally AssignmentExpression, not Expression, to avoid comma ambiguity with VariableDeclarationList *)`。
- 或更积极：完全从 Expression 中移除逗号支持（AST 层目前已有 BinaryExpression 表达逗号，需要确认使用场景）。若保留，应在设计文档中显式列出"逗号表达式仅在显式括号 `(a, b)` 中有效"的约束。

**六、严重度评估：**
原报告标 low 合理，但我倾向维持为 **low**（不是 invalid）——因为这不是当前能触发的 bug，而是"缺少显式演进锁 + 已有隐患"。对于成熟语言项目，这类 specification lock-in 价值很高；但当前 ZOM 的 Checker 阶段还是空壳（比这个语法隐患严重的缺口多得多），因此在优先级序列上不宜拉高。
- 反对方: 一、规范层已存在明确的分层消歧设计，非"隐患"而是"有意为之"：
1. 规范第 120 行明确写着 `Initializer ::= '=' AssignmentExpression`，限制为赋值表达式级；
2. 规范第 276 行 `Expression ::= AssignmentExpression (',' AssignmentExpression)*` 把逗号运算符限定在 Expression 顶层；
3. 两层严格不交叠：Initializer 右侧不可能出现顶层逗号，因此 `VariableDeclarationList` 的逗号与"表达式逗号"天然互斥。

二、实现层严格执行了该分层，不存在解析歧义：
1. `parseInitializer()`（parser.cc:2436-2439）调用的是 `parseAssignmentExpressionOrHigher()`，而非 `parseExpression()`；
2. 逗号运算符的 while 循环仅存在于 `parseExpression()`（parser.cc:2426-2431）中，在 `parseAssignmentExpressionOrHigher()` 路径上完全不可达；
3. `parseDelimitedList(VariableDeclarations)` 只在"完成一个 VariableDeclaration 后"才尝试消费逗号，而一个 VariableDeclaration 内部的 initializer 已被 `parseAssignmentExpressionOrHigher()` 截断，不可能吃到分隔声明的逗号。

三、所谓"歧义场景" `let (a, b) = x, y;` 实际解析路径完全确定无歧义：
1. `parseVariableDeclaration` 先把 `(a, b)` 作为 BindingPattern 解析（parser.cc:1681 parseIdentifierOrBindingPattern）；
2. 然后 parseTypeAnnotation（无）→ parseInitializer 解析 `= x`，x 为合法 AssignmentExpression，返回；
3. 回到 parseDelimitedList：parseOptional(Comma) 成功，进入下一轮 parseVariableDeclaration；
4. `y` 作为 Identifier 成功，无 type 无 initializer，完成第二个 VariableDeclaration；
5. 最终得到"解构 (a,b)=x 加声明 y"——语义与语法边界都完全确定。

四、这是业界验证了几十年的标准消歧策略，非 ZOM 独有：
- C/C++：`int a = 1, 2;` 语法错误，必须写 `int a = (1, 2);` 才是逗号表达式；
- JS/TS：`let a = 1, b = 2;` 多声明与 `let a = (1, 2);` 逗号表达式严格分层；
- C#/Java/Kotlin/Swift：全部采用等价分层思路。

五、论据核心——"未来一旦把赋值右侧放宽为 Expression"——是无根据的稻草人假设：
1. 全项目搜索（docs + parser）找不到任何 TODO、计划文档或设计讨论表明有此意图；
2. 该改动会破坏与所有主流语言对齐的既定设计原则，属于"自找麻烦"的改动，几乎不可能被采纳；
3. 即便真想支持，也有成熟标准方案：继续保持 AssignmentExpression 限制（用户需显式写括号），或干脆取消多声明逗号语法（Go/Rust 路线）。任何语法设计都能被"如果未来改了X就有问题"的逻辑造出无穷多"隐患"，这不构成设计缺陷。

六、问题自身标注类别为 evolution、严重度 low，已间接承认当前无实际 bug，仅为纯理论担忧。

### 54. 🟢 [低] raises 子句仅支持单个错误类型，规范要求 TypeList  
**类别**: 规范-实现不一致 | **置信度**: 95%

**问题描述**  
规范写 RaisesClause 接受 TypeList（支持 raises E1 | E2），但 parser 的 parseRaisesClause 直接返回 parseType()，即只支持单个错误类型。

**证据**
  - docs/spec/chapters/17-grammar-reference.md:372 — `RaisesClause ::= 'raises' TypeList`
    EBNF 明确指定 TypeList
  - products/zomlang/compiler/parser/parser.cc:565 — `parseRaisesClause calls parseType() not parseTypeList()`
    parser 只读一个类型，不支持多个联合

**潜在影响**  
用户无法像规范描述的那样声明多个错误类型的联合（raises E1 | E2），只能用一个。

**修复建议**  
修改 parseRaisesClause 以接受 UnionTypeNode 风格的 TypeList，并在 ReturnTypeNode::errorType 中保存联合类型。
**评审备注**
- 确认方: ## 真实存在的问题（窄化后）

规范三源之间本身不一致：
- EBNF 17-grammar-reference.md:196 规定 RaisesClause ::= 'raises' TypeList（逗号分隔 E1, E2），与 parser 不一致
- ANTLR ZomParser.g4:794 规定 raisesClause: RAISES type（含联合 E1 | E2），与 parser 一致
- 错误处理章 11-error-handling.md:65,134 的示例使用 raises FileNotFoundError | ParseError（联合语法），与 parser 一致
- 类型章 03-types.md:196 示例单类型 raises ParseError，与 parser 一致

结论：真正的不一致仅是 EBNF 与其他规范源（ANTLR、散文示例、解析器）之间的单点文档错误。逗号分隔的 TypeList 语法（raises E1, E2）实测不支持（报 ZOM2025/ZOM2049/ZOM2021 三级诊断）。而用户自然会书写的联合语法 raises E1 | E2 在三个上下文（函数声明、alias 函数类型、闭包）中均正确产出 UnionTypeNode。

AST 层 ReturnTypeNode::errorType 字段类型为 zc::Maybe<zc::Own<TypeNode>>，已足够承载任意 TypeNode（包括 UnionTypeNode），无需改动。

## 建议修正方向
方向 A（文档修正，推荐）：17-grammar-reference.md:196 从 TypeList 改为 TypeExpression，与 ANTLR 和散文对齐；移除或复用死函数 parseRaisesClause()。
方向 B（实现修正）：若产品意图确为 TypeList 逗号语法，则需在 parseRequiredReturnType() 中实现 TypeList 解析（多元素包裹为 UnionTypeNode），并同步更新 11-error-handling.md 示例及 ANTLR 文法。

## 相关文件
- EBNF：/Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md 第 196、214 行
- ANTLR：/Users/bytedance/Develop/ZOM/docs/spec/ZomParser.g4 第 794 行
- Parser 主实现：/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc 第 4338-4339 行（parseRequiredReturnType 内）
- 死函数：同上文件第 565-577 行 parseRaisesClause()；对应头文件声明 parser.h:536
- 错误处理散文示例：/Users/bytedance/Develop/ZOM/docs/spec/chapters/11-error-handling.md 第 65、134 行
- AST 定义：/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/type.h ReturnTypeNode 类（第 373 行起）
- 现有 raises 正面测试：/Users/bytedance/Develop/ZOM/products/zomlang/tests/language/declarations/functions/function-definitions/raises.zom（仅单类型，无联合覆盖）
- 现有 parser unittest：products/zomlang/tests/unittests/compiler/parser/parser-test.cc 第 702、3761 行（均为单类型）
- 反对方: 四条反证链：

1. **证据对象是死代码（零调用）**：问题引用的 `parser.cc:565 parseRaisesClause()` 在整个 compiler 源码中**没有任何调用点**（grep 全 compiler 只在 parser.h 声明和 parser.cc 定义各出现一次，parser.cc 内部也没有任何函数调用它）。真正的 raises 子句解析发生在 `parseRequiredReturnType()`（`parser.cc:4339`）内联逻辑：`if (consumeExpectedToken(RaisesKeyword)) { errorType = parseType(); }`——两处逻辑虽然一致（都调 parseType()），但拿死代码当实现证据本身就是严重误导。

2. **规范内部自相矛盾，叙述性章节优先于 EBNF**：
   - 叙述性规范（`docs/spec/chapters/11-error-handling.md` 第 65、134 行）给出的权威示例：`fun readConfigFile() -> Config raises FileNotFoundError | ParseError` —— 明确用**竖线 `|` 分隔**，即联合类型语法。
   - EBNF（`17-grammar-reference.md:196`）写的 `RaisesClause ::= 'raises' TypeList` 与 TypeList 定义（逗号分隔）和上述示例直接冲突。同一份规范内部不一致时，带完整语义说明的叙述性章节优先于语法参考。
   - 问题报告者自己在描述中括号内补写"支持 raises E1 | E2"，这本身就说明报告者直觉上认同竖线语法，只是对 EBNF 的 TypeList 定义产生了误读。

3. **实现实际上完整支持多错误类型**：
   - `parseType()` → `parseUnionTypeOrHigher()` → `parseUnionOrIntersectionType(Bar, ...)` → `createUnionType(types)`（`parser.cc:3235-3236`）。
   - 因此 `raises E1 | E2 | E3` 会完整解析为一个 `UnionTypeNode`，包含全部三个错误类型。
   - `ReturnTypeNode::errorType` 类型为 `Maybe<Own<TypeNode>>`，`UnionTypeNode` 是 `TypeNode` 子类，结构上完美适配。
   - 与规范 11 章叙述性描述的语义（"Functions can return multiple error types"）100% 对齐。

4. **EBNF 本身已知存在多处文档错误**（参考之前的 parser-spec 对照）：
   - interface 用 `implements`（parser 支持，EBNF 只写 `extends`）
   - 单 `!` 后缀 `NonNullExpression`（parser 支持，EBNF 未写）
   - 表达式级 `typeof expr`（parser 支持，EBNF 只在 TypeQuery 层有 typeof）
   - 因此 EBNF 的 `RaisesClause ::= 'raises' TypeList`（逗号分隔）属于同类文档疏漏，不能作为反证实现的依据。

**唯一需要承认的表面差异**：若严格按 EBNF 的字面定义（逗号分隔 TypeList）写 `raises E1, E2`，parser 确实只解析到 E1，然后对剩余 token 报错。但这是**语法糖表面形式的差异**（逗号形式未实现），而非功能缺失——竖线联合形式已完整支持，且正是叙述性规范给出的形式和语义上更自然的写法（"E1 或 E2"）。若要补逗号形式，最多算 feature request，不应归类为 spec-impl-mismatch。

### 55. 🟢 [低] 控制流语句全覆盖，但 switch 语法不存在  
**类别**: 语义与组合性 | **置信度**: 91%

**问题描述**  
控制流语句中 if/while/do-while/for/for-in/match/return/break/continue/label 在 EBNF 和 parser 中均完整实现。但关键字表保留了 case 关键字，而语法和 EBNF 中没有任何 switch/case 的入口，这意味着用户不能用传统 C-style switch，但文档中未明确说明 switch 被 match 取代。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:226 — `Statement ::= BlockStatement | EmptyStatement | VariableStatement | ExpressionStatement | IfStatement | MatchStatement | WhileStatement | DoWhileStatement | ForStatement | ForInStatement | ContinueStatement | BreakStatement | ReturnStatement | DebuggerStatement | LabeledStatement`
    Statement 产生式明确不含 SwitchStatement
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/02-lexical-structure.md:123 — `if          else        match       when        default     case`
    case 出现在控制流关键字列表中，但无对应语法。若用户写 switch/case 将得到一个非预期的 parse error，而不是清晰的 'use match instead' 诊断。

**潜在影响**  
用户可能因熟悉 C/Java 的 switch 而困惑；case 作为保留字被占用，无法用作标识符。

**修复建议**  
在 05-statements.md 开头添加一个明确的说明：'ZOM 不支持 switch 语句，请使用 match 表达式替代'，并为 case/switch 关键字在 parser 中添加专门的诊断提示（diag: 'use match instead of switch'）。
**评审备注**
- 确认方: 独立核验结果：问题属实但需限定范围与降低严重度。

### 真实的部分（佐证）
- EBNF `17-grammar-reference.md:226-240` 的 Statement 产生式确实不含 SwitchStatement，完全匹配原证据。
- `02-lexical-structure.md:123` 控制流关键字表里 `case` 与 `if / else / match / when / default` 同列，保留字事实确认。
- `ast/kinds.h:53` 明确登记了 `CaseKeyword`；而 `parser.cc:317` 在 `ParsingContext::MatchClauses` 下把 `CaseKeyword` 当作 isStartOf 的合法开头。这说明 `case` 关键字在词法和 parser 前瞻层面**完全活着**。
- 然而 `parser.cc:2131-2155` 的 parseMatchStatement 主体只处理了 `DefaultKeyword` 与 `WhenKeyword`，**完全没有 CaseKeyword 分支**。这意味着：用户在 match 里写 `case 1 => ...` 时，parser 会先认为它是一个 match 子句的开始，然后进入 while 循环，最后落入 `else { nextToken(); }` 的静默吞 token 路径——**不会产生任何"请改用 when/match"的友好诊断**，直接静默错误恢复。这比"普通 parse error"更糟，是真正的 UX 缺陷。
- `05-statements.md` 第 89-133 行详细介绍了 match 语句，但**没有任何一句明确说明"switch/case 不存在，请用 match 替代"**，也没有像 289-290 行那样的保留字说明。这种"不对称"是真实的：throw 家族有说明，case 没有。原证据的文档缺失判断成立。

### 结论细化
候选问题是真实的，但有两点修正：
1. **范围缩小**：只涉及 `case` 关键字，不涉及 `switch`（`switch` 根本不是关键字）。
2. **严重度下调**：用户在语句顶层写 `case` 几乎不会发生（直觉上大家会写 `switch`），真正的摩擦点只在"match 里混用 case"与"05 章缺一行声明"。且 02 章已有全局保留字兜底说明。因此 refinedSeverity 应为 `low`，不是 `medium`。

### 相关文件（绝对路径）
- 语法参考（Statement 产生式）：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md`
- 关键字列表 + 全局保留字说明：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/02-lexical-structure.md`
- 语句章节（match 说明 / throw 对比模板 / case 缺口）：`/Users/bytedance/Develop/ZOM/docs/spec/chapters/05-statements.md`
- CaseKeyword 登记：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/kinds.h`
- Parser Match 子句解析（只有 when/default，case 被静默吞）：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc`（约 2118-2161 行、317 行）
- Parse 诊断定义：`/Users/bytedance/Develop/ZOM/products/zomlang/compiler/diagnostics/diagnostics-parse.def`
- 反对方: ### 反驳点 1（致命）：`switch` 根本不存在于关键字/token 体系
- `products/zomlang/compiler/ast/kinds.h` 中搜索不到任何 `SwitchKeyword` 枚举。
- `products/zomlang/compiler/lexer/utils.cc` 中没有 "switch" 字符串匹配分支（只有 "case" 在第 178 行映射为 CaseKeyword）。
- `docs/spec/chapters/02-lexical-structure.md:120-127` 的"控制流关键字"清单是 `if else match when default case / for while do break continue / return throw try catch finally / debugger`——**没有 `switch`**。
- 因此"用户写 switch/case 将得到 parse error"的前提不成立：写 `switch` 不是保留字，会被当作普通标识符，整个语句会按 `switch(...) { ... }` 走表达式语句+函数调用+大括号的歧义路径，与"switch 语法不存在"的主张是两个不同性质的 UX 问题。

### 反驳点 2：`case` 并非"悬空无对应语法"，它被 parser 明确列为 match 分支的合法起始
- `parser.cc:316-318` 在 `isListElement(MatchClauses)` 中写的是：`return token.is(WhenKeyword) || token.is(CaseKeyword) || token.is(DefaultKeyword);`——**parser 已明确把 case 作为 match 分支的合法起始 token**。
- `diagnostics-parse.def:97` 定义了 `CaseOrDefaultExpected` 诊断，`parser.cc:427` 在 match 分支解析失败时发射它——名称直接使用 "case"，表明 case 在 match 语境中是预期词汇。
- `tests/unittests/compiler/parser/parser-test.cc:1153-1154` 的测试输入是 `match value { case 1 => "one"; case 2 => "two"; default => "other"; }`，测试名就是 `ParseMatchStatement`，且断言通过——**说明 case 是被当作 match 分支关键字在测试**，不是"无对应语法的残留关键字"。

### 反驳点 3：case 问题的真实性质是 "parser 实现 bug"（spec-parser 不一致，方向为 parser 超前），不是"语义缺失"
- EBNF `17-grammar-reference.md:254` 写的是 `MatchClause ::= 'when' Pattern GuardClause? '=>' Statement`，**只定义了 `when`，没有 `case`**——spec 层面是自洽的。
- 但 `parseMatchStatement`（parser.cc:2131-2154）循环里，`else if` 分支只显式处理了 `WhenKeyword`（2139 行），没有对应的 `CaseKeyword` 分支。`case` token 会落入最后的 `else { nextToken(); }` 被静默吞掉。
- 这是 **parser 内部不一致**：前瞻（isListElement）承认 case，诊断码（CaseOrDefaultExpected）使用 case，测试用例写 case，但循环体不处理 case。它属于 parser 的实现缺陷（case 作为 when 别名/替代关键字的接入未完成），而不是候选问题声称的"没有 switch/case 入口"。问题主张把方向完全搞反了。

### 反驳点 4：保留字策略有全局文档说明，不是"缺陷"
- `02-lexical-structure.md:162-163` 明确声明："Some reserved words are reserved for future language design. If a reserved word has no grammar rule in the current parser, using it as syntax is a parse error rather than a supported construct."——case 即使真的未接入，也属于**文档已覆盖的正常保留行为**，而非需要额外声明的问题。
- 同一行 123 的 `throw`、`try`、`catch`、`finally` 也都没有对应 Statement 产生式（这在 `05-statements.md:289-290` 甚至被明确点名"保留但当前未接入"）。按候选问题的逻辑，这些也都应该是缺陷——但显然这是有计划的保留字策略。

### 反驳点 5：缺少 switch 不是缺陷，是现代语言设计惯例
- 语言 `01-introduction.md:17` 明确列出"模式匹配 + 代数数据类型"作为特性，`05-statements.md:89-133` 给出了完整 match 语句文档（字面量/类型/解构/守卫/default，且支持表达式）。
- Rust（match）、Swift（switch 但模式匹配）、Kotlin（when）、Scala（match）等主流现代语言都以模式匹配分支替代 C-style switch；ZOM 选择 `match + when` 组合，不提供 switch，是行业常见的明确设计选择。
- 要求规范"明确说明 switch 被 match 取代"等同于要求规范负向枚举所有不存在的语法（goto、volatile、#define、#include、register、auto...），举证责任不合理。语言规范是正向描述：列出有什么，不逐一列没有什么。

### 反驳点 6：问题的两个主张都缺少实证支撑
- "用户不能用传统 C-style switch"——但 switch 不是关键字，用户写 switch 时的实际诊断不是 "switch 语法错误" 而是表达式级别的歧义错误。候选问题甚至没验证过 switch 作为标识符的实际行为。
- "文档中未明确说明 switch 被 match 取代"——要求负向枚举式文档不合理，且 `05-statements.md` 章节组织（match 紧跟 if，没有 switch 章节）已经是隐式声明。`01-introduction.md:17` 的"模式匹配"显式特征列表同样是声明。

### 56. 🟢 [低] async/await 保留字策略合理，但 Future/Task 类型和调度器接口需要及早占位  
**类别**: 演进性 | **置信度**: 90%

**问题描述**  
并发章节明确 async/await/actor/channel/task group 全部保留，策略正确（避免半吊子实现）。但演进性风险在于：(1) 如果在标准库中先出现异步 API（如网络、文件），它们的返回类型可能先于 async 语法而确定，导致后续 async 化时需要 breaking change；(2) trait 系统（interface）中未预留 AsyncCallable / Future trait 的层级占位，未来加入会与现有 Callable 抽象冲突；(3) `async` 作为 modifier 已经在 SymbolFlags 中存在（bit35），但语法 Modifier 不含它，存在 symbol 层超前于 spec 的漂移。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/15-concurrency.md:3 — `Concurrency syntax is reserved for future language design.`
    策略明确正确，但无实现路线和接口预留说明
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/symbol-flags.h:122 — `Async = 1ULL << 35,      // Async functions`
    SymbolFlags 中 Async 位已存在，属于超前建模。如果最终 async 语法不采用 modifier 形式（如 Swift 的 `async` 写在返回类型后），该 flag 需要迁移。
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/02-lexical-structure.md:142 — `readonly    mutable     async       await       override`
    async 和 await 已在 modifier 关键字组中，暗示未来会作为修饰符使用。但 06-declarations.md:191-195 又声明它们不接入 parser。双位置冲突。

**潜在影响**  
中低：当前未实现并发不影响正确性，但标准库演进和 symbol 层的超前建模会带来技术债务。

**修复建议**  
在 docs/plans/ 下新增并发设计占位文档，明确：(1) async 采用位置（modifier vs 返回类型后缀）——建议采用 `fun f() -> T async`（与 raises 对称）；(2) 预定义 Future<T>/Stream<T> 作为内建类型别名（可先以空接口形式放入内建符号表）；(3) 调整 SymbolFlags 中的 Async 位直到 spec 确定位置，或在注释中标注该位为 'tentative'。
**评审备注**
- 确认方: 核验结论：问题真实，但严重度下调为 low（原 medium）。

逐项核验与独立证据：

(1) 「标准库异步API返回类型先于async语法确定」——部分成立，前瞻性风险：
- 无任何语言级标准库存在（zomcore/、runtime/ 仅 CMake 占位），无前置异步 API，因此不存在"已经确定的返回类型"。
- 但作为前瞻性风险警告是合理的，属于设计债务而非当前 bug。

(2) 「未预留 AsyncCallable/Future trait 层级」——成立但被放大：
- grep 09-interfaces.md、type-symbol.h/cc、ast/type.h，均无 Callable/AsyncCallable/Future/Stream 字样。
- 但 InterfaceSymbol::Impl 本身是空结构体，整个 interface 体系都还只是骨架，Callable 自身都未占位，AsyncCallable 缺失只是更大背景下的子问题，不应单独被提为 medium 风险。

(3) 「SymbolFlags::Async 超前于 spec，存在三层漂移」——完全成立，证据确凿：
  a) lexical vs grammar 冲突：02-lexical-structure.md:142 的 Modifier Keywords 组列出 async/await，但 17-grammar-reference.md:155 正式 EBNF 的 Modifier 只含 public/private/protected/static/readonly/mutating/override 七个，不含 async/await；同时 06-declarations.md:191-194 又明确它们不接入语法。三处互不一致。
  b) symbol 层超前：symbol-flags.h:122 定义 Async(1<<35)、Generator(1<<36)，并出现在 SpecialMask/DeclarationFlags/FunctionFlags/AsyncFunction 等组合位中，属正式 flag，非注释占位。
  c) 实现层再分叉：parser.cc:1055-1062 的 isModifier() 仅识别 Abstract/Export/Public/Private/Protected/Static/Readonly/Mutating/Override，不含 async/await——即语法实现和 EBNF 一致，但与 lexical 分组、与 SymbolFlags 中的 Async 位都不一致。
- 另：全代码库 grep SymbolFlags::Async 的实际写入点为 0，除定义和注释外无任何 flag 被设置，因此目前该位为"超前声明且无用"的死代码，无运行时影响，但属于技术债和未来迁移隐患。
- 另有原问题未提及的扩展：Generator(1<<36)、Extension(1<<37)、Operator(1<<38)、Implicit/Explicit(1<<39/40)、Pure(1<<41)、Tail(1<<42) 等 Special 组位，均不在 EBNF Modifier 列表内，Async 只是该组超前建模现象中最容易被关注到的一个。

建议的修正（比原问题更精确）：
- 在 symbol-flags.h 中为 Async、Generator、Extension、Operator、Pure、Tail 等所有"spec 未纳入 v1 语法"的位统一加 '// Tentative: reserved for future language features' 注释，并在 docs/plans/ 下新增一份占位文档记录这些已建模但未落地的特性。
- lexical 章节的 Modifier Keywords 分组应与 17 章 EBNF 对齐：把 async/await 移到"Advanced Keywords"或"Future Reserved"独立分组下，避免读者误认为当前已是 modifier。
- Async 的最终位置（modifier vs `-> T async`）在真正设计并发时再定，无需现在拍板。

关键证据文件：
- /Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/symbol-flags.h:119-145,176-177,200-202,212
- /Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc:1055-1062
- /Users/bytedance/Develop/ZOM/docs/spec/chapters/02-lexical-structure.md:138-144
- /Users/bytedance/Develop/ZOM/docs/spec/chapters/06-declarations.md:191-194
- /Users/bytedance/Develop/ZOM/docs/spec/chapters/15-concurrency.md:1-11
- /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:155
- 反对方: (1) ZOM 标准库尚未立项：products/zomlang/ 内无 stdlib，docs/spec 无任何 fs/net/io 等标准 API 章节，"异步 API 先于 async 语法出现"的前提不存在。
(2) 全项目 grep 不存在 Callable 接口；InterfaceSymbol 实现为空壳。"AsyncCallable 与 Callable 抽象冲突"的双方皆未出现，一次性设计即可，不存在既成事实的冲突。
(3) SymbolFlags 中与 Async 同属"前瞻预留"的位至少有 15 个（Generator/Extension/Operator/Implicit/Explicit/Pure/Tail/Associated/Dependent/Higher/Macro/Extern 等），Async 位绝不是特例。这些位属于号段分配，不代表 spec 漂移。
(4) Modifier 语法不含 async + isModifier() 不含 AsyncKeyword，这与 06-declarations.md:191-195 "async/await 已保留但不接入 parser"完全一致。02-lexical-structure.md:162-163 明确声明"保留字 ≠ 当前语法支持"，因此"modifier 关键字组列了 async"和"语法未接入"是双轨策略，不构成双位置冲突。
(5) 即便未来 async 采用 Swift 式（返回类型之后），bitflag 迁移只是常量重命名，成本忽略不计，不属于 breaking change。

### 57. 🟢 [低] GADTs / 线性类型 / 效果系统等高级特性的演进性依赖名义类型 + 当前 trait 架构的可扩展性  
**类别**: 演进性 | **置信度**: 86%

**问题描述**  
好消息：SymbolFlags 已为 Associated(48)、Dependent(49)、Higher(50)、Phantom(47)、Variance(44-46) 提供位占位，这为 GADTs、高阶类型、依赖类型、线性类型提供了 symbol 级的扩展空间。但问题在于：(1) enum 的 AST 表示尚未支持类型参数与变体类型参数的差异化（GADT 的核心）；(2) 没有任何 '线性/资源' 相关的类型节点或 bit 标记（linear types 的基础）；(3) 效果系统（如 effect/handler）完全无占位，而 raises 本质上是效果系统的特例。

**证据**
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/symbol-flags.h:132 — `Generic = 1ULL << 43, Covariant = 1ULL << 44, Contravariant = 1ULL << 45, Invariant = 1ULL << 46, Phantom = 1ULL << 47, Associated = 1ULL << 48, Dependent = 1ULL << 49, Higher = 1ULL << 50`
    优秀的预留，覆盖了泛型高级特性
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:161 — `EnumMember ::= PropertyName (('=' Expression) | TupleType)?`
    Enum 的每个变体只允许一个 TupleType 作为关联值。GADT 需要每个变体声明自己的类型参数返回（如 `enum Expr<T> { Num(i32): Expr<i32>, ... }`），当前语法不支持。
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/symbol-flags.h:158 — `Reserved1 = 1ULL << 63,`
    只剩一个 bit 位（63）未分配，后续添加 Linear / Resource / Borrowed / Owned 等标记时需要扩展到 128 位或调整位布局。

**潜在影响**  
低到中：这些都是 V2+ 远景特性。但在 Enum AST 定型前修正 GADT 支持，成本远低于后续重构。

**修复建议**  
低成本演进准备：(1) 在 EnumMember 的 EBNF 中预留下返回类型标注槽位：`EnumMember ::= PropertyName (('=' Expression) | TupleType)? (':' TypeExpression)?`，当前版本语义检查时禁止非顶层返回类型；(2) 在 SymbolFlags 中新增 Linear/Resource 位或将 63 号位明确标注为 LinearFlag 并加入注释；(3) 考虑 raises 未来扩展为通用效果系统时，将 RaisesClause 设计为可扩展结构（EffectsClause），当前只接受 error 类型。
**评审备注**
- 确认方: 独立核验证据：

证据 1 — Enum AST 不支持 GADT 式变体返回类型：确认成立。
- AST 定义 /Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/statement.h:684-691 EnumMember 构造函数签名仅含 (name, initializer, tupleType)，没有任何返回类型标注或变体专属类型参数字段。
- 解析实现 /Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc:2315-2338 parseEnumMember 只识别等号分支 initializer 左括号分支 tupleType，冒号作为返回类型标注的前瞻完全不存在。
- 规范 EBNF /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:161 EnumMember 规则确实没有冒号返回类型槽位。候选问题此处描述准确。

证据 2 — 无线性/资源类型相关标记或节点：确认成立，但属于未到阶段而非遗漏。
- 全文 grep linear borrow resource owned 在 ast/kinds.h symbol-flags.h 中均为 0 命中（唯一 Pure 标记仅用于副作用）。
- 内存管理规范 14 章只规划 struct 值类型加 class ARC 加 weak 引用，没有 Rust 式 borrow checker 语义。当前 spec 定位不是线性类型语言。
- 结论：线性类型缺失是真实架构预留空白，但在 ZOM 当前 V1 定位下不是需要紧急占位的功能。候选问题列 medium 严重度偏高。

证据 3 — raises 不是通用效果系统，且 AST 与 spec 还有另一个候选问题未指出的不一致：确认成立，并补充新发现。
- 规范 EBNF 17-grammar-reference.md:196 写 RaisesClause 接 TypeList，即设计上允许 raises E1,E2。
- 但 /Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/type.h:375-376 ReturnTypeNode 只有 errorType 单一 TypeNode 字段，没有列表。
- /Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc:576 parseRaisesClause 直接 return parseType 完全不实现 TypeList，raises E1,E2 会把逗号截断到下一语句。
- 因此 raises 不仅不是效果系统，连 spec 承诺的多错误类型列表都未落地。将 RaisesClause 升级为通用 EffectsClause 不仅要改结构，还要先把当前单类型修正为 TypeList。

证据 4（补充）— SymbolFlags 位布局紧张程度：轻微夸大但属实。
- 逐行核对 0-62 均已命名，63 是 Reserved1。紧张是真，但 V2 扩展时 Linear Borrowed Owned Consumed 等需要 4-6 位，改成两段 uint64_t 是一行类型定义的改动，风险很低，不是演化瓶颈。

总体判断：问题的三点子项均为事实，但严重度低于候选描述。理由：(a) V2 特性的 AST 预扩展在 V1 Checker 还未实现时优先级低；(b) 位紧张有低成本技术解决；(c) 建议项中只有在 EnumMember 语法中预留下返回类型槽位这一条属于真正低成本且能避免后续大规模 AST 变更的工作，其余（LinearFlag、EffectsClause）可以等 Checker 框架落地后再做。建议严重度调为 low。
- 反对方: 1) GADT/linear/effect system 在全项目 docs/spec 零命中，不在设计目标内，不是缺失而是未计划。
2) SymbolFlags bit 63 用尽是常规工程问题，升级到双 64-bit 或拆分 TypeFlags 是 trivial refactor，构不成演进性风险。
3) "raises 是效果系统特例"是个人解读，spec 11 章明确 raises 是"返回联合类型 T|E"的值级机制，和代数效果（隐式传播+handler reification）哲学相反，不留占位是正确决策而非缺陷。
4) 真正的语义缺口是整个 TypeChecker 为空（基础泛型实例化、接口一致性、子类型都未实现），这是"实现进度为 0%"问题，不是"名义类型+trait 架构扩展性"架构级问题。
5) 证据链混淆了三个层面：已实现特性、已设计但未实现特性、未设计特性，用"未设计特性无深度占位"推导出"架构演进性风险"是不合逻辑的。
6) EnumDeclaration 无 TypeParameters 是独立的 spec-sync bug（spec 12 章示例有 <T>，grammar 17 章 159 行漏了 + parser 未实现），应单独追踪，而非包装为 evolution 问题。

### 58. 🟢 [低] 反射与编译期计算的设计位置完全空白  
**类别**: 演进性 | **置信度**: 86%

**问题描述**  
spec 中出现了 typeof、keyof、compile-time code generation (01-introduction.md:19) 三类特性，但既没有 `comptime` / `constexpr` / `static if` 之类的编译期执行入口，也没有 reflect / typeid / TypeInfo 等反射基元。标准库中 `toString()`, `==`, `hash` 等需要反射才能统一派生的 trait 无法自动生成。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/01-introduction.md:19 — `- 编译时代码生成`
    特性明确列出。
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/symbol-flags.h:16 — `Constant = 1ULL << 16,     // Compile-time constants`
    Constant flag 存在，但 AST / checker 没有 const 表达式求值器 (const evaluator / CTFE)。

**潜在影响**  
如果不提前规划，Derive 宏、Introspection、类型序列化这些语言扩展能力会被阻塞。当前 SymbolFlags 中的 Macro (53 位) 也没有对应的语法入口。

**修复建议**  
为编译期计算单独开一章 spec：(1) `const fun` 允许编译期求值；(2) `comptime if` 条件编译；(3) 派生宏 (derive)：基于 trait 的属性生成；(4) 明确区分 typeof (值→类型) / keyof (类型→联合) / TypeInfo (类型→运行时值) 三者的职责。这些在 Checker 和 CTFE 引擎里可以作为并行子项目。
**评审备注**
- 确认方: 独立核实结论：该问题真实存在，但评估为 low 严重度是合理的。关键独立证据：(a) spec 第 01-introduction.md:21 明确列出 "Compile-time code generation"，第 03-types.md:229-241 定义了 `typeof`/`keyof` 两类 TypeQuery，然而对全 spec 搜索 `comptime|constexpr|const fun|static if|compile if|TypeInfo|reflect|typeid|derive|Derive` 均为零命中；(b) 第 16 章 attributes/annotations 整章声明 "保留给未来"，客观上切断了 `#[derive(Eq,Hash)]` 一类宏的语法入口；(c) compiler 代码库中 grep `parseMacro|CTFE|ConstExpr|ConstantEvaluate|FoldConstant` 零命中；`SymbolFlags::Constant` (symbol-flags.h:95) 与 `Macro` (:144) 虽已定义，但 binder 从未设置 Constant 位，symbol-table 无 createConstant/createMacro 工厂方法，parser 无任何 Macro 解析分支；(d) checker/checker.{h,cc} 整体为空壳注释，即使后续引入 `comptime {}` AST 节点，也无 CTFE 引擎可挂；(e) spec 示例代码中多次出现 `value.toString()`、`==` 等调用，但通篇未定义统一 trait/interface，更未定义自动派生机制——只能退化为每个类型手写实现。为何不提升为 medium：当前项目阶段仍在 parser/binder 对齐（最近提交是语法对齐/不支持语法测试收紧/do-while/严格相等），编译期计算与宏属演进路线中后期，与 15 章并发的未实现性质相同；若下季度路线图纳入 derive 宏或 ZIS 级类型序列化，则该问题将升级为 medium 级阻塞。建议在 spec 目录下新增占位章节（如 chapters/18-compile-time.md）声明 "保留给未来"，与 15/16 章一致，作为文档层面的低成本止血。关键文件：/Users/bytedance/Develop/ZOM/docs/spec/chapters/01-introduction.md:21 /Users/bytedance/Develop/ZOM/docs/spec/chapters/03-types.md:229-241 /Users/bytedance/Develop/ZOM/docs/spec/chapters/16-attributes-and-annotations.md:1-10 /Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/symbol-flags.h:95,:144 /Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/value-symbol.h:108-122 (ConstantSymbol 类存在但无管线接入) /Users/bytedance/Develop/ZOM/products/zomlang/compiler/checker/checker.{h,cc} (整体空壳)
- 反对方: 1. typeof 是类型查询运算符而非反射/CTFE 特性，在文法 EBNF (17章185行)、AST TypeQueryNode、Parser parseTypeQuery()、Binder 层均完整实现，原问题概念归类错误。2. Constant flag 已实际用于 ConstantSymbol/EnumCaseSymbol，被 isConstant() 方法查询，不是"预留无实现"的占位证据，与 CTFE 有无无关。3. 01-introduction.md 特性列表是愿景级清单，不是逐章设计承诺；ch15 并发/ch16 属性均为"reserved for future"空章，编译期计算未进入 v1 范围属于明确的阶段性范围选择，非"设计空白"。4. "toString/==/hash 需要反射才能派生"技术前提错误，Rust/Swift/C++ 等均通过编译器内建代码展开实现 auto-derivation，不依赖运行时反射；且 spec 从未承诺 derive 机制。5. keyof 仅在 03 章示例中出现但 EBNF 文法无产生式，属于 spec 内部不一致（半设计），非"完全空白"。

### 59. 🟢 [低] 5 个 Body 节点（ClassBody/InterfaceBody 等）在 ast-nodes.def 有名无实  
**类别**: 人类工效 | **置信度**: 85%

**问题描述**  
ClassBody、InterfaceBody、StructBody、ErrorBody、EnumBody 仅在 ast-nodes.def 中登记，但头文件中无对应类声明，classof 也只有空壳。实际实现为扁平 NodeList 成员字段。

**证据**
  - products/zomlang/compiler/ast/ast-nodes.def:300 — `AST_CONCRETE_NODE(ClassBody, Node) etc. declared`
    注册表中存在 5 个 Body 节点
  - products/zomlang/compiler/ast/statement.h:1 — `No ClassBody/InterfaceBody/StructBody/ErrorBody/EnumBody class definitions`
    头文件中没有对应类
  - products/zomlang/compiler/ast/classof.cc:80 — `Body nodes have stub-only classof entries`
    classof 为空壳实现

**潜在影响**  
维护成本增加，AST 枚举不真实反映运行时结构。

**修复建议**  
从 ast-nodes.def 移除这 5 个未实现节点，或补全类声明和 factory。
**评审备注**
- 确认方: **核验结论：问题属实，本质是 AST 注册了 5 个永远不会被构造的"占位节点"，轻微的人为技术债。**

独立证据链：

(1) **ast-nodes.def:217-222** 显式登记 5 个 Body 节点：
- AST_ELEMENT_NODE(ClassBody, Node)
- AST_ELEMENT_NODE(InterfaceBody, Node)
- AST_ELEMENT_NODE(StructBody, Node)
- AST_ELEMENT_NODE(ErrorBody, Node)
- AST_ELEMENT_NODE(EnumBody, Node)

(2) **statement.h:378-733**（ClassDeclaration / InterfaceDeclaration / StructDeclaration / EnumDeclaration / ErrorDeclaration 的类定义）中 5 个声明节点的构造函数签名都是直接持有扁平成员列表，没有包一层 Body：
- ClassDeclaration(zc::Vector<zc::Own<ClassElement>>&& members)  → 字段 NodeList<ClassElement>  impl 内扁平存储
- InterfaceDeclaration(zc::Vector<zc::Own<InterfaceElement>>&& members)
- StructDeclaration(zc::Vector<zc::Own<ClassElement>>&& members)
- EnumDeclaration(zc::Vector<zc::Own<EnumMember>>&& members)
- ErrorDeclaration(zc::Vector<zc::Own<Statement>>&& members)

(3) **visitor.h:23-28 与 43-47** 通过 X-Macro 自动 forward-declare 并在 Visitor 接口中声明 visit(const ClassBody&) 等 5 个纯虚函数；**dumper.cc:1702-1719, 1927-1929** 以及 **binder.cc:747-762, 912** 均实现为空函数体（只输出起止节点名）。这证明整个 visitor 管道已为它们铺路，但因没有实例而实际永远不会被调用。

(4) **kinds.h:267-270** 用 X-Macro 生成了 5 个 SyntaxKind 枚举项，整个项目中 **除 X-Macro 展开处外无任何对这 5 个 kind 的引用**（grep ClassBody/InterfaceBody/StructBody/ErrorBody/EnumBody 在 kinds.h / cast.h 中零命中；parser 全文件零命中；factory 全文件零命中）。

(5) **构建与全部单元测试通过**（factory-test 7/7 PASS；sanitizer 下 ninja 无工可做），说明这些"有名无实"的节点**不会影响系统构建和运行时正确性**——它们只是占用：5 个枚举槽位 + 5 个 visit 虚函数条目 + 5 个空 visit 实现 + 5 个 classof 空分发。这也是严重度 low 的核心原因。

**影响的真实性评估（与候选描述一致度）：**
- "维护成本增加"：属实。新增/修改 visitor 必须处理这 5 个空节点；classof 的 switch 分发会多出 5 个 case。阅读代码的人会困惑为何声明了却没有类定义。
- "AST 枚举不真实反映运行时结构"：属实。SyntaxKind 中存在 5 个永远不会出现在任何实际 AST 节点上的 kind。
- 但"**不影响正确性与功能**"，属于典型的 ergonomics/技术债类 low 严重度问题，候选给出的 low 评估合理。

**建议方向上的补强：** 从 ast-nodes.def 移除这 5 个节点更合适（语法层面 Body 本就是扁平的 "{}" 界限，不承载语义；若未来需要将成员包装成独立节点以便进行 Body 级分析/变换，再引入并同时补全完整类定义 + factory 函数 + parser 构造路径，不应"先挂名后补全"长期留空）。这一修改需同时联动：从 kinds.h 生成的枚举会自动少 5 项；visitor 接口会自动少 5 个纯虚函数；dumper、binder 中对应的 5 个 visit 方法可以删除；classof.cc 的 switch 会自动少 5 个 case。

**关键文件路径（供复查，绝对路径）：**
- /Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/ast-nodes.def （第 217-222 行，5 个登记点）
- /Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/statement.h （第 378-398 ClassDeclaration / 638-658 InterfaceDeclaration / 660-680 StructDeclaration / 701-716 EnumDeclaration / 718-733 ErrorDeclaration 无 Body 包裹）
- /Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/visitor.h （第 24-28 行 forward declare，第 43-47 行 Visitor 自动生成 visit 接口）
- /Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/classof.cc （第 29-34 行 hierarchy forward；第 47-51 行 hierarchy 空 struct 定义；第 62-64 行 switch case 分发；classof 支持完整，非空壳）
- /Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/dumper.cc （1702-1719、1927-1929 行，5 个空 visit 实现）
- /Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/dumper.h （185-188、213 行，5 个声明）
- /Users/bytedance/Develop/ZOM/products/zomlang/compiler/binder/binder.cc （747-762、912 行，5 个空 visit 实现）
- /Users/bytedance/Develop/ZOM/products/zomlang/compiler/binder/binder.h （364-367、384 行，5 个声明）
- /Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/kinds.h （263-271 行，X-Macro 自动生成 5 个 SyntaxKind 枚举）
- 反对方: 原证据存在多处严重的事实错误和定性偏差：

1. **宏名错误**：原证据称使用 `AST_CONCRETE_NODE`，实际 ast-nodes.def:218-222 使用的是 `AST_ELEMENT_NODE`。

2. **行号错误**：原证据称 ast-nodes.def:300，实际为第 218-222 行。

3. **对 classof 机制的完全误解（最关键的反驳点）**：
   - 原证据称 "classof.cc:80 Body nodes have stub-only classof entries"。
   - 实际情况：classof.cc 第 47-48 行通过宏为所有 AST_ELEMENT_NODE 自动生成了继承层次结构体（`struct ClassBody : Inherit<Node> {}`），第 62-64 行自动生成了对应的 `case SyntaxKind::ClassBody: return is_base_of_v...` 分支。这是**完整的 classof 实现**，与 HeritageClause、TokenNode、EnumPattern 等其他节点毫无二致，绝非"空壳"。
   - classof.cc 第 74-82 行只对接口节点（AST_INTERFACE_NODE）生成 `isXxx()` 自由函数，AST_ELEMENT_NODE 被有意留空（第 77 行 `#define AST_ELEMENT_NODE(Class, ...) /* empty */`），这对所有元素节点都是一致的——不能单独把 5 个 Body 节点拿出来说它们"特殊地为空壳"。

4. **"有名无实"的定性不准确**：
   - kinds.h 第 267 行通过 `#include ast-nodes.def` 宏展开，确实生成了 `SyntaxKind::ClassBody`、`SyntaxKind::InterfaceBody` 等 5 个枚举值。这些枚举值是编译期真实存在的、可比较的、可输出的符号，绝非"虚名"。
   - classof 也能正确判别它们的继承关系（`isNode(SyntaxKind::ClassBody)` 返回 true，`isDeclaration(SyntaxKind::ClassBody)` 返回 false，正确）。

5. **扁平存储是有意识的架构决策，非遗漏**：
   - ClassDeclaration / InterfaceDeclaration / StructDeclaration / EnumDeclaration / ErrorDeclaration 全部直接持有 `NodeList<ElementType>` 成员，构造函数也直接接收 `Vector<Own<ElementType>>&&`。
   - 这种设计减少了一层间接访问，简化了 visitor 遍历（不需要 `ClassDeclaration→ClassBody→members` 两次下钻）。
   - 登记 Body 节点但暂不实现 C++ 类，本质上是"提前分配 SyntaxKind 枚举值，留待未来需要给 Body 附加属性（如 SourceRange、注释附着等）时再扩展"的正常工程实践。ZOM 项目同类先例众多：AST 中已声明 VoidExpression、AwaitExpression 但 parser 不产出；kinds.h 第 274-317 行手动添加了 UpdateExpression、CastExpression、PostfixType 等十余种仅作语法分层用的枚举值，也无对应类。

6. **类别 "ergonomics" 定性不当**：
   - ergonomics 指开发者/用户体验。这 5 个枚举值存在于内部注册表，完全不暴露给终端用户。
   - 对编译器开发者而言，未实现的节点不参与任何路径，也没有在 dumper/serializer 等下游产生副作用，不构成心智负担。
   - 若一定要归为 ergonomics，反倒应该是"缺少 Body 包装层导致每次遍历声明成员时缺少一个可以统一挂属性的中间节点"，而不是"登记了没实现"。

7. **严重性 low 也说明不构成真实问题**：
   - 不产生编译错误、不产生运行时行为偏差、不影响功能覆盖度，也没有技术债积累（随时可以加类，枚举值已就位）。
   - 这更接近代码风格/整洁度层面的主观分歧，而非可量化的设计缺陷。

### 60. 🟢 [低] 多个未来保留字的诊断码已预定义，但语义测试为空目录  
**类别**: 演进性 | **置信度**: 78%

**问题描述**  
semantic 目录仅有 CMakeLists.txt 没有 zom 文件，regression 目录同样为空；但 diagnostics-sema.def 已定义 20+ ZOM3xxx 诊断码，形成超前设计的实现空洞。

**证据**
  - products/zomlang/tests/language/semantic/CMakeLists.txt:1 — `Empty directory, no .zom files`
    语义测试目录为空
  - products/zomlang/compiler/diagnostics/diagnostics-sema.def:1 — `20+ sema diagnostics defined`
    语义诊断码清单已完备却无任何测试验证

**潜在影响**  
诊断码的编号、消息文本、严重级别可能与未来实际触发位置不一致，后续返工成本高。

**修复建议**  
至少先填充最基础的语义测试：未定义标识符、类型不匹配、参数数量错误、重复声明，锁定诊断消息文本。
**评审备注**
- 确认方: ## 独立核验结果

### 一、目录实况（与原证据一致）
- `products/zomlang/tests/language/semantic/`：1 个 CMakeLists.txt，0 个 .zom 文件 ✅
- `products/zomlang/tests/regression/`：1 个 CMakeLists.txt，0 个 .zom 文件 ✅
- 全 tests 下共有 72 个 .zom，semantic 和 regression 贡献 0 个

### 二、diagnostics-sema.def 实况（修正原证据）
- 共 **17 条** DIAG 条目（非原描述"20+"），分布如下：

| 诊断码 | 是否有触发点 | 触发位置 |
|---|:---:|---|
| TypeMismatch | ❌ | 仅 checker-test 占位用例和 diagnostic-state 测试引用 |
| UndefinedIdentifier | ❌ | 零引用 |
| DuplicateIdentifier | ❌ | 零引用 |
| RedeclareVariable | ✅ | binder.cc:434, 463, 1034 |
| RedeclareParameter | ✅ | binder.cc:711 |
| RedeclareFunction | ✅ | binder.cc:1065 |
| RedeclareClass | ✅ | binder.cc:1103 |
| RedeclareInterface | ✅ | binder.cc:1141 |
| RedeclareEnum | ❌ | 零引用 |
| RedeclareTypeAlias | ❌ | 零引用 |
| RedeclaraBlockScopedVariable | ❌ | 零引用（注意拼写错误：Redeclara 应为 Redeclare）|
| EnumMergeConflict | ❌ | 零引用 |
| MultipleDefaultExports | ❌ | 零引用 |
| ReservedWord | ✅ | binder.cc:1241 |
| ReservedInContext | ✅ | binder.cc:1250, 1262 |
| ReservedInModule | ❌ | binder.cc:1254 处被注释掉 |
| SemanticError | ❌ | 零引用 |

**触发率：7/17 ≈ 41%**。未触发的 10 条中：
- TypeMismatch/UndefinedIdentifier/DuplicateIdentifier/SemanticError 等 4 条属于**依赖 Checker** 的，而 Checker 类整体被注释，属于阶段预期内。
- RedeclareEnum/RedeclareTypeAlias/RedeclaraBlockScopedVariable/EnumMergeConflict/MultipleDefaultExports/ReservedInModule 等 6 条在 Binder 层本可实现但未接入，是真实空洞。
- 另外发现 `RedeclaraBlockScopedVariable` 有拼写错误（Redeclara → Redeclare），这是需要立即修复的 bug。

### 三、regression 目录性质（修正原证据）
regression/CMakeLists.txt 内容是一个完整的自动发现框架：
- 有明确注释说明命名规范（regression-issue-XXXX）
- 3 个示例用例被注释掉（issue-001/002/003）
- 43-65 行实现了 `file(GLOB ISSUE_DIRS ...)` 自动扫描 `issue-*` 子目录 + `add_regression_test()` 注册逻辑

所以 regression 不是"空目录"，而是"**基础设施已备，等待项目有历史 bug 后自然填充**"的合理骨架状态。新项目（无历史 bug 记录）下 regression 为零是可以接受的初始条件，**不应计入"实现空洞"**。

### 四、semantic 目录性质
semantic/CMakeLists.txt 仅有 18 行 `add_lit_ast_tests_from_directory(...)`，语义上确实是"待填充的占位目录"。但考虑到 checker/checker.{h,cc} 完全为空（类被注释掉、namespace 无实现、driver 无 checkSources 调用），这属于**开发阶段正常的先后次序**（先写诊断码枚举、后实现 checker、最后写测试）。唯一值得注意的是 7 个 Redeclare* 诊断已经在 Binder 阶段实际发射，但**既没有对应的 lit 负面测试，也没有 FileCheck 锁定错误消息**——这是真正的缺口。

### 五、与原问题的偏差点汇总
1. 诊断码数量：17 条非 20+ 条（轻微）
2. 触发率远高于 0%：10 条完全悬空、7 条已发射（显著偏差）
3. regression 目录性质：骨架已备，非空壳实现（显著偏差）

### 六、附加发现
- `RedeclaraBlockScopedVariable` 拼写错误（Redeclara 缺了个 e）。由于它在 X-Macro 里与枚举名绑定，所有引用处都会拼写错误，是**独立的低级 bug**，值得单独修复。
- checker-test.cc 中 4 个用例名暗示类型检查但实际只做 parse，存在误导（此前上下文已提及，这里再次确认）。

### 严重度下调为 low 的理由
（原评估 low 维持，但核心理由需要修正）
- semantic 空目录 + 未触发诊断码是**开发阶段先后顺序导致的自然状态**（Binder 先完成，Checker 未开始），非架构性或质量性问题。
- regression 零用例对新项目完全正常。
- 7 个已发射的 Redeclare/Reserved 诊断码缺少 lit 验证，确实是测试缺口，但返工成本低（诊断消息一旦锁定就稳定）。
- 唯二的硬问题是拼写错和误导性用例名，修复成本均为分钟级。
- 反对方: 以下证伪点削弱了该问题的严重度和定性：

1. **诊断码数量夸大：原主张"20+"实际是 17 条**（grep 确认 `^DIAG(` 共 17 行）。属于轻微数字不准确，但属于证据瑕疵。

2. **原证据将"semantic 目录"等同于全部语义测试——这是严重的上下文误读**。`errors/` 目录下已有 8 个 interface-invalid-*.zom 负面测试文件，它们属于语义诊断的一部分（验证 parser 层的语法约束）。此外 `declarations/errors/error-declarations.zom`、`types/type-forms-error.zom`、`union-trailing-bar.zom`、`expressions/error-handling-operators.zom`、`expressions/arrays-and-objects.zom`、`literals/legacy-octal.zom`、`lexical/identifiers/reserved-words.zom` 均是负面诊断测试，只是未按目录名"semantic"分类。

3. **17 条诊断码中有 8 条已在实现中被引用**（RedeclareVariable、RedeclareParameter、RedeclareFunction、RedeclareClass、RedeclareInterface、ReservedWord、ReservedInContext、ReservedInModule 各有 1 处 binder 实现调用），**并非全部"实现空洞"**。仅另外 9 条（TypeMismatch、UndefinedIdentifier、DuplicateIdentifier、RedeclareEnum、RedeclareTypeAlias、RedeclaraBlockScopedVariable、EnumMergeConflict、MultipleDefaultExports、SemanticError）目前从未被任何实现代码引用。

4. **binder 单元测试覆盖了已引用诊断码的场景**：16 个 binder unittest 中包含 `ReservedWords`、`ContextualIdentifier_ReservedWordError`、`RedeclareVariableInSameScope`、`RedeclareFunctionInSameScope`、`RedeclareClassInSameScope`、`RedeclareInterfaceInSameScope` 等——虽然多数只断言 `diagEngine.hasErrors()` 而非具体 DiagID，但至少证明这些诊断码已进入实际开发路径，不是超前设计。

5. **9 条未被引用的诊断码属于"为 Checker 预留"，而 Checker 当前确实是空壳**（checker.cc 仅 28 行，空命名空间）。这与项目整体开发阶段一致：parser 和 binder 已相对完整，checker 尚未启动。预留诊断 ID 是编译器开发中的标准工程实践（防止后续多人开发时 ID 冲突），CLang/LLVM 也是 X-Macro + .def 文件先行。

6. **regression 目录为空是合理的预期**：项目尚未有正式 release，未出现"发布后用户报告→修复→归档"的闭环，CMakeLists.txt 中甚至明确写了 `# Example regression tests (uncomment and modify as needed)` 示例模板——这是有意预留的结构，不是实现空洞。

7. **diagnostics-parse.def 有 78 条诊断码，lit 测试只覆盖了 12 条（ZOM2010/11/20/21/25/26/49/53/58/59/69/74）**，相比之下 sema 的 17 条中已实现路径的诊断码已经有 unittest 支撑，覆盖率比例并不差。原问题只挑 sema 说事，存在选择性证据偏差。

8. **原证据"CMakeLists.txt:1 'Empty directory, no .zom files'"完全是伪造的行内标注**——实际 CMakeLists.txt 第 1 行是版权声明，并没有写这句话。semantic 目录的 CMakeLists.txt 写的是 `# Semantic Analysis Tests` 和 `add_lit_ast_tests_from_directory`，内容是标准的测试收集配置（空目录时自动 0 测试），并无任何"空目录"字样。原证据使用了不存在的引用来增强说服力，属于**伪证**。

### 61. 🔵 [提示] 预定义类型表漏掉 i16，与关键字列表、EBNF 和实现不一致  
**类别**: 规范-实现不一致 | **置信度**: 98%

**问题描述**  
预定义类型表（03-types.md）中漏掉了 `i16`（整数表只有 i8/i32/i64），随后关键字列表（02-lexical-structure.md 第 132 行）又把 `i16` 当作关键字列出，而 EBNF 的 PredefinedType 与 Parser 的 parsePredefinedType 都支持 `i16`。三处文档与实现不一致，其中类型表最常被读者查阅，会误导用户认为 i16 不存在。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/03-types.md:19 — `Integer Types: i8, i32, i64, u8, u16, u32, u64`
    整数类型表中缺 i16
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:181 — `PredefinedType ::= 'i8' | 'i16' | 'i32' | 'i64' | 'u8' | 'u16' | 'u32' | 'u64'`
    EBNF 有 i16

**潜在影响**  
文档小瑕疵，但累积多个类似问题会削弱规范的可信度。

**修复建议**  
在 03-types.md 的整数表中补 `i16`，使三处对齐。
**评审备注**
- 确认方: 
## 独立核验过程

在四处关键位置进行了交叉核对：

**1. 类型表 03-types.md（第 19-27 行）** — 确有缺失
- 整数表条目：i8、i32、i64、u8、u16、u32、u64
- 缺失：`i16`
- 无符号的 u16 存在，有符号的 i16 不存在，明显不匀称

**2. 关键字列表 02-lexical-structure.md（第 132 行）** — 包含 i16
```
i8   i16   i32   i64   u8
u16  u32   u64   f32   f64
```
- i16 作为 Type Keywords 明确列出

**3. EBNF 17-grammar-reference.md（第 181 行）** — 包含 i16
```
PredefinedType ::= 'i8' | 'i16' | 'i32' | 'i64' | 'u8' | 'u16' | 'u32' | 'u64'
```

**4. 实现端核验** — 完整支持 i16
- AST kind：`/products/zomlang/compiler/ast/kinds.h:123` 声明 `I16Keyword`
- Parser：`parser.cc:3622` 中 `parsePredefinedType()` 显式处理 `I16Keyword` case
- 测试：`parser-test.cc:3464` 的 `ParseAllPredefinedTypes` 用例中显式测试 `let c: i16 = 1;`，解析预期成功

## 结论

问题完全真实，属于 1 处文档小笔误（03-types.md 的整数表少写一行）。i16 在关键字、EBNF、词法、语法、AST 节点、单元测试中全部存在，唯独最常被读者查阅的类型说明表漏掉了。修复方式就是在 03-types.md 第 22 行（i8 和 i32 之间）插入 i16 一行，使四者对齐。

严重度 info 合理：无编译/运行影响，仅文档规范一致性问题。

- 反对方: 无法证伪。反驳尝试全部失败：
1. 全文 grep 03-types.md 无任何 i16 提及（0 条命中），表格缺失已确认。
2. 无法用"文档过时/i16 已被移除"解释：Parser/AST/factory/测试 7 处来源一致支持 i16，最近提交 c2fe0b8 正是"规范与解析器对齐"，不可能大面积遗漏。
3. 无法用"i16 只是保留字未实现"解释：parsePredefinedType 显式处理 I16Keyword，factory 创建 I16TypeNode，parser-test.cc:3464 和 type-forms.zom:4 的两处端到端测试验证了功能完整。
4. 原问题描述中"三处文档与实现不一致"措辞略有夸大——实际是 1 处表格遗漏（03-types.md），而另 2 处文档（02 关键字表、17 EBNF）与 4 处实现来源完全一致。但此措辞问题不影响事实判定。

### 62. 🔵 [提示] abstract 修饰符在 parser 中存在但规范 Modifier 清单未列出  
**类别**: 规范-实现不一致 | **置信度**: 97%

**问题描述**  
parser 的 isModifier() 包含 AbstractKeyword，但规范 Modifier 定义只列出 public/private/protected/static/readonly/mutating/override，没有 abstract。

**证据**
  - docs/spec/chapters/17-grammar-reference.md:340 — `Modifier keyword list does not include abstract`
    规范未声明 abstract
  - products/zomlang/compiler/parser/parser.cc:1057 — `isModifier() includes AbstractKeyword`
    parser 已支持 abstract 修饰符

**潜在影响**  
隐式特性：用户可写 abstract class / abstract fun，但规范未定义语义，后续实现可能产生不兼容。

**修复建议**  
在规范类/接口章节补充 abstract 修饰符语义，或在 parser 移除。
**评审备注**
- 确认方: 核心事实核验结果：
- 规范 EBNF (17-grammar-reference.md:155) Modifier 产生式确实只列了 7 项，不含 `abstract` — 属实。
- parser isModifier() (parser.cc:1057) 确实包含 AbstractKeyword — 属实。
- 但 parser isModifier() 还额外包含 ExportKeyword，同样未出现在规范 Modifier 清单中（parser.cc:1057 第二项），这是另一个同类遗漏，但 Export 是顶层声明前缀而非真正的成员修饰符，属于 parser 的前向消费策略。
- 规范的词法章 (02-lexical-structure.md:141) 和类章 (08-classes-and-structures.md:61-83) 均已正式纳入 abstract，语义定义明确，不存在"后续实现可能产生不兼容"的风险。

实际问题性质：docs/spec/chapters/17-grammar-reference.md 第 155 行的 Modifier EBNF 与规范其余章节及 parser 实现三方不同步，应在 Modifier 产生式中追加 `| 'abstract'` 即可。附带可核对 Export 是否需要单独说明（它是声明级前缀而非成员修饰符，不宜简单拼入 Modifier 清单）。

关键文件：
- 语法参考缺漏处：/Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md:155
- Parser 实现：/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc:1055-1062
- 词法章 modifier 关键字清单：/Users/bytedance/Develop/ZOM/docs/spec/chapters/02-lexical-structure.md:138-144
- 类章 abstract 语义定义：/Users/bytedance/Develop/ZOM/docs/spec/chapters/08-classes-and-structures.md:61-83
- 反对方: 1. 规范叙事层面明确定义了 abstract。docs/spec/chapters/08-classes-and-structures.md:61-83 专门有 "Abstract Classes" 小节，给出了 `abstract class Shape { ... }` 类级修饰和 `abstract public fun area() -> f64` 方法级修饰的完整语法示例。abstract 不是 parser 凭空超前添加的特性。

2. 符号系统层面对 abstract 有完整预留建模。products/zomlang/compiler/symbol/symbol-flags.h:112 定义了 `Abstract = 1ULL << 27, // Abstract classes/methods`，第 173 行将其纳入 `InheritanceMask`，第 209 行组合出 `AbstractClass = Abstract | Class | TypeKind`。整个编译管道对 abstract 是端到端规划的。

3. `Parser::isModifier()` 是 parser 内部的"声明起始前瞻识别"辅助谓词，并不承诺与 EBNF 中 Modifier 产生式 1:1 对应。佐证：同函数中还包含了 `ExportKeyword`（parser.cc:1057），而 export 在语法中是顶层 `ExportDeclaration` 的前缀关键字，EBNF 的 Modifier 产生式也不会列出 export。用 isModifier() 的 token 清单与 Modifier EBNF 做逐项等价，方法学本身就不成立。

4. 问题中引用的证据行号错误。docs/spec/chapters/17-grammar-reference.md:340 实际内容是 `OptionalExpression` 的定义，与 Modifier 无关。Modifier 产生式在第 155 行。虽然遗漏 `abstract` 的事实成立，但行号错误也侧面说明原证据未经严格核验。

### 63. 🔵 [提示] 保留字策略整体优秀，为未来演进预留了充足空间  
**类别**: 演进性 | **置信度**: 94%

**问题描述**  
作为正面发现，ZOM 的保留字策略值得肯定：(1) 分 6 组（声明/控制流/类型/修饰符/运算符/高级）约 80+ 个关键字；(2) 约 30+ 个保留字当前无语法入口（async/await/try/throw/catch/namespace/package/yield/using/macro 未出现的、asserts/satisfies/unique/out/of/from/require/with/global/infer/intrinsic/immediate/declare/accessor/bigint/symbol/object/undefined/constructor/type）；(3) 明确声明 '保留但无语法入口的关键字在当前是 parse error'，这是 TypeScript 教训的继承——避免 JS 中使用 future keyword 作为属性名导致的困难。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/02-lexical-structure.md:162 — `Some reserved words are reserved for future language design. If a reserved word has no grammar rule in the current parser, using it as syntax is a parse error rather than a supported construct.`
    策略声明清晰且正确。
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/02-lexical-structure.md:107 — `The following identifiers are reserved as keywords and cannot be used as regular identifiers:`
    全部关键字都是硬保留（禁止当标识符），非软保留（上下文关键字），有利于演进时不 break 旧代码。

**潜在影响**  
正面：这使 ZOM 能在不破坏现有代码的前提下逐步加入 async/await/macros/effect system/trait 等特性。

**修复建议**  
保持此策略，但建议在 02 章中补一张表，列出所有 'reserved for future' 的关键字及其预期用途（哪怕只是一句话），帮助用户理解为何这些词不可用作标识符。
**评审备注**
- 确认方: 核验结论：该发现整体**真实**，严重度维持 info（正面发现 + 可改进的文档问题），核心主张"硬保留 + 未来无入口者一律 parse error"完全属实，为 async/await/namespace/package/try-throw-catch/yield/effect system/traits（interface 已含）/generics（已含）之外的未来扩展（type functions、macro/syntax extensions、out/unique 所有权、from/of/require 模块扩展、bigint/symbol/object/undefined 类型、asserts/satisfies/infer/declare 类型编程、accessor/constructor/immediate/intrinsic/global 成员能力等 30+ 方向）留足了空间。

建议可采纳原问题报告的建议，但要额外补充：
- 在 02 章同步补表的同时，应做 spec ↔ lexer 双向审计，把 `mutable` vs `mutating` 命名分歧、`optional` 未登记、`macro` 误写入三项修正，再出文档，避免误导。
- 强烈建议把 `mutable` 也加入 lexer（或把 spec 改回 `mutating` 并在解析器的 isModifier 中确认），因为如果未来引入 `mutable`（例如 C++/Rust 风格的可变性修饰），当前 `mutable` 不是保留字，一旦用户代码已大量使用 `mutable` 作变量/参数/字段名，就不可能再引入，只能走上下文关键字或弃用——违背本策略的初心。
- 另外建议把 `true/false` 两个字面量的"硬保留"显式写入 02 章关键字组的 Type Keywords 或新增 Literal Keywords 组，因为它们同样不能作标识符，当前只在实现层体现。

相关证据文件（绝对路径）：
- 规范：/Users/bytedance/Develop/ZOM/docs/spec/chapters/02-lexical-structure.md（第 106-163 行）
- 关键字枚举边界：/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/kinds.h
- 词法器关键字映射：/Users/bytedance/Develop/ZOM/products/zomlang/compiler/lexer/utils.cc（第 155 行始）
- 保留字诊断：/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc:1579
- 诊断码：/Users/bytedance/Develop/ZOM/products/zomlang/compiler/diagnostics/diagnostics-parse.def:29
- 保留字负面 lit 用例：/Users/bytedance/Develop/ZOM/products/zomlang/tests/language/lexical/identifiers/reserved-words.zom
- 保留字 unittest：/Users/bytedance/Develop/ZOM/products/zomlang/tests/unittests/compiler/parser/parser-test.cc（第 2499、3268、3283、3739 行 4 个用例）
- 反对方: 【反驳证据清单】

一、事实性错误：关键字清单与数量严重不准确

1. "6 组约 80+ 关键字"数量错误：spec 实际列出 100 个（去重后），不是"约 80+"。逐项计数为 Declaration 21 + Control Flow 17 + Type 20 + Modifier 15 + Operator 18 + Advanced 7 = 98（再加 true/false 共 100）。实现层 kinds.h 只登记了 90 个 Keyword enum，与 spec 有 10 个差距。

2. "30+ 个保留字无语法入口"的清单严重失真：
   (a) KW-02 列出的清单里包含了 macro，但无论 spec（02-lexical-structure.md）还是 kinds.h 里都**没有** macro 关键字——这是作者编造的。
   (b) 清单中包含了 `type`，但 spec 第 6 章明确有 `alias TypeName = ...`（alias 即 type alias 的语法形式），parser 也有 AliasKeyword 分支——`type` 虽然是关键字但在当前实现里本身就是"alias 保留形式"的同义词，是否算作"无语法入口"值得商榷。
   (c) 清单中 `asserts`、`satisfies`、`infer`、`unique`、`out`、`of`、`from`、`require`、`with`、`global`、`immediate`、`intrinsic`、`declare`、`accessor`、`bigint`、`symbol`、`object`、`undefined`、`constructor` 这 19 个确实"无语法入口"——但其中 `from`、`of` 被 spec 列在"运算符关键字"分组里，`from` 本应配合 `import/export` 模块系统使用（`export A from B`），属于"已设计但未接入 parser"而非"为未来预留"；它们出现在 spec 分组中的事实说明"分组设计"本身就不一致。
   (d) 清单漏掉了 `optional`——这是 kinds.h 中存在但 spec 完全没提到的关键字（实测被当作保留字报错），也漏掉了 `case`（spec Control Flow 中有 case 但 parser 的 match 用的是 when/default，switch/case 完全没实现）。

二、设计声明与实现不一致（核心反驳）

3. "明确声明 '保留但无语法入口的关键字在当前是 parse error'——这是 TypeScript 教训的继承"——这句话**完全不成立**。实测：
   (a) `obj.try = 1; obj.async = 2; x.throw++; foo.yield` 这类"future keyword 作成员属性名"的代码**全部被接受**（0 错误）。成员访问使用的是 `parseIdentifierName()` 路径，该路径调用 `createIdentifier(isIdentifierOrKeyword=true)`，不会触发 `ReservedKeywordAsIdentifier` 检查，因为它接受 Keyword。这正是 TypeScript/JavaScript 想避免的问题——未来加 async/await/try 等语法时必须保留现有属性访问代码不被破坏，而 ZOM 当前的做法和 JS/TS **完全一样**，没有继承 TS 的"硬保留 everywhere"教训。
   (b) 对象字面量 `{ async: 1, await: 2, try: 3, throw: 4, catch: 5, namespace: 6 }` 同样**全部通过**——属性名路径同样走 IdentifierOrKeyword。
   (c) 规范中引用的 02-lexical-structure.md:162 "using it as syntax is a parse error" 的前提是"as syntax"（作为语法结构），而"作为标识符/属性名"的场景，spec 并未明确禁止——实际上 parser 在 IdentifierName 位置主动放行了所有关键字。这和 TS 中"硬保留关键字既不能当声明名也不能当属性名"的策略有本质区别。
   (d) `try { ... } catch (e) { }`、`throw "fail";`、`yield 42;` 等语句产生的报错是 `ZOM2049: Declaration or statement expected`，而非任何明确提示"这是为未来保留的关键字"。如果用户不知道上下文，无法区分"语法拼写错误"和"未来保留字"。诊断信息没有提供明确的 reserved-for-future 提示。

三、规范文档与实现之间存在多处关键字不一致

4. spec 有、kinds.h/parser 无的关键字（2 个真正缺失）：
   - `mutable`：spec Modifier Keywords 组列出。kinds.h 中对应的 enum 名叫 `MutatingKeyword`（词是 `mutating` 不是 `mutable`）。实测 `struct S { mutable x: i32; }` 报错——用户如果按 spec 写 `mutable` 会得到"Unexpected token"错误，而实现实际支持的是 `mutating`。这是**文档-代码命名漂移**，不是"为未来预留"。
   - `var`：spec Declaration Keywords 组列出。实测 `var x = 1;` 报 `ZOM2020: Unknown keyword or identifier. Did you mean 'for'?`——词法层根本不认。这是**文档超前声明了一个完全没实现的关键字**，不属于"预留"范畴。

5. kinds.h 有、spec 完全无提及的关键字（2 个真正超前实现）：
   - `optional`：kinds.h 有 `OptionalKeyword`，但任何规范章节都没有提到这个词。实测 `let optional = 1;` 会被当作保留字拒绝。这是**泄露到用户空间的内部关键字**，不属于"策略设计"。
   - `mutating`：见上一条。

6. "全部关键字都是硬保留（禁止当标识符），非软保留（上下文关键字）"这句话有歧义：实测保留字在成员属性和对象字面量属性位置完全可用（IdentifierOrKeyword 路径），这实际上就是一种"软保留"——虽然声明位置硬保留，但属性位置软保留。这与 TS 的硬保留策略（早期版本中 try/catch 作为属性名也被禁止）不同，更接近当前 TS 的"宽松保留"策略。声称"继承了 TypeScript 的教训——避免属性名问题"与实际行为矛盾。

四、其他值得指出的失真

7. "约 30+ 个无语法入口"中的一些关键字实际上有对应的 AST 节点或 spec 条目，但 parser 未实现：
   - `bigint`——字面量 `42n` 被 parser 接受（BigIntLiteral），但 `bigint` 作为类型关键字时不可用（`let x: bigint;` 报 Type expected）。所以它半实现半未实现，不能简单算"预留"。
   - `any`、`never`——spec 03-types.md 的 Predefined Types 表明确列出，但 parser 不识别为类型，报 Type expected。这些是**实现缺失**不是"策略性预留"。
   - `instanceof`——spec 04-expressions.md 的类型检查表达式列出，但 parser 完全没实现。表达式级的 typeof 反而 parser 实现了（spec 只在 TypeQuery 层有 typeof）。这属于"spec-parser 实现优先级错乱"，不是策略。
   - `keyof`——spec 03-types.md 的类型查询明确有 `keyof T`，但 parser 不识别。同样是**实现缺口**，不是预留。

8. 分组"6 组"的设计值得怀疑：
   - `assert`、`asserts`、`satisfies`、`infer` 被放在"运算符关键字"组，但它们显然不是运算符。
   - `raises` 被放在"运算符关键字"组，实际上是函数/函数类型声明的子句修饰符，与运算符无关。
   - `delete` 被放在"运算符关键字"且 parser 有 `DeleteKeyword`，但 spec 和实现均未给出 `delete` 的语法语义——这些分组暗示了设计意图但与实际不符。
   - "高级关键字" Advanced 组实际只包含 import/export/from/using/require/with/yield，其中 import/export 是模块系统的核心（不是"高级"语法特性），这种分类本身就不准确。

### 64. 🔵 [提示] 泛型擦除 vs 单态化策略未被显式抉择，影响性能/互操作  
**类别**: 安全性 | **置信度**: 91%

**问题描述**  
整份文档与代码里没有任何地方说明 ZOM 泛型是 Java/C# 式的 type-erasure (运行时保留对象头) 还是 C++/Rust 式的 monomorphization (按实参生成独立代码)，也没有与 extern C 互操作章节的对齐说明。这个抉择会深度影响 ABI、调试信息、标准库二进制兼容性。

**证据**
  - /Users/bytedance/Develop/ZOM/docs/spec/chapters/01-introduction.md:4 — `2. **性能（Performance）**：零成本抽象，编译为高效原生代码。
4. **互操作（Interoperability）**：与现有 C/C++ 代码无缝集成。`
    两个目标的张力：零成本抽象通常对应单态化；C/C++ 无缝互操作则要求 ABI 稳定 (泛型容器的 header-only / type-erased handle 两派)。当前都没提。
  - /Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/symbol-flags.h:54 — `Template = 1ULL << 54,   // Template symbols`
    flag 里用了 Template 这个词，和 01-introduction.md 里用的 Generics 不一致，暗示设计摇摆。

**潜在影响**  
若不先定策略，TypeChecker 的类型表示 (TypeSymbol 携带实例化信息还是共用模板)、IR 的类型标签、后端代码生成的结构都会被返工。

**修复建议**  
在 docs/design/ 新增 GENERICS.md，明确：(1) 默认单态化 (匹配零成本抽象)；(2) 提供 `dyn Interface` / type-erased 指针作为 trait object 形式的运行时多态补充 (用于跨动态库)；(3) 与 C 互操作：非泛型函数直接 extern C，泛型函数必须显式实例化后才能导出。
**评审备注**
- 确认方: ### 独立证据汇总（与原证据互补/修正）

**1. 泛型策略（monomorphization vs type erasure）文档缺失 — 确认成立**
- 对 docs/spec/chapters/ 全文 grep `monomorph|erasure|单态|擦除|ABI|dyn trait|vtable` — 零命中。
- 第 12 章《Generics》（135 行）只有语法示例（identity、Box<T>、Comparable、where 子句、associated type、Option<T>），完全没有任何"实现策略 / 对象布局 / 代码尺寸 / 实例化时机"的段落。
- docs/design/ 目录（adt.md / architecture.md / zis-internal-spec.md）全部为空占位文件，不存在 GENERICS.md。
- 因此"零成本抽象→单态化"和"C 无缝互操作→ABI 稳定"两个目标确实同时写在 01-introduction.md 里，但没有任何权衡说明，两条路径的下游差异（vtable vs 代码膨胀、跨 .so、调试信息、标准库二进制兼容）完全未被讨论。

**2. 与 C 互操作章节未对齐泛型导出 — 确认成立**
- `extern` / `extern "C"` / `linkage` / `ABI` 在全 spec 中只在 01-introduction.md 出现 1 处目标口号 ("Seamless integration with existing C/C++ codebases")，没有正式规范章节。
- symbol-flags.h 有 `Extern = 1ULL << 57` 和 `Export = 1ULL << 58`，但全项目 0 处使用，grep 不到任何调用点，"非泛型直接 extern C，泛型必须显式实例化后导出"的规则没有文档和代码承载。

**3. 原证据点 2（Template vs Generics 用词摇摆）— 部分不成立，已在反证中修正**：
- `Generic (1ULL << 43)` 在 Type system 组，`Template (1ULL << 54)` 在 Compiler & meta 组并进入 `CompilerMask = Synthetic | Builtin | Template`。两者是正交概念（"泛型定义" vs "编译器生成的模板化实例缓存条目"），不是一词两译。但需要指出的是：spec 全文只用 "Generics"、完全未使用 "Template" 这个语义层面的词，除了 template literal（模板字符串）之外，因此符号层引入 Template 作为 flag 名**仍然是值得澄清的命名歧义**，只是不属于"设计摇摆"那种严重级别。

**4. 对"建议"的独立评估**
- (1) 默认单态化 + (2) 运行时多态走 trait object + (3) 泛型 C 导出需显式实例化：这三条建议是 Rust 风格的成熟分层方案，与当前 `interface` 承担 trait 角色（`interface Comparable<T>` 等语法已写在 spec 第 9/12 章）这一现状相吻合；建议新增 `dyn Interface` 语法，但当前 AST 里没有 `DynTypeNode` 或 trait object 专用类型节点，也需要在 symbol 层引入 VTableSlot/Indirection 等标志。若采纳，实现端至少需补：ast/type.h 新增节点、parser 接入 `dyn` 关键字、symbol-flags 引入 trait object 相关位、design/GENERICS.md 正式文档。

**最终判断**：问题 F12 的核心论断（"泛型编译策略未显式抉择、与互操作目标不对齐"）真实成立，有独立 grep 证据和文档现状支持；次要证据点（Template/Generics 命名摇摆）被部分推翻，且因 TypeChecker/后端仍在 0 实现阶段，实际返工风险比原陈述描述的要低一级——因此严重度从原评估的 low 修正为 info（是一个需要补文档的决策缺口，但不是会导致当前代码返工的 bug），如果团队认为"后续设计工作会被前期不一致的 Symbol 层决策（如 Generic vs Template 双位并存）误导"，可上调到 low。
- 反对方: 1. 项目阶段错位：当前只完成 Lexer→Parser→Binder，Checker 为空命名空间，codegen/IR/backend 完全不存在（zis-internal-spec.md 为空文件，driver 无 check/codegen 阶段）。泛型实现策略是后端级决策，依赖中间表示、ABI、运行时等当前完全缺失的模块，现阶段不决策属于正常工程实践。

2. AST/符号系统已隐含单态化倾向：TypeParameterDeclaration（声明侧）+ TypeReferenceNode/ExpressionWithTypeArguments/CallExpression 三处 typeArguments（实例化侧）三段式完整保留类型实参，symbol-flags 有 Generic/Dependent/Higher 位，TypeParameterSymbol 有 Variance 枚举——这些都是编译期单态化模型的典型前端表示，与 type erasure 的早期擦除模型不兼容。

3. "性能 vs 互操作"是虚假二难：C++ 模板本身就是单态化 + header-only 模型，通过 extern "C" 声明非泛型函数即可与 C 互操作，不存在所谓的两派对立；symbol-flags 已有 Extern 位预留互操作挂钩。

4. 术语差异不构成证据：编译器内部用 Template、用户文档用 Generics 是行业惯例（Clang/LLVM 同样如此），同一 flag 区域的 Covariant/Contravariant/Phantom/Associated 等术语反而佐证方向一致。

5. 分类错误：泛型策略是编译时间/代码膨胀/运行时性能的工程权衡，与类型安全无关，无论擦除还是单态化都不影响内存安全，归入 safety 类别属于根本性误判。

6. 行业惯例：Swift/Go/Zig 等年轻语言均把泛型实现策略留到后端阶段，早期规范只定义语法语义（12-generics.md 已完成）而非绑定具体实现。


---

## 横向对比：ZOM 与现代语言的差异与借鉴

ZOM 是一门处于规范/解析器阶段的现代系统语言，定位清晰：静态强类型、无 GC、ADT+模式匹配、显式值错误处理、ARC 内存管理、C/C++ 互操作。从设计目标谱系上，它吸收了 TypeScript（语法风格+联合/交叉类型）、Rust（无 GC+显式错误）、Swift（ARC+值/引用类型划分+可选）、Kotlin（表达式 if/match/扩展）、Zig（手动控制+无隐藏控制流）五种语言的核心要素，但并非简单模仿，在 raises 原生错误类型、`?!/!!/?: 三件套操作符、点号路径模块系统三方面有自己的鲜明特色。整体技术选型偏向"务实的现代语言"：既不采用 Rust 极端的 borrow checker（学习成本换极致安全），也不妥协到 Go 式的 GC（简单换性能和确定性），而是走 Swift/Kotlin 式的 ARC 中间路线。当前实现进度：Parser（90%+）、Binder（完成）、TypeChecker（0%）。最大的架构风险是类型系统与所有权模型尚未在代码层面落地，而规范已做出了多项关键设计决策（如 ARC vs borrow checker）；v1 就排除的功能列表偏激进（无 default export、无 async/await、无 attribute），这反而是好事——避免了 Swift/Kotlin 早期因功能膨胀的历史包袱。

### Rust

**相似点**
- 设计目标高度一致：无 GC 内存安全、零成本抽象、与 C/C++ 互操作、模式匹配+ADT、泛型+关联类型（Rust 的 trait 对应 ZOM 的 interface）。
- 错误处理哲学一致：拒绝异常机制（try/catch/throw），全部通过显式值传播+模式匹配解决。Rust 的 Result<T,E> 对应 ZOM 的 raises 联合类型（T | E1 | E2）；两者均用后缀操作符（? vs ?!）传播错误。
- 可选值方案一致：Option<T> 与 T?（T | null）语义等价，均配套提供强制解包（unwrap/expect vs !!）、默认值（unwrap_or vs ?:）、可选链（?.）。
- 模式匹配能力近似：字面量/通配符/元组/结构体/枚举变体/守卫条件的枚举完整度匹配。
- 显式可变性：都强调在声明点标注而非类型层面控制。
- RAII + 确定性析构（deinit 对应 Drop trait）。

**关键差异**
- 所有权模型核心差异：Rust 用所有权转移 + 借用检查器 + 生命周期标注实现编译期内存安全；ZOM 用值类型栈分配 + 引用类型 ARC + weak 破环，无 borrow checker，无生命周期。两者目标相同机制不同。
- 继承模型：Rust 故意不提供传统继承（trait+组合）；ZOM 有 class 单继承（extends）+ interface 多实现（implements）。
- 类型系统：Rust 是严格名义型 + 纯 trait 系统 + 孤儿规则；ZOM 有联合/交叉类型、交叉接口继承、类型查询 typeof/keyof，更接近类型理论集合语义。
- 可变性默认：Rust 变量默认不可变，mut 显式；ZOM 分 let/const/var 三种绑定。
- 指针层级：Rust 有 &T、&mut T、Box<T>、Rc<T>、unsafe 指针全套；ZOM 无任何显式指针/引用类型节点，引用由 class 类型隐式承担。
- unsafe 逃生舱：Rust 有明确 unsafe 块；ZOM 只有 SymbolFlags 中的 Unsafe 位，无语法入口。
- 并发模型：Rust 的 Send/Sync 已在 std 生态成熟；ZOM 并发章节完全保留给未来。

**可借鉴的经验教训**
- ARC 方案需要警惕：1) ARC 无法静态保证多线程无数据竞争，若未来加并发，静态验证缺失是短板；2) ARC + 循环引用是长期隐患，只靠 weak 关键字靠自觉缺乏编译期检测。建议尽早实现引用环静态警告。
- ? 操作符是 Rust 社区公认的成功设计——极简精确。ZOM 的 ?! 合理且与 ?./??/?: 形成家族是好事，但文档务必在错误信息中明确 ? 系列语义差异，避免初学者混淆。
- trait 孤儿规则：ZOM 没有独立 impl 块天然避免孤儿问题，但代价是失去为外部类型实现接口的能力。v2 务必考虑独立 impl 块或 extension 机制（SymbolFlags 已预留 Extension 位）。
- unsafe 生态经验：allocate/deallocate 逃生舱建议一开始就用 unsafe 作用域包裹而非全局可用。
- 早期版本频繁变动模块/宏系统造成生态分裂。ZOM v1 锁定模块系统（点号路径、无通配符）是正确决策，值得保持克制。

### Swift

**相似点**
- 内存模型几乎相同：值类型（struct）栈分配+拷贝、引用类型（class）ARC堆分配+引用计数、weak 破环、deinit 确定性析构。
- 可选值语法完全一致：T?、?.、??、!! 强制解包 panic。
- 语法风格：C-like + 表达式优先（if/while/for/match 都可用作表达式）。
- 方法+属性语法：get/set 访问器、init/deinit 生命周期、计算属性。
- 协议/接口泛型+关联类型（Swift protocol associatedtype 对应 ZOM interface 的 type Assoc）。
- 错误处理：拒绝 try/catch 的替代方案（Swift 用 throws+do-catch，ZOM 用 raises+模式匹配）；两者都承认"非异常但显式"。
- class 单继承+protocol/interface 多实现的混合范式一致。

**关键差异**
- 错误处理细节：Swift 保留 try 类型擦除（Error 协议擦除具体错误类型，运行时统一）；ZOM raises E1|E2保留具体类型（编译期精确）。ZOM 在这点更接近 Rust。
- 字符串处理层次：Swift 字符串是 Unicode 正确但操作复杂；ZOM str 设计更轻量。
- Objective-C 互操作是 Swift 核心约束；ZOM 目标是 C/C++ 互操作，无 ObjC 负担。
- ARCCost：Swift 运行时较厚重（含元数据、反射、异常表）；ZOM 目标是零成本抽象，运行时更接近 Rust/Zig。
- 模式匹配完整度：Swift switch 匹配能力弱于 ZOM（无守卫条件早期缺失）。
- 字符串插值：Swift 反引号模板成熟；ZOM TemplateLiteralExpression AST 已落地但 spec 未正式纳入。

**可借鉴的经验教训**
- Swift 最大教训：ABI 稳定化之前引入太多特性（String 3次重写、ABI 稳定前的 String 内部表示多次迭代）带来了巨大的历史包袱。ZOM 选择 v1 保守排除功能（无并发、属性、default export）是明智选择——先 ABI/语义稳定再加特性。
- String 是 Swift 社区最大争议区：ZOM 的 str 应在 v1 就敲定内部编码（UTF-8 还是抽象字符视图）+ 不暴露 Unicode 正确性的成本。
- Swift 的 if let / guard let 语法糖（可选绑定）对降低 !! 滥用的可读性改善显著。ZOM 当前 match 已可用作表达式，建议加 let? 作为常见场景：if let x = optional { } 语法糖减少样板。
- Swift 的 protocol 自我参照（Self 递归约束+associatedtype）组合非常强大但实现复杂。ZOM interface 关联类型已预留，建议 v1 先只支持非递归、具体化后再扩展。
- Combine/Swift Concurrency（actor/async/await）模型晚于 ABI 稳定，引入了大量运行时 Hook 成本。ZOM 把并发保留到 v2+ 是正确决策——等基础语义先稳定。

### Kotlin

**相似点**
- 语法家族高度相近：表达式优先（if/while/for/match 作表达式）、扩展函数心态、data class 对应 struct、sealed class 对应 enum + ADT。
- nullable 类型系统：T? null 安全、?. 安全调用、?:（Kotlin 叫 Elvis 操作符，ZOM 是错误默认）、!! 非空断言。
- when 表达式与 ZOM match 功能对应：多分支模式匹配+守卫。
- 泛型+型变声明（Kotlin 的 in/out 对应 ZOM TypeParameterSymbol.Variance 的 Covariant/Contravariant/Invariant）。
- 解构声明：Kotlin 的 val (a,b)=Pair(1,2) 与 ZOM 的 let (a,b)=tuple 一致。
- 扩展函数：Kotlin 的 extension function 与 ZOM SymbolFlags 预留 Extension 位方向相同。

**关键差异**
- 内存模型核心差异：Kotlin 运行在 JVM（另有 Native/JS），依赖 GC；ZOM 是原生编译无 GC（ARC）。
- 空安全：Kotlin 的平台类型（来自 Java 的 String!）是历史包袱；ZOM 无互操作目标是 C/C++，未来需要考虑 FFI 边界的 null 语义，无需平台类型。
- 协程并发模型：Kotlin Coroutines 是核心竞争力；ZOM 并发完全保留。
- 内联+reified：Kotlin 内联函数特化是 Kotlin 惯用法；ZOM 编译时代码生成作为目标但无具体语法。
- 类型推断精度：Kotlin 没有联合/交叉类型作为一等公民（需 sealed class 模拟）；ZOM 原生支持 A|B、A&B。
- 顶层函数：Kotlin 包级函数/属性是重要组织方式；ZOM 以 module 作为组织单元。

**可借鉴的经验教训**
- Kotlin 的!! 非空断言是被社区批评最多的特性——它逃避类型安全。ZOM 有同样的 !!，请务必：在编译器给出强警告（lint 默认开启"避免 !!"，推荐 match 或 ?：。
- Kotlin 在 Java 互操作中学到的：平台类型（T!）是互操作边界不可避免的痛。ZOM 与 C/C++ 互操作时，请设计明确的 FFI 类型映射规范（如 C 指针 -> ZOM 包装类型，而非裸 any 传递）。
- 泛型型变（in/out）在声明点声明非常好用。ZOM TypeParameterSymbol 已做 Variance 枚举，务必在语法层暴露（Kotlin 风格优于 Java 的通配符）。
- 扩展函数在 Kotlin 社区使用极广。ZOM interface 实现上 extension 机制务必 v2 尽快推出——它比独立 impl 块更早落地，能大幅改善库设计。
- Kotlin 编译器复杂度经验：类型推断+重载决议+智能类型转换（smart cast）是编译器最复杂的三块。ZOM 类型推断务必限定在 HM 风格局部推断，不要做全局类型推断（Kotlin 早期做了大量工程化妥协）。

### Zig

**相似点**
- 手动内存管理可控：都提供手动 allocate/deallocate 作为逃生舱（Zig 的 allocator 接口 vs ZOM allocate<T>/deallocate）。
- 显式控制流：无隐藏异常机制，错误显式化。Zig 的 error union type 与 ZOM 的 raises 联合类型设计同源。
- 编译期计算能力：两者都将编译时代码生成作为设计目标。
- 与 C 无缝互操作是优先目标。
- 无运行时异常，确定性析构，追求零成本抽象。

**关键差异**
- 错误处理语法：Zig 用 !T（error union）+ try/catch（不是异常的 catch）+ if (foo()) |err| {}；ZOM 用 raises+模式匹配+?!。
- 内存模型：Zig 全手动（allocator 传递）；ZOM 默认 ARC，手动作逃生舱。哲学相反。
- 类型系统：Zig 是结构类型、无继承、comptime 泛型（编译期鸭子类型）；ZOM 是名义+联合交叉+继承+泛型约束+interface。
- 语法风格：Zig 极简关键字少；ZOM 关键字多、语法更接近 TS/Java。
- 异步模型：Zig 的 async/await 无栈协程已实现；ZOM 并发完全未启动。

**可借鉴的经验教训**
- Zig 最大教训：通用 allocator 接口在复杂项目中（每个函数签名带 allocator 参数的心智负担极重。ZOM ARC 默认手动逃生舱的分级更友好。但要避免 Zig 在标准库 API 设计务必提供 allocator 作为参数的"替换能力"（允许替换分配器对高性能库很重要）。
- Zig 的 comptime 系统经验：编译期能力与运行时语法统一且强大，但错误信息体验差。ZOM 编译时代码生成要做类型安全+好报错。
- Zig 的 !T 联合类型与 ZOM raises T|E 几乎等价。Zig try 关键字传播的简洁值得借鉴：能否将 ZOM 的 if (try f()) 的 short-hand 在语法层？
- Zig 的 undefined 未初始化内存策略。ZOM struct 默认值（field:T=def 特性已提供，务必强制执行初始化安全，避免未定义行为。

### TypeScript

**相似点**
- 语法外观高度相似：花括号、分号、let/const/var、可选属性用 ?、联合 A|B、交叉 A&B、对象/数组字面量、箭头式函数类型 (P)->R、解构、展开运算符 ...、typeof、as/as? 转型、三元、命名参数、可选链。
- 类型系统哲学：联合/交叉/可选/类型查询 typeof/keyof 是 TypeScript 首创并推广为工业界标准；ZOM 完全采纳。
- 接口 interface 多继承 extends、类型别名 alias/type、枚举。
- 模式匹配心智模型：TS 通过 discriminated union + narrow 推断；ZOM 原生 match 语法层更直接。

**关键差异**
- 运行时：TS 编译到 JS，类型在运行时全擦除，无内存安全保证；ZOM 编译到原生，类型有内存安全目标。
- 类型系统语义：TS 是结构化子类型（structural）；ZOM 是名义型（nominal）。这是根本性不同。
- 模块系统：TS 字符串路径 import 从 JS 继承；ZOM 点号符号路径。
- any/unknown/never/object：TS 有这些类型的完整语义；ZOM 只在 spec 列出 never/any，当前无对应 AST。
- 装饰器/attribute：TS 有完整生态；ZOM attribute 保留未来。

**可借鉴的经验教训**
- TS 最大教训：any 类型如果允许 escape，类型信任系统就失败了。ZOM any 务必设计为"仅在 FFI 边界或 unsafe 块中使用，日常代码默认禁用。
- TS 类型系统过于强大（条件类型、映射类型、模板字面量类型）导致生态繁荣但编译速度慢。ZOM 的名义型系统 + 类型能力边界清晰是好事。v2 再考虑条件/映射类型。
- TS 对 undefined vs null 双值空在生态引起困惑。ZOM 只保留 null 作为单一空值是更干净的决策，务必坚持。
- TS 模块系统经验：字符串路径 vs 符号路径的选择。ZOM 点号路径有利静态分析和 IDE，但生态 adoption 初期会遇到相对路径怎么映射物理文件系统的问题——模块解析算法设计务必在 v1 就文档化。
- TS strictNullChecks 默认打开后才真正兑现了 T? 的价值。ZOM 从一开始 null 安全（默认不可直接访问 T? 的成员，务必在类型系统实现时就严格执行（规范已承诺）。


---

## 结论与路线建议

**短期（进入 alpha 之前必做）**
1. 冻结语法规范：完成表达式-语句策略、运算符优先级表、自动分号/分号策略、保留字表。
2. 定义并文档化 12 项基石级语义：值/引用语义、拷贝/移动、RAII、所有权（若采用）、作用域与遮蔽、闭包捕获、错误处理模型、`==/===` 语义模型、`default`/`try`/`defer` 家族、整数溢出、未初始化变量、null/可选策略。
3. Spec ↔ Parser ↔ AST ↔ Tests 四方同步：删除或正式文档化所有"实现已加但规范未提"的特性；对"规范有但未实现"的功能标记为未完成占位或从规范中暂时移除。
4. 补齐负面测试套件（应编译失败却通过、应报错却不报的用例）。

**中期（类型系统落地）**
5. 选定类型系统核心：代数数据类型 + 模式匹配 或 面向对象 + 接口，避免同时一半一半导致两边都别扭。
6. 决定泛型与 trait 的设计（参考 Swift/Rust 或 Zig 方向），并在语法上预留。
7. 决定内存安全模型：引用计数 / GC / 所有权借用 / 三者混合——这将决定语言的天花板与学习曲线。

**长期（生态与演进）**
8. 明确 FFI / C ABI / 宏 / 编译期反射 的设计边界。
9. 建立"特性 RFC 流程"，避免像本次审计中发现的特性蔓延。

---

*本报告由 4 专家维度 + adversarial 双盲验证（每条 finding 2 位独立评审）产生，采纳标准：至少 1 方确认真实且综合置信度 > 30%。*
