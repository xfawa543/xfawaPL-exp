# `pinocchio` —— 自指命题（匹诺曹悖论）

`pinocchio` 是一个**语句**：给它一个**命题**（判断真/假的表达式），它把命题当做一个"自指真相"反复检验。每个回合：

1. 用**当前状态**求值命题 → 得到真/假；
2. 命题为真执行 `then` 块、为假执行 `else` 块（块里可以改变状态）；
3. 检查命题读到的变量有没有变化——没变，命题就"自洽"了，输出稳定结论并结束；变了就再试一轮。

它模拟的就是匹诺曹悖论：**"我的鼻子会变长"**。命题的真假决定"说谎"（鼻子变长/状态变化），状态变化又反过来改变下一轮命题的真假。和 `paradox`（改写过去/幽灵论）不同，`pinocchio` 关心的是**当下的反馈环**会稳定下来、永远振荡、还是死循环。

```
x = 0
pinocchio (x < 3) {
    x = x + 1
    print(x)
}
// 输出：1  2  3  [pinocchio] stable false
```

## 语法

```
pinocchio (<命题>) { then-块 }                    // 只有 then
pinocchio (<命题>) { then-块 } else { else-块 }    // 可带 else
pinocchio (<命题>) { then-块 } else { else-块 } limit: N   // 可带最大轮数
pinocchio <命题> { then-块 }                      // 括号可省（同 if）
```

- `<命题>` 是任意布尔/数值表达式：`x == 3`、`x >= 0`、`n`（数值本身的真值性）。
- `then`/`else` 块是普通语句块，可写任意语句（赋值、print、函数调用……）。
- `else` 块可省略；省略时不写任何东西等于"假时什么都不做"。
- `limit: N` 覆盖默认的 **1000** 轮上限；`N` 必须是 `>= 1` 的整数，写 `limit: 0` 或负数 → 编译错误。

## 一轮的执行（也是最关键的读法）

> **命题在每轮一开始，用当时的变量值求值；为真 → 这一轮 it 的鼻子会"变长"（执行 then）。**

- 第 i 轮开始时命题为 **真** → 执行 `then` 块（谎话说出口，鼻子开始变长）。
- 第 i 轮开始时命题为 **假** → 执行 `else` 块（或什么都不做）。
- 块里的赋值改变的是**下一轮**看到的状态。

例如上面 `x < 3`：x=0 → 真(增长)，x=1 → 真(增长)，x=2 → 真(增长) → x=3，第 4 轮开始时 `x < 3` 已是**假**，什么都不做，x 不再变化 → 命题自洽，稳定在 **假**。这就是"鼻子一直变长，直到有一天这句话不再是真话"。

## 稳定（三种结果之一）

每个回合结束时对比 **命题引用过的那些变量** 的快照：

- **一个都没变** → 命题自洽，输出 `[pinocchio] stable true`（命题最终为真）或 `[pinocchio] stable false`（命题最终为假），`pinocchio` 语句结束。
- 块里只写了**命题没引用的变量**，不算状态变化——命题看不到它，它并不影响命题真假，所以第 1 轮就能稳定。

```
x = 3
pinocchio (x == 3) { x = 3 }      // 命题真、x 未变 → stable true
x = 2
pinocchio (x == 3) { x = 3 }      // 命题假、无 else → stable false
y = 0
pinocchio (x == 3) { y = 99 }     // 只写了 y（P 不读 y）→ 立即 stable true，之后 y == 99
```

## 振荡（遭遇自我否定）

如果命题的真假**每轮都翻**，就是真正的"自 相 矛 盾"（这句话既真又假）：

```
x = 0
pinocchio (x >= 0) { x = x - 1 } else { x = x + 1 }
```

真值序列 T → F → T → F，连续两次完整交替（T,F,T,F）后判定为振荡，输出 `[pinocchio] oscillation` 并立刻终止（不会一直空转到 limit）。两轮完整交替是"最严格"的振荡证据：命题既不真也不假，是发散的。

## 无解（死循环 / 永不收敛）

如果命题始终为真、但状态**每轮都变**（例如 `x > 0` 且块里不断 `x = x + 1`），它永远不会稳定（命题总为真、翻转检测为 0），一直跑到 `limit` 轮（默认 1000）：

```
x = 1
pinocchio (x > 0) { x = x + 1 } limit: 5
// 输出：[pinocchio] no stable solution (max iterations 5)
```

输出 `[pinocchio] no stable solution (max iterations N)` 并结束。三种结果（稳定 / 振荡 / 无解）每一个都保证 `pinocchio` 语句必然终止，绝不死循环。

## 状态到底是哪些

稳定性检查的对象 = **命题中引用到的变量**（`x < 3` → `x`；`a >= b` → `a`、`b`）。只有这些变量的**新旧值对比**才算"状态变化"。其他变量在 then/else 里怎么改都不参与稳定性判定。这些变量用于快照/比较，要求必须是**标量数值/布尔**：

- `int` / `long` / `float` / `bool` 变量参与快照比较。
- **字符串**、**数组** 作命题引用的变量 → 编译错误（无法安全快照/比较）。
- 命题表达式本身是字符串字面量 / 数组 → 编译错误。

## 编译错误一览

| 场景 | 错误信息关键词 |
|---|---|
| 命题引用了字符串/数组变量 | `[pinocchio] proposition may only reference numeric/bool variables` |
| 命题本身是字符串/数组表达式 | `[pinocchio] proposition must be a bool/numeric expression` |
| `limit: N` 不是 `>= 1` 的整数 | `[pinocchio] limit must be a positive integer (>= 1)` |

## 与其它 EXP 的关系

- 与 `paradox`（改写过去、GHOST）/ `wrath`（重算历史）正交：`paradox` 破坏的是**因果链**，`pinocchio` 检测的是**当下反馈环**，互不干扰。
- 与 `fate` / `envy` / `deja` 一样，都是按语句执行性的 EXP；`pinocchio` 一轮轮重复执行 then/else，等价于一个受控制的自指循环。

## 目前限制（已知）

- 命题的求值必须**纯**（没有副作用）；在命题里调用带输出/改状态的函数会破坏快照语义（不被支持，也不诊断）。
- 只比较"命题引用的变量"在回合前后的值；不追踪函数内全局量的跨轮变化。
- `limit` 太小时（如 1、2）振荡还没触发就先到"无解"；这是有意设计——振荡需要至少两轮完整交替（4 轮）才能认定。
- 变量是函数局部的：跨函数共享状态不在稳定性检查的范围内。

## 测试

- `tests/exp/test_pinocchio.xf` — p1 命题为真且状态未变 → `stable true`；p2 命题为假无 else → `stable false`；p3 状态变化推动命题收敛（1 2 3 后 `stable false`）；p4 真值交替 T,F,T,F → `oscillation`；p5 `limit: 5` 下的 `no stable solution (max iterations 5)`；p6 写无关变量 y 不算状态变化（先后输出 `stable true` 与 99）。
- `tests/exp/test_pinocchio_fail_string_var.xf` — 命题引用字符串变量 → 编译失败（`numeric/bool variables`）。
- `tests/exp/test_pinocchio_fail_string_lit.xf` — 命题是字符串字面量 → 编译失败（`bool/numeric expression`）。
- `tests/exp/test_pinocchio_fail_limit0.xf` — `limit: 0` → 编译失败（`limit must be a positive integer`）。
- `tests/run_pinocchio_tests.ps1` — 编译断言 + 6 节运行时断言（逐行比对）+ 3 个编译失败断言。