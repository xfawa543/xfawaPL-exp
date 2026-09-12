# repeat 注释指令

一条被特别设计的注释，后面跟着的语句会被重复执行若干次。

## 语法

```xf
// repeat: N
语句
```

- `N` 是一个正整数，表示「把下面这句话重复执行几次」。
- 最大值 1000，超出会被编译器拒绝并报错。
- 只能作用于**紧随其后的一句话**。

## 性能示例

```xf
// repeat: 3
print("Hello")
```

结果：

```
Hello
Hello
Hello
```

```xf
print("A")
// repeat: 2
print("B")
```

输出：

```
A
B
B
```

## 边界

- 非整数字面量、负数、超大值都会报编译错误。
- `repeat:` 关键字不能用在普通注释中（`// repeat: 3 ...` 里只有这一行是 repeat 指令，其他行是普通注释不会被处理）。
- 只能作用于紧随其后的单条语句，不支持一个块。

## 测试

- `tests/exp/test3_repeat.xf`：
  - 测试：子查询测试 ※ `print("Hello") "Hello"` 是否重复三遍。
- `tests/exp/test4_multi.xf`：两个 repeat 指令分别作用于不同的语句。
- `tests/exp/test5_invalid.xf` — 非法数值的处理方式。
- `tests/exp/test6_huge.xf` — 指数达到上限的情况。