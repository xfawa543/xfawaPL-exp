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
| `wrong <cond>` | 反事实守卫：条件为真时把参与值最小扰动到假（整数 +1 起、浮点 1% 步长、bool 翻转、关系边界直跳；派生链重算），8 次失败打印「无处可逃」并 exit(1)；守卫在赋值后持久复查 | [wrong.md](wrong.md) |
| `paradox x` | 改变过去：下游因果链重新验证失败，受影响变量保留原值并成为 GHOST（重合论→幽灵论） | [paradox.md](paradox.md) |
| `pinocchio (P) { } else { } limit: N` | 自指命题：反复"用当前状态求值 P → 执行 then/else → 比对 P 引用的变量"；状态不再变化 → 稳定（stable true/false）、真值连续交替 → 振荡、超过 limit 轮 → 无解，三种结果必终止输出 | [pinocchio.md](pinocchio.md) |
| `deja x` | 偷未来：把 `x` 的第一个常量未来赋值提前可见；非常量/控制流内/跨函数时保持常规语义 + 警告 | [deja.md](deja.md) |
| `dual x` / `dual x = v` | 双生：把一个存在的变量/值分裂成两个独立存在的自身——原身 `x[0]`（=裸 `x`）与分身 `x[1]` 同值出生，之后各自独立读写演化；仅限数值/布尔基类型 | [dual.md](dual.md) |
| `disposable fn f() { ... }` / `disposable x = v` / `disposable[N] x = v` | 一次性：函数只能调用一次（再次调用报"不可用"）；变量的赋值是一层一次性覆盖值——每次读取都消耗一次（一个表达式里的多次读取各消耗一次），同名层按 LIFO 栈叠加、耗尽后回退到旧层/普通值；普通赋值清掉全部一次性层；`N` 指定可读次数 | [disposable.md](disposable.md) |
| `¥[r] x = v` / `$[r] x = v` | 利息变量：只在读取时结算——先返回当前值再结算；同一表达式读几次就结算几次；`¥` 单利（每次固定 + 本金×利率）、`$` 复利（每次 x = x + x×利率）、默认利率 0.0001；同名规则按声明顺序叠加；整数初值自动转浮点 | [interest.md](interest.md) |
| `drift fn f/...` | 递归随机参数：drift 修饰的函数，其递归自调用在运行时按参数类型与范围现算随机实参（外部调用仍传原值）；内置 1000 层深度上限保证终止，可 `drift(depth: N)` 覆盖；字符串/类型不可推导参数 → 编译错误 | [drift.md](drift.md) |
| `fate x = v` | 设定命运值：反抗后被以"中点折半"的方式不完美拉回（永不直接等于命运值）；恢复史上最高结果成为持久底数，之后不可低于 | [fate.md](fate.md) |
| `envy a b` | 嫉妒比自己更好的变量：按差距强度选择 超越(+1)/成为(复制目标)/摧毁(目标被拉低)；自身不弱于目标则完全不动；不可比较维度 → warning + 原样 | [envy.md](envy.md) |
| `a ?! b` | 随机二元运算符：运行时从对 a、b 类型都合法且结果类型一致的一组运算（`+ - * / % && \|\|`，按类型分组）中随机挑一个真算；比较类因结果类型不同被排除；字符串等无候选组合 → `[?!]` warning + 退化为左操作数 | [randop.md](randop.md) |
| `value <expr>` | 保护前缀/void 守卫：照常求值 RHS 表达式；`value` 后跟无返回值函数调用 → 编译报错；`value` 同时保留为普通变量名，按下一记号自动区分 | [value.md](value.md) |
| `A fu*k B` | 列表合并：两边各取 floor(长度/2) 个互不重复随机元素拼成新列表（长度/类型编译期已知；int/long/string，float与非列表报错） | [fuk.md](fuk.md) |
| `valuable { }` / `A + B` / `A * n` | 代码即值：记下还没执行的代码；`+` 编译期拼接、`* n` 编译期重复；语句位置 `A` / `A B { }` 在**使用点**编译并运行；自由变量在使用点解析，创建处不捕获 | [valuable.md](valuable.md) |
| `call valuable A for x` | 把代码值 A 在这里编译运行一次，把 x 的最终值作为**普通值**交出来（使用点原来的 x 不被写回） | [valuable.md](valuable.md) |
| `inject v valuable A for x` | 记号级替换：把片段里的自由变量 x 换成 v，拼出新代码 | [valuable.md](valuable.md) |
| `fn f(A) { A }` + `f(valuable { ... })` | 代码值当参数：调用处静态特化出函数副本，代码操作的变量属于调用点（传入副本、返回写回）；代码参数不可赋值，且同一参数必须处处传代码 | [valuable.md](valuable.md) |
| `fn make() { return valuable { ... } }` | 代码工厂：函数体只构造代码、以 `return <代码值>` 结束，调用它得到代码而不是运行期结果；普通参数不能被返回的代码引用，工厂不可递归 | [valuable.md](valuable.md) |
| `zombie x` | 算术瘟疫：x 参与的 `+ - * / %` / 一元 `-` 结果取**最左感染变量**的当前值，表达式里每个变量永久变为感染者（值不变）；比较/逻辑不传染；不跨函数；感染值不能进列表 | [zombie.md](zombie.md) |
| `kill[N]` | 行级杀手：杀掉源码第 N 行，之后该行的所有执行（跨函数、循环迭代）永远跳过；越界行号无害忽略；无法撤销 | [kill.md](kill.md) |
| `censer["文本"]` / `censer[666]` | 内容熔断：之后控制台打印若与登记文本完全相等（strcmp），打印完立即 exit(0)；子串/前后缀不触发 | [censer.md](censer.md) |
| `noclip a` / `shuffleback` | 后室变量：`a` 跌出正常执行空间，存储值不变但每次读取不稳定——60% 正常 / 20% 引用失败（数值 0、字符串空）/ 20% 漂移（读到同类型其它后室变量的值）；`shuffleback` 让后室全体归位并两两 50% 交换重排；数值/布尔/标量字符串 | [noclip.md](noclip.md) |
| `try { } expect { }` | 编译器错误拦截：编译期捕获语义错误，执行 expect 块 | [try_expect.md](try_expect.md) |
| `sorry` | 向编译器道歉，使 rage 随机下降 delta∈[0,rage]（最低 0） | [rage.md](rage.md) |
| 编译器红温机制 | rage 持久化状态（0–5）+ 成功捕获升温 + sorry 随机降温；红温时的"每五行"强制规则见 try_expect.md；`xfawac rage` / `rage reset` | [rage.md](rage.md) |
| `deny x = v` / `deny "x + y = z"` | 不认账：变量形式不改写值，只让"承认"失效——之后 `x == v` 为假、`x != v` 为真（其它比较照常）；字符串形式收回一句 believe（该运算恢复按数学算）；没有对应 belief → 编译报错 | [deny.md](deny.md) |
| `regret "x + y = z"` / `regret all` | 后悔：收回一句 believe（恢复按数学算）/ 一次收回全部 believe；没有可后悔的 → 编译报错 | [regret.md](regret.md) |
| `doubt x` | 怀疑：只能写在 lie 块里，当场拆穿谎言——块内剩余读取恢复真实值；没有正在生效的谎言 → 编译报错 | [regret.md](regret.md) |
| `a <->[K] b` | 可逆反应：把差额/K 从一边拨到另一边，a + b 永远守恒（目标 = a 拿 T/(1+K)、余数留在 b）；平衡附近随机游走；仅限 int/long，同一变量两次报错 | [reaction.md](reaction.md) |
| `env 类型 参数名 { f, g }` / `env X = v` | 随身携带：给一组函数都加上携带参数（可同时出现在多个块里，携带多个值，按块顺序排在参数列表前面），组内互传时自动带上同名值、外部调用需显式交出；`env X = v` 把携带的值重新指向（只能写在携带它的函数里）；配合 `type` 声明的结构体可写 `发出者.行动点 -= 1` 与 `血量 -= 实际伤害`（无同名局部变量时的隐式 this）；函数不存在/类型冲突/参数位置不对/字段歧义 → 编译报错 | [env.md](env.md) |
| `type 名字 { int 字段, ... }` / `名字 变量 = { 值, ... }` | 结构体：程序或模块级声明字段（int/long/float/bool/string），按字段顺序创建；变量是引用语义，成员写入对外部可见，`a = b` 为重新指向 | [env.md](env.md) |

## 使用注意

- 这些都是实验语法，行为可能会变化。
- 有些语法是专门针对“视频演示”设计的，也许不会留着正式版本。
- 如果你发现了 bug，记下最小复现示例，然后试着用现有语法绕过或简化，以便能够在仓库上重现问题。