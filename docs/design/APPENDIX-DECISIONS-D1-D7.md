# 七项基石决策摘要（Phase 2 Output）

## 1. D1 错误通道选型

**推荐方案**：B 方案 —— T|E 联合类型的语法糖/校验轨（raises E 归一并内化为 T|E），用户 Result enum 作为非编译器特化的"带标签离散通道"

**语法契合度**：文法与实现天然偏向：ANTLR ZomParser.g4:794 写的是 `raisesClause: RAISES type`（单个 type，天然承载 `A|B` 联合）；Parser `parseRaisesClause()` 返回裸 `TypeNode*` 并由 `parseRequiredReturnType()` 写入 `ReturnTypeNode.errorType`（/products/zomlang/compiler/parser/parser.cc:565-577, 4323-4342）；AST 层无专用 RaisesClause/ErrorTypeList 节点。规范正文 11-error-handling.md 的所有示例（L65、L134）使用 `raises FileNotFoundError | ParseError`（竖线联合）。这三者（ANTLR 参考文法 + Parser 实现 + 规范示例）构成 3/4 多数对「raises 是联合类型上的注解/校验轨」的承诺，走 A 或 C 都要回头改写文法与 AST。

**审计闭环**：直接关闭三份审计的核心 P0 缺口，不引入额外抽象层级：
(1) 设计审计 Finding 18（四形式未归一，docs/reports/zom-design-audit-2026-06-23.md:1320-1395）提出的「T?/T|null/raises E/Result 四者语义冲突、子类型组合爆炸」问题 —— B 方案在 Checker 入口做一次 canonicalize：T? → T|null，嵌套联合 flatten，去重排序，得到归一化集合 SetType；raises E 只影响 FunctionTypeSymbol 的 errorTypes 集合（等价于把返回值联合中的 error-tag 元素提出来作为校验目标），不引入新的运行时值表示；用户自 Result enum 保持 nominal，不参与自动归一，边界清晰。
(2) 错误审计 缺口 C（/docs/reports/zom-error-system-audit-2026-06-23.md:259-281）—— `FunctionTypeSymbol` 只需补 `Vector<TypeSymbol*> errorTypes` 字段，Binder 在 `visit(ReturnTypeNode)`（现状 L812 只 `returnType.getType().accept()`）增加对 `getErrorType()` 的 Union flatten + 逐元素 lookup，即可把 raises 从 AST 注释推进为符号层一等公民。
(3) 错误审计 ETM 系列提出的 `?!`/`!!`/`?:` 适用域（L114、L130）—— 因为 B 方案下 raises 的值就是 T|E 联合，三运算符直接作用于「含 error 标签元素的联合」，无需再区分「只对 raises 生效 / 对任意联合生效」。
(4) 规范矛盾（17-grammar-reference.md:196 写 `RaisesClause ::= 'raises' TypeList` 逗号列表 vs 其他三处都是 `|` 联合）—— B 方案直接裁定「`|` 联合是唯一规范表示」，把 17-chapter 的 TypeList 改为 Type，一处修正即解耦。

**用户意图对齐**：对齐规范已显式声明的立场与行业 2020+ 共识分层：
- 规范 11-error-handling.md:3 写「ZOM has no implicit error control flow」、L99 写自造 Result「not treated as error types by the compiler」—— B 方案把 raises 定义成「编译器对返回值联合做 error 子集校验 + 语法糖入口」，不是异常机制也不是新的运行时通道，完全匹配。
- 设计审计反对方也论证（L1360-1390）「四形式并存是标准分层（语法糖 / 显式联合 / 编译器校验轨 / 用户自由降级），Swift/Rust 全部采用」—— B 方案把这四层显式落地为单向脱糖链：用户代码(T? + raises E) → AST(OptionalTypeNode + ReturnTypeNode.errorType) → 符号层(SetType = {T}∪{null}∪errorTypes，T?=语法糖、raises=校验标签)。
- 2020+ 语言（Swift throws/any Error、Zig !T、Kotlin Result + null、TS `T | E`）的共识是「值语义 + 编译期标注 + 可选的强制解包」，B 方案不落 Java checked exception（栈异常 + 声明式）陷阱，也不强制 Rust 式 Result 泛型嵌套。

**实现代价**：三条路径的实现成本对比（以「从零到能过 checker 第一批 20 个语义用例」为度量）：
- A（raises 单通道，Result 等同取消）：成本 2.0x。需：改写 06-declarations.md 中两处 Result 示例（L227 alias 与 L368 enum）并 deprecate；新增 error 类型 nominal 标签体系；?!/!! 必须只对 raises 生效，与任意联合 T|E 不互通 → 需要额外的子类型分支。
- B（raises 是 T|E 的语法糖/校验轨，双轨但值层合一）：成本 1.0x。只需：(a) FunctionTypeSymbol 加 errorTypes 字段；(b) Binder visit(ReturnTypeNode) 补 errorType 分支 + Union flatten；(c) Checker 第一个 pass 做 canonicalize（T?→T|null，联合归一，去重，error 集合提取）；(d) 定义 ?! 展开式（规范文字，不增加 IR 节点）。这四项正是设计审计 Finding 18 + 错误审计 缺口 C/缺口 H 已给出的 P0 修复路径。
- C（仅 union，raises 取消）：成本 1.8x。需：移除 lexer/parser 的 RaisesKeyword/parseRaisesClause/ReturnTypeNode.errorType 四套语法；所有示例与测试（raises.zom、11-error-handling.md、03-types.md:196 的 SafeParser）重写；?! 的 early-return 语义需要一个新的非 raises 声明来承载（否则调用方无法知道函数会返回哪些 error）—— 实际上要引入另一套「函数元数据注解」，成本被低估。
关键节约：B 方案不需要新增任何 AST 节点种类（SyntaxKind 里已声明但未用的 RaisesClause/ErrorTypeList 可删除，审计 L199-214 已指出这是死代码），也不需要重写 parser（当前 parseRaisesClause 直接走 parseType→UnionTypeNode 就是正确的）。

**主要风险与缓解**：

- R1 — error 类型与普通值在 T|E 联合中的标签混淆。缓解：规范定义 error 声明的实例携带编译器注入的 nominal tag（类似 Swift 的 Error existential 的 _typeDiscriminator），在归一化阶段对「是否 error 元素」做确定性判定；?! 的 match 展开只对 error-tag 元素执行 early-return，非 error 元素走值通道。写入 checker 第一个子类型规则。
- R2 — 自造 Result enum 与 T|E 联合之间的互转缺失导致生态分裂。缓解：在标准库 prelude 中提供（不内建在编译器）：(a) `Result<T,E>.fromUnion(u: T|E): Result<T,E>` 与 `u.toUnion(): T|E`；(b) `trait Try<Output=T, Error=E>` 统一 `?!` 可作用的类型（可追加到 enum 上），保证库作者可以写 `enum MyResult { ... } impl Try for MyResult { ... }` 后自动接入 `?!`。这一点对齐 Rust Try trait，避免重复 Swift 5 之前 Result/throws 不可互转的历史债务。
- R3 — 规范文法矛盾（17-chapter TypeList 逗号列表 vs 其他三处 `|` 联合）未修会在用户测试中被放大。缓解：作为 D1 决策的直接动作，冻结 17-grammar-reference.md:196 的 `RaisesClause ::= 'raises' TypeExpression`，删除 TypeList 在 raises 语境下的使用；同时移除 kinds.h 中已声明但零引用的 `RaisesClause`/`ErrorTypeList` SyntaxKind（审计 L199 点名的死代码），一次性对齐。
- R4 — async fn 扩展时 `Future<T raises E>` 与 `T raises E` 双轨语义漂移。缓解：在 B 方案的语义中，「raises E」永远只是「返回值联合的 error 子集声明」，不承担额外异步语义；因此 `async fn f() -> T raises E` 天然等价于 `fn f() -> Future<T> raises E`（同步返回一个 Future，值域上 E 不并入 Future 的 Output，而是和同步 raises 一样走函数返回联合），await 只做 Future<T> → T 的值解包，不重新发明错误通道。写入并发规范章节（docs/concurrency/ 已有设计稿 v1）的 D1 对齐条款。

**下游绑定约束**：

