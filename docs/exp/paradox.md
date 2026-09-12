# paradox —— 祖父悖论（重合论 → 幽灵论）

`paradox` 表达"改变过去"后，无法重新建立因果链、但旧存在无法被因果回收的状态：**祖父悖论**。

```
a = 1
b = a + 1
c = b + 1

print(c)   // 3
paradox a
print(c)   // 3#
```

## 语法

```
paradox <变量名>
```

## 语义：两个连续阶段

`paradox x` = 改变过去。它不做任何"重跑程序"，而是对受影响变量按 **birth point（诞生顺序）** 做**局部因果重新验证**：

### 阶段一：重合论（重新验证）

`x` 的因果根源消失（"a 不存在"），于是 `x` 及所有**直接或间接依赖 `x`** 的变量，在其诞生位置被重新验证。在直线代码中，被摧毁的根源永远无法重新诞生：

```
a 不存在
  ↓
b = a + 1  无法重新建立 → 诞生失败
  ↓
c = b + 1  也无法重新建立 → 诞生失败
```

### 阶段二：幽灵论（GHOST）

旧时间线里 `b`、`c` 的**值已经存在**。因果链失效不等于值消失，因果"找不到它们"，也就无法让它们消失。于是每个受影响变量：

- **值仍然保留**（可读、可参与运算，绝不是 0）；
- **失去因果来源** → 进入 `GHOST` 状态；
- 直接 `print` 幽灵变量 → 值后面追加 `#`（本 EXP 的可视化标记）。

即：`«祖父不存在，那么你也不会存在；你存在的意义被因果抛弃了。但因果已经找不到你，因此它也不可能让你消失。»`

## GHOST 状态的行为（确定性）

| 情况 | 行为 |
|---|---|
| 值 | **保留原值**，读取照常（`c + 1 == 4`） |
| 直接 `print(幽灵变量)` | 输出 `值#`（如 `3#`） |
| 参与 `if` 条件 | 用保留的真实值判断（`b > 0` 当 b=2 时为真） |
| 改为幽灵后面的**纯赋值** | 结果变量同样诞生为 GHOST（方案 B 传播） |
| 赋值 RHS 含**函数调用** | 边界：结果变量为 NORMAL（幽灵不穿过未知函数） |
| 对幽灵变量**显式重新赋值** | 获得新的因果来源 → 恢复 NORMAL |
| 再次 `paradox`（已 GHOST） | 幂等，无额外效果 |

## GHOST 传播（方案 B：ghost → ghost）

传播的是**因果状态**，不是任何特殊数值（绝不把 `c` 变成 0 再让 `d=1`）：

```
a = 1
b = a + 1      // b 依赖 a
c = b + 1      // c 依赖 b

paradox a

d = c + 1      // d 读取幽灵 c → 诞生为 GHOST
e = d + 1      // e 读取幽灵 d → GHOST

print(c)   // 3#
print(d)   // 4#   （值照常计算 = 3 + 1）
print(e)   // 5#
```

意义：`«一个失去因果来源的存在，继续影响后来由它产生的存在。»`

## 例子

```
// 无关变量不受影响（严格依赖闭包）
a = 1
b = a + 1
x = 100
y = x + 1

paradox a

print(b)   // 2#
print(y)   // 101   （x → y 完全无关，NORMAL）
```

```
// 多级依赖全部幽灵、值全保留
a = 1
b = a + 1
c = b + 1
d = c + 1
e = d + 1

paradox a

print(b)   // 2#
print(c)   // 3#
print(d)   // 4#
print(e)   // 5#
```

```
// 无依赖：被 paradox 的变量自身成为 GHOST
a = 123
paradox a
print(a)   // 123#
```

## 副作用：绝不重复执行

`paradox` 是**局部因果重新验证**，不是重跑程序：

- `print("Hello")` 之前的输出不会重放；
- 对 impure RHS（如含函数调用）**不重放、不重复副作用、不伪造结果**；
- 受影响变量按幽灵语义处理（值保留，状态 GHOST）。

```
fn tick() { print("TICK"); return 1 }

print("Hello")        // 出现一次
a = 1
b = a + tick()        // TICK 出现一次（正常执行）
paradox a             // 不会再次执行 tick()
print(b)              // TICK 只出现一次；b 是 GHOST → 2#
```

## 与 wrath 的关系

`wrath` = 改变过去 + **下游因果重新建立成功**；`paradox` = 改变过去 + **下游因果无法重新建立**，旧值无法被因果回收 → GHOST。二者是同一个因果系统的不同结果，但实现上保持独立，`paradox` 不复制 `wrath` 的 RHS 重放代码，因此不继承 wrath 对 impure RHS 可能出现的副作用重复问题。

```
x = 1
y = x + 10
wrath x = 5
print(y)   // 15
paradox x
print(y)   // 15#
```

## 实现位置

- Lexer / Parser：不变（`paradox x` 仍是 `ParadoxStatement`，关键字已存在）。
- AST：新增 `GhostExpression(name)` 节点（`NodeType::GHOST_EXPRESSION`，`toString()` → `GHOST`）。
- 变换：`applyWrathParadoxTransform` 针对 `paradox` 走全新分支——用 `ghost` 集合替代旧的 PARADOX 中毒集合；不再改写读取；幽灵直接 `print` 时替换为 `GhostExpression`。
- Codegen：`GhostExpression` 读取**变量原本的 alloca**（值照样出来），`print` 检测到直接打印幽灵时在格式串里加 `#`。旧的 `ParadoxExpression`（求值 0 / 打印 PARADOX）保留但不再是可达状态。

## 目前限制（已知）

- 依赖追踪仅覆盖**直线代码**（与 `wrath` 相同），控制流块内使用**全新历史**递归处理（块内不继承外层幽灵状态；外层幽灵值仍可按真实值读取）。
- **函数边界**：ghost 不自动穿过未知函数调用（调用 RHS 的赋值产物为 NORMAL）。
- **数组 / Loop / `do.` / `ignore` / `please` / `lie` 容器 / for-loop body**：幽灵追踪不做跨容器传播（保持原行为），幽灵值本身仍可读取。
- 显式重新赋值可恢复单个变量为 NORMAL，但**不会**级联恢复其已幽灵的下游（记录为已知取舍）。
- 窗口（`#xfw`）打印的幽灵标记同样生效（字符串/数字缓冲统一追加 `#`）。

## 测试

- `tests/exp/test_paradox.xf` — 核心祖父悖论、无依赖、无关变量、多级依赖、重复 paradox 幂等、与 wrath 叠加、重新赋值恢复、幽灵值参与 if。
- `tests/exp/test_paradox_ghost.xf` — 方案 B 传播（3#/4#/5#）、函数边界、（主 chain 外）无关链正常。
- `tests/exp/test_paradox_side.xf` — 副作用不重复、`Hello` 只出现一次。