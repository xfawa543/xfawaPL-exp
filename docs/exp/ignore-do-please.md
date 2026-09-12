# ignore / do / please

三个**句子抽象词**：写在一条语句前，改变它的执行方式。

## 语法

```xf
ignore.print("Hello")
do.print("Hello")
please.print("Hello")
```

都是 `关键字.语句` 的形式。

## 各自效果

### ignore.stmt

完全不执行内部语句，像没写一样。

```xf
print("first")
ignore.print("second")
print("third")
```

输出：

```
first
third
```

（`second` 没有输出，被忽略了。）

### do.stmt

强制「直接执行」，两层意思：

1. **逃生 `un`**：哪怕当前处于 `un print` 这样的禁用状态，`do.print` 也照样执行。

```xf
un print
print("A")
do.print("B")
print("C")
```

输出就只有 `B`。

2. **忽略外层条件/循环**：`do` 不管外围的 `if`/`while`/`loop` 条件是否成立，直接把内部语句提升到分支之前、无条件执行一次（同时从分支中移除）：

```xf
int a = 10
if a == 0 {
    do.print("我free了！")
}
```

虽然 `a == 0` 为假，程序依然输出 `我free了！`。

### please.stmt

先打印 `thank you!`，然后执行内部的语句（`please` 后面可以接任意语句，不光 `print`）。

```xf
please.print("Hello")
```

输出：

```
thank you!
Hello
```

`please.` 是纯语句修饰符，只影响这条语句的运行行为。

## 注意事项

- 这三个修饰符只对**紧跟后面的一条语句**生效，不能覆盖整段。
- `please.` 后可以跟任意语句（`print`、`sleep`、`sorry`……）。
- `ignore` 和 `do` 如果同一个语法：[ignore.do.xxx] 是不可以的（你只需要选一个）并且 lie 等写出来会刺激编译系统但不破坏到运行结果

## 测试

- `tests/exp/test_ignore.xf`
- `tests/exp/test_do.xf`
- `tests/exp/test_please.xf`