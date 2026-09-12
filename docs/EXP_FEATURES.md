# xfawa-exp 实验语法总览（近期新增）

以下内容均为 xfawa-exp 的实验语法（EXP），不会影响正式 xfawa 编译出来的代码。

| 语法 | 效果 | 示例 |
|---|---|---|
| `boom` | 播一个爆炸音（Beep），打印 `BOOM!!`，然后程序正常退出 | `boom` |
| `bsod` | 弹出全屏假蓝屏 2.5 秒，结束后程序继续运行 | `bsod` |
| `believe "2 + 2 = 5"` | 让编译器把这个等式当真；表达式里 `2+2` 会按你给的值算 | `print(2 + 2)` 输出 5 |
| `lie x = v { ... }` | 块内读 `x` 得到 v，出块回到真实值；真实存储不被修改 | `lie answer = 42 { print(answer) }` 打印 42 |
| `un print` | 之后所有 `print()` 都不再输出；进程一直跑 | `un print` |
| `un boom` / `un bsod` | 同理禁止 `boom` / `bsod` | `un boom` |
| `!print(...)` | 单次绕过 `un print` 的禁用， 只作用于这一条语句 | `un print; !print("x")` 会打印 x |
| `ignore.stmt` | 整条语句不生成代码，不会被运行 | `ignore.print("hi")` 什么都不做 |
| `do.stmt` | 强制执行这条语句，哪怕当前有 `un` 也有效 | `do.print("hi")` 在 un 状态下也会打印 |
| `please.stmt` | 先打印 `thank you!`，再执行这条语句（不改 rage，也不算红温时的合规请字） | `please.print("hello")` |
| `shutup` | 输出 "o……o…ok"，然后静默所有后续 warning（error 仍然显示） | `shutup` |
| `...` | 随机执行 3 个无害演示语句之一，每次随机 | `...` |
| `// repeat: N` | 紧随其后的第一条语句被复制执行 N 次 | `// repeat: 3` 后的 `print("x")` 打 x 三遍 |
| 可执行注释 | 一段注释能解析为完整一条 xfawa 语句时，编译器会把它当成程序的一部分，插到注释所在行直接执行 | `// print("hi")` 会真的打印 hi |

## 编译器自带修正（Auto Fix）

当你写了一个看起来像关键字拼写错误的词，编译器会提示并询问：

```
Unknown statement: printt
Did you mean: print?
Apply this fix? [y/N]
```

- 回答 `y` / `Y` → 编译器把拼错的词改成建议词，继续编译。
- 回答任何其他内容（或回车）→ 保持原样，并报错。
- 编译器不会自己闷头把你的代码改完。

## 自动写注释（`--annotate`）

在编译时加 `--annotate`：

```
xfawac.exe my.xf --annotate
```

编译器会遍历 AST，给每个 EXP 语法添加 `// EXP: <名称> —— <说明>` 注释，写回源文件（UTF-8），同时也会往控制台输出同样内容。

目前覆盖的语法：

| 语法 | 注释内容（简要） |
|---|---|
| `boom` | 让程序播放爆炸效果后正常退出 |
| `bsod` | 显示一个受控假蓝屏后继续执行 |
| `believe` | 让程序相信一个原本错误的事实 |
| `lie` | 修改变量的被观察值（真实值不变） |
| `un` | 禁用某个语言行为 |
| `ignore` | 忽略该语句，不执行 |
| `do` | 强制执行该语句，不受 un 影响 |
| `please` | 裸请字：无运行时行为；仅红温（rage>=3）时 rage -1 并满足"每五行一次 please"规则 | `please` |
| `shutup` | 立即压制后续所有 warning |
| `...` | 随机执行一个允许调用的安全动作 |

## o-literal（十进制位字面量）

```
o / oo / ooo       → 10 / 100 / 1000
1o / 2o / 15o      → 10 / 20 / 150
1o + 1o            → 100（o 个数合并，非普通加法）
```

已记录在 `docs/exp/O_LITERAL_EXP.md`。
