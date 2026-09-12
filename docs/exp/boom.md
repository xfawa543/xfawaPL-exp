# boom

让程序播放一段爆炸音效，打印一行 `BOOM!!`，然后**正常退出**。

## 语法

```xf
boom
```

## 效果

```text
BOOM!!
```

然后程序退出，exit code 为 0。boom 之后的语句不会执行。

## 示例

```xf
print("start")
boom
print("never reached")
```

输出只有一行 `BOOM!!`。

## 注意事项

- boom 之后写的所有语句都会被忽略（不只是跳过，是根本不编译进去）。
- 音效是系统`Beep`模拟的爆炸音，在不同机器上效果可能不一样（有的机器没有蜂鸣器就只有终端提示音）。
- 可以用 `un boom` 禁用：`un boom` 之后的 `boom` 什么都不做。