# `come` —— 把执行线"拎起来丢回去"（跳转）

`come <行号>` 是**跳转语句**：它让程序的执行线在走到**源码某一行**时不再前进，而是**回到 `come` 所在的位置**，把 `come` 之后到那一行之间的代码再跑一遍。`come` 和它的目标行拼在一起，就是一条**循环**。

```
#test {
    fn main() {
        t = 3
        print(t)   // 3
        come 8     // ← 执行线想继续往前？先回去重跑
        t = t - 1
        if t < 0 { boom }
        print(t)   // ← 第 8 行：这里是"回跳点"
    }
}
// 输出：3  2  1  0  BOOM!!
```

## 语法

```
come <行号>                    // 无条件跳转
come if(<条件>) <行号>         // 条件为真才跳转
```

- `<行号>` 是**源码里的物理行号**（从 1 数起），指向当前函数体内的一条语句。
- `<条件>` 是任意布尔/数值表达式（如 `counter < 4`）。

## 无条件 `come N`：一条循环

`come N` 的语义是：**执行线每次来到第 N 行的那条语句时，先被送回 `come` 所在位置，重新把 come 之后到第 N 行之间的语句跑一遍（第 N 行本身也每次都执行）**。

- 所以 `come` + 目标行 = 循环：`come N` 之前的代码只跑一次，`come N` 之后的代码反复重跑。
- **无条件 come 是无限循环**——循环体内必须想办法叫停，否则停不下来。可以用 `boom`（爆炸退出）、`return` 等。

```
print(n)        // 3   ← 只在进来时打印一次
come 8          // ← 回来
n = n - 1
if n < 0 { boom }   // ← 叫停点
print(n)        // 8  ← 第 8 行：循环的"最后一步"，跑完就回去
```

## 有条件 `come if(条件) N`：可以退出的循环

`come if(cond) N` 给"到达第 N 行"这个动作加了判断：

- **条件为真** → 回到 `come` 位置重跑（和上面一样）；
- **条件为假** → **放行**，跳过跳转，正常继续执行第 N 行之后的代码。

条件**每次到达第 N 行时都会重新判断**，所以循环会随着状态变化自然地停下来：

```
counter = 1
come if(counter < 4) 14   // ← 每次都问"还没到 4 吗？"
counter = counter + 1
print(counter)            // counter 变成 2、3、4 …
print(counter)            // 14 ← 第 14 行：到这里问一次
print("--")
// 输出：2  2  3  3  4  4  --
// counter 到 4 时条件为假，放行继续，程序正常结束
```

条件从一开始就不成立也一样——直接放行，一次都不跳。

## 目标行规则

`come` 的 `<行号>` 必须满足：

- 是**当前函数体内**的一条**语句**（不是空行、不是注释、不是 `}`）。
- 目标行本身**不能是** `come`、`return`、`break`、`boom` 语句。
- 不能跳到**别的函数**里的行（跨函数 come 禁止）。
- 目标行必须**真的会被执行**：如果它永远执行不到（例如在 `while(0)` 里），编译器会报错。
- 前向（目标在 come 之后）和后向（目标在 come 之前）都能跳。
- 同一行有多个 come 指向同一目标时，以**行号最小的 come** 生效。

## 不能放在哪里

`come` **不能放在这些"独立代码单元"里**：`loop {}` 块、按钮（`button`）、窗口（`window`）循环体、或嵌套函数体。原因很简单——它们各自有独立的执行现场，跳转不能跨"单元"乱飞。普通函数体、`if`、`while`、`for in` 里可以正常使用（`while` 里的 come 属于同一单元）。

## 编译错误一览

| 场景 | 错误信息关键词 |
|---|---|
| 目标行在当前函数里没有语句（空行/注释/`}`） | `contains no statement in this function` |
| 目标行在另一个函数里 | `lies inside another function (cross-function come)` |
| 目标行是 `return`/`break`/`boom` | `is a return/break/boom statement and cannot be jumped to` |
| 目标行本身是 `come` | `is itself a come statement` |
| 目标行永远不会被生成执行（如 `while(0)` 内） | `is never executed, so no come jump could be created` |
| `come` 写在 loop/按钮/窗口/嵌套函数里 | `come cannot be used inside a loop{} / button / window / nested-function body` |
| `come` 后没跟行号 | `Expected a line number after 'come'` |
| 行号 < 1 | `come: invalid target line number 0 (line numbers start at 1)` |
| `come if` 缺右括号 | `Expected ')' after come condition` |

## 与其它 EXP 的关系

- `come if` 的条件在**每次到达目标行时**实时求值（本质是运行时循环判断），与 `if`/`while` 的条件语义一致（数值按非 0 即真处理）。
- 与 `deja`/`wrath`/`paradox`（改写未来/过去）不同，`come` 只改**执行流**，不改数据、不改历史。

## 目前限制（已知）

- 行号必须硬编码物理行号：改动源码（加注释、空行）后行号会变，需要同步修改所有 `come`。
- 无条件 `come` 若没有 `boom`/`return` 兜底就是死循环（设计如此，无编译器警告）。
- 目标行不能是 `come`/`return`/`break`/`boom`；不能跨函数、跨 loop/按钮/窗口单元。

## 测试

- `tests/exp/demo_come.xf` — 视频演示：无条件 `come 15` + `boom` 叫停的"倒计时循环"（3 2 1 0 BOOM!!），一行代码讲清楚 come=循环。
- `tests/exp/demo_come_if.xf` — 视频演示：`come if` 条件变假循环自然退出（2 2 3 3 4 4）+ 恒假条件直接放行。
- 行为回归样例（已存在）：`test_come_basic.xf`、`test_come_multi.xf`、`test_come_offset.xf`（注释/空行间的行号照样准）、`test_come_multi_block.xf`（`if` 块与 come 组合）、`test_come_in_while.xf`、`test_come_multi_function.xf`（跨函数调用点的布局）；编译失败样例 `test_come_err_*.xf` 覆盖上表各条。