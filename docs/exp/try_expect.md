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