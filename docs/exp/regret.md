# regret / doubt

后悔与怀疑——两条"收回"语句：`regret` 收回一句 `believe`（或全部），`doubt` 在 `lie` 块里当场拆穿谎言。

## regret

### 语法

```xf
regret "x + y = z"    # 收回这一句 believe
regret all            # 一次收回全部 believe
```

### 效果

```xf
believe "1 + 1 = 3"
believe "2 * 3 = 7"
print(1 + 1)          # 3：相信自己
print(2 * 3)          # 7

regret "1 + 1 = 3"
print(1 + 1)          # 2：这一句撤回了
print(2 * 3)          # 7：还相信别的

regret all
print(2 * 3)          # 6：全都撤回了
```

`regret "..."` 收回对应的一句 `believe`，该运算恢复按数学算。`regret all` 一次收回全部。没有可后悔的内容时（收回不存在的 belief），`regret` 是编译错误。

## doubt

### 语法

```xf
lie 变量名 = 假值 {
    # ...
    doubt 变量名
    # ...
}
```

### 效果

```xf
int real = 41

lie real = 42 {
    print(real)       # 42：此刻在撒谎
    doubt real
    print(real)       # 41：拆穿之后，块里剩下的都是真话
}

print(real)           # 41：谎言从来没有离开过这个块
```

`doubt` 只能写在 `lie` 块里。执行到 `doubt` 时，当前的谎言被当场拆穿：块内剩余的读取恢复真实值。没有正在生效的谎言可以怀疑时（`doubt` 写在 lie 块外，或该变量没被谎言覆盖），是编译错误。

## 规则

- `regret` 的对象是 `believe` 登记的等式；`doubt` 的对象是 `lie` 盖的谎言。
- `doubt` 是块级作用域内的"当场拆穿"：拆穿后，这个块里对该变量的后续读取都是真话。
- 嵌套 `lie` 时，`doubt` 拆穿的是当前生效的那层谎言。

## 注意事项

- `regret all` 会把之前所有 believe 一次清空，包括很多行以前的。
- `doubt` 拆穿谎言后无法重新"盖上"——同一个 lie 块里一个变量只能拆穿一次。

## 测试

对应测试文件：`tests/exp/test_regret_basic.xf`（一次收回一句 + regret all）、`tests/exp/test_doubt_basic.xf`（doubt 拆穿谎言）。

视频演示：`tests/exp/demo_deny_regret.xf`（与 deny 一起）。
