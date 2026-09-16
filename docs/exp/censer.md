# censer —— 内容熔断（打印到敏感内容即终止程序）

`censer[<文本>]` 登记一段"敏感内容"：之后程序**每打印一条输出**都和它精确比较，一旦完全相同，打印完这一条立即以退出码 0 **整个程序终止**：

```
censer["bomb"]
print("hi")        // 打印 hi
print("bomb")      // 打印 bomb 后立即退出（exit 0）
print("never")     // 永远不会打印
```

## 语法

```
censer["文本"]      // 字符串内容
censer[666]        // 数字内容（按十进制文本，如 "666"）
```

文本来自字符串字面量或十进制数字常量。

## 语义

- **精确匹配才熔断**：`censer["word"]` 不会熔断 `print("words")`、`print("a word")`，只有整条输出与登记文本**完全相等**才触发（逐字节 `strcmp`）。因此子串、前后缀、被空格包裹都不算。
- **打印完再退**：当次打印仍照常输出（内容可见），之后进程立刻以退出码 0 结束，后续语句全部不执行。
- **只针对控制台 print 输出线**：熔断挂在普通 `printf` 打印路径上（`print` 的默认控制台输出）；Windows 窗口模式（`--window` 之类自绘输出线）不参与。
- 登记的敏感内容在编译期收集；与打印内容比较发生在运行时每次打印后。多个 `censer` 可以重复登记（同一个或多个文本都查）。
- 数字内容按十进制文本比对（`censer[666]` ↔ `print(666)` 互相命中）；`print("666")` 与 `censer[666]` 视为同一文本，也会触发（同属十进制 "666"）。

## 实现位置

- Lexer：`censer` → `KEYWORD_CENSER`；Parser：`censer[...]` → `CenserStatement`（存文本，字符串取内容、数字取十进制文本）。
- AST：`NodeType::CENSER_STATEMENT`。
- Codegen：
  - `codegen(CenserStatement*)` 把登记文本压进 `censoredTexts`（模块级静态向量）。
  - `codegen(PrintStatement*)` 控制台分支里，在 `fflush` 之后：用同一格式（去掉换行、保留幽灵 `#`）把本次输出的内容 `snprintf` 进栈缓冲，逐个 `strcmp` 登记文本，命中则调用 `exit(0)`。

## 边界说明（已定）

- 只在"console 打印"这条路径生效；行内 `print` 的任何其他变体（`please.`/`do.`/`!` 前缀等）只要走同一 printf 分支同样生效。
- 熔断只做"打印即终止"，不限制其它 I/O、不改值。
- 文本精确相等才触发，意味着对"打印一模一样内容即收手"的语义负责，子串不触发（有意为之，测试覆盖）。

## 测试

- `tests/exp/test_censer.xf` — 子串不触发（words / a word / plain 照常打印），完全相等 `word` 触发并在其后不打印 `c3never`。
- `tests/exp/test_censer_num.xf` — `censer[666]`：c2a 打印、666 打印后立即退出，c2never 不打印。
- `tests/run_exp_new_tests.ps1` — 编译 + 退出码 0 + 逐行比对。
- `tests/exp/demo_censer.xf` - 视频演示：四幕（子串不触发 / 变量值不触发 / 精确命中立即熔断 / 熔断后静默）。
