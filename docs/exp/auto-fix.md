# 自动修复（Auto Fix）

编译器主动发现你写了一个像关键字但拼错的地方，然后**先问你**，得到你的确认之后才把它修正。

## 场景

```xf
printt("Hello")
```

`printt` 不是任何合法的 keyword。编译器检测到它与 `print` 的编辑距离是 1，认为这是一个 typo。

编译时打印：

```
Unknown statement: printt
Did you mean: print?
Apply this fix? [y/N] 
```

- 输入 `y` 或 `Y` → 编译器把它改成 `print`，继续编译运行。
- 输入其他任何字符或直接回车 → 放弃修复，按原代码报错并退出。

## 最终效果

接受修复之后运行结果：

```
Hello
```

## 核心规则

- **永远不未经询问就改**。修复必须先问、由用户明确回答 `y/Y` 才能生效。
- 只对看起来像合法关键字、距离很近的标识符生效。随便打的汉字ID、项目常量、完全不相干的变量不会动。
- 目前只支持关键字 typo 修复，不支持更深层的类型/表达式错误。

## 测试

- `tests/exp/test_autofix.xf`：写 `printt` 并 testing。