下游决策/冻结顺序（D1 结论直接约束这些子项，不按顺序实现会触发返工）：\n\n1. **S-1 规范冻结（前置，~0.5 人日）**：修改文件 docs/spec/chapters/17-grammar-reference.md（L196、L214）与 docs/spec/chapters/03-types.md（在 Union Types 段落后新增 1 节「Canonical Normalization」：T? → T|null；T|(U|V) → T|U|V 扁平；去重；`never` 单位元 `T|never == T`），docs/spec/chapters/11-error-handling.md 在首节末尾加「raises E 语义等价于返回值类型为 T | E，外加编译器对 error 子集可枚举性与穷尽性的静态校验」一段显式说明。\n2. **S-2 AST 符号层落位（P0，~2 人日）**：products/zomlang/compiler/symbol/type-symbol.h 给 FunctionTypeSymbol::Impl 加 `zc::Vector<zc::Ref<TypeSymbol>> errorTypes` + `addErrorType/getErrorTypes` API；products/zomlang/compiler/binder/binder.cc 在 `visit(ReturnTypeNode)`（L812 附近）补 errorType 的 UnionTypeNode 递归 flatten + 逐 TypeReferenceNode 符号 lookup；新增 diagnostics-sema.def 条目 RaisesMismatch / ErrorNotInSignature。\n3. **S-3 Checker 第一版归一化管线（P0，~3 人日）**：在 checker/checker.cc 实现 `canonicalize(TypeNode*) -> SetType`（去重、扁平、T?→T|null、吸收元）；实现 `isSubtypeOf` 对 SetType 的子集判定；实现 match 穷尽性（对 SetType 各元素 nominal 覆盖检查）；接入 driver 管线的 checkSources() 调用。\n4. **S-4 运算符语义形式化（P1，~1 人日）**：docs/spec/chapters/04-expressions.md L254-266 把三个运算符的语义写成等价展开式 —— `e?!` = `match e { when E(x) raises return x; when v => v }`（要求 E 是外层 raises 声明子集），`e!!` = `match e { when E(_)/null => panic(); when v => v }`，`e?: d` = `match e { when E(_)/null => d; when v => v }`。\n5. **S-5 标准库 Try trait 与 Result 桥（P1，~1.5 人日）**：在 products/zomlang/stdlib/prelude.zom（若不存在则新建，审计 L1478 已指出 stdlib 目录 0 存在）定义 `trait Try<Output, Error>`（`fn intoUnion(self) -> Output | Error`；`fn fromUnion(u: Output | Error) -> Self`），并为内建 union（自动 impl）和 `enum Result<T,E>`（默认实现）提供桥。\n6. **S-6 C ABI 边界（P2，~3 人日，推迟到有 FFI 里程碑再定）**：错误审计 L532-580 提出的 extern \"C\" / errno 映射，B 方案下因值层是 T|E 扁平联合，可用「tagged union C struct + 两个返回寄存器」或「out 参数 + 线程局部 last_error」两种常规 ABI，避免自定义布局。\n\n关键文件绝对路径速查：\n- 规范层：/Users/bytedance/Develop/ZOM/docs/spec/chapters/03-types.md；/Users/bytedance/Develop/ZOM/docs/spec/chapters/04-expressions.md；/Users/bytedance/Develop/ZOM/docs/spec/chapters/11-error-handling.md；/Users/bytedance/Develop/ZOM/docs/spec/chapters/17-grammar-reference.md；/Users/bytedance/Develop/ZOM/docs/spec/ZomParser.g4\n- 实现层：/Users/bytedance/Develop/ZOM/products/zomlang/compiler/parser/parser.cc（L565-577 parseRaisesClause，L4323-4342 parseRequiredReturnType）；/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/type.h（L372-389 ReturnTypeNode）；/Users/bytedance/Develop/ZOM/products/zomlang/compiler/binder/binder.cc（L812 visit(ReturnTypeNode)，L398-415 TypeNode visitor）；/Users/bytedance/Develop/ZOM/products/zomlang/compiler/symbol/type-symbol.h（L197-226 FunctionTypeSymbol）；/Users/bytedance/Develop/ZOM/products/zomlang/compiler/ast/kinds.h（L315-317 RaisesClause/ErrorTypeList/ErrorReturnClause 死代码）\n- 证据/审计：/Users/bytedance/Develop/ZOM/docs/reports/zom-error-system-audit-2026-06-23.md（缺口 C L259-281、缺口 ETM-归一 L114+L786、async 对齐 L312-353、Result 桥 L1512-1544）；/Users/bytedance/Develop/ZOM/docs/reports/zom-design-audit-2026-06-23.md（Finding 18 四形式归一 L1320-1395）"}

**否决方案**：

- A 方案 —— raises(E) 单通道归一（取消/弱化 union 与 Result） — 与实现和规范样例双向冲突：Parser 把 raises 的 E 解析成普通 TypeNode（含 UnionTypeNode），若走「raises 是独立通道、其 E 不参与 T|E 联合」，需要在 IR 层重新发明一个 error 包装器；同时规范 03-types.md 的通用联合章节（L89-110）与 10-enumerations.md 的 Result 示例均已存在，撤回会形成 30+ 页级规范回滚；此外 A 方案把 error 类型与普通值强分，意味着 `when E(e) => return e; when v:T => v` 的 match 展开式（raises 语义的核心）在 A 下需要两种类型系统视图（error-world vs value-world），子类型算法复杂度与 C 接近。

- C 方案 —— 仅 union 联合类型（移除 raises 关键字） — 三点致命：(1) 丢失「编译器校验的错误传播边界」：没有 raises 就没有函数签名上可枚举的 error 集合，`?!` 子集检查（E_in ⊆ E_out）、match 穷尽检查、跨模块 API 兼容对比全部要靠用户手工注释实现，退化到 Go `if err != nil` 的无校验层级；(2) 已落地资产浪费：lexer 已切 RaisesKeyword、parser 已实现 parseRaisesClause + parseRequiredReturnType、AST 已有 ReturnTypeNode.errorType、测试已有 raises.zom 基线，删除 raises 意味着回滚 8 个文件约 300 行已验证代码；(3) 规范 06-declarations.md 把 `error` 声明为第一类类型构造（L393-432 完整一章），配套的 `extends` 继

---

## 2. D2 并发安全标记体系

**推荐方案**：条件通过（CONDITIONAL GO）——保留三核心 marker 方向，必须在实现前完成 7 项强制修正

**语法契合度**：语法层 3 处不匹配，严重度 HIGH：
① 设计文档全线使用 `trait` 关键字（§7.1 `unsafe trait Sendable {}`），但 SPEC ch.09 仅定义了 `interface` 关键字（`interface Drawable { ... }`），`class Button implements X, Y {}` 为唯一实现路径。独立 `impl<T> Sendable for Foo {}` 语法、`negative impl` 语法、`auto_trait` 修饰符——三者在当前语法中均不存在。
② 设计大量使用 `#[linear]` / `#[zom::runtime_only]` / `#[zom::scope_guard]` 等内建属性，但 SPEC ch.16 明文声明「Attribute and annotation syntax is reserved for future language design」。
③ `unsafe` 关键字/语法块在任一 SPEC 章节（ch.09 interfaces、ch.12 generics、ch.14 memory）中均未出现；`unsafe trait` 与 `unsafe impl` 的语法合法性为零。
三者合计：设计方向正确，但语法载体与现有 SPEC 完全断裂。需在落地前统一关键字命名体系，且不能违反 NP-6（仅 suspend/spawn 两个新并发关键字）——建议复用 `interface` 并扩展修饰符，不引入 `trait` 新关键字。

**审计闭环**：敌对审计 10 条 Findings 中与本标记体系直接相关的共 5 条（B.3 / B.8 / B.9 / B.10 的 waker 维度、B.5 的 Linear 路径），全部未闭环：
- B.3-AUD-DR-01 场景 B（🔴 Critical）：`Shared` 未对 `UnsafeCell<T>` 做负 impl。这是整个标记体系 soundness 的核心命门——auto-trait 机制会将 `struct WrapCell { c: UnsafeCell<u32> }` 错误推导为 Shared，跨 spawn 以 `&WrapCell` 共享即静默数据竞争，trait 系统完全失去门控能力。
- B.8-AUD-CT-01（🟠 High）：跨门控表中 `spawn_scope 隐式 join` 行标注「编译期 ✓」实际依赖 task-local scope 栈的运行时信息，lexical 跨函数包装场景下编译器不可能静态证明。该声明有误导性。
- B.9-AUD-RL-01（🟡 Medium）：P10 生命周期门控要求 HRTB（`for<'a> body: FnOnce(&'a Scope<R>) -> R`）级别的借用分析器，而当前类型系统仅有基础生命周期推导，HRTB 未在任何 SPEC 位置出现。
- B.10-AUD-RL-02（🟡 Medium）中 Channel `send_ev/recv_ev` 单一 waker 的 clone 注释与 SuspendEvent 单-shot 语义矛盾，本质反映出：若 Linear 类型允许 clone（`send_ev.clone()`），Linear one-shot 约束即被语义上绕过——需要「引用计数包装 + 内部 Linear 降级」的明确规则。
- B.5-AUD-DO-02（🟠 High）Double-Panic 路径下 Linear 资源未被完整清理，证明 Linear 的「恰好一次消费」保证在 panic-unwind 边界上不成立，必须降级为「正常路径编译期 100% + unwind 路径 linear_drop 运行时兜底」并显式声明边界条件。

