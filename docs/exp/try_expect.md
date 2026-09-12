# try...expect — 纯编译期错误检查区

`try...expect` 是**纯编译期**的错误检查与拦截区：`try` 块里的代码**运行时永不执行**，编译器只检查其中的语句是否存在可捕获的错误（语义错误、关键字拼写错误）；一旦捕获，错误被吞掉（编译继续），由 `expect` 块执行。

```
try {
    <任意数量的检查语句，运行时都不执行>
} expect {
    <错误被拦截后执行的代码>
}
```

每次成功捕获都会触发编译器红温（`rage += 1`，上限 5）。

## 核心语义：try 永不执行

```
检查 try 块内的所有语句
   ↓
有可捕获错误？
   ├─ 是 → 拦截（吞掉错误，编译继续）→ 执行 expect
   └─ 否 → 检查通过 → try 块与 expect 块都不执行，直接继续
```

**`try` 块不是普通代码块。** 它是给编译器看的「检查区」：

```
try {
    print("hello")      // 这句不执行！
    x = 10              // 这句也不执行！
    someOperation()     // 错误 → try 拦截
} expect {
    print("caught")     // 执行
}
// 输出：caught
// print("hello") 永远不会出现在生成代码里
```

多条语句、空块都允许（空块 = 没东西可检查，直接通过）。

## 能拦截两类错误

1. **语义错误**：如调用不存在函数 `notFound()`、内置函数参数数量不对。
2. **解析期错误**：如关键字拼写错误 `prin`。解析阶段通过错误恢复把出错语句跳过（跳到下一行语句边界），并记录为「可拦截的解析错误」。

```
try {
    prin
    print
    notfound()
} expect {
    print("caught multi")
}
// 输出：caught multi
// prin（拼写错误）和 notfound()（未定义函数）都被检查、被拦截
```

## 程序员自己的可能出错操作

`try` 不负责定义什么是错误。你可以放入任何你认为可能出错的真实 xfawa 操作：

```
try {
    readFile("test.txt")
} expect {
    print("读取失败")
}
// 输出：读取失败
```

## notFound() — 通用错误入口

`notFound()` 表示「不指定具体错误，只要这里发生一个当前系统可以捕获的错误，就交给 expect」。它在本实现中就是一个调用不存在函数的调用，属于真实的「未定义函数」编译期错误，会被 try 拦截：

```
try {
    notFound()
} expect {
    print("caught")
}
// 输出：caught（并触发编译器红温 rage +1）
```

## 与「询问修复」机制的关系

项目存在的询问修复（关键字拼写修复：「Did you mean: xxx? Apply this fix? [y/N]」）**在 try 内不会弹窗**。`try` 内的关键字拼写错误被当作可拦截的解析错误交给 `try` 处理，绝不交互询问、绝不静默修改源代码。普通代码的询问修复行为完全不变。

## expect 内部错误

`expect` 块不能捕获自己的错误；其中的错误会继续向外层错误处理机制传播（交给外层 `try`，若无外层则编译失败）：

```
try {
    notFound()
} expect {
    notFound2()
}
// 结果：编译失败（Undefined function: notFound2），不会再次被自身捕获
```

## 嵌套

允许嵌套。外层 try 是检查区，整个区运行时都不执行；只有「外层可拦截的错误」才让其 expect 运行：

内层检查正常 + 外层检查正常 → 整个外层区不执行：

```
try {
    try { notFound() } expect { print("inner") }
    print("outer zone")
} expect { print("outer caught") }
print("outer continues")
// 输出：outer continues
// 内层 notFound 被内层 expect 挡住，外层区检查通过 → 整区不执行
```

内层 expect 自身出错 → 向外层传播，外层拦截：

```
try {
    try { notFound() } expect { notFoundInner() }
} expect { print("outer caught") }
// 输出：outer caught（内层 expect 错误被外层捕获）
```

## 与红温机制的联系

每次成功捕获 `rage += 1`（上限 5），详见 [rage.md](rage.md)。红温只在编译期发生（try 不再有运行时行为）。

- `try...expect` 捕获本身**不直接触发**红温强制规则：红温（`rage < 3`）之前，无论捕到多少次错误，编译器都不会做任何额外要求。
- 裸 `please`（红温时固定 `rage -= 1`）与 `sorry`（随机 `rage -= delta`，任何状态都有效）可以把 rage 降回 `rage < 3`，此时该函数不再处于红温态，强制规则随之解除——但下一次捕获仍会重新升温。**注意：`please.`（修饰符，如 `please.print(...)`）不改 rage、也不是合规字，只改变运行行为（thank you! + 执行），见 [ignore-do-please.md](ignore-do-please.md)。**

### 红温状态下的"每五行 please"强制规则

这是红温对编译结果**唯一**的语义影响，且只在红温（`rage >= 3`）状态下生效：

> 编译到任一函数结束时，编译器会按**源代码物理行号体系**扫描该函数体内所有语句：**连续 5 行代码内必须出现一次裸 `please`**，否则编译失败（强制错误）。

规则细节：

