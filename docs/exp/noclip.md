# noclip —— 后室变量（变量跌出正常执行空间）

`noclip x` 把变量 `x` 从正常的执行空间"撞"出去：它的**真实存储值不变**，但之后每次**读取**都不再稳定——读到的可能是原值，可能是空（引用失败），也可能是别的后室变量的值（漂移）。

```
int x = 7
noclip x
print(x)   // 7 | 7 | 0 | 7 | 0 | ... 每次读取掷骰
```

## 语法

```
noclip a           // 使变量 a 跌入后室
shuffleback        // 后室临时"全体返回并重排"：每对同型后室变量 50% 概率交换存储值
```

- `noclip` 只接受**单个标识符**；`noclip a, b`、块级 `noclip { ... }` 都不支持。
- `shuffleback` 是独立语句、不带参数。
- `noclip` 目标不存在 → 编译错误。

## 语义

### 后室读取骰（每次读取独立掷骰）

| 概率 | 结果 |
|---|---|
| 60% | **正常**：读到变量存储的真实值 |
| 20% | **引用失败 / 腐化**：数值类读到 0/0.0、布尔读到 false、字符串读到空串 `""` |
| 20% | **漂移**：读到另一个**同类型**后室变量的当前值；若没有同类后室变量，退化为腐化默认值 |

- 漂移候选只包括"同一函数作用域、同一切片类型（VarType 与 IR 分配类型一致）"的后室变量。
- 每次 `x` 在表达式里出现都会独立掷骰；`print(x + x)` 中两次 `x` 结果可以不同。
- 派生传播：`noclip base` 之后 `sum = base + 10` 的 `sum` 自然继承不稳定性（15 或 10）——不稳定只进不出的传染，`sum` 本身不是后室变量就保持生成后的值。

### shuffleback（全体返回、立即乱序）

- 把后室变量暂时当成"同一批返回"的载体，并按**两两一组的硬币**重新洗牌：
  每一对同类型后室变量掷一次公平硬币，50% 交换两者的存储值、50% 保持。
- 由于读本身不稳定，`shuffleback` 之后你**观察到的**是"重排 + 不稳定"的叠加：交叉值出现的概率显著上升。

### 资格与作用域

- 支持类型：`int` / `long` / `float` / `bool` 标量，以及**标量字符串**（`string s = "hi"` 那种非数组的字符串变量）。
- 数组、未声明变量不能 `noclip`。
- 后室状态按函数隔离：进入函数时清空、返回时恢复；按钮/事件处理函数同样各自隔离。

> 注意：`string s = "hello"`（显式写类型）在内部是数组字符串布局，但按"非数组"标量处理——`noclip` 的字符串资格只看它是不是数组变量，与写法无关。

## 示例

不稳定读取：

```
int x = 7
noclip x
print(x)   // 时而 7，时而 0
```

漂移（a 读到 b 的值）：

```
int a = 11
int b = 22
noclip a
noclip b
print(a)   // 11 或 0 或 22（漂移到 b）
print(b)   // 22 或 0 或 11（漂移到 a）
```

重排：

```
int a = 11
int b = 22
noclip a
noclip b
shuffleback          // 50% a↔b
print(a)
print(b)
```

> `shuffleback` 本身不带任何运行时输出。它改的是**存储值**；读取层的不确定性仍在。

## 与其它 EXP 的交集

- `noclip` 记的是"变量名"这一层，和 `fate` / `dual` / `disposable` / `interest` 等按各自管道叠加，互不清档。
- 表达式中多次读取各自掷骰，与 `interest` 的"每次读取结算"一样都是发生在读取路径上的行为，互不覆盖。

## 实现位置

- Lexer：`noclip` → `KEYWORD_NOCLIP`、`shuffleback` → `KEYWORD_SHUFFLEBACK`。
- AST：`NodeType::NOCLIP_STATEMENT`（存变量名）、`NodeType::SHUFFLEBACK_STATEMENT`。
- Parser：`noclip IDENTIFIER` → `NoclipStatement`；`shuffleback` → `ShufflebackStatement`；都进 `isStatementStart` 与自动修复候选。
- Codegen：
  - 编译期集合 `backroomVars`（每个函数入口清空、出口恢复）记录后室成员。
  - `codegen(VariableExpression*)` 先查后室：命中走 `codegenBackroomRead` —— 多块 PHI 模式：掷骰块 → 正常块(normBB)、腐化块(corrBB)、漂移块(driftBB) 三路汇入 mergeBB，每块拿同一类型值。
  - `codegen(NoclipStatement*)` 做存在性/可后室性检查后把名字加入 `backroomVars`。
  - `codegen(ShufflebackStatement*)` 对每对同型后室成员运行时掷硬币，50% 交换 alloca 内容（标量 string 走 `memcpy`）。

## 测试

- `tests/exp/test_noclip_basic.xf` — int 不稳定：`7` 与 `0` 都必须出现。
- `tests/exp/test_noclip_string.xf` — 字符串不稳定：`hello` 与空串都必须出现。
- `tests/exp/test_noclip_drift.xf` — 双后室变量交叉漂移：a 能读到 22、b 能读到 11。
- `tests/exp/test_noclip_shuffleback.xf` — 重排后 a 能看到 22、b 能看到 11。
- `tests/exp/test_noclip_long.xf` — long 不稳定。
- `tests/exp/test_noclip_mixed.xf` — 不稳定传播到派生变量（sum/k）与字符串。
- `tests/exp/test_noclip_error.xf` — 未定义变量的 `noclip zz` 编译报错。
- `tests/run_noclip_tests.ps1` — 统计式断言：每个测试编译后连跑 25 轮，按集合/计数不变量判定。
- `tests/exp/demo_noclip.xf` — 视频演示：六幕（跌出正常空间 7/0、字符串变空、双变量交叉漂移、shuffleback 重排、派生传染、浮点腐化），由 `tests/run_demo_videos.ps1` 多轮聚合断言。