**用户意图对齐**：标记体系与用户 1.0 规范的 3 条直接诉求高度对齐：
① §9 P0-1「无 Send/Sync 门控 → 数据竞争 UAF」→ Sendable/Shared 正确响应（迁移表 #8 keep Gap-5 闭环）。
② §12 P1-3「TaskHandle 线性消费未定」→ Linear + #[linear] 属性正确响应（迁移表 #12 replace）。
③ §3 原则 3「契约驱动唯一挂起机制」→ NoInternalMutability/SuspendEventContract 作为辅助 trait 支撑。
但存在一处原则冲突：NP-6「仅两个新关键字」与设计中隐含的 `trait` / `unsafe` / `impl` 三个新语法构件冲突（见 grammarFits）。此外用户规范完全未提及 `auto_trait` / `negative_impl`，二者是引入 marker 体系的技术前提，需显式写入并做复杂度评估。

**实现代价**：实现标记体系需要编译器前端新增 7 个独立子系统，当前均为空壳：
① Trait 求解器（interface 约束匹配 + where 子句）——依赖 generic 系统完整后才能开工；
② Auto-trait 传播引擎（struct/enum 字段递归闭包推导）——约 2~3 KLOC 的 checker 级代码；
③ Negative impl 解析、冲突检测与合并规则（RFC 105 级复杂度）；
④ Closure 捕获分类器（按值 / & / &mut 三路分类 + 捕获集合的最小化分析）——Rust 的 NLL 借用检查等价子集；
⑤ Flow-sensitive liveness 分析（suspend 点活跃变量扫描）——需 CFG 构建 + 数据流迭代算法；
⑥ Linear use-def 链检查（含 partial move 语义：`let a = foo.a` 后 `foo.b` 仍需追踪）；
⑦ Lifetime 分析器至少到 HRTB 子集（P10 spawn_scope 生命周期声明确认）。
其中 ④⑤⑥⑦ 四个子系统共同构成「Rust borrow checker 约 40% 能力」的等价实现，复杂度为整个类型系统总量的 60~70%。
按当前团队速率（parser/binder 约 12 KLOC / 6 个月）估算：marker 体系端到端落地需 8~11 个月，是整个并发系统里成本最高的单项。

**主要风险与缓解**：

- R1🔴 关键字命名战争：设计内部统一使用 `trait`，SPEC 使用 `interface`。若不提前统一，实现中途将出现 AST 节点 / diagnostic 命名 / 文档三方不互通。必须在立项 48 小时内定案，推荐方案：复用 `interface`（`unsafe marker interface Sendable` 语法，新增 `marker` 修饰符 + `unsafe` 关键字，不引入 `trait` 新关键字）。
- R2🔴 UnsafeCell 负 impl 缺失导致整体系 sound：若 B.3-B 不在实现第一行代码前写入规范正文，编译期会静默放行最核心的数据竞争场景。该问题在 Rust 历史上是 2015 年 pre-1.0 的 CVE 级缺陷，ZOM 没有理由重复犯错。
- R3🟠 Linear 检查器在 panic-unwind 边界的双标语义：正常路径编译期强保证，异常路径运行时 best-effort。若不显式文档化并降级为「在 unsafe 块中可被绕过」，用户会形成错误心智模型（「Linear = 永远恰好一次」），在 FFI + panic 组合场景出现资源泄漏或 double-free。
- R4🟠 Auto-trait 与 OOP interface 现有设计的哲学冲突：SPEC ch.09 的 interface 目前是 nominal + class-implements 的 OOP 模型（class 内部写 implements，每类显式声明）。Auto-trait 本质是 structural（按字段结构自动推导），两者在同一 interface 关键字下共存需要明确的「marker interface 与普通 interface 的分裂规则」——最典型的冲突：`class Foo implements Bar {}` 显式实现普通 interface Bar，同时 auto-trait Sendable 因为字段有 `*mut u8` 而隐式为 !Sendable，用户在类声明处看不到这一点。
- R5🟠 HRTB 能力缺口：若借用分析器未达 HRTB 级别即启动 spawn_scope 实现，P10 会降级为运行时检查，编译期 18/20 的陷阱覆盖率数字即不成立，整体对外宣称的「18 compile / 1 runtime / 1 sanitizer」矩阵需立即修订为 16/3/1。
- R6🟡 跨 suspend 锁检查（NoInternalMutability）的假阳性/假阴性平衡：liveness 若采用 lexical scope（简单实现），`drop(guard); suspend;` 这种显式 drop 会被错误判为 ZOM8006；若采用 flow-sensitive（正确实现），复杂度上升一个数量级且诊断信息难以理解。用户体验与正确性的矛盾点。
- R7🟡 `Movable` 负 impl 的传播不透明：类型因依赖 OS TLS 而负 impl Movable，聚合了它的 struct 自动 !Movable，用户在 3 层嵌套后无法定位原因。必须在诊断中给出完整的负 impl 传播链，类似 Rust 的 `the trait Send is not implemented for *mut u8` 的字段级展开。

**下游绑定约束**：

D2 标记体系决议向下游绑定以下 5 条不可变约束，任何后续并发子系统设计（运行时、scope、channel、FFI）不得违反：\n\n【DS-1 关键字约束】 不得引入 `trait` 作为新关键字。所有 marker 语义必须通过扩展 `interface` 表达（新增修饰符 `marker` / `unsafe` / `auto`；实现语法统一为 `impl InterfaceName for TypeName {}`，与 OOP 的 `class implements` 分流）。\n\n【DS-2 原子顺序约束】 跨 `Sendable` 边界的内存可见性保证为「C11 SeqCst 语义的子集」：spawn 将任务入队的原子操作（§5.2 `原子 push`）与 worker 取出任务的原子 pop 之间构成天然的 release-acquire 对，用户无需额外 fence。文档化这一保证，避免下游 FFI 层过度插入屏障。\n\n【DS-3 Linear 不可绕过约束】 Linear 类型的编译期检查在 `unsafe` 块中可以被显式绕过（`unsafe { forget(linear_value) }`），但编译器必须在 UNSAFE_USAGE 诊断列表中输出具体 Linear 类型名与行号。纯 safe 代码中 Linear 必须维持编译期 100% 保证。\n\n【DS-4 陷阱矩阵公开约束】 若 R5（HRTB 缺口）在实现阶段未解决，P10 必须立即从 `compile` 降级为 `compile+runtime`，20 陷阱矩阵对外公开数字由 18/1/1 修正为 16/3/1，并在 CHANGELOG 首条显示。禁止虚假宣传编译期覆盖率。\n\n【DS-5 B.3 前置条件约束】 编译器首条支持 marker 的提交必须包含 `UnsafeCell<T>` 的 `!Shared` 负 impl + 对应 lit 测试（L03-bis：`struct X{ c: UnsafeCell<u32> }` 被 spawn 以 `&X` 捕获 → ZOM8002 错误）。缺失该测试，整条 PR 不得合入。"}

**否决方案**：

- 否决（NO-GO）：整体放弃 marker interface 体系，改用 Go 风格 runtime + TSan 的纯动态检查 — 违背 ZOM 全局 NP-10 原则「显式优于隐式 + 编译期优于运行期」。20 陷阱矩阵中编译期捕获会从 18 骤降为 3~4，P01/P02/P03/P04/P10/P12/P18 全部丢失。Go 路线的代价是「用户必须在 CI 中强制跑 3 组以上 TSan 种子 + 生产环境数据竞争仍为 UB」，不符合 ZOM 定位为系统级语言的安全目标。此外 Go 社区 10+ 年的经验证明纯 runtime 检查无法支撑无 GC + 裸指针的系统编程场景（Rust 之所以成功，Send/Sync trait 是核心分水岭）。

- 延期（DEFER）：标记体系推迟到 v1.5 实现，并发第一版仅做 runtime + 文档约定 — 不成立。spawn 边界无 Sendable/Shared 门控 = P01 数据竞争 UAF 为默认行为，并发第一版即为内存不安全的，生态一旦形成无门控代码，v1.5 再引入标记体系会产生 breaking change 级成本（Rust 2018 edition 的 async Send 边界收紧即为前车之鉴，历时 2 年完成过渡）。ZOM pre-1.0 虽无向后兼容承诺，但生态方向一旦走偏，修复成本不可逆。

- 简化（SIMPLIFY）：仅保留 Sendable + Linear 两个 marker，删除 Shared — 不成立。删除 Shared 意味着 spawn 闭包 `&X` 捕获无法静态区分安全与不安全，只能一律拒绝按引用捕获（强制 Arc 包装 + 按值捕获）或一律放行（退化为 Go）。前者在 §11.1 parallel_map 示例中无法通过——`input[start..end]` 为 `&[i64]` 的切片引用，目前正是靠 Shared 门控合法通过。强制改为 Arc<Vec<i64>> 会让并发 API 体验降级为 Rust 2015 早期手写 Arc<Mutex<...>> 的水平，与 Swift/Kotlin 的现代结构化并发体验差距过大。后者直接退回 NO-GO 路线。

- Linear 降级为 must_use lint（仿 Rust），不做 one-shot 强制 — 不成立。TaskHandle 的线性消费约束是 P02（僵尸任务）编译期闭环的唯一机制。若降级为 lint，用户简单加 `#[allow(dead_code)]` 即可绕过，结构化并发 nursery 模型失去根基。Rust `JoinHandle` 不是 Linear 的历史教训已经证明：大量生产代码中 spawn 后完全忘记 join，导致静默任务泄漏与测试 flaky。Linear 是本设计相对 Rust 的明确增量价值点，降级即丢失架构差异。

