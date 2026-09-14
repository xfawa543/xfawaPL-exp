# xfawa-exp 实验语法手册

这个目录是 xfawa-exp 引入的实验性语法（EXP）的用户手册。每一条语法都是给使用这门语言的开发者看的参考文档——教你这些功能怎么用、效果是什么、注意什么。

所有 EXP 都不是正式语言的一部分，这些是探索/演示用的扩展，主意在正式版里不保证保留。

## 语法列表（按字母序）

| 语法 | 说明 | 文档 |
|---|---|---|
| `boom` | 播放爆炸音效、打印 BOOM!!、正常退出 | [boom.md](boom.md) |
| `bsod` | 显示一个假蓝屏，2.5 秒后继续执行 | [bsod.md](bsod.md) |
| `believe "x + y = z"` | 让程序把一个等式当成真的（影响该运算的常量折叠） | [believe.md](believe.md) |
| `come 行号` / `come if(条件) 行号` | 跳转：执行线走到目标行时回到 come 位置重跑一段（构成循环）；`come if` 条件为真才跳、为假放行；目标必须是本函数内的普通语句行 | [come.md](come.md) |
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
| `pinocchio (P) { } else { } limit: N` | 自指命题：反复"用当前状态求值 P → 执行 then/else → 比对 P 引用的变量"；状态不再变化 → 稳定（stable true/false）、真值连续交替 → 振荡、超过 limit 轮 → 无解，三种结果必终止输出 | [pinocchio.md](pinocchio.md) |
| `deja x` | 偷未来：把 `x` 的第一个常量未来赋值提前可见；非常量/控制流内/跨函数时保持常规语义 + 警告 | [deja.md](deja.md) |
| `drift fn f/...` | 递归随机参数：drift 修饰的函数，其递归自调用在运行时按参数类型与范围现算随机实参（外部调用仍传原值）；内置 1000 层深度上限保证终止，可 `drift(depth: N)` 覆盖；字符串/类型不可推导参数 → 编译错误 | [drift.md](drift.md) |
| `fate x = v` | 设定命运值：反抗后被以"中点折半"的方式不完美拉回（永不直接等于命运值）；恢复史上最高结果成为持久底数，之后不可低于 | [fate.md](fate.md) |
| `envy a b` | 嫉妒比自己更好的变量：按差距强度选择 超越(+1)/成为(复制目标)/摧毁(目标被拉低)；自身不弱于目标则完全不动；不可比较维度 → warning + 原样 | [envy.md](envy.md) |
| `a ?! b` | 随机二元运算符：运行时从对 a、b 类型都合法且结果类型一致的一组运算（`+ - * / % && \|\|`，按类型分组）中随机挑一个真算；比较类因结果类型不同被排除；字符串等无候选组合 → `[?!]` warning + 退化为左操作数 | [randop.md](randop.md) |
| `try { } expect { }` | 编译器错误拦截：编译期捕获语义错误，执行 expect 块 | [try_expect.md](try_expect.md) |
| `sorry` | 向编译器道歉，使 rage 随机下降 delta∈[0,rage]（最低 0） | [rage.md](rage.md) |
| 编译器红温机制 | rage 持久化状态（0–5）+ 成功捕获升温 + sorry 随机降温；红温时的"每五行"强制规则见 try_expect.md；`xfawac rage` / `rage reset` | [rage.md](rage.md) |

## 使用注意

- 这些都是实验语法，行为可能会变化。
- 有些语法是专门针对“视频演示”设计的，也许不会留着正式版本。
- 如果你发现了 bug，记下最小复现示例，然后试着用现有语法绕过或简化，以便能够在仓库上重现问题。