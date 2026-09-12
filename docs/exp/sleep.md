# sleep（暂停指定的秒数）

让程序在当前位置暂停指定的秒数再继续执行。单位是**秒**，可以传整数或小数（浮点）。

## 语法

```xf
sleep(秒数);
```

`秒数` 是任意数字表达式：

```xf
sleep(2);      // 暂停 2 秒
sleep(0.5);    // 暂停半秒
sleep(1.5);    // 暂停 1.5 秒
```

底层调用的是 Windows 的 `Sleep(毫秒)`，编译器自动把秒数 ×1000 再传入。

## 与其他 EXP 的配合

`sleep` 也可以被 `un` 禁用、被 `do.` 强制执行、被 `!` 单次绕过：

```xf
un sleep
sleep(5)      // 立即跳过，不等待（被 un 禁用）
!sleep(1)     // 单次强制等待 1 秒（绕过 un）
do.sleep(1)   // 强制执行（不受 un 影响）
```

## 边界行为

- `sleep(0)`：立即返回。
- 负数秒数：按 0 处理（不会变成几十天的等待）。
- 超大数字：作为 DWORD 毫秒传给系统，和系统限制一致。

## 示例

```xf
#test {
    fn main() {
        print("start")
        sleep(1)
        print("after-one-second")
    }
}
```

输出（打印之间隔了 1 秒）：

```
start
after-one-second
```

## 实现说明

- 词法：`sleep` 现在是保留字（关键字 `sleep`）。
- 语法：`Statement` 节点 `SleepStatement`，带一个表达式成员（秒数）。
- 生成：`codegen(SleepStatement)` 计算毫秒数（整数秒 ×1000 / 浮点秒 ×1000）后调用外部 `Sleep(DWORD)`；负数钳制为 0。

## 测试

- `tests/exp/test_sleep.xf`：整数秒、浮点秒、循环内多次 sleep、`un sleep` 生效。
- `tests/exp/test_sleep_override.xf`：`!sleep` 和 `do.sleep` 绕过 `un sleep` 的路径。