- 复用 Rust 现有 Send/Sync 语义，做到 100% 兼容并直接 import 学习资料 — 技术上可行，但会丧失两处关键差异：① Linear（Rust 无等价概念，JoinHandle 仅有 must_use）；② Zero function color 与 Send 边界的交互方式不同——Rust async fn 的 Send 是 Future trait 的 associated bound，ZOM 无 Future 故 Send 边界必须出现在 spawn 闭包捕获上。强行 100% 兼容会把 Rust 的 Pin/Unpin 包袱一起背过来。正确策略：「语义上对齐 Send/Sync，命名上 Sendable/Shared 主动偏离以避免用户假设完全等价，并显式文档化差异点」。

---

## 3. D3 spawn 边界生命周期安全

**推荐方案**：方案 B：词法作用域块（lexical scope block）+ spawn_scope 内建闭包签名特例（等价于编译器内建有限 HRTB）。核心机制：(1) spawn_scope 是普通库函数签名 `fun spawn_scope<R>(body: fun(scope: &Scope) -> R) -> R requires R: Sendable`；(2) 编译器通过 `#[zom::scope_guard]` 属性识别，对 body 闭包注入「所有捕获引用的生命周期严格包含在 spawn_scope 返回之前」的内建借用检查（等价于 `for<'a> FnOnce(&'a Scope<R>) -> R + 'a` 的编译器内建特例，不暴露通用 HRTB 语法到用户侧）；(3) spawn 的静态分析边界严格限定为「spawn_scope body 闭包的词法内部」，跨函数传播走运行时 scope_stack 验证（B.8 修复）并在 lint ERROR 等级告警；(4) Scope::drop 生成「drop-with-suspend」特殊帧，与普通 RAII Drop 解耦以避免 B.4 的互斥矛盾。

**语法契合度**：方案 B 完全符合当前 grammar，spawn_scope 为库函数（无新关键字，符合 NP-6），仅依赖 `#[zom::scope_guard]` 的编译期内建属性识别；方案 A 要求开放通用 HRTB 语法（`for<'a>` 限定词），grammar 未定义且需新增 lifetime 语法层面；方案 C 不改变 grammar 但违反 NP-10

**审计闭环**：方案 B 可闭环 B.8（词法边界明确，运行时栈检查兜底）、B.9（内建闭包签名特例绕过用户侧 HRTB 缺口）、B.4（scope_guard 属性使编译器在 drop 点生成特殊「drop-with-suspend」帧，与普通 Drop 解耦）；方案 A 可闭环 B.9 但需完整 HRTB；方案 C 无法闭环 P01/P10（ARC 与数据竞争正交）

**用户意图对齐**：方案 B 人体工程学最优：用户无需手写 HRTB 签名，闭包捕获自动推断；写 `spawn_scope(fun(scope) { ... })` 即可；方案 A 要求用户理解 `for<'a>` 生命周期限定，学习曲线陡峭（Rust scoped_threads API 用户抱怨最多的点）；方案 C 人体工程学中等但每次 spawn 隐式 clone Arc 导致不可控性能退化

**实现代价**：方案 B 成本最低：复用现有闭包借用分析 + 新增 scope_guard 属性识别 + scope_stack task-local 运行时变量；方案 A 成本最高：通用 HRTB 的类型检查器/借用分析器扩展（Rust 用了约 5 年才 sound）；方案 C 成本中等但引入原子引用计数的运行时开销，且无法静态证明安全

**主要风险与缓解**：

- RISK-1（中）：编译器内建 spawn_scope 闭包特例若存在 bug，可能导致 scope 引用被逃逸——缓解：在内建分析器的所有出口路径插入 debug 模式运行时断言 `scope.borrow_escape_detected()`，det_sched 模式下 CI 必跑；对 spawn_scope 的标准库实现标注 `#[zom::trust_me]` 但编译器内部不走用户侧信任路径
- RISK-2（低）：跨函数 spawn（将闭包传给辅助函数，辅助函数内 spawn）的运行时验证可能在 release 模式被关闭——缓解：运行时 scope_stack 验证默认开启，release 模式仅移除 scope_id 字符串等调试信息，核心 id 匹配不关闭；提供 `-Z disable-scope-runtime-check` 但强制标注为 unsafe
- RISK-3（中）：lexical 边界与动态 scope_stack 的不一致可能导致合法代码被误报（false positive）——缓解：提供 `#[zom::within_scope(scope_id)]` 的辅助属性，允许用户显式声明某函数在特定 scope 内被调用，编译器可做跨函数静态传播（与 B.8 的 lint 告警联动，标注后 lint 静默）
- RISK-4（高）：Scope drop 与 panic unwind 的互斥（B.4）若实现不当会触发资源 leak 或 double-drop——缓解：Scope drop 入口首行执行 `if in_panic_unwind() { goto fast_path_resources_only }`，fast_path 不执行任何 suspend，仅对所有子任务调用 `mark_cancelled_async()`（只写 CancelToken.requested 原子，不等待），并记录 leak 到 Scope 错误聚合器
- RISK-5（中）：开发者可能通过 `clone(Arc<Scope>)` 或全局静态存储绕过词法边界——缓解：Scope 类型不实现 Clone（P15 Linear 结构约束），Arc<Scope> 不被 auto-trait 推导为 Shared（负 impl），unsafe 块内的全局存储触发 lifetime-taint 标记，debug 模式下 Scope 析构时扫描全局表检查是否存在逃逸引用

**下游绑定约束**：

1. 推荐方案 B（词法作用域块 + 有限 HRTB 内建特例）需与 D6 并发内建属性（`#[zom::scope_guard]` / `#[zom::requires_executor]`）联合推进，二者互相依赖
2. 实现阶段必须先落地 spawn_scope 的闭包签名特例化（绕过通用 HRTB 的 parser/type-checker 复杂度），再评估是否开放通用 HRTB 到用户侧
3. AUD-CT-01（lexical 边界降级为运行时检查）需在文档中明确标记：`spawn_scope` 闭包内所有 `spawn` 的静态分析范围限定为「闭包词法内部」，跨函数传播需引入 `fn run_within_scope<F>(scope: &Scope, f: F) where F: WithinCurrentScope` 的内建 marker trait
4. AUD-RL-01（unsafe 绕过 HRTB）要求编译器对所有通过 `unsafe` 函数返回的 `&Scope` 引用插入 lifetime-taint 标记，防止被 `'static` 推断
5. B.4（Scope drop 中 suspend 与 unwind 互斥）要求 Scope::drop 中必须先检查 `current_task().in_panic_unwind()`，禁止在 unwind 路径执行 suspend
6. B.9（spawn_scope 的 HRTB 缺口）要求若通用 HRTB 未实现，必须在 spawn_scope 的类型签名中注入编译器内建的「body 闭包的所有借用生命周期严格短于函数返回」语义，等价于内建 HRTB 特例
7. 不能选择方案 C（全部 ARC）的硬约束：违反 NP-10「编译期优于运行期」，且 ARC 不能防止数据竞争（Sendable/Shared 门控与 ARC 正交），回退空间为零
8. 方案 A（纯 scope 显式借用）依赖完整 HRTB 实现，若 parser/type-checker 进度落后则必须回退到方案 B
9. 所有方案均需预留 FFI 侧 ZomScope* 的独立 refcounted 路径（与 ZOM 侧 Linear 解耦），符合 P17 与 AUD-B.3 修复要求
10. 与 Linear trait 体系对接：Scope 本身不是 Linear，但 Scope::drop 必须 auto-consume 其内部注册的所有 TaskHandle；此机制是 Linear 检查的特殊豁免路径，需在编译器 Linear 分析中显式标注

**否决方案**：

- 方案 A：Scope 显式借用（完整通用 HRTB 暴露到用户侧） — ① 实现成本过高：通用 HRTB 对类型系统/借用分析器的 sound 化扩展工作量相当于再造 Rust NLL 的 30-40%，当前 parser 完成度约 15%，type-checker 为空壳占位，前置依赖未满足；② 学习曲线陡峭：要求用户手写 `for<'a> Fn(&'a Scope<R>) -> R` 限定，对非系统级开发者不友好，与 ZOM 「并发对普通开发者可用」的定位冲突；③ 与 AUD-B.8 finding 矛盾：通用 HRTB 仍无法解决「闭包传递给其他函数后 spawn」的跨函数静态分析（因为 HRTB 签名不携带 scope_stack 的动态信息），仍需运行时兜底；④ 与 NP-10「显式优于隐式」原则不完全对齐：HRTB 是类型系统显式声明，但 spawn 的 scope 绑定关系对阅读代码的人不如词法块直观

