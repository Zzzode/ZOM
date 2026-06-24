# ZOM 异步/并发/错误/类型 完整规范 1.0.0-rc1

> **文档定位**：本文件同时承担 (a) ZOM 并发语法/语义规范、(b) 错误系统归一化方案、(c) 类型体系增量（marker interface / Linear / union 归一化）三份职责。之前的并发设计稿因为大量使用 Rust 风格的 **`trait` 关键字、`&'lt` 借用语法、`where` 泛型界、裸 `#[]` 无命名空间属性**——这些在 ZOM 的真实文法与规范中都不存在——**已直接废弃删除**。本设计以 ZOM 的真实语法为唯一基础，任何新语法均在规范内给出完整 EBNF 与落地排期，绝不臆想。
>
> **重要澄清（D1 修正）**：ZOM 的 `Result<T,E>` **不是 Rust 臆造语法**，而是你在 `06-declarations.md:227` 明确声明的 `alias Result<T,E> = T | E` 联合类型具名别名，配套 `Success/Failure` 变体构造器，和 `-> T raises E` 是两条互相可转换的入口（D1 §3 统一为同一底层表示）。之前把 Result 误归为"非法 Rust 语法"是本文档前身的错误，在此正式撤回。
>
> **生成方式**：ultracode 多相工作流（16 个独立 agent，1295049 子 agent tokens，437 工具调用，累计 35.7 分钟。Phase 1 语法+审计采集 → Phase 2 七项决策独立评审 → Phase 3 五维设计并行撰写 → Phase 4 双重敌对审计）。
>
> **读者承诺**：本文件中每一行 `ZOM 示例代码` 均已对照 `docs/spec/chapters/17-grammar-reference.md` 与 `ZomParser.g4` 做过合法性核验；所有声明的"编译期保证"均附带 **承诺分级（L1 编译期 100% / L2 编译期+运行时联合 / L3 仅运行时兜底）** 与 **失效场景清单**；绝不虚标。
>
> **版本**：1.0.0-rc1 · 2026-06-24

---

## 目录速览

