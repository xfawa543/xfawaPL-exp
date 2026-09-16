# `dual` —— 双生：一个独立存在，变成两个独立存在的自身

`dual` 是一个**语句**。它的核心语义只有一句话：

> **让一个原本独立存在的东西，变成两个独立存在的自身。**

它**不是** `if/else`（不是分支），**不是** `thread/fork`（不并发），**不是**把变量复制到另一个名字（复制是"造一个别人的克隆"，而这里的两个自己都还姓同一个名字）。`dual` 是把**一个变量的存储存在**在语义层面"一分为二"：分裂的瞬间，两个自己拥有完全相同的值；从下一个语句开始，它们各自独立继续运行、独立变化。

```
dual x = 10
print(x)     // 10   —— 原身
print(x[1])  // 10   —— 分身（分裂的瞬间与原身相同）
x = x + 1    // 这一刀只落在原身上
x[1] = x[1] + 5  // 这一刀只落在分身上
print(x)     // 11
print(x[1])  // 15
```

## 语法

```
dual <变量名> = <expr>
dual <变量名>
```

- `dual x = expr` —— 把值 `expr` 的存在一分为二：x 从此是"双生变量"，**原身**（`x` 或 `x[0]`）和**分身**（`x[1]`）出生时都等于 `expr`。
- `dual x`（不带 `=`）—— 分裂一个**已经存在**的变量：分身按原身**当前值**出生（一个存在变成两个相同的存在）。变量尚未声明/赋值时这样写 → 编译错误（没有东西可分裂）。

## 这两个自己怎么活着

分裂之后，`x` 名下有两个相互独立的存储槽。读写规则非常直接：

| 写法 | 作用 |
|---|---|
| 读 `x` | 读**原身**（分裂前的那个自己，行为完全不变） |
| 读 `x[0]` | 读**原身**（和 `x` 是同一个自己） |
| 读 `x[1]` | 读**分身**（第二个自己） |
| `x = v` | 只写**原身** |
| `x[0] = v` | 只写**原身** |
| `x[1] = v` | 只写**分身** |

所以下面的程序里，两个自己从"同值出生"走向"各自人生"：

```
dual t = 1
while (t < 4) {          // 读的是原身
    t = t + 1            // 原身 1→2→3→4
    t[1] = t[1] * 2      // 分身 1→2→4→8
    print(t)             // 2 3 4
    print(t[1])          // 2 4 8
}
```

`while` 的条件只看到了原身，所以循环在 `t` 到 4 时结束；分身早就翻到了 8。两个自己互不打扰——判断原身的循环不会因为分身的变化而失速。

## 类型

- 支持 `int` / `long` / `float` / `bool`（出生时也可直接给小数/布尔：`dual val = 1.5`、`dual flag = true`）。
- **字符串、数组不能分裂**：字符串/数组变量的"存在"是一个指针，一个指针造不出"另一个独立存在的自己" → 编译错误（`cannot be split into two independent existences`）。
- 对已存在的变量**重新分裂**（`dual t = val`）时，新值必须和原身同基类型（都是整数系 / 都是浮点系），否则编译错误（`must keep the same base type`）——分裂不是"改类型"。

## 下标规则

分身的寻址 `x[0]` / `x[1]` 里，下标必须是**编译期常量 0 或 1**：

- `0` = 原身，`1` = 分身；任何其他常量（`x[2] = ...`）→ 编译错误（`exactly two selves`）。
- 变量下标（`idx = 1; x[idx] = v`）→ 编译错误（同一个原因：`exactly two selves`）。双生变量只有两个确定的存在，不接受动态选身。

> 注意：`x[1] = v` 这种"带下标写数组元素"的语法在基础语言里本来是不支持的（会报 `Expected '=' after variable name`）。`dual` 只把**双生变量**的 `x[0]`/`x[1]` 地址化为有效的读写；对**普通数组元素赋值**仍然不支持，也不会意外开启——不破坏已有的数组语义。

## 实现方式

- 纯 **LLVM 后端**实现：`dual` 在原身的 alloca 之外为 `name.dual2` 再创建一个同类型 alloca 作为分身存储；`x` / `x[0]` 读写原身 alloca，`x[1]` 读写分身 alloca。
- 分裂状态按函数隔离（同一份 `locals` 生命周期清理），双生变量不会跨函数泄漏。
- 与 `come` 一样属于"语句型"EXP：`scanFunctionBody` 等 walker 对它不构成干扰。

## 编译错误一览

| 场景 | 错误信息关键词 |
|---|---|
| 分裂字符串/数组变量或字符串/数组值 | `[dual] ... cannot be split into two independent existences` |
| 对非双生变量写 `x[1] = v` | `[dual] "x" is not a dual variable` |
| 下标不是常量 0/1（`x[2]`、`x[i]`） | `[dual] "x" has exactly two selves` |
| `dual x`（不带 `=`）但 x 从未声明/赋值 | `dual: variable 'x' has no existence to split yet` |
| 重新分裂时基类型不同 | `[dual] "x" already exists as a different type` |

## 与其它 EXP 的关系

- 与 `deja`（偷未来）/ `fate`（命运）/ `wrath`（改写历史）无关：`dual` 不碰时间线，只把**当下的一个存在**变成两个并行的存在。
- 与 `paradox`（GHOST 幽灵论）不同：分身不是"更小/幽灵"的存在，它是一个**完整、独立、可继续演化**的自身。

## 测试

- `tests/exp/test_dual.xf` — d1 出生（10/10）；d2 独立演化（11/15）；d3 继续分歧并验证 `x[0]` == 原身（30/8/8）；d4 `dual x` 不带值分裂当前值后各自增长（7/7/8/10）；d5 浮点（1.5/1.5/4.0）；d6 布尔（1/1/0/1）。
- `tests/exp/test_dual_fail_string.xf` — 分裂字符串 → 编译失败（`cannot be split into two independent existences`）。
- `tests/exp/test_dual_fail_not_dual.xf` — 对普通变量 `a[1] = 9` → 编译失败（`is not a dual variable`）。
- `tests/exp/test_dual_fail_range.xf` — `a[2] = 9` → 编译失败（`exactly two selves`）。
- `tests/exp/test_dual_fail_undeclared.xf` — `dual nothing` → 编译失败（`has no existence to split yet`）。
- `tests/exp/test_dual_fail_mismatch.xf` — 重分裂类型不同 → 编译失败（`must keep the same base type`）。
- `tests/run_dual_tests.ps1` — 编译断言 + 6 节运行时断言（逐行比对）+ 5 个编译失败断言。
- `tests/exp/demo_dual.xf` - 视频演示：六幕（出生同值 / 独立演化 / x[0] 即原身 / 分裂既有变量 / 浮点 / 布尔）。