- 方案 C：全部 ARC（所有 spawn 捕获强制引用计数） — ① 违反 NP-10「编译期优于运行期」核心原则：所有生命周期安全退化为运行时 refcount，丢失编译期 UAF 证明；② ARC 与数据竞争安全正交（致命缺陷）：`Arc<NonSharedType>` 跨线程共享仍会触发静默数据竞争，Sendable/Shared trait 门控不能被 ARC 替代——二者解决不同问题，ARC 只回收内存不互斥；③ 不可控性能开销：对大对象（如 1MB 缓冲区）的每一次 spawn 边界穿越都要做 atomic inc/dec，且缓存失效（B.7 false-sharing 问题同样影响 Arc 计数器），高并发场景性能退化可测 20-60%；④ 破坏 P19 的内存 backpressure：Arc 循环引用若未被 Linear/Weak 联合约束，debug 模式下也无法完全检测（与 P15 矛盾）；⑤ FFI 边界复杂度翻倍：ZOM 侧 A

---

## 4. D4 任务模型：stackful vs stackless

**推荐方案**：推荐：Stackful Segmented M:N（链式分段栈 + M:N work-stealing + per-worker reactor）。否决：Stackless Enum Generator（编译器将每个 suspend 点切分为 enum variant 的状态机）。

**语法契合度**：stackful M:N 模型与规范 §5.1 suspend 语义展开 10 步中第 7 步「保存上下文（寄存器 + SP）切换到调度循环」完全一致（直接对应 setjmp/longjmp 或 callee-saved register push + SP swap），语法 EBNF 无需变更；enum generator 方案需引入额外 SSA-form 解构与跨 suspend 活动变量的 enum payload 编码，虽然可在 IR 层完成，但会产生「用户栈 → enum payload → 恢复栈」的双重拷贝语义，与 Zero Color（NP-1）在调试信息质量上冲突（每个 suspend 处调试器局部变量栈帧链被打断）。

**审计闭环**：本评审覆盖了 ZOM 设计规范 10 条 NP 原则中与任务模型强相关的 7 条（NP-1/2/3/4/5/8/10），同时对 20 个陷阱中 12 个直接与栈/上下文管理相关的陷阱（P02/P04/P06/P07/P10/P11/P14/P17/P18/P19/P20 及 P08 的运行时部分）做了逐选项验证，确保推荐方案能支撑现有陷阱矩阵而无需降级 compile 到 runtime。同时兼容现有 /Users/bytedance/Develop/ZOM/libraries/zc/async/ 中 ZC_USE_FIBERS=1 的 FiberStack + jmp_buf/ucontext 实现路径（18KLoC 已有代码资产），降低落地成本约 60%。

**用户意图对齐**：用户在规范 §1.0-§8 中明确选择了 (a) 零函数颜色 NP-1、(b) 显式挂起点 NP-2、(c) 契约驱动唯一挂起 NP-3、(d) Eager Task NP-4、(e) 业务不可阻塞 worker NP-5 五条与任务模型直接绑定的原则。Stackful 方案对这 5 条全部直接支撑，零妥协；enum generator 方案在以下两处需软性妥协：① NP-5（栈 frame 总大小超过阈值时 enum variant payload 可能爆堆且无法 backpressure 到编译器）——Stackful 方案的分段栈 + guard page 在运行时自然处理 64KB-8MB 范围；② NP-8 单一真相来源——enum generator 的 lowering 结果与用户手写「等价函数」之间调试视图不一致，违反了「parser/type-checker/runtime/document/LSP 从同一 EBNF 派生」的精神（LSP 悬停信息在 suspend 之后断点处无法显示原栈帧）。

**实现代价**：Stackful 方案在现有 zc/async/async.cc ~500 行 FiberStack 基础上扩展：(1) M:N worker = 新建 ~1500 行调度器（含 work-steal 队列 + per-worker reactor），(2) 分段栈 = ~300 行 StackSegment + 栈搬迁触发 __zom_stack_grow，(3) suspend/resume 编译器内置 = ~200 行编译器内建属性展开，合计 ~2000 行新增 + 18K 已有复用，预计 6-8 周落地可运行原型。enum generator 方案：需在编译器中实现「coroutine state machine lowering pass」——每个函数按 suspend 点切分 basic block、活动变量交集分析、enum variant 生成、跨 block 活动变量重命名 pass，估计 ~4000-6000 行编译器代码（Go 标准编译器此部分约 8K 行、Rust compiler/rustc_mir_transform/src/ 下 coroutine 相关约 6K 行），且调试信息支持额外 ~2000 行，预计 12-16 周，代码量 2-3x、风险 2-3x。

**主要风险与缓解**：

- R1 Stackful 栈切换性能：setjmp/longjmp 在 aarch64/x86_64 上约 ~30-60ns（含 callee-saved reg save/restore），对比 stackless enum resume（函数调用 ~3-10ns）有 3-10x 单次切换开销。缓解：合并连续 suspend（编译器在 loop+suspend 场景下合并多次 checkpoint 为单次原子检查），典型工作负载（网络 I/O 密集）中 suspend 间隔 >>10μs，相对开销 <1%。
- R2 栈分段内存碎片：链式分段 64KB 首段 + 后续按需 64KB，1M 任务在 1000 并发运行时实际内存占用 ≈ 1000 × 64KB = 64MB（符合 P19 内存约束），但 10K 任务同时进入大递归（每任务 4-8 段）会产生 ~640 个段链表 + guard page 碎片。缓解：段分配按 per-worker bump allocator（madvise(MADV_FREE) + 段池复用），并提供 Scope.max_stack_size 上限。
- R3 Signal handler 与栈搬迁并发：P06 的 SIGSEGV handler 触发时若任务正处于 suspend 上下文切换中（SP 半更新），会导致 handler 无法正确识别栈归属。缓解：在每个 suspend/resume 切换代码块中使用 per-task in_switch: AtomicBool，handler 检测到 in_switch=true 时延迟 1 个调度 tick 重试。
- R4 Windows/arm32 平台移植性：现有 zc/async/async.cc（1404-1500 行）中 ucontext/makecontext 在 arm32 平台上不可用（musl libc 1.1.24 起支持但部分发行版禁用），Windows 平台走 CreateFiber()（现有代码路径）。缓解：所有平台提供 fallback 的 asm 级手动 SP 切换（参考 boost::context 约 50 行/per arch 汇编），作为 ZC_USE_FIBERS=1 的强制实现路径。
- R5 Appendix B AUD-B.7（TaskHeader false-sharing）：本方案将 TaskHeader 拆分为三 cacheline 组（本地/跨线程/只读），是必须项而非可选。如未实施，32 核扩展性崩溃。缓解：将此作为 implementation checklist P0 项，阻塞 merge。
- R6 与 det_sched（确定性调度种子模式）兼容性：stackful 方案的寄存器上下文保存 + 恢复在 det_sched 下要求 SP 基址、段地址空间布局也确定性（mmap 地址不随机）。缓解：det_sched 模式下使用固定基址的 per-worker bump allocator 分配栈，关闭 ASLR。debug 工具压力降低 80%。
- R7 enum generator 方案作为 debug/det_sched 可选路径的维护成本：保留两条实现路径意味着每个核心特性（suspend semantics、取消传播、Scope RAII 语义、traits 门控）需双验证。缓解：只保留 enum-generator 作为「reference interpreter」用于 spec 合规性 fuzz 测试，不作为生产路径；生产路径 100% stackful。

**下游绑定约束**：

1. 推荐方案（Stackful Segmented M:N）需在 3 个 Critical Finding 闭环后启动实现；2. 否决方案文档化为 §13 新增否决条目（RA-9/RA-10）；3. Appendix B B.7（false-sharing）作为 Stackful 方案的必须项；4. B.1（零颜色边界）和 B.3（Channel waker）与本选型正交，但优先级不低于本选型；5. enum-generator 方案可作为 debug/det_sched 模式下的可选实现路径保留（便于确定性调度）。

**否决方案**：

- Stackless Enum Generator（纯 stackless，编译器 lowering 为 enum state machine） — 1. 与 §9.2 分段栈规范根本冲突：stackless 无独立栈，不存在 StackSegment；规范 P06（栈溢出精确到任务级）无法在 stackless 模型下自然实现（stackless 共享 OS 线程栈，溢出 = 崩进程，除非用 OS 线程栈 copy-up，开销极大）。2. 与 §10 FFI 双向互操作场景冲突：C 回调向上调用含 suspend 的 ZOM 函数（B.1 场景），stackless 方案需要 C 侧也参与 state machine，根本不可行；stackful 方案通过独立上下文切换自然支持。3. 调试信息 / crash dump 质量：stackless 方案每个 suspend 点是函数返回 + 恢复入口重定向，栈回溯需编译器额外合成 DWARF CFI（Zig 在 0.10-0.12 曾为此投入约 3000 行代码仍不完美）。4. 大型栈帧