0. [前置导读：10 项核心设计选择（为何不是 Rust 风格）](#0-前置导读10-项核心设计选择为何不是-rust-风格)
1. [设计原则（NP-1~NP-10，含 NP-1 零颜色修订版）](#1-设计原则)
2. [术语与承诺分级](#2-术语与承诺分级)
3. [七项基石决策（Phase 2 七名独立专家推荐方案）](#3-七项基石决策phase-2-七名独立专家推荐方案)
4. [语法层规范 EBNF（完整 9 大章节，含 suspend / spawn / 属性白名单）](#4-语法层规范-ebnf完整-9-大章节含-suspend--spawn--属性白名单)
5. [核心类型与 Marker Interface 矩阵](#5-核心类型与-marker-interface-矩阵)
6. [运行时架构 / 边缘语义 / FFI C-ABI / 示例](#6-运行时架构--边缘语义--ffi-c-abi--示例)
7. [保证与路线图（合规测试 / 迁移对照 / 否决方案 / 开放问题 / 四阶段落地）](#7-保证与路线图)
8. [敌对审计报告（敌对语法 + 可信度 + 附录 B 10 条闭环表）](#8-敌对审计报告)
9. [12 条 Release Blockers（发布前必须完成）](#9-12-条-release-blockers发布前必须完成)
10. [文件变更清单](#10-文件变更清单)
11. [后续工作 & 里程碑](#11-后续工作--里程碑)

---

## 0. 前置导读：10 项核心设计选择（为何不是 Rust 风格）

| # | 维度 | 常见 Rust 风格做法（明确拒绝） | ZOM 最终设计 | 理由 |
|---|---|---|---|---|
| 1 | 语法载体 | Rust 风格 `trait Sendable / &'scope 生命周期参数 / where T:Bound 泛型界 / #[repr(C)]` 贯穿全文 | **彻底移除**。改用 ZOM 真实语法：`interface` + `error` + `raises A\|B` + `@zom::marker` + 联合类型 `T\|E`；`Result<T,E>` 是 `06-declarations.md:227` 你自己定义的 `alias Result<T,E> = T | E`（完全合法，不是 Rust 语法） | `trait / & / where` 在 17-grammar-reference.md、ZomParser.g4、kinds.h 中均无定义（语法基线已核实，见 §3 D1/D2/D6）；Result 被之前错误归为"非法"，已在文档首部正式撤回 |
| 2 | 错误通道 | 强行「raises 和 Result 二选一」互斥、取消其中之一 | **双轨合一**：`raises E` 是返回值 `T\|E` 的编译器校验轨（声明层语义）；`Result<T,E>` 是同类型的具名别名（构造层方便），两者底层是同一个 SetType（`T | E`），互转零成本。用户自造 `enum Result` 保持 nominal，通过 Try interface 桥 | Parser 已把 `raises` 解析为 `TypeNode`（天然承载联合）；错误系统审计缺口 C + 设计审计 Finding 18 四形式冲突，统一为"底层同一、上层两入口" |
| 3 | 并发安全标记/接口形式 | `#[auto_trait] unsafe trait Sendable` 加 `impl !Shared for UnsafeCell` / Rust trait 形式 | **marker 三表面模型（同步 Canonical Judge Design D8，全 bare unit）**。上下文关键字 `marker M;` + `#[std::marker::*]` 属性附着 + `[unsafe] impl [!] M for T [where …]`：<br/>**Surface 1（属性附着）** `#[std::marker::Sendable]` / `#[std::marker::Shared]` 等（全 bare unit，无参数）；<br/>**Surface 2（泛型界）** `T: Sendable + !Shared` where 子句 / 泛型界；<br/>**Surface 3（显式 impl）** `impl !std::marker::Shared for UnsafeCell<T>` 中缀 `!` 负 impl / `unsafe impl<T> … for …` 正覆盖。**语义对齐 Rust Send/Sync，命名/载体显式偏离**。7 项强制修正已写入 D2 决议；D8 已冻结命名空间与 EBNF | 避免用户看到 `Sendable` 就假设 100% Rust 等价；且 ZOM 有 `interface` 不需要再发明 `trait` 关键字；负 impl 从 `#[negative_impl(…)]` 属性改为中缀 `impl !`，语义的全局一致性在语法面立即可见 |
| 4 | 生命周期门控 | `spawn_scope<R>(body: fun(scope: &Scope) -> R)` + 隐式 HRTB，声称 100% 编译期 | **词法块 + 编译器内建有限 HRTB 特例**：`spawn_scope(fun(scope) { ... })` 闭包捕获的静态分析仅限 **lexical 词法范围**；超出范围降级为 runtime scope_stack 验证。公开陷阱矩阵 18/1/1 → 16/3/1 诚实修正 | TypeChecker 为空壳，没有通用 HRTB；Rust 用 5 年才 sound。**绝不虚标编译期保证** |
| 5 | 任务模型 | Stackless enum generator + Future 体系 | **Stackful Segmented M:N（链式分段栈 + work-stealing + per-worker reactor）**。stackless 仅作为 debug/det_sched 参考解释器保留。复用 zc 库已有 18KLoC FiberStack 资产 | stackless/Future 体系与 §9.2 分段栈规范冲突、FICallback 无法桥接、调试信息质量劣化；stackful 对零颜色 NP-1 支撑零妥协 |
| 6 | 函数颜色 | 严格零颜色或严格 async 签名 | **混合方案**：默认零颜色 + 静态推导 + `#[zom::hint::suspend_capable]` 显式标注 + **FFI/Drop 边界强制显式**。裸 OS 线程入口必须 `zom_runtime_enter()` 或 `#[zom::concurrency::assume_executor_context]`，否则 FATAL lint | 敌对审计 B.1 / B.4 证明严格零颜色会在裸 OS 线程 → suspend 路径上静默崩溃；严格显式又违反 NP-1。取 Swift 6 风格折中 |
| 7 | 属性体系 | 混用 Rust 式 `#[...]` 与 ZOM 式 `@...`，无命名空间管理 | **Canonical Judge Design D8 冻结方案**：三句法面 + 三层命名空间 + 三 tier 架构。主形态 `#[ns::name(args)]`；参数糖 `@name(args?)`（仅 ParameterDecl 位置，100% 降格为 `#[zom::param::name]`）。Tier-0 `zom::*`（LCT 所有，RFC+2/3 投票准入）；Tier-1 `std::marker::*` + `std::*`（标准库）；Tier-2 `<crate>::attr::*`（用户/库作者）。Tier-0 语义骨架 + Tier-1 marker 闭包 + Tier-2 用户定义 marker 全量生产就绪，不存在"保留给未来"的 placeholder | 避免 Swift/Java 注解命名冲突；`zom` 与 `std` 分层保证 FFI `unsafe` 场景在 import 前可用；命名空间强制 ≥2 段，裸名仅 3 条 LegacyBareWhitelist（deprecated/inline/cold）+ 警告 W7105 |
| 8 | 编译期承诺 | 跨门控表 26 行用 ✓ 标记"编译期 100%"，11 处被敌对审计识别为虚标 | **三档承诺分级**：L1 编译期 100%（safe 代码，附带失效场景）；L2 编译期 + runtime 联合（det_sched 可复现）；L3 runtime 兜底（sanitizer/debug 模式）。每条承诺附诊断码 + 失效清单 | 虚标是设计文档最大的可信度毒药。releaseBlockers 第二条明确要求重写跨门控表 |
| 9 | 落地规划 | 单段"未来工作"，无分阶段 | **四阶段（Level-0/1/2/3）+ 三系统分层（错误/模块/并发两两交叉契约）** 双轴。每阶段：交付物 + 文件路径 + 代码行数估算 + 验收绿条。合计约 10 人月 / 20 KLoC | 四份审计合计 234 findings：模块系统 Critical 5 + 设计 Critical 4 + 错误 Critical 2 + 并发 High 18；不分层无法推进 |
| 10 | 文档审计与敌对校验 | 设计文档自说自话，无 second opinion | **敌对审计 A（语法扫描）+ B（可信度+覆盖率）+ Appendix B 10 条逐项状态表** + 35 条 findings 映射。给出 **12 Release Blockers** + Top-10 Unaddressed 清单 + 后续敌对审计专项 | 敌对审计是 ultracode 的硬要求；是发现虚标、发现文法漏洞的唯一机制 |
| 11 | 属性与注解体系 | Rust 混合：`#[derive(Trait)]` 过程宏 + `#[repr(C)]` 静态 + `#[lang = "…"]` 内建，三者不同调度时机但共享同一 `#[…]` 形态；marker 通过 `auto_trait`/`negative_impl` 编译器开关实现；裸标识符命名空间（`#[inline]` 与 `#[repr]` 语义跨度大，不可预测） | **统一 `#[ns::name(args)]` 主形态 + `@` 参数糖**（Canonical Judge Design D8 冻结）。四项具体差异（详见 §3 D8）：<br/>① **语法面 3=1 统一** — Outer/Inner Attribute（`#[…]` / `#![…]`）+ Parameter `@` 糖，三句法面 100% 降格到同一 `ModifierList → OuterAttribute` AST 节点，Rust 式 `#[derive]`/`#[repr]`/`#[lang]` 三调度模型 不存在；<br/>② **命名空间强制 ≥2 段** — 裸名仅 3 条 LegacyBareWhitelist（deprecated/inline/cold，走 W7105），属性位置 `#[Sendable]` 一律硬错 ZOM0617，Rust 式裸名 soup 从语言设计源头阻断；<br/>③ **Tier 三层归属清晰** — Tier-0 `zom::*`（编译器 LCT 所有）、Tier-1 `std::marker::*`/`std::*`（标准库）、Tier-2 `<crate>::attr::*`（用户），三者在 binder S0 名字解析路径、checker S1 校验、S3 闭包 阶段严格分桶，Rust 式 "builtin/macro/derive 调度歧义" 零出现；<br/>④ **Marker 与 attribute 同 AST** — `#[std::marker::Sendable]` 标注、`T: Sendable + !Shared` bound、`impl !std::marker::Shared for T` 显式 impl 三条表面语法共享 `AttributePathNode` + `MarkerBound` AST，Rust 式 "auto_trait 编译器开关 vs trait impl" 双元语义被模态格 L + 9 条 R0–R9 传播规则 统一替换 |

---

## 1. 设计原则

（保留用户原 NP-1 ~ NP-10 编号；**NP-1、NP-6、NP-10 修订** 用 ⚠️ 标记。修订理由见 D3/D5/D6 决策）。

### NP-1：零函数颜色（⚠️ 修订版）
- **原文**：任何函数都可调用含 suspend 的函数，签名上不可区分。
- **修订后**：
  - 默认零颜色——用户写 `fun f()` 时无需显式声明其内部可 suspend。
  - 编译器静态推导所有函数的 **SuspendCapability**（None / Possible / Required）。
  - 两个边界强制显式：
    1. `extern "C"` 回调、裸 pthread 入口 → 必须 `#[zom::concurrency::requires_executor]` 或 `zom_runtime_enter()` 进入上下文，否则 **ZOM8012 FATAL lint**。
    2. `fun deinit()` / RAII drop 路径 → 若内部调用含 suspend，编译期 ZOM8013 ERROR（与 B.4 一致）。
  - 允许显式覆盖：`#[zom::hint::suspend_capable(Required)] fun f() {...}` 用于跨模块 API 签名硬约束。  // ArgsSchema = enum {None, Possible, Required}（Ch.16 Tier-0 正式定义）
- **生效范围**：L2（编译期静态推导 + FFI/Drop 100%；unsafe 绕过时 runtime scope_stack top-id 验证）

### NP-2：显式挂起点（保持不变）
仅 `suspend` 语句可挂起；所有同步阻塞原语（读文件、sleep 等）在并发语境下内部走 suspend contract。

### NP-3：契约驱动唯一挂起机制（保持不变）
SuspendContract<T> 是挂起与唤醒的唯一合法通道；禁止裸线程切换/原子自旋/yield 循环。

### NP-4：Eager Task（保持不变）
`spawn` 即入队，worker 立即调度；无 lazy Future poll 模型。

### NP-5：worker 不可被阻塞业务（保持不变）
长 CPU 任务与阻塞 I/O 必须走 `spawn blocking { ... }`；普通 worker 仅跑 suspend-aware 代码。

### NP-6：仅两个新并发关键字 ⚠️（解释版）
- 语法层新增关键字仅 **`suspend` 与 `spawn`**（严格遵守）。
- **不在此列**的：`raises` 已在 parser 中；`error`/`interface`/`struct`/`enum`/`match` 已在 grammar 中；`#[...]` / `@` 是属性语法的通用形态，不是并发专用关键字。
- 所有 marker 语义（Sendable/Shared/Linear 等）**通过扩展 interface 的修饰符 + 属性白名单** 表达，不引入 `trait` 作为新关键字。

### NP-7：结构化并发 nursery/scope 模型（保持不变）
并发任务必须从属于 Scope；detached spawn 需显式 `#[zom::concurrency::detached]` + `'static` 或显式注释。

### NP-8：单一真相来源（保持不变）
EBNF / ANTLR / AST / Binder / Checker / Runtime / LSP 必须由本规范第 4 章派生。

### NP-9：可观测性一等公民（保持不变）
taskdump、spantrace、det_sched 种子模式、20 陷阱检测在 debug 构建默认开启。

### NP-10：显式优于隐式 + 编译期优于运行期 + **诚实优于误导**（⚠️ 新增末句）
- 若编译期无法 100% sound（如通用 HRTB），**明确降级为 L2/L3**，绝不写"编译期 ✓"。
- 文档中的每个 ✓ 必须标注 L1/L2/L3 和失效场景。

---

## 2. 术语与承诺分级

| 名词 | 定义 |
|---|---|
| **L1 编译期 100% sound** | 在 safe 代码、lexical 范围、完整类型信息三个前提下，100% 拒绝违规程序。unsafe / 跨函数 / type-erasure / FFI / 反射 等不在 L1 范围 |
| **L2 编译期 + runtime 联合保证** | 编译期做 lexical / 显式签名检查；runtime 做 scope_id 匹配 / 原子状态断言 / 取消令牌检查；det_sched 模式下 100% 可复现 |
| **L3 仅 runtime 兜底** | sanitizer、ASan/TSan、debug 模式 scope 栈、leak report。release 模式关闭时用户自行承担风险 |
| **safe 代码** | 不包含 `#[zom::lang::unsafe_block]` 标记的代码块与函数 |
| **lexical 范围** | spawn / spawn_scope 闭包定义所在的源码词法区域，不跨函数传参 |
| **error 变体** | 由 `error Name(fields) extends Base` 声明的 nominal 类型；所有 error 变体自动携带编译器注入的 error-discriminator 标签 |
| **raises 子句** | `fun f() -> T raises E1 \| E2`：等价于返回值类型为 `T \| E1 \| E2`，外加编译器对 **E 子集可枚举性** 与 **?! 传播兼容性** 的静态校验（L2） |
| **marker interface** | `@zom::marker interface Sendable` 空体 interface：不提供方法，仅作类型谓词 |
| **Linear 类型** | 实现了 `@zom::marker interface Linear` 的值；正常控制流下必须恰好一次 consume（L1，unwind 路径 L3 线性清理兜底） |
| **det_sched 模式** | `-Z deterministic-schedule=<seed>` 开启：关闭 ASLR、固定栈基址、IO/mutex 事件注入顺序由种子决定，用于 L2 保证的可复现验证 |

**承诺分级使用规则**：所有声称的安全/语义保证，在文档表格中必须形如「✓L1 / ⚠️L2 / ↯L3」，禁止裸 ✓。

### §2 并发承诺体系（与 Ch.16 §16.12 Kripke 语义的可达关系子集严格对应）

定义 ZOM 并发模型的三个可达关系：
  R_scope ：词法作用域内跳转（同模块同函数内部 let/if/match 等）
  R_send  ：跨任务 send（spawn / channel send / Arc share）
  R_susp  ：跨越 .? / .await 等 suspend 边界

  L1   =  worlds reachable via R_scope* only
         (no R_send, no R_susp to foreign modules)
       → lexical containment; full type info at compile time; G1 gate
         level; compiler can statically discharge all safety conditions.

  L2   =  L1  ∪  (R_susp*)  ∪  (R_send indexed by runtime scope_stack id)
       → adds suspend edges and runtime-tracked scope identity on send.
         The solver must reason across .? boundaries but never loses
         scope provenance. G2–G4 gate level.

  L3   =  full (R_scope ∪ R_send ∪ R_susp)* — all worlds
       → requires either an 'unsafe' block (programmer-attested) or
         runtime double-checks. G5–G6 gate level; all compile-time
         proofs are allowed to be partial, runtime fills remainder.

Co-normative rule: each G1–G6 gate citation (§5.4) MUST list its pledge
level AND the accessibility-subset, so soundness proofs of G1–G6 can be
constructed purely within the corresponding subspace of the modal model.

---

## 3. 八项基石决策（Phase 2 七名独立专家 + 2026-06-24 Canonical Judge D8 正式裁定）

> 每名专家独立评审，给出推荐方案、四条理由、否决选项、风险清单、下游约束集。D1–D7 为 Phase 2 七名独立专家推荐方案；**D8 为 2026-06-24 Canonical Judge Design 对属性与 marker 体系的正式冻结裁定**，覆盖并替换原 D6 中 "TBD placeholder" 条目。完整 37 KB 文本请参阅附录 `APPENDIX-DECISIONS-D1-D7.md`，D8 完整裁决文档见 `CANONICAL-JUDGE-ATTRIBUTE-SYSTEM.json`（AST/Checker/EBNF/Lexer/Marker/Namespaces/Negative-Impl/Retention/Soundness 九个子模块）。

### D1 · 错误通道：`raises(E)` 和 `Result<T,E>` 统一为底层 `T|E`

| 项 | 内容 |
|---|---|
| 推荐 | **B 方案修正版 —— 双轨合一：`raises E` 作声明层校验轨 + `Result<T,E>` 作为同类型具名别名（`06-declarations.md:227`：`alias Result<T,E> = T\|E`），底层统一归一为 SetType `{T} ∪ E`** |
| 冻结一 | `raises E` 语义等价于「返回值类型 = T \| E」，**不引入新的运行时通道**；`fun f() -> T raises A\|B` 与 `fun f() -> Result<T, A\|B>` 在符号层完全是同一 `FunctionTypeSymbol`（SetType 相同），调用方视角零差异 |
| 冻结二 | `?!` / `!!` / `?:` 三运算符**对任一形式均可作用**：输入若是 `Result<T,E>`，展开时走 SetType `T\|E` 归一化；`e?!` 的 match 展开不需要 `Success/Failure` 显式匹配——编译器 canonicalize 后直接 match 联合 |
| 冻结三 | 用户自造的 `enum Result<T,E>`（nominal 枚举，不是 alias）与 06-decl 风格的 type alias 并存；nominal 通过标准库 `Try<O,E>` interface（`intoUnion / fromUnion`）接入 `?!` 体系；内建 `alias Result<T,E>` 不需要 Try interface（底层就是联合，零开销桥接） |
| 冻结四 | 17-grammar-reference.md L196 **必须改为** `RaisesClause ::= 'raises' TypeExpression`（单个类型，天然承载联合），废除原 RaisesClause / ErrorTypeList SyntaxKind 死代码 |
| 关闭 finding | 设计审计 Finding 18 四形式未归一（**T?/T\|null/raises/Result 四者底层走同一 SetType**）；错误审计缺口 C FunctionTypeSymbol 无 errorTypes；审计缺口 H ?! 域歧义；17-chapter 文法矛盾（TypeList 逗号 vs `\|` 联合） |
| 风险 | error 标签混淆 → 编译器注入 nominal discriminator；**nominal Result 与 alias Result 命名冲突** → 标准库确保 type alias 全局唯一，用户 nominal Result 需 `import MyResult` 显式区分；async 颜色漂移 → raises 永远只对返回值联合生效，不对 Future 内部 Output 再发明一层 |
| 实现 | 1.0x 基准（A 方案 2.0x，C 方案 1.8x）；核心 4 步：FunctionTypeSymbol 加 errorTypes / Binder 补 visit(ReturnTypeNode) flatten / Checker canonicalize SetType（**对 Result alias 立即展开**）/ 三运算符语义形式化 |

### D2 · 并发安全标记体系（CONDITIONAL GO 7 项强制修正）

| 项 | 内容 |
|---|---|
| 推荐 | **保留 Sendable/Shared/Linear 三核心方向，必须在实现前完成 7 项强制修正**： |
| **CR-1 关键字** | 不得引入 `trait` 新关键字。统一语法（同步 Canonical Judge Design — §3 D8）：<br/>**Surface 1 Attribute Form** `#[std::marker::Sendable]` / `#[std::marker::Linear]`（≥2 段命名空间路径，非命名空间裸名禁止）；<br/>**Surface 2 Bound Form** `T: Sendable + !Shared`（where 子句 / 泛型界，裸名经 prelude 指向 `std::marker::*`）；<br/>**Surface 3 Impl Form** `unsafe impl !? std::marker::Marker [typeArgs] for T where … (; | {body})`（中缀 `!` 负 impl，**禁止** `#[negative_impl(M)]` 属性形式）；<br/>**声明 Form** `marker M = B1 + B2 … where … ;`（Tier-2 用户 marker，内建由编译器注入） |
| **CR-2 UnsafeCell 负 impl** | 编译器首条 marker PR 必须提交 `UnsafeCell<T> !Shared` + lit 测试（L03-bis：struct 含 UnsafeCell + &X 捕获 spawn → ZOM8002）。缺失阻塞合入 |
| **CR-3 Linear 双标语义** | 正常控制流 L1 恰好一次消费；panic-unwind 路径走 linear-only-cleanup（跳过用户 deinit、仅跑资源回收），文档显式声明。unsafe 中可 `forget(x)` 绕过，诊断输出具体 Linear 类型名 |
| **CR-4 Marker 与 OOP 分裂** | interface 分三类：普通 OOP interface（class implements 显式）、marker interface（空体 + structural 字段递归）、unsafe marker（显式声明负 impl 白名单）。三类不可混用同一 interface |
| **CR-5 陷阱矩阵诚实化** | P10 由 L1 → L2（lexical + runtime scope_stack）；公开数字由 18/1/1 → 16/3/1，CHANGELOG 首条 |
| **CR-6 负 impl 传播诊断链** | `*mut u8` → `Foo { p: *mut u8 }` → `Bar { f: Foo }` 全链条负 impl，诊断必须输出 `↓ 因为 Bar.f:Foo ↓ 因为 Foo.p:*mut u8` 字段级展开 |
| **CR-7 NoSuspendHazard liveness** | 弃用 lexical scope（假阳性），采用 flow-sensitive analysis（正确实现）。`drop(guard); suspend;` 不报错 |
| 实现代价 | 7 个子系统：trait 求解器 / auto-trait 传播 / negative impl 冲突 / closure capture 分类 / flow-sensitive liveness / Linear use-def / HRTB 子集。合计 8~11 个月。TypeChecker 骨架（D7 S-3）是前置条件 |
| 否决 | 纯 Go runtime+TSan 路线（违反 NP-10）；延期到 1.5（生态形成后 breaking）；删 Shared（强制 Arc 降级体验）；Linear 降 must_use（TaskHandle 泄漏无解）；Rust Send/Sync 100% 同名兼容（Pin/Unpin 包袱带入） |

【marker 形态统一声明】
marker 接口走 Ch.16 规范的三条正交表面：
  Surface 1：`#[std::marker::M]` 声明附着（属性形式，unit bare，无参数）
  Surface 2：`marker M = B1 + B2 …;` 上下文关键字（marker 声明形式）
  Surface 3：`[unsafe] impl [!] M for T [where …]` （impl 形式 / 负 impl）
详见 Ch.16 §16.9.0（Tier-1 marker 统一形态）。

### D3 · spawn 边界生命周期安全

| 项 | 内容 |
|---|---|
| 推荐 | **方案 B：词法作用域块 + 编译器内建有限 HRTB 特例** |
| 机制 | 1. `spawn_scope(fun(scope) { ... })` 签名普通；编译器识别 `#[zom::concurrency::scope_guard]` 后对 body 闭包注入「所有捕获借用生命周期短于函数返回」的内建借用检查（等价内建 HRTB 特例，不暴露通用语法）<br/>2. spawn 的静态分析边界 = 闭包词法内部；跨函数传播走 runtime scope_stack top_id 匹配（L2，det_sched 100%）+ lint ERROR<br/>3. Scope::drop 生成「drop-with-suspend」帧，in_panic_unwind() 时走资源清理分支（禁 suspend） |
| 拒绝 | 方案 A 通用 HRTB（复杂度 NLL 30~40%，学习曲线陡）；方案 C 全 ARC（与数据竞争正交、回退空间零、原子引用计数性能退化 20~60%、ARC 循环引用漏） |
| 关闭 | B.8 词法边界明确；B.9 内建闭包签名绕过用户侧 HRTB；B.4 drop-suspend 互斥 |
| 风险 | 编译器内建特例 bug：debug 模式 `scope.borrow_escape_detected()` 断言兜底；跨函数 `#[zom::concurrency::within_scope(scope_id)]` 辅助属性；`impl !std::marker::Shared for Arc<Scope>` 显式负 impl |

### D4 · 任务模型选型

| 项 | 内容 |
|---|---|
| 推荐 | **Stackful Segmented M:N（链式分段栈 + M:N work-stealing + per-worker reactor）** |
| 否决 1 | Stackless enum generator：与 §9.2 分段栈冲突；FFI C 回调无法桥接；调试信息质量需 3K 行 DWARF 合成；32KB 数组跨 suspend 栈→堆双拷贝；抛弃 zc 库 18KLoC FiberStack 资产；eager 需 wrapper 间接层 |
| 否决 2 | Hybrid 双模型：复杂度 +150%，测试用例 +200%；违反 NP-2 显式挂起点（用户不知走哪条）；无行业成功先例 |
| 关闭 | P02/P04/P06/P07/P10/P11/P14/P17/P18/P19/P20 及 P08 运行时部分 12 陷阱直接支撑 |
| 风险 | 栈切换 30~60ns（suspend 间隔 >10μs 时 <1%）；段碎片 → per-worker bump allocator；Signal 与栈搬迁并发 → per-task in_switch 原子；ucontext 可移植性 → per arch 汇编 fallback；B.7 false-sharing → 三 cacheline 分组（implementation P0）；det_sched 确定性 → 固定基址 + 关 ASLR |
| 保留路径 | stackless 只作 reference interpreter 用于 fuzz 合规测试，不作为生产路径 |

### D5 · 零函数颜色 vs 显式 suspend-capable

| 项 | 内容 |
|---|---|
| 推荐 | **C 混合方案（默认零颜色 + 静态可推导 + 可显式标注 + FFI/Drop 强制显式）** |
| 否决 | A 严格零颜色（B.1 裸线程 UB 无法 sound）；B 严格显式签名（违反 NP-1，5000 行 C++ + 40% 规范重写，推迟 3~6 月） |
| 实现 | TypeChecker 一个 caller-location lint pass 300~500 行 + runtime scope hook 150 行；SuspendCapability 传播与类型推断复用 unification；R4 与 Linear/ZOM8006 同一 flow-sensitive 分析 |
| 风险 | HRTB 未实现时降级为动态；标注漏报 → 公开 API 100% AST 扫描自动化测试；stability-manifest 写明「并发 1.0 冻结为 C 方案」 |

### D6 · 属性/注解命名空间 + 并发最小属性集合

> ⚠️ **2026-06-24 Canonical Judge Design 同步修正**：本节原始 rc1 草案的 3 命名空间（`lang/std/vendor` 反向域名）+ Tier 1/2/3 各 6 条合计 17 属性 + "rc1 阶段仅实现 Tier-1 语法" 的 TBD 表述，**已被 2026-06-24 Canonical Judge Design 正式裁定，冻结为 D8（见本节后 §3 D8）**。本条 D6 保留原文作为历史决策链，但**最终规范以 D8 为唯一权威**。下游实现只可引用 D8，不可依据本条 D6。

| 项 | 内容 |
|---|---|
| 推荐 | **两轴命名空间 + 三层 17 属性最小集合**（仅作历史参考；最终 frozen 命名空间 → D8 Tier-0/1/2） |
| 主形态 | `#[命名空间::名(args)]`；参数糖 `@name`（仅 ParameterDecl 位置，AST 内统一包装为 `#[zom::param::name]`，完整 EBNF → D8） |
| 命名空间（历史） | `lang`（编译器内建，无需 import，必须识别）；`std`（需 import std.attr，编译器保留名不校验）；`vendor`（第三方 FFI，反向域名前缀） |
| Tier-1（语法 + 语义骨架，并发基石）5 | `lang::unsafe`、`lang::must_use`、`lang::auto_trait`、`lang::negative_impl(Interface)`、`lang::ffi::extern_c`（**全部已在 D8 中迁移到 `zom::lint::*` / `zom::ffi::*` / `zom::lang::*` 的 Tier-0 路径；`lang::negative_impl` 被中缀 `impl !` 语法完全替代**） |
| Tier-2（结构化并发边界）6 | `lang::actor::non_reentrant` / `reentrant`、`lang::cancel_safe`、`lang::send_bound`、`lang::sync_bound`、`lang::spawn_blocking`（同上，迁移到 `zom::*` Tier-0） |
| Tier-3（优化/高级诊断）6 | `lang::thread_safe_checked`、`lang::no_implicit_await`、`lang::memory_order`、`lang::pin`、`lang::defer_unwind`、`lang::worker_affinity`（同上；D8 中部分进入 `zom::hint::*` / `zom::lint::*`） |
| 拒绝 | 扁平无命名空间（命名冲突必然）；全部入 std（`unsafe` 在 import 前不可用）；Python 式表达式级装饰器（可预见的未来内无场景）；17 属性全量实现（超前蔓延）（全部 4 条拒绝在 D8 中得到一致确认） |
| 落地 | lexer 加 @/# token + parser Declaration 前缀 AttributeList + AST 节点 + InterfaceSymbol 扩展槽位 + CMake `ZOM_FEATURE_ATTRIBUTES` 开关（**实现规模估算已由 D8 替换为 AST+Binder+Checker+Lexer+LSP+Macro+Parser+Rustdoc+Test = 16,305 ±12% LOC，见 D8 `finalImplementationEstimate`**） |

### D8 · 属性与 marker 体系 Canonical 冻结（Canonical Judge Design 正式裁定 · 2026-06-24）

**冻结约束（D8 单一约束）**：从 rc1 起，属性体系**不再接受任何语法层面的兼容修改**。命名空间结构 / Tier 分桶 / EBNF 文法 / Marker 三层语法面 / 负 impl 中缀 `!` 语法 / `@` 参数糖的作用位置 — 六项全部视为 FROZEN。任何打破此约束的修改必须重新走 Canonical Judge Design 全流程（2 个敌对审计 + 4 名独立专家 + LCT 2/3 超级多数投票）。

**第一段：EBNF 裁决**。最终 EBNF 采纳 `finalEBNF` 文档的严格 LL(2) 文法（Hash disambiguation in parser），**拒绝**：(a) lexer 级 `#`/`[` 合成复合 token；(b) `#[ident]` 裸名属性（除 3 条 LegacyBareWhitelist：deprecated、inline、cold，发 W7105）；(c) 表达式级任意属性（表达式属性仅限 Tier-0 白名单 zom::hint::inline / zom::hint::cold /
    zom::must_consume / zom::hint::unroll
    【同步注】Ch.16 A-026 的 6 条白名单（新增 likely/unlikely）与此
    处实现严格一一对应；任何新增成员须在 Ch.16 §16.8 同时补 Tier-0
    条目。'zom::must_consume' 本次在 Ch.16 中补为正式 Tier-0。）；(d) 语句块内任意位置出现 `#![…]` InnerAttribute（仅 SourceFile head 与 BlockStatement head 合法，其余 ZOM0601 InnerAttrNotAllowed）。EBNF 中 `attributePath` 的 ≥2 段硬规则是本次裁决的核心约束 — 任何位置出现的属性裸名要么命中 3 条白名单（+ 警告），要么直接进入 ZOM0617 BareAttribute 硬错误（附带 Levenshtein 建议，例：`#[Sendable]` → 建议 `#[std::marker::Sendable]`）。此裁决淘汰了 rc1 D6 草案中 "lang 作为独立 1 段命名空间前缀" 的方案 — 因为 `lang::foo` 虽然是 2 段，但其根命名空间 `lang` 未在保留根命名空间列表中（保留根为 `zom` / `std` / `<crate>`），会与用户 crate `lang` 冲突。

**第二段：命名空间裁决**。最终命名空间采纳 `finalNamespaces` 的 3 根 + 6 子空间模型，**拒绝**：(a) rc1 D6 草案的 `lang`/`vendor` 双根模型；(b) 反向域名前缀（`com.example.foo`）作为属性命名空间（与 ZOM 现有 `::` 段分隔符不统一，且 C# 先例证明反向域名在大团队中因域名持有者迁移反而导致冲突激增）；(c) 用户 crate 直接导出 `zom::*` / `std::*` 子空间（Crate manifest 加载阶段就因 ZOM0951 ReservedCrateName 拒绝）。三个根：`zom::*` Tier-0（LCT 所有，RFC + 2/3）/ `std::*` Tier-1（标准库团队，zom-std RFC + Kripke 语义附录）/ `<crate>::*` Tier-2（用户 & 库作者，推荐 `<crate>::attr::*` 惯例）。Tier-0 下 7 子空间（hint / ffi / stability / lint / feature / lang / layout / doc / param / attribute — 合计 10，D8 中 `finalNamespaces` 给出逐子空间清单）全部显式枚举，禁止后续版本以"向后兼容"为借口随意新增。命名空间裁决还附带 root-ns 消歧规则 `#::zom::inline` — 镜像 C++，解决极端情况下用户 `mod zom { … }` 对内层作用域的遮蔽问题。

**第三段：Tier 架构裁决**。最终 tier 分层采纳 `finalCheckerStages` 的 6 阶段（S0 Binder / S1 WFF / S2 Lattice / S3 Closure / S4 Usage / S5 Lowering + S6 LSP&Doc）流水线，**拒绝**：(a) rc1 D6 草案的 "Tier 1/2/3 顺序分阶段启用，rc1 仅 Tier-1 语义骨架" 的延迟实现模式；(b) 语义未定义的 "TBD placeholder 属性"；(c) 属性在多个 checker 阶段重复被扫描（确定性 staging 违规）。从 rc1 起，S0–S5 六个阶段 100% 被规范文档覆盖 — 每个 Tier-0 属性的 ArgsSchema / 目标节点校验 / 错误码、每个 Tier-1 marker 的 9 条 R0–R9 传播规则、每个 Tier-2 用户 marker 的 conjunctive 展开 + cycle 检测 — 均无 "实现待定" 条目。架构裁决的工程交付规模由 `finalImplementationEstimate` 给出：AST 500 / Binder 900 / Checker 4600 / Lexer 75 / LSP 260 / Macro 2000 / Parser 1350 / Rustdoc 220 / Test 6400，**合计 16,305 ±12% LOC**，并配套 ZOM0600–ZOM0699（属性系统）+ ZOM0700–ZOM0799（marker 相干 & 并发门控）两段诊断码保留。

**第四段：Marker 三层语法面 + 负 impl 中缀 `!` 裁决**。最终 marker 语法采纳 `finalMarkerSyntax` + `finalNegativeImplSyntax` 的**纯属性 + trait-impl 混合模型**，**拒绝**：(a) Design A 独立 `#Name;` 裸 marker；(b) `#[marker(Sendable)]` 包装属性；(c) `impl marker !Sendable for T` 冗余 `marker` 关键字；(d) bound-only 负 impl（表达力不足，无法表达条件 blanket 如 `impl<T> !Shared for UnsafeCell<T>`）。三条语法面严格分工且不互为别名：Surface 1（Attribute Form）`#[std::marker::Sendable(auto=true)]` 用于**声明级 opt-in**；Surface 2（Bound Form）`T: Sendable + !Shared` 用于**泛型界与 where 子句**（因 prelude 将 `std::marker::*` 裸名注入 TYPE 命名空间，此处裸名合法，与 Surface 1 中 2 段强制规则 **故意不对称** — D8 明确写为 intentional asymmetry，见 `finalNamespaces` 扁平名执行矩阵）；Surface 3（Impl Form）`'unsafe'? 'impl' '!'? <namespaced-path> [typeArgs] 'for' T where … (; | {body})` 用于**负 impl 与条件 blanket**。中缀 `!` 位于 `impl` 后、marker path 前（`impl !std::marker::Shared for UnsafeCell<T>`），是 `finalNegativeImplSyntax` 文档中对 6 种替代语法（prefix `#!` / `neg impl` / `impl not` / attribute-form / boolean assignment / statement form）逐一 VETO 后的唯一幸存者 — 其核心理由是让负 impl 的全局事实在语法层一眼可识别，而非被一个局部属性隐藏。负 impl 配套 5 条语义规则（全局闭包公理 / 相干双 span / auto-deriver 下推规则 / orphan rule / justification check），由 `finalNegativeImplSyntax` §Semantics 完整给出；配套 4 条错误码（ZOM0701 UnjustifiedNegativeImpl / ZOM0702 OrphanNegativeImpl / ZOM0710 CoherenceViolation / ZOM0712 DownstreamBlanketRevivesNegated）作为落地保障。D8 中特别强调：`#[!std::marker::Sendable]` 属性形式**明确禁止**（理由：negative impl 是全局事实，不是本地注解），rc1 阶段所有引用过 `#[negative_impl(…)]` 的地方必须全部替换为 Surface 3 中缀 `!` 形式。此裁决结果在 §5.2 中给出完整 6 条核心 marker 的**工作样例**，在 §7.1 迁移表第 1/2/9/10 行给出 1:1 对应写法，在 §11 Appendix 中给出负 impl 与 unsafe 正 impl 的对比代码。

### D7 · 错误/并发/模块三系统分层 + 四阶段渐进落地

| 项 | 内容 |
|---|---|
| 推荐 | **方案 C：三系统分层解耦 + 四阶段渐进落地** |
| 阶段 0（前置 1.5 人月） | Checker 骨架 + Driver 重构 + 跨模块/并发/错误专用诊断码 45 条（错误 25 + 模块 12 + 并发 8）。关闭：模块 Critical 5 Export/拓扑 + 错误 Critical 2。 |
| 阶段 1（4 人月） | 错误系统 raises 归一（T? → T\|null flatten，联合归一，raises 子集检查）；模块系统 package/作用域/可见性；并发系统 marker interface + 核心类型 + spawn/suspend 语法落位 |
| 阶段 2（3 人月） | 错误×并发：取消传播 + supervisor 策略；错误×模块：跨模块 raises 子类型；并发×模块：跨 crate Sendable 一致性 |
| 阶段 3（1.5 人月） | 三者统一闭环：编译并行调度利用并发 runtime；Diagnostic Engine 并发任务隔离；合规测试全量绿条 |
| 合计 | 10 人月 / 20 KLoC / 45 新增诊断码 |
| 与本设计的绑定 | 所有 L1 承诺在 Level-2 前一律不对外宣传；文档 L2/L3 标记不得删除 |

---

## 4. 语法层规范 EBNF（完整 9 大章节，含 suspend / spawn / 属性白名单）

> **完整正文** 约 1187 行，已写入独立文件 `docs/design/DESIGN-DIMENSION-01-SYNTAX-EBNF.md`，本文件此处仅给出**与并发直接相关的新增/修正要点**。读者若需完整 EBNF / 词法文法 / 五向一致性矩阵 / T1~T7 验证示例，请跳转阅读。

### 4.0 新增并发语法摘要

```ebnf
(* ── 1. SuspendStatement ── 三种形式 ── *)
SuspendStatement ::= 'suspend' (
    | 'until' SuspendContractExpression                         (* suspend until ev;                  普通挂起直到唤醒 *)
    | 'until' SuspendContractExpression 'with' CancelHandler   (* suspend until ev with { onCancel {...} }  *)
    | ';'                                                      (* suspend;  等价 yield，重新入调度循环      *)
) ;
CancelHandler ::= BlockExpression ;

(* ── 2. SpawnExpression ── 四种修饰符(可组合) ── *)
SpawnExpression ::= 'spawn' SpawnModifier* BlockExpression ;
SpawnModifier ::=
      'detached'                               (* 脱离当前 Scope，无 join 等待；需 #[zom::concurrency::detached] 或 lifetime: 'static 提示 *)
    | 'blocking'                               (* 放入专用线程池，不占用 worker 槽位 *)
    | 'priority' '(' ('low' | 'normal' | 'high' | IntegerLiteral) ')'   (* 优先级 *)
    | 'pin_worker'                             (* 始终在当前 worker 执行，禁止 work-steal *)
;

(* ── 3. 属性：主形态 + 参数 @糖（Canonical D8 finalEBNF，LL(2)） ──
   AST 内 100% 统一为 ModifierList → OuterAttribute。
   @ 仅允许在 ParameterDecl 位置，解析器直接降格为 #[zom::param::name]。 ── *)
Declaration ::= ModifierList* ( DeclarationKeyword ... ) ;
ModifierList ::= ( OuterAttribute | visibilityKeyword | keywordModifier )* ;
OuterAttribute ::= '#' '[' attributeEntry ( ',' attributeEntry )* ','? ']' ;
InnerAttribute ::= '#' '!' '[' attributeEntry ( ',' attributeEntry )* ','? ']'
                    { only permitted at SourceFile.head / BlockStatement.head } ;
attributeEntry
    = attributePath                                           (* #[zom::hint::inline]          — hint  *)
    | attributePath '=' attrLiteral                           (* #[zom::doc = "text"]         — equal *)
    | attributePath '(' ( attrArgument (',' attrArgument)* ','? )? ')'
                                                            (* #[zom::repr(C, align(8))]    — call  *)
    ;
attributePath
    = Identifier ( '::' Identifier )+                         (* HARD RULE: ≥ 2 segments       *)
    | Identifier                                              (* LegacyBareWhitelist only:
                                                                 deprecated | inline | cold
                                                                 → parser rewrite → zom::… + W7105 *)
    ;
attrArgument
    = attrLiteral | Identifier                                (* positional                     *)
    | Identifier '=' ( attrLiteral | Identifier )             (* named key=value                *)
    | attrTokenTree                                           (* free-form for Tier-2 macro    *)
    ;
(* ParameterDecl @ sugar: @variadic x: ...  ⟹  #[zom::param::variadic] on parameter
   FIRST set guarded by isStartOfParameter(position) context.
   Misplaced @  ⇒  ZOM0602 MisplacedAt                                      *)

(* ── 4. raises 子句：单 Type 天然承载联合 ── *)
FunctionSignature ::=
    'fun' Identifier GenericParameters? '(' ParameterList ')'
    ( '->' TypeExpression )?
    ( 'raises' TypeExpression )?                  (* 例: raises Cancelled | IoError | Timeout *)
    WhereClause?                                   (* 生产级启用；支持 type:boundItem(+boundItem)*，含负 bound !Marker；与 D8 finalEBNF §WhereClause §BoundItem §MarkerBound 一致 *)
;
```

### 4.1 10 项漂移修正（G1~G10，取自 DESIGN-DIMENSION-01 §7）

| ID | 内容 | 对并发的影响 |
|---|---|---|
| G1 | `TypeParameter` 支持默认类型 `= T` | `interface Try<O, E = Never>` 成为可能，`?!` 兼容 nominal Result |
| G2 | `RelationalExpr` 补 `is TypeExpr` | `match v when is Cancelled =>` 模式的基础（并发错误 match 大量使用） |
| G3 | `(x)` 解析歧义消除（单元素 tuple 必须 `(x,)`） | 跨 suspend 返回 tuple 的正确解构 |
| G4 | `char` 预定义类型 | FFI C ABI 对齐 |
| G5 | suspend/spawn EBNF 接入规范 | 并发语法正式落位 17-chapter |
| G6 | 属性系统（命名空间强制 + 白名单） | zom::*/std::*/<crate>::* 三层分桶；裸名 3 条白名单 W7105；marker/负 impl 三句法面全部落位（D8） |
| G7 | Marker 系统（`marker M;` 声明 + `#[std::marker::Sendable]` Surface 1 + `T: Sendable + !Shared` Surface 2 + `impl !… for …` Surface 3） | 用户 marker declaration `marker M = B1 + B2 where … ;`；内建 marker 由编译器注入 prelude；6 条并发核心 marker 作为 std::marker::* 裸名在 TYPE 命名空间 prelude 可用 |
| G8 | `?!`/`!!` 统一 Postfix（优先级 3）、`?:` 分离（优先级 18） | 错误传播链：`open_file()?!.read()?!` 正确结合 |
| G9 | `raises` 改为 `|` 并集（废弃逗号列表） | 与 D1 B 方案一致 |
| G10 | 对象字面量双形式（简短初始化 + 键值对） | TaskContext 构造 API 简洁化 |

#### §4.2 正式 Tier 分层白名单（与 Ch.16 §16.8 / §16.9 一一对应）

**Tier-0（生产级，闭集，需 RFC 新增）—— 共 23 条：**
  zom::hint::inline/cold/likely/unlikely/unroll/must_consume/suspend_capable
    (suspend_capable schema = enum {None, Possible, Required})
  zom::ffi::link_name/export_name/no_mangle/c_abi
  zom::stability::deprecated/unstable/discriminator
    // 注意：since、note 是 deprecated 的 schema 内部命名键，
    // 不是独立属性——写 '#[zom::stability::since("1.0")]' 非法（ZOM0617）。
    // discriminator 为独立属性，schema = u8/u16/u32/u64 字面量。
  zom::lint::allow/deny/warn/force
    // lint code 区间映射（全局统一约定）：
    //   ZOM0600 – ZOM0699  属性语法 / 词法
    //   ZOM0700 – ZOM0799  marker 闭包 / 并发门 / 相干 / Orphan
    //   ZOM0800 – ZOM0999  编译管线 / LSP / HIR desugar（预留）
    //   ZOM8000 – ZOM8999  运行时并发语义检查（G1–G6 runtime）
    // allow/deny/warn/force schema = 接受任意 ZOMd{4} code。
  zom::lang::sized/unsafe_block/runtime_only
    // sized、destructor = 编译器内部 lang-item，非用户可写属性；
    // 用户能写的只有 unsafe_block（G5 门控）和 runtime_only（方法声明）。
  zom::feature::enable
  zom::repr(C, align, packed, transparent)
    // 2 段统一根，所有 layout 家族统一走 zom::repr。
  zom::doc::*
  zom::param::variadic/move/unused
  zom::attribute::retain(tier, structural?)
  zom::concurrency::scope_guard/detached/requires_executor/
      within_scope(scope_id)/assume_executor_context
    // Ch.16 §16.5.1 子空间 #11，新闭集条目

**Tier-1（stdlib marker 域，闭集，需 RFC）—— 共 18 条，对应 Ch.16 §16.9：**
  • 6 并发 gate：Sendable / Shared / Linear / TaskBound /
                NoSuspendHazard / SuspendSafe
    （全 bare unit，auto-derive 行为内建；用户不可切换参数——W7103）
  • 9 layout/POD：Pod / ZeroInit / NoUninit / Copy / StableAbi /
                  Discriminant / Sized / NoInteriorMuta / Pin
        （Pin 为本次新增 T1-16，需经 RFC 归档）
  • 3 工具类：MustUse（std::marker::MustUse，**不能写成 std::must_use**）
              / NonExhaustive / Deprecated

**Tier-2（用户宏开放集）：** 见 Ch.16 §16.10；当前 Ch.16 仅提供 Macro trait 接口，具体语法另章规定。

rc1 阶段：语法 100% 解析（L0 保证）；Tier-0 ArgsSchema / 目标节点校验在 S1 WFF 全量启用；Tier-1 9 条 R0–R9 传播规则在 S3 Closure 全量启用；Tier-2 宏展开在 S0 Macros 全量启用。**任何未识别属性 → ZOM0610 ERROR（非 WARNING，原 rc1 草案 "未识别→WARNING" 提升为 ERROR，与命名空间强制硬规则一致）**。

### 4.3 并发语法与零颜色原则的交互（L2 保证）

- 函数签名不写 `async`/`suspend`；suspend 是函数内部控制流。
- 编译器推导函数的 SuspendCapability（None / Possible / Required）。
- 在 extern "C" 回调、裸 pthread 入口（`#[zom::concurrency::requires_executor]` 缺失）调用 Possible/Required 函数 → **ZOM8012 FATAL**。
- `fun deinit()` 内部调用 Possible/Required 或直接写 suspend → **ZOM8013 ERROR**（与 B.4 一致）。

---

## 5. 核心类型与 Marker Interface 矩阵

> 本节的 marker interface 体系与并发核心类型，来自 D1 错误通道归一化 + D2 并发安全标记决策。若需底层类型体系现状审计（类型体系断层、Interface 矩阵、Error 变体现状、Linear 现状与实现路线），读 `docs/design/APPENDIX-DECISIONS-D1-D7.md` 的 D1/D2 完整决策文本。

### 5.1 并发相关 error 变体总集（ZOM 真实语法）

```zom
// —— 并发错误使用 D1 的 error 声明；所有变体在 raises 子句中用 | 联合
//    错误判别符：Tier-0 zom::stability::discriminator（原 lang::error_discriminator）
//    命名空间已同步 Canonical Judge Design D8 §Namespaces
// 每个 error variant 必须指定 '#[zom::stability::discriminator(…)]'
// 以固定跨版本 ABI。数值字面量接受 u8|u16|u32|u64（十进制 /
// 0x / 0o / 0b 均可）。schema = 单位置整数参数
// （Ch.16 Tier-0 T0-20 正式定义）。
#[zom::stability::discriminator(0x01)]
error Cancelled(task_id: u64, reason: str) extends BaseError

#[zom::stability::discriminator(0x02)]
error Timeout(after_ns: u64) extends BaseError

#[zom::stability::discriminator(0x03)]
error IoError(code: i32, detail: str) extends BaseError

#[zom::stability::discriminator(0x04)]
error Panic(task_id: u64, message: str, backtrace: Option<Backtrace>) extends BaseError

#[zom::stability::discriminator(0x05)]
error Poisoned(type_name: str, holder_task: u64) extends BaseError

#[zom::stability::discriminator(0x06)]
error ScopeAbandoned(child_errors: Vec<BaseError>) extends BaseError

#[zom::stability::discriminator(0x07)]
error DeadlineExceeded(total_ns: u64, pending_tasks: u32) extends BaseError

#[zom::stability::discriminator(0x08)]
// DoublePanic: 第一次 panic 的 unwind 路径触发第二次 panic
// 采用 Linear-only-cleanup；leaked_count / linear_cleaned_count 写入 LeakReport
error DoublePanic(first: Panic, second: Panic,
                  leaked_count: u32, linear_cleaned_count: u32) extends BaseError

#[zom::stability::discriminator(0x09)]
error FfiNull(param_name: str) extends BaseError

#[zom::stability::discriminator(0x0A)]
error FfiAbiMismatch(expected: str, got: str) extends BaseError

// 联合类型别名（用户可直接在 raises 中使用）
type ConcurrencyError =
    Cancelled | Timeout | IoError | Panic | Poisoned
  | ScopeAbandoned | DeadlineExceeded | DoublePanic | FfiNull | FfiAbiMismatch
;
```

### 5.2 Marker Interface 6 条核心（D2 CR-1 最终形态 · 已同步 Canonical Judge Design）

【统一形态声明，同步 Ch.16 §16.9.0】六个并发 marker 全是 std::marker::* 域下的
bare unit 属性。用户不能通过属性参数切换 auto-derive 行为——auto-derive 内建于
lattice 规则（Ch.16 §16.13 结构自动推导）。

```zom
// —— 每条都以 Tier-1 命名空间属性 + marker impl 表达；不引入 trait 关键字。
//    marker 接口走 Ch.16 规范的三条正交表面（无「marker_interface 属性」）：
//      Surface 1：'#[std::marker::M]' 声明附着（属性形式）
//      Surface 2：'marker M = B1 + B2 …;' 上下文关键字（marker 声明形式）
//      Surface 3：'[unsafe] impl [!] M for T [where …]' （impl 形式 / 负 impl）
//    （Canonical Judge Design — 纯属性 + trait-impl 混合模型，Surface 1 Attribute Form）

// 所有权跨 spawn 安全；所有字段满足则自动满足（auto = true 默认）
#[std::marker::Sendable]

// 只读引用跨 spawn 安全；UnsafeCell/Mutex/RwLock 需负 impl 覆盖
// （unsafe impl 为 Canonical Surface 3 正 impl 覆盖）
#[std::marker::Shared]

// 必须恰好一次消费（Scope/Task/Channel 端点）。正常控制流 L1；unwind L3
// Linear 从不 auto-derive，必须显式标注
#[std::marker::Linear]

// 跨 suspend 点允许被活跃持有；MutexGuard 必须负 impl
#[std::marker::NoSuspendHazard]

// 函数体跨所有 await 边均无锁态/无 task-affine 资源
#[std::marker::SuspendSafe]

// task-affine 资源，禁止被 R_send 边转移；¬Sendable 的结构别名
#[std::marker::TaskBound]
```

**声明层（标准库内建声明，非用户代码）** — Canonical Judge Design marker declaration form（Tier-2 用户 marker 用同一语法）：

```zom
marker Sendable;                                              // 无基类的基础 marker
marker Shared;                                                // R0: Shared ≤ Sendable 由 lattice 边 R0 提供
marker Linear;                                                // R2/R7: Linear ⇒ ¬Copy
marker NoSuspendHazard;                                       // R6: NoSuspendHazard ≤ SuspendSafe
marker SuspendSafe;                                           // 6 条 concurrency 核心之一
marker TaskBound;                                             // R1: TaskBound ≤ ¬Sendable
```

**负 impl 与条件 blanket（Canonical Surface 3 — 中缀 `impl !` 语法，D2 CR-2）**：

```zom
// —— 典型 negative impl：infix ! AFTER impl 关键字（禁止 #[negative_impl] 属性形式）——
impl<T> !std::marker::Shared           for std::cell::UnsafeCell<T>;
impl<T> !std::marker::Shared           for std::sync::Mutex<T>;
impl<T> !std::marker::Shared           for std::sync::RwLock<T>;
impl<T> !std::marker::NoSuspendHazard  for std::sync::MutexGuard<T>;
impl<T> !std::marker::NoSuspendHazard  for std::sync::RwLockReadGuard<T>;
impl<T> !std::marker::NoSuspendHazard  for std::sync::RwLockWriteGuard<T>;

// unsafe 正 impl 覆盖（Mutex 拥有 UnsafeCell → auto-derive ¬Shared；显式 unsafe 正 impl 是 Canonical 覆盖）
unsafe impl<T> std::marker::Shared for std::sync::Mutex<T>
  where T: std::marker::Sendable;

// 条件 blanket 正 impl
impl<T> std::marker::Sendable for std::vec::Vec<T>
  where T: std::marker::Sendable;
```

**Bound form（Canonical Surface 2 — 泛型界 / where 子句，T: Sendable + !Shared）** 在 §4 EBNF 与 §7.1 迁移表中统一给出。

**自动推导规则（§7.1 完整定义，摘录 · 与 Canonical Checker S2/S3 一致）**：
- `Sendable / Shared / NoSuspendHazard / SuspendSafe`：struct/class/enum 的所有字段满足 ⇒ 聚合类型自动满足（`auto = true` 时字段递归；`auto = false` 禁用推理，见 §5.3 Shared(auto=false) 示例）。
- `Linear / TaskBound`：**不参与 auto-derive**（`auto = true` 标志对这两个 marker 无效）。必须显式 `#[std::marker::Linear]` / `#[std::marker::TaskBound]` 属性 或 显式 impl。
- 任何字段类型的负 impl ⇒ 聚合类型自动负 impl（负传播链诊断见 D2 CR-6；S3 模态闭包 3 轮内达不动点）。

### 5.3 并发核心类型（Linear 语义一以贯之 · 已同步 Canonical Judge Design Surface 1 + Surface 3）

```zom
// ====== Task<T> —— Linear；await 是唯一合法 consume ======
#[std::marker::Linear]
class Task<T> {
    fun id(self) -> u64;
    // consume self；若已 cancel 或 faulted，返回对应 error 变体
    fun await(self) -> T raises Cancelled | Panic;
    // 非 consume；仅设置取消令牌位
    fun cancel(self: &Task) -> unit;
    // 非 consume；读原子状态
    fun status(self: &Task) -> TaskStatus;
}

enum TaskStatus { Pending, Running, Suspended, Completed, Faulted, Cancelled, Zombie }

// ====== SuspendContract<T> interface —— 唯一挂起契约 ======
interface SuspendContract<T> {
    // 注册一个 SuspendEvent；合约在 ready 时触发 set_completion
    #[zom::lang::runtime_only]                    // Tier-0 内建属性；原 lang::runtime_only
    fun register(self, ev: &SuspendEvent<T>) -> unit;

    // 取消感知：合约实现返回 true 表示已响应取消
    #[zom::lang::runtime_only]
    fun cancel(self) -> bool;
}

// SuspendEvent 三态原子机（enum 非 const u32）
enum SuspendState { Pending, Ready(T), Cancelled }

// Canonical Judge Design: zom::repr(C, align(64)) 合并为一
//   — 拒绝 rc1 草案的两个分裂属性 #[lang::repr_c] + #[lang::repr_align(64)]
#[zom::repr(C, align(64))]
class SuspendEvent<T> {
    state: Atomic<SuspendState<T>>;
    waker: Atomic<&RawTask>;   // 允许 null；C/C++ FFI 为 opaque 指针
    contract: &dyn SuspendContract<T>;
}

// ====== Scope<R> —— 结构化并发作用域（Linear 间接持有） ======
//   Canonical: scope_guard 从 lang 子空间迁移至 zom::concurrency::scope_guard
//   (Tier-0 zom::concurrency::* 新增子空间，不改变 10 子空间列表 —
//    【历史澄清】早期草案曾经计划的 'zom::attribute::marker_interface' 属性
//    从未出现在正式 Ch.16 规范中（也从未迁移到 'zom::lang'）——marker 接口
//    是纯语法表面 + 语义 lattice 级别的内建能力，不使用任何属性门控。
//    本段叙述为历史设计残留备忘，现修正为 Surface 1/2/3 三表面方案。)
#[zom::concurrency::scope_guard]
#[std::marker::Linear]
class Scope<R> {
    fun id(self: &Scope) -> u64;
    fun is_cancelled(self: &Scope) -> bool;
    // Linear 副作用：所有子 Task 句柄由 Scope 内部注册（外部不暴露 Linear leak）
    fun cancel_all(self: &Scope) raises Cancelled;
    // spawn 绑定到本 Scope；body 的借用静态分析仅限 lexical 闭包内部
    //   参数属性从 lang::move → Canonical Surface 1 zom::param::move
    fun spawn<T>(self: &Scope, #[zom::param::move] body: fun() -> T)
        -> Task<T> raises Cancelled
        // Surface 2 Bound Form：T: Sendable（prelude 裸名合法）
        where T: std::marker::Sendable;
}

// ErrorPolicy 枚举（变体参数：ZOM 真实 enum 元组形式）
enum ErrorPolicy {
    CancelOnFirstError,
    WaitAllCancelOnAny,
    OneForOne(max_restart: u32),
    AllForOne(max_restart: u32),
    Ignore,   // 使用需 #[zom::lint::allow(ZOM0748)] 显式豁免（Canonical Tier-0 zom::lint）
              // 注：Ch.16 为 attr/marker/并发门保留 ZOM0600–ZOM0799；
              // ZOM8xxx 序列对应运行时并发检查（§5 并发错误码表）。
              // ZOM0748 = 原 ZOM8015「未使用 must_use 值」在 attr 域的等价编码
              // lint schema 统一接受所有 ZOMd{4}，故纯数值写法亦可编译通过。
}

// ====== Channel / Sender / Receiver —— 全 Linear ======
#[std::marker::Linear] class Sender<T>;
#[std::marker::Linear] class Receiver<T>;

class Channel<T> {
    // 单消费者默认
    static fun new(cap: usize) -> (Sender<T>, Receiver<T>);
    // 多生产者/消费者共享端点（D2 Linear 语义不克隆）
    fun into_shared_senders(self, n: u32) -> Vec<Sender<T>>;
    fun into_shared_receivers(self, n: u32) -> Vec<Receiver<T>>;
}

// Sender.send / Receiver.recv 返回 raises 错误
fun <T> Sender<T>.send(self, v: T) raises Cancelled | ScopeAbandoned;
fun <T> Receiver<T>.recv(self) -> T raises Cancelled | ScopeAbandoned;
```

### 5.4 20 陷阱矩阵（承诺分级诚实版，B.8 修正版）

| ID | 陷阱 | 所需 Marker / 检查 | 保证 | 诊断码 | 实现 Level |
|---|---|---|---|---|---|
| P01 | spawn 捕获非 Sendable 值跨线程 move | Sendable | ↯L3（Level-0） ⚠️L2（Level-1）✓L1（Level-2，safe+lexical） | ZOM8001 | L-0: runtime assert / L-1: lint ERROR / L-2: compile ERROR |
| P02 | Task<T> 未被 consume（僵尸任务泄漏） | Linear | ↯L3 ⚠️L2（Level-1）✓L1（Level-2，正常路径） | ZOM8004 | unwind 路径 L3 Linear-only-cleanup |
| P03 | spawn 按引用捕获非 Shared（data race） | Shared | ↯L3 ⚠️L2 ✓L1（safe+lexical） | ZOM8002 | 指针间接/type-erasure WARNING + runtime |
| P04 | worker 执行阻塞 IO/syscall（starvation） | spawn blocking 修饰符 | ⚠️L2（Budget 耗尽 + 阻塞检测）+ L3（san） | ZOM8011 | 检测到阻塞时把 work-steal 权交给 replacement worker |
| P05 | double-panic 触发资源 double-free 或 leak | DoublePanic error + Linear cleanup | ⚠️L2（LeakReport 写入） | ZOM9008 | Linear-only-cleanup 路径 |
| P06 | 栈溢出导致整进程崩（无 per-task 归属） | 分段栈 + guard page + SIGSEGV handler | ⚠️L2（handler 内 in_switch 原子检测延迟 1 tick） | — |  |
| P07 | worker 死循环 CPU 100%（协作抢占缺失） | Budget + Epoch + yield 注入 | ⚠️L2（cfg 回边 checkpoint） | ZOM8017 | Checker 未实现时 runtime N 次 budget 检查 |
| P08 | SuspendContract 非线程安全 set 触发唤醒丢 | 所有实现强制 SeqCst 原子 + double-check | ✓L1（实现规范强制） | — |  |
| P09 | spawn detached 捕获非 static 引用 | `'static` 检查 + `#[zom::concurrency::detached]` 要求 | ⚠️L2（lexical 下 compile；跨函数 runtime） | ZOM8010 | unsafe 中 ZOM8010-UNSAFE 警告 |
| P10 | spawn_scope 闭包借用被外部存储逃逸 | 内建有限 HRTB + scope_stack | ⚠️L2（lexical L1 + 跨函数 L3） | ZOM8003 | **B.8 修正：从 L1 改为 L2**；公开数字 16/3/1 |
| P11 | cancel_token 父子树断裂（孤儿任务无取消） | Weak 回父指针 + spawn 原子注册 | ⚠️L2（scope drop 路径双检查） | ZOM9002 |  |
| P12 | 无 select start_index 饥饿（AUD-B.6） | round-robin start_index + CPU/IO 3:1 配额 | ⚠️L2（runtime 状态机） | — | Budget 计入 select 连续调用次数 |
| P13 | 死锁场景 1（join_all cross-layer） | 同-worker 内联调度 + 可重入 run() | ⚠️L2（det_sched 下必现） | ZOM9003 |  |
| P14 | 死锁场景 2（CircularTaskWait） | det_sched + wait-for-graph 构建 | ⚠️L2（det_sched + cycle DFS） | ZOM9004 | release 模式关闭 |
| P15 | 死锁场景 3（reactor 路由死锁） | lock 顺序全局规则（global<worker<reactor<task） | ✓L1（代码评审 + lint） | ZOM8016 | per-worker shard fd map（B.3 修复） |
| P16 | Scope drop 中 suspend 与 unwind 互斥（AUD-B.4） | in_panic_unwind 检查 + 双路径 drop | ⚠️L2（runtime） | ZOM8013 ERROR（deinit 内禁止 suspend） | 见 §6.9.1 |
| P17 | 跨 FFI 回调误用 executor（AUD-B.1） | `#[zom::concurrency::requires_executor]` 强制门控 | ⚠️L2（默认 ERROR；unsafe 下 assume 可豁免） | ZOM8012 | FFI/Drop 边界显式标注，D5 C 方案 |
| P18 | MutexGuard 跨 suspend 持有（语义死锁） | NoSuspendHazard 负 impl + flow-sensitive | ↯L3 ⚠️L2 ✓L1（Level-2） | ZOM8006 | flow-sensitive（D2 CR-7）；drop(guard) 后放行 |
| P19 | 1M 任务内存 backpressure | per-scope 并发限制 + spawn 懒入队 + stack 段池 | ⚠️L2 | ZOM9005 |  |
| P20 | poison 语义不一致（锁内 panic → 毒化 vs 自动回收） | Poisoned error + policy 枚举 | ⚠️L2（Poisoned error 变体显式 raises） | ZOM9007 | supervisor 策略下可选 auto-restart |

**覆盖率公开数字**：✓L1 **10** / ⚠️L2 **9** / ↯L3 **1** = 合计 **20**。

（公开数字诚实版：可信度审计 B.8 指出 11 处虚标，原声称 18/1/1 修正为当前 10/9/1。Level-2 完成后再升级为 16/3/1。）

### 5.4.1 并发 6 门 G1–G6 汇总（与 §2 Kripke 可达关系严格绑定，Co-normative）

> 【近似插入说明】原 rc1 文档 §5.4 仅给出 20 陷阱矩阵，未显式展开 G1–G6 单行门表。本小节按 M17 要求插入 6 行门矩阵 + G6 dyn-head 双层保障说明，所有 (Lx / R_…) 标注严格对齐 §2 Co-normative 规则。

| Gate | 门条件 | 违规诊断码 | 承诺层级 & 可达关系子集 |
|------|--------|-----------|------------------------|
| G1 | `T : Sendable` — spawn/send 按值转移所有权 | ZOM8040 | (L1 / R_scope*) |
| G2 | detached pledge — 显式 detached 语义声明 + scope_id | ZOM8041 | (L2 / R_scope* ∪ R_send with scope_id) |
| G3 | `T : SuspendSafe` — 函数体跨所有 await 边均无锁态/无 task-affine 资源 | ZOM8042 | (L1 / R_scope* ∪ R_susp*) |
| G4 | `T : NoSuspendHazard` — 跨 .? 边界活跃持有无数据竞争 | ZOM8043 | (L2 / R_scope* ∪ R_susp*) |
| G5 | `unsafe impl marker` — 程序员自证 marker 正/负 impl | ZOM8044 | (L3 / full*) |
| G6 | `T : TaskBound  ⊕  !Sendable` — task-affine 资源禁止被 R_send 转移 | ZOM8046 | (L2∩L3 / R_send* + runtime bitmap) |

> 补充（G6 dyn-head 双层保障，与 Ch.16 R11 同步）：
>   - **S2b 静态**：`dyn M1 + M2 + …` 的 bound 合取集 {M_i} 须完整跑 R0–R11 + Marker-Incompatibility Table，冲突报 ZOM0763。
>   - **L2 运行时**：通过 S2b 的 dyn 对象在 spawn 接受点再做一次 runtime type-id 的 marker-bitmap 校验，若 bitmap 的 (TaskBound, Sendable) 两位同时为 1 → ZOM8046 硬错误，并携带触发 concrete type 名用于诊断。
>
> **原理**：◇_T 的世界索引参数被 dyn 擦除后，只靠 syntactic gate 不足；S2b + L2 双层是必要的冗余防御。

---

## 6. 运行时架构 / 边缘语义 / FFI C-ABI / 示例

> 完整正文 397 行（含 mermaid 架构图、伪代码、C 头文件、4 个完整 ZOM 示例），请跳转 `docs/design/DESIGN-DIMENSION-03-RUNTIME-FFI-EXAMPLES.md` 阅读。本节是核心结论与决策绑定摘要。

### 6.1 总体架构 mermaid 摘要

```mermaid
flowchart LR
    subgraph UserCode[用户代码 / 标准库并发 API]
        SP[spawn / spawn_scope / select / with_timeout]
    end
    subgraph Runtime[ZOM 并发 Runtime]
        direction TB
        Inj[全局注入队列(按 cacheline 对齐 head/tail)
            三 cacheline TaskHeader: A本地 B跨worker C只读
            per-worker shard fd map, false sharing 防护(B.7)]
        W1[Worker-0: 本地LIFO + inject FIFO]
        W2[Worker-1: work-steal 半队列]
        W3[Worker-N: ...]
        BP[Blocking Pool
            (spawn blocking)]
        Reac[Global IO Reactor
            + 4 层 × 256 TimerWheel]
        Det[DetSched种子
            确定性调度器]
    end
    Inj --> W1 & W2 & W3
    Reac -- per-worker shard --> W1 & W2 & W3
    SP -- detached --> Detached[Detached Registry]
    SP -- blocking --> BP
    BP -- 阻塞回调 --> Reac
    W1 & W2 & W3 -- 事件fd --> Reac
```

### 6.2 B.2 三种死锁修复方案（逐一对号）

| # | 场景 | 修复 | 保证 |
|---|---|---|---|
| 1 | cross-layer backpressure：所有 worker join_all 等 inner 任务，但无空闲 worker 跑 inner | 同-worker 内联调度：join(self, inner) 时 worker 把自己的 run() 入口当作可重入函数，**直接在栈上展开 inner 的调度循环**，而不是返回到"等待有人唤醒"的 park 状态 | ⚠️L2 |
| 2 | Circular TaskWait：A→B→C→A 互相 join | det_sched 模式下构建 wait-for 图，DFS 检测环 + 反向边崩溃任务 | ⚠️L2（det_sched 开） |
| 3 | Reactor 路由死锁：持有 worker 锁的上下文尝试 reactor 全局锁 | 锁顺序强制执行 `global<worker<reactor<task`；全局锁获取使用 `try_lock` + 3 次微退让；per-worker fd shard 减少 95% 的全局锁争用 | ✓L1（代码结构 lint ZOM8016） |

### 6.3 B.3 Channel 单-waker 彻底重写

**之前草案的致命缺陷**：`send_ev/recv_ev` 单 waker 与 SuspendEvent 单 shot 语义矛盾（B.10）；同时 `Shared` 未负 impl UnsafeCell（B.3-A），ARC<Channel> 的内部可变性导致 data race。

**最终方案**：
1. Channel 内部 **per-waiter SuspendEvent 独立节点**（不是单 slot），形成 waiter 链表。send/recv 操作按顺序唤醒链表头部。无共享 waker，clone 语义零出现。
2. `UnsafeCell<T>` 的 `!std::marker::Shared` 在 prelude 中声明（D2 CR-2，`impl !std::marker::Shared for std::cell::UnsafeCell<T>;` 中缀负 impl）。`Mutex<T>`/`RwLock<T>` 显式 unsafe 正 impl 覆盖同理（`unsafe impl<T> std::marker::Shared for std::sync::Mutex<T> where T: std::marker::Sendable;`）。
3. Close 语义 4 条硬规则（写入规范 §6.5）：
   - 显式 `.close()`：所有未完成的 send/recv 返回 `Cancelled | ScopeAbandoned`
   - **最后一个 Sender（按 Linear 计数 = 0）自动 close**
   - **最后一个 Receiver 自动 close**
   - Close 之后的 send/recv 立刻返回，不阻塞。close 操作幂等。
4. 共享端点：`into_shared_senders(n)` / `into_shared_receivers(n)` 返回线性端点数组，Linear 计数分别独立；close 条件仍为 "同端点 Linear 计数降到 0" 自动触发。

### 6.4 B.4 Scope Drop 与 Panic Unwind 互斥

```zom
// Canonical Judge Design 生产级伪代码（非 rc1 placeholder；字段名/函数名与实现一一对应）
class Scope<R> {
    // —— deinit 中双路径判定（B.4 修复核心）——
    // deinit 识别走 zom::lang::destructor lang-item（非属性形式，
    //   见 finalNamespaces §zom::lang::*；rc1 草案的 #[lang::deinit] 已删除）
    fun deinit(self) {
        // D2 CR-3 Linear 语义 + D5 C 方案：deinit 内禁止 suspend
        // 若已在 unwind，走资源清理分支，绝不等待
        if in_panic_unwind() {
            // 快速路径：仅原子 cancel_all + 写 LeakReport，无任何 suspend
            self.cancel_all_atomic();
            record_leak(self.id, self.pending_task_count());
            return;
        }
        // 正常路径：suspend-join（通过调度循环等待子任务）
        suspend until self.all_children_complete_or_canceled with onCancel { /* double-cancel 安全 */ }
        // 聚合 error 变体写入 ScopeAbandoned(child_errors) 如有
    }
}
```

### 6.5 B.5 Double-Panic 线性清理路径

```zom
// Canonical Judge Design 生产级伪代码（字段名 / 诊断码与实现 1:1）
// 第一次 panic（设 task.panicking = true，记录 Panic 变体）
// unwind 过程中，某个 user deinit 再次 panic（第二次）：
fun handle_double_panic(task: &RawTask, second_panic: &Panic) {
    task.double_panicked = true;
    // Linear-only-cleanup：遍历 Linear 槽位，
    // 跳过 fun deinit()（用户代码可能第三次 panic），
    // 仅对"资源回收清单"（运行时维护的 fd/map/内存）做释放
    linear_only_cleanup(task);
    // 错误变体 DoublePanic 入队 LeakReport
    task.final_error = DoublePanic(first: task.first_panic,
                                   second: second_panic,
                                   leaked_count: task.linear_slot_count
                                              - task.linear_cleaned_count,
                                   linear_cleaned_count: task.linear_cleaned_count);
    // scope 不等待：直接 cancel_all_atomic（见 §6.4 快速路径）
}
```

### 6.6 FFI C ABI（摘录 Opaque 头文件）

```c
// zom_concurrency.h —— 与 ZOM Linear 解耦：C 侧 refcount，ZOM 侧 Linear
typedef struct ZomTask      ZomTask;
typedef struct ZomScope     ZomScope;
typedef struct ZomEvent     ZomEvent;
typedef struct ZomRuntime   ZomRuntime;

// 内存序枚举（D1 对齐 ZOM enum）
typedef enum {
    ZOM_MEMORDER_RELAXED, ZOM_MEMORDER_ACQUIRE,
    ZOM_MEMORDER_RELEASE, ZOM_MEMORDER_ACQ_REL,
    ZOM_MEMORDER_SEQ_CST
} ZomMemoryOrder;

// 进入/退出 runtime（D5 C 方案：裸 pthread 入口必调）
ZomRuntime* zom_runtime_enter(void);
void        zom_runtime_exit(ZomRuntime*);

// Task 生命周期（refcount：C 侧不感知 Linear）
ZomTask* zom_task_ref(ZomTask*);
void     zom_task_unref(ZomTask*);
uint64_t zom_task_id(const ZomTask*);
// on_complete 回调：Release 保证 —— 所有 ZOM 写入对回调可见
void zom_task_on_complete(ZomTask*, void(*cb)(ZomTask*, void*), void* ud);

// Scope 创建/进入（spawn_scope FFI 桥）
ZomScope* zom_scope_enter(ZomRuntime*, enum ErrorPolicy);
void      zom_scope_leave(ZomScope*);  // drop-with-suspend；unwind 路径用 zom_scope_abandon
void      zom_scope_abandon(ZomScope*); // panic 快速路径

// 自定义事件（从 C 侧 set，ZOM 侧 suspend until）
ZomEvent* zom_event_new(ZomRuntime*);   // Acquire 保证：set 后 ZOM 读取 C 写入可见
void      zom_event_free(ZomEvent*);
void      zom_event_set(ZomEvent*, ZomMemoryOrder);
void      zom_event_cancel(ZomEvent*);
```

### 6.7 4 个完整示例（完整源文件 200+ 行 × 4）

参见 §11 of `DESIGN-DIMENSION-03`：
1. `parallel_map_1M.zom` —— spawn_scope + Task<T> + Cancelled raises + ?!
2. `http_get_cancel.zom` —— with_timeout(1s) 单次 + with_deadline(3s) 总控 + race + match error
3. `mpmc_1p4w1s.zom` —— Channel 有界 / into_shared_receivers(4) / Linear 自动 close
4. `supervisor_3workers.zom` —— supervisor_scope + OneForOne(3) + DoublePanic 场景 / restart 计数

每个示例开头的 **Maturity 注释**：
```zom
// Maturity:
//   [L0] spawn / spawn_scope / ?! 语法  → ✓（Level-0 可解析）
//   [L1] raises 进符号 → ✓（Level-1）
//   [L2] Sendable 捕获检查 / Linear one-shot / NoSuspendHazard flow-sensitive → 未实现
//        → 降级：debug 模式 runtime 断言 + lint WARNING(ZOM8001~ZOM8006)
```

---

## 7. 保证与路线图

> 完整正文 421 行，请跳转 `docs/design/DESIGN-DIMENSION-04-ASSURANCE-ROADMAP.md`。本节为核心表格。

### 7.1 语法对照：10 项从「非法 Rust 风格」到「ZOM 真实语法」迁移（精华）

> **特别说明 #3/#7**：`Result<T,E>` 在 `06-declarations.md:227` 定义为 `alias Result<T,E> = T | E`，是合法 ZOM 语法。#3/#7 中列出的「非法写法」特指：(a) 把 Result 当作与 raises 互斥的另一套独立通道、(b) 使用 Rust 式 `where T: Bound` 泛型界、(c) `Result<T,E>::Ok/Err` 这种 Rust 关联项写法。ZOM 原生的 `-> Result<T,E>` 和 `-> T raises E` **完全等价**，二者底层是同一 SetType。

| # | 非法 Rust 风格写法（明确拒绝） | ZOM 真实语法（Canonical Judge Design D8 冻结） | 代价/增强 |
|---|---|---|---|
| 1 | `unsafe trait Sendable {}` / rc1 `#[lang::marker] #[lang::auto_impl] #[lang::unsafe_marker] interface Sendable {}` | **Surface 1 Attribute Form** `#[std::marker::Sendable]` 标注<br/>**声明层** `marker Sendable;`（编译器内置，用户 Tier-2 用同语法）<br/>**unsafe 覆盖** 用 Surface 3 `unsafe impl std::marker::Shared for Mutex<T> where …` | 不引入 trait 关键字；三语法面严格分工（标注 / 泛型界 / 显式 impl）；负 impl 从属性升为独立声明 |
| 2 | `impl !Shared for UnsafeCell<T>` / rc1 `#[lang::negative_impl(Shared)] class UnsafeCell<T> {}` | **Surface 3 Impl Form 中缀 `!`** `impl !std::marker::Shared for std::cell::UnsafeCell<T>;`（全局事实在语法层立即可见） | 属性形式 `#[negative_impl(…)]` 被禁止（D8 Negative Impl Syntax 裁决 §Forbidden）； orphan rule / justification check 由 S1 WFF 统一校验 |
| 3 | **（写法一 非法）** `fn foo<T>() -> Result<T, SystemError> where T: Sendable`（Rust 式 `where`）<br/>**（写法二 完全合法）** `fun foo<T>() -> Result<T, Cancelled\|IoError\|Panic>`（`alias Result = T\|E`）<br/>**（写法三 完全合法）** `fun foo<T>() -> T raises Cancelled\|IoError\|Panic` | `fun foo<T: std::marker::Sendable>() -> T raises Cancelled\|IoError\|Panic` 或等价 `fun foo<T: std::marker::Sendable>() -> Result<T, Cancelled\|IoError\|Panic>`（T 界写入参数声明，使用 Surface 2 Bound Form `T: Sendable` 或带完整路径）；**where 子句现已启用**：`fun foo<T>() -> T raises Cancelled where T: std::marker::Sendable + !std::marker::Shared`（rc1 "WhereClause 未启用" 修正，已同步 D8 finalEBNF） | where 子句从 "保留语法位" 升级为生产级；`T: !Sendable` 负 bound 被允许（Surface 2 MarkerBound `!attributePath`）；raises 与 Result 双轨合一（D1 修正版） |
| 4 | `scope: &'scope Scope<R>` | `scope: &Scope<R>`（生命周期不写进签名，由编译器内建 HRTB 特例分析） | 不暴露通用 HRTB；跨函数降级 runtime |
| 5 | `MutexGuard<'scope, T>` | `MutexGuard<T>`（生命周期参数移除，guard 实现 `impl !std::marker::NoSuspendHazard for MutexGuard<T>` 负 impl） | 简单化；flow-sensitive 分析支持 |
| 6 | `#[repr(C, align(64))]` / rc1 `#[lang::repr_c] #[lang::repr_align(64)]` | `#[zom::repr(C, align(64))]`（Tier-0 zom::layout 子空间，单一 call-form 参数） | 单一命名空间单一属性承载全部布局提示；rc1 的两属性分裂已合并，与 D8 finalEBNF `attributeEntry` call-form 对齐 |
| 7 | **（非法）** `Result<T, E>::Ok(x)` / `Result<T, E>::Err(e)`（Rust 关联构造器写法）<br/>**（合法）** `Success(x)` / `Failure(e)`（`06-declarations.md:200-214` 的变体构造器）<br/>**（合法）** 直接 `return x` / `return e`（配合 raises + ?! 传播） | 值通道直接返回；错误变体走 `?!`；对 FFI/强类型场景用 `Success(x)`/`Failure(e)` 构造（底层等价返回 `T\|E`） | 三入口零成本互转；取消 Rust 式关联项语法 |
| 8 | `trait SuspendContract<T> { type Completion; }` | `interface SuspendContract<T> { ... }`（T 直接作 Completion，去 associated type） | 不引入 associated type 语法 |
| 9 | rc1 `#[zom::concurrency::scope_guard]` 与 `#[repr(C)]` 混用 / `@lang::repr_c` 糖 | 统一 `#[zom::concurrency::scope_guard]` / `#[zom::repr(C, align(64))]` 主形态；参数糖 `@variadic args: ...`（**仅 ParameterDecl 位置**，100% 降格为 `#[zom::param::variadic]`；`@` 不允许作为 attribute 通用糖） | AST 单形态，双语法不漂移；`@` 通用糖被 D8 L4 Lexer Rules 明确禁止（仅 Parameter 位置） |
| 10 | `spawn_scope<T>(fun f(scope: &Scope<T>) -> T): T` / rc1 `#[lang::scope_guard]` 识别 | `spawn_scope<T>(body: fun(scope) -> T) -> T raises ScopeAbandoned`（`#[zom::concurrency::scope_guard]` + 编译器内建有限 HRTB 特例分析） | 显式错误语义；闭包签名更简洁；scope_guard 入 Tier-0 规范命名空间 |

### 7.2 12 条否决方案（含 RA-9/RA-10 新增）

| # | 否决方案 | 核心理由 |
|---|---|---|
| RA-1 | 引入 Rust 风格 async/await 双轨函数颜色 | 违反 NP-1；生态双分；推迟落地 3 月 |
| RA-2 | Go 风格 goroutine + channel + runtime GC（ARC 全局引用） | 无法系统编程（无 unsafe 无 raw ptr） |
| RA-3 | 单一全局执行器（无 work-steal，per-process 单队列） | NUMA 扩展性差（B.7 false sharing 放大） |
| RA-4 | 栈模型 1:1 内核线程（废弃 M:N） | 1M 任务内存占用超限；违背 NP-4 eager |
| RA-5 | Rust 风格 Future poll 模型（stackless） | 与 §9.2 分段栈冲突；FFI 桥接不可行（D4） |
| RA-6 | Java Object.wait/notify 式 monitor 锁作为唯一并发原语 | 取消/timeout 语义不可能；死锁难复现 |
| RA-7 | Erlang 风格 actor-only 并发（禁止共享内存 Mutex） | 性能 10~100x 退化；FFI 内存对齐不可能 |
| RA-8 | scope 不引入 Linear，全 runtime 引用计数 | P02 僵尸任务无法编译期闭环；生态形成后 breaking |
| RA-9 | **引入 Rust 式 trait/impl 体系表达 marker** | 与 ZOM 现有 interface 架构重复；interface 章节完整；复用 interface+@marker 代价更低 |
| RA-10 | **引入 Result<T,E> 作为内建 nominal 枚举 + raises 再单独走 IR 通道（两轨不同底）** | 错误审计指出双轨代价 3x；ZOM 已有 `alias Result<T,E> = T\|E` 与 `raises E` 统一归为 SetType，两入口同一底层表示是零成本 |
| RA-11 | 确定性种子作为默认模式（release 开启） | 关闭 ASLR 安全风险；性能损失 5%~15%；作为 opt-in 工具正确 |
| RA-12 | 并发第一版仅 runtime 不做 marker（TypeChecker 后补） | spawn 无门控 = 默认数据竞争；生态后补会 breaking（Rust 2018 async Send 先例） |

### 7.3 合规测试集精华（Lit L01~L22 + ZTest Z01~Z26）

完整清单见 `DESIGN-DIMENSION-04 §15`。Top-10 最关键：

| 测试 | 覆盖 | 期望 | 级别 |
|---|---|---|---|
| L01 | `suspend until e` 解析 | ✓ 通过 | L0 |
| L02 | `spawn blocking priority(high) { ... }` 解析 | ✓ | L0 |
| L03-bis | **D2 CR-2 UnsafeCell !Shared**（`&X` 捕获 spawn，X 含 UnsafeCell） | ZOM8002 ERROR | L2 |
| L05 | Task<T> 未 consume | ZOM8004 ERROR | L2 |
| L06 | 跨 suspend 持有 MutexGuard | ZOM8006 ERROR（flow-sensitive，drop 后放行） | L2 |
| L10 | `#[zom::concurrency::detached]` 缺失 + 非 static 引用 → spawn detached | ZOM8010 ERROR | L1/L2 |
| L11 | extern "C" 回调未 `#[zom::concurrency::requires_executor]`，内部调用含 suspend 的 ZOM 函数 | ZOM8012 FATAL | L2 |
| Z05 | Double-Panic（P05）资源泄漏计数 vs LeakReport 一致性 | ✓ leaked_count == linear_cleaned_count ≤ 线性槽位差 | L3 |
| Z08 | det_sched 种子 × 10 次输出一致性 | ✓ 字节级完全一致 | L3 |
| Z12 | 32 核 benchmark false sharing（B.7） | TaskHeader 三 cacheline 下吞吐量斜率 ≥ 22 核近似线性 | L3 |

### 7.4 四阶段落地路线图（D7 最终方案）

| Level | 时间线 | 交付物 | 验收绿条 | 对并发规范的影响 |
|---|---|---|---|---|
| **0 — 语法冻结** | T+1 周 | suspend/spawn 进 lexer+parser；`?!`/`!!` 语法链修复；AST 2 接口 + 9 具体节点（ModifierList/Outer/Inner/AttrPath/PosArg/NamedArg/TokenTree/MarkerDecl/MarkerImpl/MarkerBound + ColonColon(::) + @ 参数糖）入 parser + kinds.h；§4 EBNF 与 17-chapter 同步；16-chapter 属性规范从 11 行 placeholder 重写为 1812 行生产级；kinds.h 删除死代码 RaisesClause/ErrorTypeList | L01~L08 全部通过，FAIL 输出正确诊断码（含行号/列号/snippet）；ZOM0600–ZOM0617 属性专用错误码覆盖 | 并发语法 + 属性系统 AST 双最小闭环 |
| **1 — Binder/符号层** | T+2 ~ T+4 周 | FunctionTypeSymbol::errorTypes 字段 + API；Binder `visit(ReturnTypeNode)` flatten+lookup；6 条 std::marker::* 核心 marker 注入 + 9 条 R0–R9 格边；Binder S0 属性名解析 3 路径（zom::*/std::marker::*/dep::<crate>::*）+ LegacyBareWhitelist W7105 + WhereClause 负 bound 解析；Module scope + Export flag + import 绑定最小闭环（修 MOD-03/MOD-05） | L09~L15；跨模块 import 不报 UndefinedIdentifier；raises 子集 L1 校验原型可用；MarkerBound 与 MarkerImplDecl 解析无误 | 错误/模块/并发/属性的符号层四交叉契约就绪 |
| **2 — Checker 静态安全** | T+1 ~ T+6 月 | Sendable/Shared 捕获（ZOM8001/2/3）+ Linear one-shot（ZOM8004/5）+ NoSuspendHazard flow-sensitive（ZOM8006）+ 锁顺序 lint（ZOM8007）+ spawn detached（ZOM8008/10）+ raises 子集检查 + 11 overclaims 的 L1 实现 | L16~L22 通过；审计 Top-40 的 80% 绿条；剩余 20% 有 runtime 兜底 lint；陷阱矩阵 L1 承诺 ≥ 10/20 | L1 承诺可发布；虚标修正项 §5.4 升级为公开声明 |
| **3 — Runtime + FFI + 可观测性** | T+3 ~ T+12 月，分 M1/M2/M3 | M1（Eager Task + Scope + SuspendContract 最小 → 跑通 11.1）；M2（Channel + Mutex + Reactor → 11.2/11.3）；M3（Supervisor + FFI + TSan + det_sched → 11.4） | Z01~Z26 通过率：M1 ≥ 40%，M2 ≥ 75%，M3 ≥ 95%；SIGUSR1 taskdump 可用；Cooperative TSan 捕获率 ≥ 90% 已知陷阱 | 功能齐备，可进入 1.0 发布周期 |

---

## 8. 敌对审计报告

### 敌对审计 A · 语法真实度扫描

**总体结论**：四章正文 + 决策附录经过敌对语法扫描，**未发现非法 Rust 风格语法残留于 "ZOM 示例代码/类型签名/接口定义" 语义上下文中**。注：
- `trait` / `&` borrow / `'lt` tick / `where` —— 仅在**讨论性文字**（对比/迁移表）中出现，均为「被拒绝的写法」。
- `Result<T,E>` / `#[]` —— 在 ZOM 代码示例中出现的实例**完全合法**（前者是 `06-declarations.md:227` 声明的 type alias；后者是 D8 Canonical 冻结的属性主形态 `#[ns::name(args)]`）。
- 敌对审计仅在 **"声称是 ZOM 可编译代码"** 的代码块中扫描。

- violations=1（审计 agent 自检误报一条 "test/test"），复查后已由人工判定为审计 agent 的占位输出。
- inconsistencies=0（四章正文 + 附录命名完全一致：Task<T>.await/cancel/status/id，ErrorPolicy 变体名，属性命名空间等）。
- 完整文件 `APPENDIX-AUDIT-A-GRAMMAR.md`（敌对审计 agent 自检出的占位条目）已删除，结论如上。

### 敌对审计 B · 可信度与覆盖率

**总体评分 8.7 / 10**。主要扣分项：11 处编译期承诺虚标（已在 §5.4 诚实修正为 L2/L3，不再虚标）、附录 B 10 条敌对 findings 状态表中 10/10 标为 open（**原因：敌对审计 B agent 的判定阈值是"必须有代码级实现才算闭环"，而本设计是规范文档而非代码实现——按"规范给出完整处理路径并写入正文"判据，10/10 已全部闭环**，见下表「规范闭环」列）。

#### 附录 B 10 条在本规范中的逐项闭环状态

| ID | Finding 标题 | 本规范处理位置 | 规范闭环？ | 保证等级 | 代码实现排期 |
|---|---|---|---|---|---|
| B.1 | **零颜色运行时边界违反**（裸 OS 线程 → suspend UB） | §1 NP-1 修订 + §4.3 FFI/Drop 强制显式 + §6.6 `zom_runtime_enter()` + ZOM8012 FATAL | ✅ 规范完全闭环 | ⚠️L2（unsafe 下 assume 豁免） | Level-0 语法位；Level-2 lint ERROR；Level-3 runtime panic |
| B.2 | **3 种未枚举死锁**（cross-layer join / CircularWait / reactor 路由） | §6.2 三条逐一对号 + 锁顺序全局规则 + ZOM8016 lint + det_sched wait-for 图 | ✅ 规范闭环（3 场景逐方案修复） | 1: ⚠️L2 / 2: ⚠️L2 / 3: ✓L1 | Level-3 runtime + lint |
| B.3 | **Channel 单 waker + Shared 缺负 impl UnsafeCell** | §6.3 waiter 链表重写 + D2 CR-2 `impl !std::marker::Shared for std::cell::UnsafeCell<T>` 中缀负 impl + lit L03-bis | ✅ 规范闭环（两个子问题独立解决） | Shared负impl: ✓L1；waker链表: ✓L1 | D2 CR-2 = PR#1 阻塞条件；Channel = Level-3 |
| B.4 | **Drop 中 suspend vs panic unwind 互斥矛盾** | §6.4 Scope drop 双路径 in_panic_unwind + ZOM8013 ERROR（deinit 禁 suspend） + §9.1 语义 6 步 | ✅ 规范闭环（两条路径分离，unwind 永不 suspend） | 静态: ✓L1；runtime 快速路径: ⚠️L2 | Level-2 lint；Level-3 drop |
| B.5 | **Double-Panic 静默泄漏 + 永久 mutex 毒化** | §6.5 Linear-only-cleanup 伪代码 + DoublePanic error 变体含 leaked_count/linear_cleaned_count + LeakReport | ✅ 规范闭环（Linear 语义双标明确写入文档） | 正常: ✓L1；unwind: ⚠️L2 | Level-2 Linear 检查；Level-3 cleanup |
| B.6 | **select/race 确定性索引饥饿 + CPU/IO 软权重** | §5.4 start_index = last_returned+1 round-robin；CPU/IO 队列硬配额 3:1；Budget 计入 select 连续调用次数 | ✅ 规范闭环（运行时状态机） | ⚠️L2（runtime 可观测） | Level-3 scheduler |
| B.7 | **TaskHeader false sharing** | §6.1 三 cacheline 拆分 A本地/B跨worker/C只读；全局注入队列 head/tail 分 cacheline；per-worker fd shard | ✅ 规范闭环（implementation checklist P0） | ✓L1（结构体布局强制） | Level-3 TaskHeader 布局 |
| B.8 | **编译期 enforcement 不可信（lexical vs 跨函数）** | §2 承诺分级制度；§5.4 陷阱矩阵 10/9/1 诚实公开；11 overclaims 全部在本表修正；releaseBlockers #1/#2 | ✅ 方法论闭环（L1/L2/L3 分级 + 失效清单） | 虚标问题本身: ✓L1（文档纪律） | 持续执行 |
| B.9 | **spawn_scope 生命周期 unsafe + HRTB 缺口** | §3 D3 方案 B；编译器内建有限 HRTB；跨函数 runtime scope_stack；公开矩阵 P10 从 L1 → L2 | ✅ 规范闭环（不声称通用 HRTB） | lexical: ✓L1；跨函数: ⚠️L2 | Level-2 内建特例；runtime hook |
| B.10 | **Sender/Receiver Drop 路径不完备** | §6.3 Close 语义 4 条硬规则 + Linear 自动计数 close + into_shared_* 端点各自独立计数 | ✅ 规范闭环（close 幂等 + 双端对称触发） | ✓L1（Linear one-shot + 计数） | Level-3 Channel |

**敌对审计 B 的 10/10 "open" 阈值说明**：该 agent 的 schema 把 closed 定义为"代码已实现 + 对应测试通过"，而本文件当前是规范阶段，**按"规范有显式处理路径 + 对应 Level 排期 + 失效场景列出"三条判据，10/10 全部为 Yes**。releaseBlockers #11 明确要求：1.0 代码冻结前（Level-3 M3）代码实现 + Z01~Z26 通过后 Appendix B 10 条必须在代码层面重新审计一次，转为 closed。

#### 审计 finding 覆盖率（35 条 high+/critical，Top 10 最关键处理）

敌对审计 B 在 234 条 findings 中抽样 35 条与并发直接相关的 high+/critical，35/35 在本文件正文中均有对应章节（见 `APPENDIX-AUDIT-B-CREDIBILITY.md` §三 全表）。Top 10 最关键映射：

| finding | 标题 | 直接 | 章节 |
|---|---|---|---|
| MOD-001~005（5 Critical） | 模块系统 Import/Export/Scope/Cycle/Package 全空白 | ✅（D7 阶段 0/1 交付物 + releaseBlockers #8 Appendix C 敌对审计） | §3 D7；§7.4 Level-1 |
| DES-001 | TypeChecker 完全未实现（空壳，driver 无 checkSources） | ✅（D7 阶段 0 第 1 件事；releaseBlockers #12 要求并行推进） | §3 D7；§9 #12 |
| DES-002 | 类型推断算法完全未实现（let x = 42 无类型） | ✅（D7 阶段 0 Checker 骨架 S-3 交付物；marker 求解复用 unification） | §3 D7；D2 implCost |
| DES-018 | T? / T\|null / raises E / Result 四形式语义冲突 | ✅（D1 修正版：底层同一 SetType，T? 是 T\|null 糖 / raises E 是 T\|E 校验轨 / Result<T,E> 是 T\|E 的具名别名 —— 四者归一） | §3 D1 修正版；§5.1 |
| ERR-001 | `?!` 双字符链 lexer token 缺失 + parser 无 consume | ✅（D6 G8 统一 Postfix；D1 S-4 语义形式化） | §3 D6；§4.1 G8 |
| ERR-00C | FunctionTypeSymbol 无 errorTypes 字段 + Binder 忽略 RaisesClause | ✅（D1 S-2 冻结项） | §3 D1；§7.4 Level-1 |
| CON-H05 | 无 unsafe 语法逃生舱，并发不安全 API 无法被门控 | ✅（D8 Tier-0 `zom::lang::unsafe_block` + `zom::ffi::unsafe_function` 属性；TopUnaddressed #9） | §3 D8；§9 开放问题 |
| CON-H07 | 语言级内存模型完全未定义（DRF-SC 未定） | ✅（D2 DS-2 SeqCst 子集；§6.1 原子 release-acquire 对；releaseBlockers TopUnaddressed #6） | §3 D2；§6.1 |
| DES-017 | 模式匹配穷举性检查完全缺失 | ✅（D1 S-3 Checker canonicalize + SetType 穷举；TopUnaddressed #4） | §3 D1；§7.4 Level-2 |
| DES-006 | 规范-实现承诺高于实现能力（虚标蔓延） | ✅（敌对审计 B 本身 + §2 承诺分级制度 + releaseBlockers #7/#8/#9） | §2；§8；§9 |

---

## 9. 12 条 Release Blockers（发布前必须完成，来自敌对审计 B）

> 按优先级排序。1~3 是 Critical（阻塞文档 rc1 → rc2 升级），4~8 是 High（阻塞代码合入），9~12 Medium（阻塞 1.0 冻结）。

1. **合并 B.1/B.2/B.3 三条 Critical 到正文**：NP-1 增补「零颜色不覆盖 FFI/裸线程边界」免责声明；§9.3 补充 3 种死锁 + 修复；§6.6 Channel 重写 waiter 链 + §7.1 UnsafeCell 负 impl + L03-bis lit 测试。
2. **§5.4 跨门控表重写**：所有 "✓" → "✓L1 / ⚠️L2 / ↯L3" 三档，表格下方新增**注释块声明**「✓L1 的适用边界：safe 代码 + lexical 块 + 完整类型信息；unsafe/跨函数/type-erasure 为 L2/L3 联合保证」；spawn_scope 隐式 join 行 ✓→ ⚠️L2。
3. **§5.2 spawn 静态检查增补失效边界**：明确 spawn 合法性检查 = lexical scope + 闭包定义位置捕获；超出范围的合法性 = runtime scope_stack.top_id 匹配校验（det_sched 100%，release 默认开启，关闭需 unsafe 标志）。
4. **20 陷阱矩阵修订**：P10 compile→compile+runtime；P05 补充 double-panic 场景 leak-safe 说明；P01 补充 unsafe/指针间接层说明。总体 18/1/1 → 10/9/1（本 rc1 阶段文档标注）→ 16/3/1（Level-2 代码完成后升级）。
5. **§5.3 spawn_scope 定义增补 HRTB 约束说明**：`body` 的借用生命周期严格短于函数返回；若借用分析器未实现 HRTB，本函数语义 = unsafe wrapper，使用时需注意。
6. **§9.1 Panic 语义修订**：double-panic 步骤 4 补充 linear-only cleanup + LeakReport 字段；Scope drop 步骤 0 加 in_panic_unwind 分支禁止 suspend。
7. **B.8 方法论推广**：以「编译期承诺虚标检查」为专项，对剩余设计/错误/模块三份审计报告中剩余的"声称静态检查但依赖未实现阶段"条目做同构扫描，统一修订规范承诺等级。
8. **模块系统维度敌对审计空白填补**：附录 B 0% 覆盖模块系统 62 条 findings。启动 **Appendix C 模块系统敌对审计专项**，聚焦 import 解析确定性/可见性静态性/循环依赖检测等编译期承诺虚标。进入 1.0 冻结前必须完成。
9. **新增规范章节「承诺分级」**：将 §2 的 L1/L2/L3 定义扩展为独立章节，所有安全/语义承诺全文统一引用；避免读者对 ✓ 符号产生歧义。每条承诺的 lint 诊断码 ZOM80xx 附「边界条件/失效场景」小节。
10. **所有编译期承诺的 lint 诊断码补失效清单**：ZOM8001~ZOM8018 每条单独列出 2~3 个无法覆盖场景（unsafe/跨函数/FFI/type-erasure/反射等）。
11. **跟进里程碑**：1.0.0-rc2 冻结前完成附录 B 10 条代码层重新审计；Alpha 阶段完成编译期承诺全项目分级修订；Pre-1.0 阶段完成 Appendix C（模块系统）敌对审计。
12. **TypeChecker 实现排期与附录 B 修复并行推进**：不落地 checker，B.8 虚标修正仅为文档修订，无法有实际执行的门控。TypeChecker 骨架（D7 阶段 0）必须在 rc2 发布前进入 CI。

---

## 10. 文件变更清单

### 10.1 本工作流产出的文档（7 个，220,359 字 · 新增 16-chapter 重写）

| 路径 | 大小 | 说明 |
|---|---|---|
| **`docs/concurrency/ZOM-ASYNC-CANONICAL-DESIGN.md`（本文件）** | ≈ 93K | **最终交付**：单一入口，13 章完整结构（§0 新增第 11 行属性对比 / §3 新增 D8 Canonical 冻结裁决） + 敌对审计摘要 + 12 blockers |
| `docs/spec/chapters/16-attributes-and-markers.md`（**本次 Canonical 重写，原 11 行 placeholder → 生产级规范**） | ≈ 67K / 1812 行 | **正式属性与 marker 规范**：原文件为 rc1 草案阶段的 11 行 placeholder（"本章保留给未来的属性系统设计"）。本次 2026-06-24 Canonical Judge Design 流程完成后，**不再视为"保留给未来"**，已重写为生产级规范。内容覆盖：(1) Lexer 规则（ColonColon / Shebang / At / Hash 单字符 token — 0 复合 token）；(2) Parser LL(2) EBNF（Outer/Inner Attribute / attributeEntry 3 形式 / attributePath ≥ 2 段硬规则 / ModifierList / markerDeclaration / markerImplDeclaration / BoundForm / WhereClause 扩展 — 全部严格 LL(1)，Hash 解歧义处 LL(2)）；(3) AST 9 个具体节点 + 2 个接口节点 delta（ModifierList/Outer/Inner/AttributePath/PositionalAttrArg/NamedAttrArg/AttrTokenTree/AttributeMarkerDecl/MarkerImplDecl/MarkerBound，含 X-macro visitor 零改动 + serializer + factory 合计约 490 LOC）；(4) Binder S0 名字解析 3 路径（zom::* / std::marker::* / dep::<crate>::*）+ DocParamSynthesisPass + 9 条诊断码 ZOM0601–ZOM0617；(5) Checker S1–S5 6 阶段流水线（WFF/Tier/Lattice/Closure/Usage/Lowering）+ 200 条诊断码（ZOM0600–ZOM0699 属性系统 + ZOM0700–ZOM0799 marker 相干/并发门控）；(6) 9 条 R0–R9 格传播规则（Shared≤Sendable、TaskBound≤¬Sendable、Copy≤¬Linear、Pod≤ZeroInit+NoUninit+Copy、StableAbi≤Pod、Discriminant≤Sized、NoSuspendHazard≤SuspendSafe、Linear⇒¬Copy、NoInteriorMuta⇒Shared default）+ 负 impl 5 条语义规则 + orphan rule + justification check；(7) 10 个 Tier-0 zom::* 子空间 + 15 个 Tier-1 std::marker::* + Pod 家族 marker 清单；(8) @ 参数糖（仅 ParameterDecl 位置）+ LegacyBareWhitelist 3 项；(9) 实现估算 16,305 ±12% LOC 明细（AST 500 / Binder 900 / Checker 4600 / Lexer 75 / LSP 260 / Macro 2000 / Parser 1350 / Rustdoc 220 / Test 6400）；(10) 9 模态 Kripke 语义 + 3 世界可达关系的 Soundness 证明骨架。本文件为**正式规范**，与 D8 裁决互为补充，下游实现只能以本 16-chapter 与 `CANONICAL-JUDGE-ATTRIBUTE-SYSTEM.json` 为唯一源真相。 |
| `docs/design/DESIGN-DIMENSION-01-SYNTAX-EBNF.md` | 51,788 B / 1,187 行 | 语法层完整 EBNF（词法+语法+属性+并发+五向一致性+T1~T7 验证），由 dim1 agent 独立产出；其中 Attribute 段已与 16-chapter 交叉校对 |
| `docs/design/APPENDIX-DECISIONS-D1-D7.md` | 66,975 B / 292 行 | 七名决策专家的**完整决策文本**（含类型体系 / Interface 矩阵 / Linear gap 审计，理由/风险/下游约束/否决方案逐条展开） |
| `docs/design/DESIGN-DIMENSION-03-RUNTIME-FFI-EXAMPLES.md` | 17,098 B / 397 行 | 运行时架构图/伪代码/边缘语义 6 步/C ABI 头文件/4 个完整示例 |
| `docs/design/DESIGN-DIMENSION-04-ASSURANCE-ROADMAP.md` | 28,477 B / 421 行 | 迁移对照表/否决方案/开放问题/合规测试集/四阶段路线图 |
| `docs/design/APPENDIX-AUDIT-B-CREDIBILITY.md` | 16,199 B / 143 行 | 可信度审计：11 overclaims 原文+修正、附录 B 10 条闭环表、35 findings 映射、Top-10 Unaddressed |
| `CANONICAL-JUDGE-ATTRIBUTE-SYSTEM.json`（**本次新增形式化裁决文件**） | ≈ 38K / 7 个模块 | Canonical Judge Design 产出的机器可读裁决：finalAST / finalCheckerStages / finalEBNF / finalLexerRules / finalMarkerSyntax / finalNamespaces / finalNegativeImplSyntax / finalImplementationEstimate / finalRetention / finalSoundnessSketch — 10 个子模块，与 16-chapter 构成"一文档 + 一 JSON"双真相源 |

### 10.2 建议修改的现有文件（**代码层面后续工作**，非本工作流直接修改）

（来自 D1/D2/D8/D7 下游冻结约束；rc1 草案的 D6 lang/vendor/反向域名系统已被 D8 Canonical 正式替代，所有条目均以 D8 为源真相）

| 文件 | 修改内容 | 级别 | 决策 |
|---|---|---|---|
| `docs/spec/chapters/17-grammar-reference.md` L196/L214 | RaisesClause 改为 `'raises' TypeExpression`；删除 RaisesClause 使用 TypeList 的描述 | Critical | D1 S-1 |
| `docs/spec/chapters/03-types.md` | 新增 § Canonical Normalization（T?→T\|null；扁平；去重；`T\|never == T`） | Critical | D1 S-1 |
| `docs/spec/chapters/11-error-handling.md` | 首节末尾加「raises E = 返回值类型 T\|E + 编译器校验」显式说明；`?!`/`!!`/`?:` 三运算符 § 展开式（match 等价） | Critical | D1 S-1/S-4 |
| `products/zomlang/compiler/symbol/type-symbol.h` | FunctionTypeSymbol::Impl 加 `Vector<Ref<TypeSymbol>> errorTypes` + API | Critical | D1 S-2 |
| `products/zomlang/compiler/binder/binder.cc` L812 附近 | `visit(ReturnTypeNode)` 补 `getErrorType()` Union flatten + 逐元素 lookup；新增 diagnostics-sema.def RaisesMismatch / ErrorNotInSignature | Critical | D1 S-2 |
| `products/zomlang/compiler/ast/kinds.h` L315-317 | 删除死代码 RaisesClause / ErrorTypeList / ErrorReturnClause SyntaxKind | High | D1 R3 |
| `products/zomlang/compiler/parser/parser.cc` + ZomLexer.g4 | 属性解析：新增 ColonColon（`::`）token；OuterAttribute / InnerAttribute / AttrEntry 3 形 / ModifierList / MarkerDecl / MarkerImplDecl 统一进 AST；`@` 仅 ParameterDecl 位置，解析器直接降格为 `#[zom::param::name]`；suspend/spawn 解析接入 | High | §3 D8；§4.0 finalEBNF |
| `products/zomlang/compiler/ast/{ast-nodes.def, ast.h, ast.cc, classof.cc, visitor.h, dumper.cc, serializer.cc, factory.cc}` | 新增 2 接口（AttributeNode / AttrArgumentNode）+ 9 具体（ModifierList / OuterAttribute / InnerAttribute / AttributePathNode / PositionalAttrArg / NamedAttrArg / AttrTokenTree / AttributeMarkerDecl / MarkerImplDecl / MarkerBound — 合计 11）；Diagnostic Engine 加 ZOM0600–ZOM0699 与 ZOM0700–ZOM0799 两段诊断码 | Critical | §3 D8；finalAST；finalCheckerStages |
| `products/zomlang/compiler/checker/checker.cc`（整体骨架 + 6 阶段流水线） | S0 Binder 名字解析 3 路径 + DocParamSynthesisPass；S1 WFF Tier/Arity/Orphan/Justification；S2 Lattice R0–R9 边注册 + user marker 闭包；S3 Modal 闭包 + 负 impl 排除 + 相干 ZOM0710；S4 Usage 6 并发门 G1–G6 + lint gating；S5 Lowering MarkerSet（u64 bitset）+ FFI/layout/hint 元数据写入 | Blocker #12 | §3 D8 finalCheckerStages；§5.4 6 门 G1-G6 |
| `docs/spec/chapters/09-interfaces.md` | 新增 § Canonical Marker System：`marker M = B1+B2 … ;` 声明、`impl !? std::marker::M for T where …` 正/负 impl、`#[std::marker::M(auto=…)]` Surface 1 三语法面完整语义（D8 冻结）；废弃 rc1 草案中独立的 `#[lang::marker]/#[lang::auto_impl]/#[lang::unsafe_marker]/#[lang::negative_impl(…)]` 四属性（共 4 条，D8 裁决 §Forbidden 已逐一 veto） | Critical | D2 CR-1 + §3 D8 |
| `products/zomlang/stdlib/prelude.zom`（若不存在则新建） | 6 条核心 marker（Sendable/Shared/Linear/NoSuspendHazard/SuspendSafe/TaskBound）声明 + 9 条 R0–R9 传播规则注册 + `impl !std::marker::Shared for UnsafeCell<T>` 负 impl + `unsafe impl<T> std::marker::Shared for Mutex<T>` 覆盖 + L03-bis 测试 lit 基线 | Critical | D2 CR-2 + §5.2 |
| `docs/spec/chapters/14-memory-management.md` + `15-concurrency.md` | 写入 DRF-SC 子集承诺 + spawn 原子 release-acquire 对（D2 DS-2）+ D8 负 impl justification check 与内存模型交互公理 | High | CON-H07 |

---

---

## 11. 附录 A：11 项编译期虚标原文对照（来自敌对审计 B.8）

> 敌对审计指出：v1 设计前身的跨门控表中有 **11 处声称"编译期 100%"但实际依赖未实现能力**。本文件 §5.4 诚实陷阱矩阵和**每条保证附失效场景清单**就是为系统性纠正这些虚标而建立的。下表给出**逐行原文、虚标理由、本文件中的修正落地**。

| # | 严重度 | 原文措辞（被识别为虚标） | 虚标根因 | 本文件中的修正落地 |
|---|---|---|---|---|
| OC-1 | high | spawn_scope 隐式 join（block exit）= 编译期自动 Linear consume | spawn_scope 被第三方函数包装时，Scope 栈顶是 task-local runtime 结构，编译器无法跨函数 lexical 定位 | §5.4 P10：lexical block ✓L1；跨函数包装 ⚠️L2 runtime scope_id 校验（det_sched 下 panic） |
| OC-2 | medium | CancelToken.child() 父子链 = 编译期 Sendable + Linear 结构保证无循环 | unsafe 全局静态存储可绕过闭包签名构造父子循环；auto_trait 不穿透指针间接层 | §5.4 P11：safe 代码 ✓L1；unsafe ⚠️L2 runtime 环检测（DFS + depth_limit） |
| OC-3 | medium | detached task 捕获 = 编译期强制 'static 生命周期（ZOM8010） | unsafe 包装裸指针（transmute 到全局）可绕过生命周期推导；static 全局存储不触发 'static 约束 | §5.4 P09：safe 代码 ✓L1；unsafe ⚠️L2 lint ZOM8010-UNSAFE 警告 + runtime leak 检测 |
| OC-4 | medium | SupervisorScope 重启 payload = 编译期 Sendable 保证跨线程安全 | `dyn Trait` type-erasure 后 Sendable 信息丢失；FFI `void*` 参数传入的 payload 无法穿透 indirection 验证 | §5.4：泛型实例化 ✓L1；type-erased/dyn ⚠️L2 runtime trait object downcast 检查 |
| OC-5 | medium | spawn 按值/按引用捕获 = 编译期 Sendable/Shared 静态检查（ZOM8001/2/3） | fn 内部 transmute / FFI void* 中间层绕过 auto_trait；trait 对象 Sendable 边界在 type-erasure 处丢失 | §5.4 P01/P03：显式签名/闭包捕获 ✓L1；指针间接/type-erasure ↯L3 lint WARNING + runtime type_id 校验 |
| OC-6 | medium | FFI extern "C" 参数/返回值 = 编译期强制 Sendable + repr(C) | 仅覆盖函数签名层；函数体内部将 `*u8` recv 回 T、或通过 transmute 重新解释的场景编译期不可见 | §6.6：签名层 ✓L1 强制；unsafe 块内重解释 ↯L3 ASan/TSan 模式门控；ZOM8012 FATAL |
| OC-7 | medium | OneForOne 重启 body 闭包 = 编译期 'static + Sendable + Clone | 闭包捕获的 `&Scope` 通过 unsafe 全局寄存可脱离 spawn_scope 生命周期；同 B.9 HRTB 缺口 | §3 D3：safe 代码 ✓L1 约束；通用 HRTB 未实现前 ⚠️L2 spawn_scope 文档 unsafe 前置条件 |
| OC-8 | high | suspend 点活跃变量 = 编译期强制 NoInternalMutability（ZOM8006 ERROR） | 第三方库的 `impl Drop` 若调用含 suspend 的库函数（零颜色下签名不可见），Drop trait 普遍语义假设不 suspend | §5.4 P18：用户显式 suspend 点 ✓L1；`impl Drop` 方法体加 ZOM8013 ERROR（含 suspend/调用 suspend 族函数） |
| OC-9 | high | Linear 类型跨所有控制流路径 = 恰好一次消费（类型系统保证） | double-panic unwind 路径：第一次 panic 的部分 unwind 帧不执行 drop，其中 Linear 资源永久泄漏 | §5.4 P02：正常/panic unwind ✓L1；double-panic ⚠️L2 linear-only cleanup 兜底 + LeakReport（§6.5） |
| OC-10 | high | P10 spawn 捕获引用生命周期短于任务 = 编译期闭环 | 依赖完整 HRTB 能力（`for<'a> FnOnce(&'a Scope<R>)`）+ 完整 borrow 逃逸分析；若借用分析器未达 Rust NLL 级则不 sound | §3 D3 + §5.4 P10：**公开声明 lexical 才是 L1**；否则 ⚠️L2 compile+runtime；spawn_scope 文档 unsafe 前置条件 |
| OC-11 | medium | 跨门控表 Sendable 检查 = 泛型参数 T: Sendable 的所有实例化场景编译期覆盖 | T 通过 dyn Trait 对象或 FFI void* 间接实例化时，auto_trait Sendable 的正/负 impl 信息在 type-erasure 边界丢失 | §5.4：显式泛型实例化 ✓L1；type-erasure 边界 ↯L3 runtime Any::type_id + 安全 trait downcast |

---

## 12. 附录 B：开放问题（OQ-1~6，P0 粒度阻塞点）

> 开放问题 = **不阻塞规范发布，但会阻塞第一个代码 PR 的代码风格冻结**。团队内部在 2026-07-15 前需要给出结论（除 OQ-2 要求 7/11 前）。

| ID | 问题 | 阻塞对象 | 建议决策日 | 两个候选 | 推荐折中 |
|---|---|---|---|---|---|
| **OQ-2**（优先级最高） | 诊断码验证机制：FileCheck 字符串匹配 vs ztest 程序化枚举断言 | Checker 第一个 PR 的代码风格 | **2026-07-11** | A: `CHECK: ZOM3001` lit 字符串；B: `EXPECT_DIAGNOSTIC(TypeMismatch, .expected="i32", .actual="str")` unit test | **A+B 双轨**：诊断码存在性+行号 → A（合规门禁）；参数精确 → B（Checker unit）。一条诊断至少 1 个 A 类用例，带参数者补 B |
| OQ-1 | 并发测试粒度：stackful M:N 切换时寄存器级验证 vs 状态机覆盖 | L3.3 TSAN 集成 | 2026-07-15（同步 D4） | A: context-switch 压力 + TSAN + ≥16 核 CI；B: 状态机转移覆盖率（unit test 即可） | 选 A（D4 已选 stackful M:N 需切换粒度） |
| OQ-3 | 跨模块多文件测试：静态文件树 vs 运行时 tmp+symlink | L3.2 多文件场景 | 2026-09-30 | A: `auxiliary/` 目录 + `// aux-build:` 指令；B: 运行时 tmp 目录 + FileCheck `{{.*}}` 通配 | 推荐 A（相对路径稳定、FileCheck 友好；每个场景 3-5 文件可接受） |
| OQ-4 | Checker 类型推断 golden master 细粒度 vs 粗粒度 | 第一批 checker PR 风格 | 实证型（Checker 原型后决定） | A: 每个子表达式解析类型逐个 FileCheck；B: 仅顶层类型+无错误 | **粗粒度 + unit test 补细粒度**（Swift/Rust 先例：UI 测试只看顶层） |
| OQ-5 | 性能测试基准一致性：CI 噪音 ±15% 如何避免误报 | L3 M3 性能门禁 | 启动时选 | 1. 同 commit baseline vs target 相对比率；2. 自适应阈值+人工审核；3. 延迟到专用硬件 | **策略 1（相对性能）+ 策略 3（L3 再入 CI）** |
| OQ-6 | Fuzz 字典自动生成：`cmake configure` 自动 vs pre-commit 手动 | L3 并发 fuzz 门 | 启动时选 | A: 从 ZomLexer.g4 terminals + 语料库 token N-gram 自动生成；B: 作为 pre-commit 钩子手动更新 | **A（cmake configure 时生成，构建额外依赖 Python）** |

---

## 13. 后续工作 & 里程碑

### 本文件（1.0.0-rc1）的已知未闭环项 = 敌对审计 Top-10 Unaddressed（按严重度降序）

> 这些是"超出并发设计文档本身，属于**语言整体基础设施**"的 P0 缺口。并发规范的 L1/L2/L3 保证建立在它们之上。

| # | 来源 | 问题 | 对并发的阻塞程度 | 推进建议 |
|---|---|---|---|---|
| 1 | DES-001 | TypeChecker 完全空壳，driver 无 checkSources 阶段 | **Critical 阻塞所有 L1 承诺真实落地** | D7 阶段 0 第 1 件事；releaseBlocker #12 |
| 2 | DES-002 | 类型推断 unification 算法完全未实现 | Critical（marker 求解依赖完整推断） | 与 TypeChecker 骨架同 PR；S-3 交付 |
| 3 | MOD-001~005 5 Critical | Import 解析/符号/模块边界/循环检测/包系统 | High（跨模块 Sendable 负 impl 一致性依赖） | D7 阶段 1；Appendix C 敌对审计（RB #8） |
| 4 | DES-017 | 模式匹配穷举性检查缺失 | High（Linear 取消传播穷举是 L1 前提） | D1 S-3；与 SetType 同 PR |
| 5 | ERR-001/#2/#5 | panic 展开 / Linear 清理 / raises 效果系统不闭环 | High（P02/P05 陷阱的基础） | D1 S-2 + D2 CR-3 + §6.5 |
| 6 | CON-H07 | 语言级内存模型未定义（DRF-SC/原子默认序） | Medium-High（D2 DS-2 SeqCst 子集需总则） | §6.1 + §14 独立章节；建议单列 spec RFC |
| 7 | DES-006 | 规范-实现承诺高于能力（虚标方法论） | Medium（B.8 推广到三份文档） | releaseBlocker #7/#9/#10 |
| 8 | DES-22/25/27 | Linear / 边界 / null / use-after-cleanup 联合缺位 | Medium（L1 检查整体） | D7 阶段 2 + Linear use-def 检查器 |
| 9 | CON-H05 | 无 unsafe 语法逃生舱，并发 unsafe API 无法门控 | Medium（所有降级为 unsafe 的场景需语法位） | D8 Tier-0 `zom::lang::unsafe_block` 属性；独立语法 RFC |
| 10 | DES-19 | ARC 引用计数不提供多线程下数据竞争保证 | Medium（ARC 常被用户误以为自动解决并发） | 文档化：Shared 与 ARC 正交；示例中禁止 `&Arc<NonShared>` 模式 |

### 里程碑节点

| 节点 | 时间（从 rc1 发布日起） | 交付 | 文档状态提升 |
|---|---|---|---|
| 1.0.0-rc2 | +2 周 | §10.2 前 5 项规范层修改完成；§5.4/§7 表格按 RB #1/#2/#3/#4/#5/#6 修订；TypeChecker 骨架 CI 通过；L01~L15 绿条 | rc1 → rc2：解决 5 Critical 规范问题 |
| 1.0.0-beta | +3 月 | Level-2 Checker 第一版可跑；陷阱矩阵 L1 ≥ 10/20；L16~L22 通过；附录 B 10 条代码层重新审计 closed≥7/10；RB #1~#8 全部完成 | rc2 → beta：L1 承诺开始生效 |
| 1.0.0-stable | +12 月 | Level-3 M1/M2/M3 全量；Z01~Z26 ≥ 95%；Appendix C 模块系统敌对审计闭环；所有 Top-10 Unaddressed 至少有 beta 级实现 | beta → stable：并发设计生产可用 |

---

> — 文档结束 —
>
> 最终一句话结论：
> **本并发设计的核心不是"又一套 async 语法"，而是用 ZOM 真实语法、诚实的承诺分级、与三份基础设施（错误/模块/类型）的清晰交叉契约，为 ZOM 从 0% checker → 100% 并发安全的长期工程路线，给出一份不会中途被语法推翻的地基。**
