# `disposable` —— 一次性：用过即碎，读完即耗

`disposable` 是一个**语句/修饰符**。它的核心语义只有一句话：

> **给一个函数"只能调用一次"，给一次变量赋值"只能用几次"，读完即耗。**

`disposable` 有两种身形：

- `disposable fn f() { ... }` —— **一次性函数**：第一次调用正常执行，之后再调用 → 运行时错误「函数不可用」并终止程序。
- `disposable x = v` / `disposable[N] x = v` —— **一次性变量层**：给 `x` 放上一层"一次性覆盖值"。每次**读取**都消耗一次（一个表达式里的多次读取，每一次各消耗一次）；层消耗完就消失，`x` 回到上一层/普通值；读到无可读（且原变量从未有普通值）→ 运行时错误「未定义/不可用」并终止程序。

## 一次性函数

```
disposable fn one_shot() {
    print(42)
}

one_shot()   // 42
one_shot()   // 错误：函数 one_shot 不可用（disposable 只能调用一次）→ 程序终止
```

- 只有 `disposable fn` 修饰过的函数才是一次性的；普通函数可以随便调用多少次。
- 「不可用」错误在**函数体入口**检查：第一次调用把函数置为"已用过"，第二次调用直接报错退出，函数体不执行。

## 一次性变量层：核心例子

```
a = 1                 // 普通值 = 1
disposable a = 2      // 一层一次性值 = 2（覆盖在上）
b = a + 1             // 读到 2 并消耗这层 → b = 3

disposable c = a + 1  // 读到的是回退后的 1 → c = 2
print(c)              // 2（并消耗 c 的这层）
print(c)              // 错误：变量 c 未定义/不可用（disposable 已消耗）→ 程序终止
```

- 每次读取都会**消耗**：`print(a + a)` 会把这一层连续用掉两次（第一次读完，第二次就落到下一层/普通值）。
- 同名多层一次性值按 **LIFO 栈**叠加，永远先读最新的一层：

```
disposable a = 4
disposable a = 3
disposable a = 2
print(a)   // 2  （读最上层并消耗）
print(a)   // 3  （消耗第四层后，回退到 3）
print(a)   // 4
print(a)   // 1  （三层全消耗 → 回退到普通值 1）
print(a)   // 1
```

## 指定可读次数：`disposable[N]`

```
disposable[5] a = 100
print(a) // 100
print(a) // 100
print(a) // 100
print(a) // 100
print(a) // 100
print(a) // 错误：未定义/不可用 → 程序终止
```

- `N` 必须是 `>= 1` 的整数字面量；`disposable[0]` → 编译错误。
- `disposable[2] a = v` 等价于连续推两层 `disposable a = v`。

## 普通赋值会清掉所有一次性层

```
disposable a = 2
disposable a = 3
a = 10
print(a) // 10 —— 普通赋值清空 a 的所有一次性层
print(a) // 10
```

## 先读后耗的内存模型

每次读取都是一次"拿走这一层然后把这层丢掉"的动作。上面的写法验证了同一次表达式里的多次读取：

```
a = 1
disposable a = 2
print(a + a) // = 3（第一次读走 2 并消耗，第二次落到普通值 1）
```

## 类型规则

- 支持 `int` / `long` / `float` / `bool`；`disposable x = 1.5`、`disposable flag = true` 均可。
- 同名的多层一次性层必须是**同一基类型**（整数系 / 浮点系可互相换宽度，但基类型不同 → 编译错误 `must keep the same base type`，和 `dual` 一致）。
- 字符串/数组等复合类型的一次性层 → 按该类型的存储约束处理（一次性读写走普通的变量存储通道）。

## 与现有变量的关系

- 没有普通值也能建一次性层（`disposable x = 5`，然后先读这层）：层读完、又从来没有普通值 → 运行时「未定义/不可用」错误。
- `disposable x = v` **不会**把它当成"声明"，所以后面出现的普通赋值 `x = w` 仍是普通赋值语义（还能借此清空一次性层）。

## 实现方式

- 纯 **LLVM 后端**实现，运行期维护每变量的"一次性层栈"：`top`（-1 = 空）+ 每层 `counts`（剩余次数）+ `values`（层值），单变量层数上限 **64**（满了新层覆盖最旧的头，`min(top+1, 63)`）。
- 读取走运行时循环：从栈顶往下找剩余次数 `> 0` 的层，先读值再减一（**先读后耗**）；找不到 → 回退到普通值 alloca；普通值也不存在 → 打印「未定义/不可用」并 `return 0` 终止。
- 一次性函数的「已用过」标记是模块级全局（`__xfawa_disp_<函数名>`），函数入口检查、第一次调用置 1。
- 栈与 `locals` 同生命周期：按函数隔离、按钮处理函数里同样保存/清理，不会跨函数泄漏。

## 编译错误一览

| 场景 | 错误信息关键词 |
|---|---|
| `disposable[0]` / 负 / 非数字 | `disposable read count must be >= 1` / `Expected a read-count number in 'disposable[n]'` |
| 漏掉 `=`（`disposable x 5`） | `Expected '=' after the disposable variable name` |
| `disposable` 后没跟 `fn`/变量 | `Expected 'fn' after 'disposable'` |
| 同名层基类型不同 | `must keep the same base type` |

## 与其它 EXP 的关系

- 与 `fate`（命运回拉）/ `deja`（偷未来）/ `wrath`（改写历史）无关：`disposable` 不碰时间线，只是把**当下的一次读写**变成一次性资源。
- 与 `dual`（分裂成两个存在）互补：`dual` 让一个存在"变多"，`disposable` 让一次可用性"有限"。

## 测试

- `tests/exp/test_disposable.xf` — d1 覆盖与回退（3）；d2 消耗后未定义（3/2/2 → 未定义终止）；d3 LIFO 栈（4/3/2/1/1）；d4 普通赋值清空（10/10)；d5 `disposable[5]` 可读 5 次（100×5）；d6 表达式内每读各耗一次（4）；d7 浮点（1.500000→0.500000→1.500000）；d8 布尔（1→0→1）；d9 循环里重推（1/2/3）。
- `tests/exp/test_disposable_undefined.xf` — 用户原例逐行复现：3 → 2 → 未定义终止。
- `tests/exp/test_disposable_fn.xf` — 一次性函数：首调 42，次调「不可用」终止。
- `tests/exp/test_disposable_fail_count.xf` / `test_disposable_fail_mismatch.xf` / `test_disposable_fail_noeq.xf` — 编译失败断言（count / 类型 / 缺 `=`）。
- `tests/run_disposable_tests.ps1` — 编译断言 + 运行时断言（d1–d9、undefined、fn）+ 3 个编译失败断言。
- `tests/exp/demo_disposable.xf` - 视频演示：七幕（读一层耗一层 / 表达式连读 / 多层 LIFO / 清层 / 限次 / 一次性函数 / 耗尽终止）。