- 混合方案：浅层调用用 stackless enum，深调用/大栈帧用 stackful（Two-mode Hybrid） — 1. 复杂度倍增：两套 suspend/resume 路径在同一 runtime 中共存，跨 mode 调用（浅→深、深→浅）需要动态判定 + 栈搬迁 + 上下文切换桥接，估计 runtime 复杂度 +150%、测试用例数 +200%。2. 违反 NP-2 显式挂起点：用户无法从源码判断某个函数的某次调用走哪条路径，性能特征不透明。3. 附录 B AUD-FC-01（零颜色运行时边界违反）中描述的场景在混合方案中更难追踪：C 回调入口处需先判定「当前调用深度 / 当前函数栈帧集合是否超过阈值」，实现不可靠。4. 调试与可观测性 L1/L2（taskdump / span trace）需要支持两套 backtrace 格式，工具链工作量增倍。5. 无行业成功先例：Zig 曾短暂尝试 hybrid（async function lowering + stackful fallback）但在 

---

## 5. D5 零函数颜色 vs 显式 suspend-capable

**推荐方案**：C. 混合方案（默认零颜色 + 静态可推导 + 可显式标注 + FFI/Drop边界强制显式）

**语法契合度**：选项定义严格穷尽且互斥：A=严格零颜色(签名无标记，UB靠requires_executor编译期lint补救)；B=严格显式(签名强制suspend修饰，UB消除但生态双分)；C=默认零颜色+可推导+可显式覆盖(混合方案，消除FFI边界UB但不强制全量签名污染)。三选项覆盖了从Go/JDK21风格(A)到Zig/Rust风格(B)再到Swift6风格(C)的完整工业设计谱系，无遗漏。

**审计闭环**：本决策覆盖ZOM-ASYNC-CANONICAL-DESIGN §3 NP-1(零颜色原则)与附录B.1 AUD-FC-01(敌对审计指出的运行时边界静默违反)之间的核心矛盾。敌对审计的结论是：NP-1声称的「调用者不可从签名感知」在裸OS线程→ZOM运行时边界是直接的UB崩溃路径(无调度循环可切、task-local scope栈为空、CancelToken不存在)，且零颜色使得任何impl Drop中意外调用含suspend的库函数无法被调用者静态发现(§B.4)。D5面板将这一矛盾提升为显式抉择而非隐含假设。

**用户意图对齐**：用户在并发规范10条原则中NP-1明确要求零函数颜色(「挂起是控制流内部行为而非类型属性」)，同时敌对审计B.1/B.4/B.8三项Finding均指出纯零颜色在边界上不可sound。用户的真实目标是「开发者体验优先但不能有静默UB」，而非教条式的零颜色或教条式的显式。选项C恰好同时满足：默认开发者体验零颜色(95%场景无签名污染)、FFI边界和Drop边界显式标记(消除UB)、lint可配置(允许团队自行收紧为全显式)。

