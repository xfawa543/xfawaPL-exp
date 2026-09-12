# bsod

让程序展示一个**假的 Windows 蓝屏**（全屏蓝色窗口），停留 2.5 秒后自动恢复。

## 语法

```xf
bsod
```

## 效果

程序运行到 `bsod` 时，会弹出一个全屏蓝色窗口，上面写着类似：

```
:(
Your xfawa program ran into a problem and needs to restart.

Stop code: EXP_BSOD_0x7E3F
```

2.5 秒后窗口关闭，程序照常继续执行。

## 示例

```xf
print("before")
bsod
print("after")
```

你会看到 `before`、一个闪过的蓝屏、然后 `after`。

## 注意事项

- 这只是一个看起来像蓝屏的窗口——**它是安全的**，不会真的崩溃，不会影响系统，2.5 秒后自动消失。
- 在没有桌面的环境（比如某些 CI）中，蓝屏弹不出来，程序会退化成一声蜂鸣，不会出错。
- 可以用 `un bsod` 禁用这条语法。