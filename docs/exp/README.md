# xfawa-exp 实验语法手册

这个目录是 xfawa-exp 引入的实验性语法（EXP）的用户手册。每一条语法都是给使用这门语言的开发者看的参考文档——教你这些功能怎么用、效果是什么、注意什么。

所有 EXP 都不是正式语言的一部分，这些是探索/演示用的扩展，主意在正式版里不保证保留。

## 语法列表（按字母序）

| 语法 | 说明 | 文档 |
|---|---|---|
| `boom` | 播放爆炸音效、打印 BOOM!!、正常退出 | [boom.md](boom.md) |
| `bsod` | 显示一个假蓝屏，2.5 秒后继续执行 | [bsod.md](bsod.md) |
| `believe "x + y = z"` | 让程序把一个等式当成真的（影响该运算的常量折叠） | [believe.md](believe.md) |
| `lie x = v { ... }` | 块内读 x 得到 v（谎言值），真实值不变 | [lie.md](lie.md) |
| `un print/boom/bsod` | 禁用某个语句，直到程序结束 | [un.md](un.md) |
| `!print(...)` | 单次绕过 `un print` 的禁用 | [un.md](un.md)（含在 un 的文档里） |
| `ignore.stmt` | 完全忽略这条语句 | [ignore-do-please.md](ignore-do-please.md) |
| `do.stmt` | 强制执行（不受 un 影响） | [ignore-do-please.md](ignore-do-please.md) |
| `please.stmt` | 先打印 thank you! 再执行（不改 rage） | [ignore-do-please.md](ignore-do-please.md) |
| 裸 `please` | 无运行时行为；仅红温时 rage -1 并满足"每五行一次 please"规则 | [try_expect.md](try_expect.md) |
| `shutup` | 从此刻开始关闭所有 warning（error 不变） | [shutup.md](shutup.md) |
| `...` | 随机挑一个安全的函数并真正调用它（参数按真实类型随机生成） | [ellipsis.md](ellipsis.md) |
| `sleep(秒数)` | 程序暂停指定的秒数（支持小数） | [sleep.md](sleep.md) |
| `// 语句` | 可执行注释（注释里真的是语句） | [executable-comments.md](executable-comments.md) |
| `// repeat: N` | 紧随的语句重复执行 N 次 | [repeat.md](repeat.md) |
| `--annotate` | 编译时自动给程序添加注释说明 | [annotate.md](annotate.md) |
| 自动修复 | 拼错的关键字会被检测出来，询问确认后自动修正 | [auto-fix.md](auto-fix.md) |
| `O` / `OO` / `1O` 等 | O 十进制位字面量（一种另类数字写法） | [O_LITERAL_EXP.md](O_LITERAL_EXP.md) |
| `wrath x = v` | 修改已经发生的历史，重算依赖状态 | [wrath.md](wrath.md) |
| `paradox x` | 改变过去：下游因果链重新验证失败，受影响变量保留原值并成为 GHOST（重合论→幽灵论） | [paradox.md](paradox.md) |
| `try { } expect { }` | 编译器错误拦截：编译期捕获语义错误，执行 expect 块 | [try_expect.md](try_expect.md) |
| `sorry` | 向编译器道歉，使 rage 随机下降 delta∈[0,rage]（最低 0） | [rage.md](rage.md) |
| 编译器红温机制 | rage 持久化状态（0–5）+ 成功捕获升温 + sorry 随机降温；红温时的"每五行"强制规则见 try_expect.md；`xfawac rage` / `rage reset` | [rage.md](rage.md) |

## 使用注意

- 这些都是实验语法，行为可能会变化。
- 有些语法是专门针对“视频演示”设计的，也许不会留着正式版本。
- 如果你发现了 bug，记下最小复现示例，然后试着用现有语法绕过或简化，以便能够在仓库上重现问题。