**实现代价**：实现成本基于ZOM当前Codebase状态评估：TypeChecker当前是空stub(AGENTS.md §Architecture Critical Gap #1)，尚未实现trait检查、借用检查、effect系统。选项A仅需在TypeChecker中新增一个caller-location lint pass(~300-500行C++)和Scope绑定的运行时检查hook(~150行)；选项B需要在Parser(Binder→TypeChecker全链路)新增effect传播类型系统，语法层面修改fun签名解析，全量规范rewrite ~40%，预计~5000行C++ + 规范重写12人日；选项C需要在TypeChecker中做推导(与类型推导复用同一unification框架) + 显式标记解析 + 四级lint体系，预计~1500-2000行C++ + 规范增量修订3人日。

**主要风险与缓解**：

- R1. 推导路径复杂度：当闭包作为参数跨多层函数传递时(如用户定义的run_parallel包装)，SuspendCapability的静态推导需要HRTB级别的借用分析器。当前ZOM借用分析器未实现(属于TypeChecker stub的一部分)，若HRTB延迟则推导降级为动态检查。→ 缓解：C方案本身允许降级为动态检查(runtime scope栈top-id验证panic)，不阻塞功能；同时在Pre-1.0前将HRTB列为Must-Fix。
- R2. lint噪声：默认ERROR等级下，如果标准库函数的suspend能力标注不完整(漏了某个内部含suspend的helper)，用户调用即报ERROR。→ 缓解：标准库100%由编译器团队维护，用自动化测试(对所有公开fun执行AST suspend扫描)保证标注完整性；同时提供#[allow(ZOM8018)]局部豁免。
- R3. 迁移压力：Post-1.0如果决定收紧默认lint等级(例如从ERROR升级为默认显式标注)，将是breaking change。→ 缓解：在§3 NP-1修订中明确声明「C方案为1.0最终策略，不接受Post-1.0变更为B方案」，写入stability-manifest。
- R4. 与Linear检查交互：SuspendCapability的推导与Linear跨suspend检查(§6.2/P02)共享同一活跃变量分析框架，二者联合实现时可能出现double-report同一违规。→ 缓解：在lint引擎中加入deduplication规则(同一源码位置的ZOM8006与ZOM8018只报前者，后者suppress)。
- R5. FFI边界与C ABI的suspend标记兼容：§10.2中C回调的suspend能力无法在C侧表达。→ 缓解：规定所有extern "C"回调默认视为SuspendCapability::Unknown，调用ZOM侧函数必须显式#[zom::assume_executor_context]或zom_runtime_enter()包装，否则FATAL lint。
- R6. 确定性调度种子模式(-Z deterministic)与零颜色场景的结合：用户如果用零颜色调用了含select的函数，select本身的确定性索引偏置(§B.6)与「调用者不可感知」组合会产生难以复现的时序问题。→ 缓解：§8.5 det_sched模式下，所有suspend点的task_id和event_id必须包含per-suspend-site哈希(来源于调用栈hash)作为输入之一，保证零颜色下的确定性不依赖于调用栈不可见性。

**下游绑定约束**：

1. 若选零函数颜色(A)：要求在实现TypeChecker前完成NP-1修订文字 + §B.1的requires_executor属性 + §B.4的ZOM8012 lint三条补丁写入规范正文；spec版本需bump到1.0.0-rc2并在AGENTS.md已知缺口列表新增一条「executor上下文检查实现排期」。
2. 若选显式suspend-capable(B)：要求废弃§3 NP-1零颜色原则，重写§5所有签名示例，新增suspend关键字到fun签名，同步更新§7跨门控表中所有spawn闭包签名约束为FnOnce() -> T + SuspendCapable，工作量等价于规范全量rewrite ~40%；同时§10.2 C ABI中ZomTask*回调签名需加ABI级suspend标记。
3. 若选混合(C)：要求在§2术语表定义「SuspendCapability传播三元组(lexical/static/dynamic)」，新增Lint等级表(INFO/WARN/ERROR/FATAL四级，默认ERROR)，并在§14开放问题中将OQ-1..OQ-10中与颜色传播相关的项(OQ-1/OQ-8)升级为Pre-1.0 Must-Fix。

**否决方案**：

- A. 严格零函数颜色（当前规范 NP-1 原版） — 无法sound消除AUD-FC-01指出的裸OS线程→suspend UB；仅靠requires_executor lint补救属于「编译期检查无法100%覆盖(见§B.8)」，unsafe绕过或回调包装即失效。与NP-10「编译期优于运行期」自相矛盾。

- B. 显式suspend-capable签名（Zig/Rust async fn模式） — 违反用户明确的NP-1原则(「函数颜色导致生态分裂」为§13 RA-1否决理由)；同时需要重写TypeChecker effect系统，当前TypeChecker是空stub(AGENTS.md已知缺口#1)，实现成本最高(约为C的3-4倍)且推迟整个并发系统落地时间线3-6个月。Kotlin协程、Rust async生态双分的教训已被用户在RA-1中明确拒绝。

---

## 6. D6 属性命名空间 + 最小属性集合

**推荐方案**：采用两轴命名空间 + 三层并发最小属性集合方案。命名空间轴：lang（编译器内建/无导入/保留名，v1 必须识别）、std（标准库级，需 import std.attr，编译器仅保留名不检查语义）、vendor（第三方/FFI，必须使用反向域名前缀如 com_example::，编译器不做语义门控）。并发最小属性集合分三层：Tier-1 并发语义基石 5 个（lang::unsafe、lang::must_use、lang::auto_trait、lang::negative_impl、lang::ffi::extern_c），Tier-2 结构化并发边界 6 个（lang::actor::non_reentrant / reentrant、lang::cancel_safe、lang::send_bound、lang::sync_bound、lang::spawn_blocking），Tier-3 诊断与优化 6 个（lang::thread_safe_checked、lang::no_implicit_await、lang::memory_order、lang::pin、lang::defer_unwind、lang::worker_affinity）。语法采用 Rust 风格括号式 #[lang::unsafe] 主形态，单属性允许 @lang::unsafe 语法糖但 AST 统一降格，避免双形态长期漂移。

**语法契合度**：语法层面零冲突：当前 grammar-reference 和 ZomLexer.g4 中 @ 未被任何产生式占用（仅 chapter 16 声明为保留给未来属性语法），# 未被用作任何运算符或前缀。将 AttributeList 插入 Declaration 层级最前端作为可选前缀，不改变任何已有产生式的 FIRST 集，无回溯风险。AST 层只需新增 AttributeSyntax { path: Vec<Ident>, args: Option<AttributeArgs> } 节点类型，与现有 InterfaceSymbol/TypeSymbol 机制正交。SymbolFlags 方面：现有 21+ 个预留位中，方案不要求新增任何位——lang::unsafe 直接映射已有的 SymbolFlags::Unsafe 位；lang::must_use 走独立语义检查路径不占 flag；lang::auto_trait / negative_impl 复用 InterfaceSymbol 的扩展字段。

**审计闭环**：直接闭合 44 条审计 finding 中至少 11 条的缺口无门控位问题：high-5（unsafe 逃生舱对应 lang::unsafe）、high-11（Send/Sync trait 声明入口对应 lang::auto_trait + lang::negative_impl）、high-14（取消传播静态门控对应 lang::cancel_safe）、info-43（并发诊断码触发点对应 Tier-1 所有 5 个属性的 checker 入口）、medium-22（actor 非重入默认 + opt-in 属性对应 lang::actor::non_reentrant / reentrant）、high-18（async 与 raises 交互门控对应 lang::must_use 在 Future 返回值上的检查）、medium-16（unsafe 操作边界对应 lang::unsafe 在函数上的边界声明）、medium-17（spawn_blocking 标注入口对应 lang::spawn_blocking）、high-8（跨 await 借用自引用结构的 Pin 属性入口对应 lang::pin）、medium-11（task-local 与 Send 屏障对应 lang::send_bound）、以及 .agents/subagents/concurrency.md Review Checklist 第 7 条的 actor 非重入默认 opt-in 属性要求。

**用户意图对齐**：完全满足两部分诉求：命名空间分层 + 并发最小属性集合。三层设计的关键价值在于最小：Tier-1 仅 5 个，是所有并发语义的前置条件（没有 unsafe 就没有 FFI 逃生舱，没有 auto_trait 就没有 Send/Sync 静态检查，没有 must_use 就没有 Future/JoinHandle 结果丢弃诊断），去掉任何一个都会导致上层并发 API 必须以 unsafe 全量门控。Tier-2 的 6 个对应审计报告 high finding 中明确要求必须在编译器层可见的属性入口。Tier-3 是 SWERC 级优化和高级诊断，v1 可不实现。两轴命名空间设计与六语言对标中的最佳实践一致（lang/内建 vs std/标准库 vs vendor/第三方），且与 AGENTS.md Attention 中 『删除无用事物 + 最佳实践优先』原则完全对齐。

**实现代价**：v1 落地成本可控：语法词法层约 150 行（lexer 新增 @ 与 # 分支 + parser 新增 AttributeList 产生式 + AST 节点）；语义层零新增（所有 Tier-1 属性仅需在 checker 中存元数据，v1 不启用校验逻辑）；与 SymbolFlags 的映射只有 Unsafe 一位已有位，其余走 SymbolTable 的 metadata 侧通道不占 flag 位。v2 启用并发时 checker 中增加约 800 行静态检查逻辑（分层可增量实现）。对比方案 A（扁平无命名空间）避免了 Swift 式 UI thread 属性侵入语言核心、C# 式全局属性名冲突；对比方案 B（全部在 std 命名空间）避免了 Rust 式 lang 内部属性混入 std 导致的实现可见性混乱。

**主要风险与缓解**：

- 命名空间三段式（如 lang::actor::non_reentrant）的 lexer 中 :: 与类型层级 :: 冲突风险——通过在 Attribute 产生式中强制路径必须出现在方括号内且后跟 Declaration 关键字来避免歧义，因为该位置的 :: 永远不会被 parse 为类型表达式的一部分。
- AST 统一降格策略执行不严风险：如果 parser 同时接受 @foo 和 #[foo] 但 dumper 各自原始形式，长期导致 lit 测试双形态漂移——规避措施：在 parser 层立刻把 @foo 包装为单元素方括号 AttributeList，AST 中不区分来源，dumper 统一输出 #[foo]。
- vendor 反向域名前缀在 CJK 用户群体接受度低风险——可能导致生态自创简短别名。规避措施：在 std.attr 模块提供 use_attr 别名机制，但 v2 再实现，v1 文档明确要求反向域名格式。
- lang::auto_trait 与现有 InterfaceSymbol 机制兼容性风险——当前 InterfaceSymbol 无 OIBIT 字段和推导逻辑。规避措施：Tier-1 的 auto_trait 在 v1 仅语法保留，不启用推导；symbol-flags.h InterfaceFlags 新增 AutoTrait 一位（大量空位）但 v1 不赋值。
- 并发属性与 async/await 关键字落地顺序错位风险：若先实现 async fn 语法但无 Send/Sync 检查，导致用户写出大量跨 await 传递引用代码后在 v2 被整体 break。规避措施：AGENTS.md 中增加约束——async/await 语法落地必须与 Tier-1 全部 5 个属性的 checker 逻辑同时提交。
- memory_order 属性默认值选择争议——relaxed vs acq_rel vs seq_cst 三选一。规避措施：Tier-3 中 lang::memory_order 在 v1 仅语法占位，v2 并发落地时单独评审默认值，默认与 zc 库当前内部倾向一致的 acq_rel，但允许 per-atomic 覆盖。

**下游绑定约束**：

下游实现约束与依赖清单：1) lexer: 需新增 @ 与 # 的 token 定义（ZomLexer.g4: 新增 AT / HASH 两个 token，同时在 case '@' 和 case '#' 中派发）；2) parser: Declaration 层级最前端插入可选 AttributeList 产生式，且必须在 Modifier Keywords 之前消费（保证 #[lang::unsafe] fun f() 的解析顺序）；3) AST: 新增 AttributeSyntax 节点与 AttributeListSyntax 容器，所有 Declaration 类增加 attributes 字段；4) symbol-table: InterfaceSymbol 扩展需保留 auto_trait / negative_impl 两位的槽位，在 v2 并发实现前不启用推导；5) diagnostics: 新增一个 diagnostics-concurrency.def 占位文件，内含 8 条被门控的诊断码声明，全部由 ZOM_FEATURE_CONCURRENCY 宏门控，v1 不编译；6) spec: chapters/16-attributes-and-annotations.md 需从 11 行扩展为完整章节，chapters/15-concurrency.md 需增加并发属性依赖关系图；7) lit 测试: 新增 attributes 子目录，覆盖 5 个最小属性的语法解析；8) CMake: 新增 ZOM_FEATURE_ATTRIBUTES=ON/OFF 编译开关，默认 ON 仅开启解析不启用语义检查。

**否决方案**：

- 方案 A：扁平无命名空间（所有属性全局名，如 @inline、@unsafe、@send_bound） — 完全不可行。对标教训：C# 在 .NET 生态中 20 年仍在新增属性时出现名称冲突（System.Obsolete vs 第三方 Obsolete）；Java 注解早期无强制命名空间导致 JSR 305 vs JSR 330 两套注解长期共存。并发属性尤其集中在简短语义词（send、sync、unsafe、cancel），扁平命名几乎可以肯定 3 年内冲突。

- 方案 B：全部在 std 命名空间（编译器内建属性也走 std，无 lang 级） — 架构分层错误。对标教训：Rust 中 lang 项与 derive 项分属两层，前者是编译器实现细节、后者是用户可见语法糖。对于 ZOM：lang::unsafe 必须在任何 import 之前生效（控制 unsafe block 的语法门控），如果放在 std::unsafe 则 import std 之前无法使用，与 FFI 场景矛盾。

- 方案 C：Python 装饰器风格（@dec(args) 可出现在函数内任意位置，表达式级） — 过度设计。对标教训：Python 装饰器的表达式级灵活性导致大量滥用。ZOM v1 只需要声明级属性（函数、类型、接口、模块四位置），表达式级属性在 v3 前完全无使用场景。

- 方案 D：一步到位（17 个并发属性 v1 全量实现 checker） — 违反 AGENTS.md Attention 第 4 条删除无用事物和第 1 条无前向兼容。当前 TypeChecker 整体为空实现（被注释掉），并发 API 完全未定义，v1 实现 17 个并发属性的静态检查没有任何可触发入口，相当于超前蔓延 17 倍。

---

## 7. D7 三系统分层 + 分阶段落地

**推荐方案**：方案C：三系统分层解耦 + 四阶段渐进落地（基础设施先行 → 独立核心打通 → 两两交叉契约 → 三者统一闭环）

**语法契合度**：当前三大系统共享6项基础设施依赖（TypeChecker空白、Symbol Export flag未写入、Binder import语义空壳、Diagnostic Engine缺跨模块/并发专用码、Driver无拓扑排序、Runtime并发目录为零），不分层就无法在任一系统独立推进——例如并发系统的Sendable trait检查需先有Checker，而错误系统的raises验证也需先有Checker，两者在Checker内部必须有清晰接口分界而非混写。

**审计闭环**：审计发现170条中：阶段0直接关闭23条（含模块系统2条Critical：Export flag未写入/MOD-007、拓扑排序缺失/MOD-004；错误系统1条Critical：Checker空白/ETM-002）；阶段1关闭87条（错误系统语法+类型模型60条、模块系统包+作用域+可见性27条、并发系统trait+核心类型基础层）；阶段2关闭45条（两两交叉：错误×并发的取消传播与supervisor策略、错误×模块的跨模块raises子类型、并发×模块的跨模块Sendable边界）；阶段3关闭15条（三者统一：模块级编译并行调度利用并发runtime、错误系统Diagnostic Engine的并发任务隔离、最终合规测试集全量）。

**用户意图对齐**：符合决策评审面板D7的核心诉求：顶层边界无歧义（每系统5个专属域+3层接口契约矩阵清晰界定）、分阶段可验收（每阶段产出可运行的编译器版本+对应测试通过绿条）、风险可控（阶段0卡住则全部后续可暂停而无半成品代码漂移）。

**实现代价**：估算人月：阶段0约1.5人月（Checker骨架+Driver重构+Diagnostic码注册，约3000行）；阶段1约4人月（错误系统raises体系+模块系统包/作用域/可见性+并发系统trait+核心类型，约8000行）；阶段2约3人月（交叉契约实现，约6000行）；阶段3约1.5人月（三者统一闭环+合规测试+文档，约3000行）。合计约10人月，代码总量约20000行，新增ZOM诊断码约45个（错误25+模块12+并发8）。

**主要风险与缓解**：

- R1-依赖风险：阶段0的TypeChecker骨架是全部后续的单点阻塞。若阶段0延期超过+50%（即从1.5人月变为2.25人月），则全部后续阶段同步顺延。缓解：阶段0拆为可独立交付的3个子里程碑（M0a: Checker骨架+Driver接入≈0.5人月，M0b: Diagnostic Registry统一≈0.4人月，M0c: Driver拓扑排序+Export flag写入≈0.6人月），每个里程碑完成后都有对应的绿条测试（Checker空跑不崩溃、10个测试诊断码注册查询返回正确、拓扑排序3节点DAG输出正确），任一子里程碑卡住不影响其他两个并行推进。
- R2-接口漂移风险：阶段1定义的3系统接口契约（错误→Checker的"验证raises子集"API、并发→Checker的"检查spawn捕获类型"API、模块→Checker的"查询符号导出标记"API）在阶段2实现交叉时发现参数不足。缓解：阶段1的契约接口全部采用"参数结构体+返回Result"模式而非零散参数列表，结构体字段可在阶段2新增时用Option<T>扩展，老调用方无需修改；同时每个接口配套1个最小契约测试（contract test），阶段2修改接口时必须同时更新契约测试，保证向后兼容。
- R3-并发运行时与zc库执行器架构冲突风险（并发审计中风险项M-11/M-19）：zc库当前的"每线程EventLoop+Executor点对点投递"模型与设计文档要求的M:N work-stealing模型存在架构不兼容。缓解：在阶段1尾声（第4人月末）插入一个专门的架构验证spike：取zc库async目录的10个核心测试，用新的M:N Runtime跑一遍，通过率<70%则触发架构分叉决策——要么在zc库之上封装一层Adapter（保留zc API不变，内部桥接到新Runtime），要么zc库的async目录整体标记deprecated，用户代码全部迁移到新Runtime（ZOM全局设计原则#3 Radical Refactoring支持此选择）。
- R4-跨模块raises子类型规则争议风险：错误×模块的交叉边界中最可能产生设计分歧的是"A模块import B模块的函数f() raises E1，在A模块内包装为g() raises E2，是否要求E1是E2的子集"。这在Rust没有对应机制、Zig的error set有不同语义，属于"无成熟最佳实践可直接抄"的场景（违反设计原则NP-1）。缓解：在阶段2启动前（不晚于阶段1第3人月）召开一次专门的设计评审会，要求提交至少3份备选语义方案，每份附：(a) 2个真实代码示例在该语义下的编译结果对比、(b) 对错误系统和模块系统各自的实现复杂度估算（行数量级）、(c) 3年内改语义的迁移成本评估。原则NP-1要求"选少数派方案必须写清迁移成本"。
- R5-测试偶发失败风险：阶段3并发系统引入TSan/确定性调度种子模式后，若存在数据竞争会在CI中产生偶发失败，排查成本极高。缓解：阶段2起，每个涉及并发×其他系统交叉的PR必须附加一个"3组不同SEED×100次循环"的压力测试脚本，作为PR门禁之一；AGENTS.md中verification-agent的checklist新增"并发相关PR必须通过3×SEED压力测试"条目。

**下游绑定约束**：

1. 阶段0完成前，禁止子代理提交任何向 checkSources() 主循环插入自定义检查pass的代码——所有临时语义检查代码必须写在binder层或通过Diagnostic Engine的\"临时lint码\"机制注入，阶段0完成后集中迁移到Checker的正式pass架构。违者在verification-agent环节标记为blocker。
2. 阶段1中，错误系统的error类型、模块系统的module/package作用域、并发系统的Sendable trait三者的Symbol注册路径必须统一走SymbolTable::registerSymbol()单一入口，不得直接操作symbolsByName HashMap。此约束在code-review中由task-router-agent强制检查。
3. FunctionTypeSymbol 结构在阶段1有且仅有三个子系统各自允许添加的字段：错误系统→error_union: TypeHandle，并发系统→async_flags: BitField<AsyncFlag>，模块系统→export_vis: Visibility。三方都不得擅自添加属于其他子系统领域的字段。
4. 诊断码号段永久分配并写入docs/spec/chapters/ZZ-diagnostic-codes.md：ZOM0xxx=Lexer，ZOM1xxx=Parser，ZOM2xxx=Binder，ZOM3xxx=Checker-General，ZOM4xxx=Error-System，ZOM5xxx=Module-System，ZOM6xxx=Type/Generics，ZOM7xxx=Concurrency-Safety，ZOM8xxx=Concurrency-Runtime，ZOM9xxx=Driver/Crate/Internal。任何子系统不得越界占用其他系统的码号区间。
5. 交叉边界API变更需经过\"双owner签字\"流程：例如错误系统想修改\"验证raises子集\"的接口签名，必须同时得到错误子系统owner和并发子系统owner的同意（因为并发系统的supervisor错误聚合也依赖此接口）；在当前无明确owner的阶段，由task-router-agent分别调用对应两个子agent做独立review，两个子agent都返回approve才允许合入。
6. 阶段2启动时，必须产出一份三系统交叉边界测试集（不少于15个测试文件），每个文件对应一个明确的交叉场景：例如\"跨模块spawn一个返回raises E的函数\"同时覆盖模块/并发/错误三者。此测试集作为阶段2和阶段3的核心回归门禁，任何PR破坏其中任一测试即视为blocker。"

**否决方案**：

- 方案A：三系统按审计报告逐条修复，无分层与阶段划分 — 直接从170条findings出发逐条修复会导致：(1) Checker空白这一前置依赖不解决，错误系统raises验证、模块系统跨模块可见性、并发系统Sendable检查三者在试图落地时会各自写入自己的"临时Checker代码"，产生3份重复且不一致的语义检查实现，未来合并成本>5000行；(2) 并发审计高优先级第18条「async与错误系统raises语法交互未定义」在无阶段2交叉契约设计时必然被各自误解——错误系统实现者会认为"并发的raise传播是并发子系统的事"，并发实现者会认为"raises本身是错误系统的责任"，最终产生悬空语义；(3) 170条findings中约28条是交叉边界问题，无顶层分界会每条都需要跨子代理协调，单协调成本约增加1.5人月。

- 方案B：优先完成并发系统（因为有1.0.0-rc1完整设计），再补错误与模块 — 违反"基础设施先于特性"的工程顺序：(1) 并发系统的7项基石决策中4项依赖Checker（Sendable/Shared/Linear/NoInternalMutability四个trait的静态检查、spawn边界门控、跨suspend锁守卫检查ZOM8006），Checker空白则并发系统的18/20编译期陷阱全部无法落地，只能退化到运行时TSan检查——这等于并发设计文档中最核心的编译期安全承诺全部无法兑现，交付的是"有语法但无安全门控"的Go式并发，与ZOM定位为Rust级系统语言的目标矛盾；(2) 并发设计中SystemError枚举（Cancelled/Timeout/Panic/Poisoned/ScopeAbandoned/DoublePanic）6个变体全部是错误系统领域的类型，若错误系统的error/raises/union模型未定，并发系统只能先用"临时i32错误码

- 方案D：三系统各建独立团队并行推进，在"最终合入时"再解决交叉问题 — 三个系统共用的代码位置存在冲突点：(1) 三处都会修改checker/checker.cc的TypeChecker::process()主循环——错误系统要插入raises-clause验证pass、模块系统要插入visibility-check pass、并发系统要插入sendable-boundary/suspend-liveness pass，三方并行修改同一段50行代码的协调成本远超串行成本；(2) 三处都会修改FunctionTypeSymbol结构——错误系统要加error_union字段、并发系统要加is_async/cancel_scope字段、模块系统要加export_visibility字段，三方对同一结构的layout定义不一致将导致至少2次破坏性重构；(3) Diagnostic Engine三处都要新增ZOMxxxx码，若没有中央Registry先建立（阶段0任务

---

