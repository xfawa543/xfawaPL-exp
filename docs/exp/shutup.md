# shutup

让编译器在接下来的编译中闭嘴——忽略所有 warning。

## 语法

```xf
shutup
```

## 效果

编译器遇到 `shutup` 时会打印一行小心讨好的回应：

```
o……o…ok
```

然后从这个点之后，**所有的编译时间警告（warning）都不会再显示了**。错误（error）仍然会正常显示。

## 示例

```xf
x = 10          # 这里"没初始化声明好"会 warning
print(x)
shutup          # 之后没有 warning 了（但这个 warning 仍然有意义）
y = 20          # 这里也不会 warning
```

## 规则

- `shutup` 只是编译时行为，对生成程序没有影响。
- 它只能抑制 warning；erro r 会继续正常报错并终止编译。
- `shutup` 是**全局**的：从出现位置到文件结束，整个后续的 warning 都抑制。不能在中途恢复。

## 测试

- `tests/exp/test_shutup.xf`