- **红温判定**：每个函数在**语义分析结束后**检查——用"该函数分析完时刻"的 `rage` 值判定。函数内部若有裸 `please`/`sorry` 把自己降回 `rage < 3`，检查时已不在红温态，规则不触发。
- **合规字**：只有**裸 `please`**（单独一行，无任何运行时行为）满足规则。`please.`（如 `please.print(...)`）只是一条普通代码行，会推进计数但**不会**重置窗口。
- **计数单位**：按**源代码物理行号**（语句起始行）计数，不是"每五个 AST 节点 / 每五条语句 / 每五个 token / 每五次循环"。连续的多行结构、空行、注释、与上一句同行的闭合花括号都不增加计数——只有语句起始物理行推进计数。
- **窗口重置**：裸 `please` 所在行就地重置计数，覆盖其后最多 4 行代码。
- **检查范围**：函数体内所有语句（含 `try`/`expect`、`if`/`else`、`while`、`lie`、`loop` 等块内的语句，嵌套同类节点不遗漏）；嵌套函数体由各自的 `analyzeFunction()` 独立检查。
- **违规后果**：强制错误 → 编译失败（exit 1），例如：

```
error: red-hot (rage 4/5): a `please` is required within every 5 code lines - missing at line 9
```

- **红温之前**：普通代码、`try...expect` 捕获、`sorry`、`please.` 等都**不**要求出现裸 `please`；未红温时写不写裸 `please` 纯属个人选择。
- 非红温程序里出现裸 `please` 只会收到一句"现在没红温，please 没用"的提示，不会被要求进入任何红温流程。

裸 `please` 的红温行为：仅红温（`rage >= 3`）时固定 `rage -= 1`（触发时 `rage >= 3`，下降后 `>= 2`，绝不产生负值），并输出一行 `[warning:syntax] [rage -1] ... rage = N/5`。

示例（红温到 3 后满足规则）：

```
please                        // 裸请字，重置窗口
try { notFound() } expect { }
try { notFound() } expect { }
try { notFound() } expect { }
please                        // 5 行内出现裸 please → 合法
print("ok")
```

红温后不满足规则 → 编译失败（没有裸 please 兜底，连续 5 个起始行无合规字）。

**实现位置**：`SemanticAnalyzer::checkRedHotPlease` / `checkRedHotPleaseStatement`（每个函数语义分析结束时、`rage >= 3` 时按物理行号扫描"每五行必须出现裸 please"；窗口只在 `PLEASE_NOTICE_STATEMENT` 处重置）；`PLEASE_STATEMENT`（please.）不参与。裸 please 的固定降温在 `PLEASE_NOTICE_STATEMENT` 的语义分析里、且仅当 `rage >= 3` 时生效。

## 代码生成行为

- **捕获到错误**（`trySucceeded = false`）：仅生成 `expect` 块代码，`try` 块完全不生成。
- **检查通过**（`trySucceeded = true`）：什么都不生成——`try` 块和 `expect` 块都不出现在程序中。

没有运行时异常机制：不再有全局异常标志、不再有除零/模零运行时拦截。除零/模零恢复为 LLVM 原生行为。

## 限制

- `try` 是纯编译期检查，运行时没有等价机制；不要期望 `try` 在运行时「拦截」除零等程序崩溃。
- 仅捕获 try 块内的可捕获错误，不引入跨函数的异常传播框架。
- 不实现异常类型匹配（符合「不要创建异常系统」）。
- 注：内置函数 `rnd()` 因与关键字 `and` 拼写相近（编辑距离 1）会在普通代码里触发拼写修复询问——这是既有的启发式行为，与 `try...expect` 无关。

## 测试

**合法用例（应编译并正确运行）：**
- `tests/exp/test_try.xf` — 一份完整覆盖：检查通过（try 与 expect 都不执行）、notFound() 被捕获、解析期拼写错误 prin 被拦截、用户示例（prin/print/notfound()）全部被拦截、try 内合法语句不执行（纯检查区）、嵌套整区不执行、嵌套 expect 错误向外传播、真实操作（readFile）、非 print 语句大杂烩（证明拦截不特判 print）

**非法用例（应被拒绝）：**
- `tests/exp/test_try_illegal_noexpect.xf` — 缺少 expect
- `tests/exp/test_try_illegal_nobody.xf` — try 后缺少 `{`
- `tests/exp/test_try_expect_error.xf` — expect 自身错误传播 → 编译失败

**红温"每五行 please"专项（`tests/exp/test_rage_please_t*.xf`，由 `tests/run_rage_please_tests.ps1` 驱动）：**
- `t1_basic` / `t7_catch_not_trigger`：未红温时普通 `try...expect` 不做任何 please 要求；
- `t2_redhot_no_please`（应编译失败）/ `t2_redhot_comply`（应编译成功）：红温时"每五行 please"启用、遵守与否（合规字是裸 `please`）；
- `t3_please_minus_one`：裸 please 精确 `rage -= 1`，且只在红温时生效（4 捕 → 4，3 裸 please → 停在 2，第三个无效；`rage` CLI 验证）；
- `t4_sorry_random`：sorry 随机下降且永不为负（5 捕 + 5 sorry 随机 [0,5] + 5 裸 please → 停在 [0,2]；`rage` CLI 用范围断言）；
- `t5_cross_a/b/c`：跨文件共享持久化 rage（A 升温 → B 承继红温 → C 用裸 please 从 4 降到 2，同一份状态停下）。