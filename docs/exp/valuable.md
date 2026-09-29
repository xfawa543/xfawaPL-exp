# valuable —— 值也可以是代码

`valuable { ... }` 把一段**还没执行的代码**当成一个值来用：这个值可以拼、可以复制、可以当参数传给
`call`，也可以在语句位置直接跑起来。

一句话：这里的高级语言是"写完就跑"的；`valuable` 让你把**代码本身**当成数据搬来搬去。

```
fn main() {
    x = 0
    A = valuable { x = 3 }
    A              // 这里才真的执行
    print(x)      // 3
}
```

## 语法

```
<代码值>   := valuable { 语句... }
            | <代码值> + <代码值>            // 编译期拼接
            | <代码值> * <整型常量>          // 编译期重复
            | inject <值> valuable <代码值> for <自由变量>
            | <代码变量>
            | <代码工厂>(实参...)             // 函数体只构造代码、return <代码值>
```

## 五条规则

1. **创建处什么都不捕获。** `valuable { ... }` 记下的是**记号**（token），不是变量值。
   片段里出现的名字（自由变量）要到**使用点**才去那个作用域里解析。创建处有什么、使用处没有 →
   编译错误。
2. **`+` 与 `*` 是编译期操作。** `A + B` 把两段代码的记号首尾相接；`A * 3` 把记号抄三遍；
   `A * 0` 得到一段空代码（合法，跑起来什么也不做）。运行期不会重复执行。
3. **只有语句位置会跑代码。** 写 `A` 就是在**这里**把 A 的代码编译进当前函数并执行一次；
   写 `A B` 就是把 A、B 的代码按从左到右拼起来跑一次；`A B { ... }` 还会把后面的原始块接到末尾。
4. **代码值是编译期实体。** 一次运行（`A`）是把这个片段单独编译成一个内部函数再调用，
   所以每个函数里同名代码值可以绑到不同片段（`func` 里的 `A` 与 `main` 里的 `A` 互不相干）。
5. **代码值不参与普通运算。** `print(A)` 打出字面量 `code`；`A + 6`、`A * n`（n 是变量）、
   `6 + A`、把代码值塞进普通变量等都是编译错误。

## 自由变量

片段里**读**的名字在使用点取值，取到的是**使用点那个作用域**的值；片段里**写**的名字
（`x = ...`）如果是使用点没有的，就成为这次运行的局部变量；如果是使用点已有的，写回使用点。

```
fn main() {
    x = 0
    arr = [1, 2, 3, 4]
    A = valuable { for item }   // 只有 for 头
    B = valuable { in arr }     // 只有 in 尾
    C = valuable { x + item }   // 读 x、读 item
    A B { x = C }               // 拼成 for item in arr { x = x + item }，跑一次
    print(x)                    // 10
}
```

这里 `arr`、`x`、`item` 全部在使用点解析：`item` 由 for 循环自己绑定，`arr`/`x` 取自 `main`。
把这段代码搬到另一个函数，`arr`、`x` 就按那个函数的作用域解析——代码本身没变。

## `call valuable A for x`

把 A 的代码在**这里**编译运行一次，并把 `x` 的最终值作为**普通值**交出来：

```
fn main() {
    x = 0
    A = valuable { x = 3 }
    y = call valuable A for x   // 跑一次，返回 3
    print(y)                    // 3
    print(x)                    // 0：x 是表达式的结果载体，使用点原来的 x 不被写回
}
```

`for x` 里的 `x` 从使用点当前的值开始（`x = x + 1` 这种先读后写是合法的），
返回的是跑完之后的 x。两个要点：

- 返回的是**普通值**，不能再当代码用（`B = call valuable A for x + C` 报错）；
- 目标变量在使用点不被改写，要留结果就自己接住：`y = call valuable A for x`。

## `inject`

`inject <值> valuable <代码值> for <自由变量>` 在**记号层面**把片段里那个自由变量替换成给定的值，
拼出一段新的代码：

```
fn main() {
    y = 0
    A = valuable { y = y + 10 }
    B = inject 5 valuable A for y   // 片段变成 y = 5 + 10
    B
    print(y)                       // 15
    C = inject y valuable A for y  // 片段变成 y = y + 10（普通值代入）
    C
    print(y)                       // 25
}
```

`for` 后面只能写片段**真的提到**的自由变量；写一个片段里根本没读的名字 → 编译错误
（"只能绑定自由变量"），这样能挡住拼错的模板参数。

## 作为函数参数

代码值可以直接当参数传：

```
fn run(A) {
    A
    A
}
fn main() {
    x = 0
    run(valuable { x = x + 5 })
    print(x)          // 10
}
```

代码没有运行期表示，所以它**不是**被"传进去"的，而是被**编译进去**的：调用处按实参把被调函数的
函数体记号复制一份、把参数出现的位置换成 `valuable { ... }`，再把这份副本当成另一个函数编译。
因此：

- 同一段代码从两个地方传进来，得到两份副本，各自在自己的使用点解析变量
  （`test_valuable_arg_scope.xf`：一个动 `x`，一个动 `y`）。
- 代码读写的变量属于**调用点**：调用处把该变量的一份副本交给副本，函数返回时再写回，
  和一次 `A` 运行的做法完全一样（`test_valuable_arg.xf` / `test_valuable_arg_mixed.xf`）。
- 普通参数照常传递，代码参数的位置传 `0`——函数体里已经不提这个名字了（`test_valuable_arg_mixed.xf`）。
- **一个参数要么是普通值、要么是代码值**：只要有一个调用处给它代码，它对所有调用处都是代码值参数，
  传普通值过去会报错。
- 代码参数不能被赋值（代码值没有存储），也不能和函数内部同名变量重名。

## 作为返回值：代码工厂

函数体只由"构造代码值"的语句组成、并且以 `return <代码值>` 结束时，它就是**代码工厂**：
调用它得到的不是运行期结果，而是它构造的那段代码。

```
fn make(unused) {
    return valuable { x = x + 1 y = y + 2 }
}
fn main() {
    x = 0
    y = 0
    A = make(10)      // A 是代码值，make 一次也没跑
    A                 // 这里才跑，跑在 main 的 x、y 上
    print(x)          // 1
    print(y)          // 2
}
```

- 工厂里可以先拼再返回：`B = valuable { ... }` 然后 `return B + C`（`test_valuable_factory_mix.xf`）。
- 工厂可以收代码值再原样交出：`fn twice(A) { return A + A }`（同一文件里既有工厂用法，
  也有当普通函数传参用法）。
- 工厂的**普通参数**只能是不被返回的代码提到的名字：代码在使用点编译，看不到工厂的运行期值。
- 工厂不能递归调用自己（没有运行期可以逐层展开），普通函数返回代码值也会报错。

## 编译期错误

| 写法 / 场景 | 报错 |
|---|---|
| `A`（片段读的 `nothing` 使用点没有） | 代码片段读取的自由变量 '...' 在使用点不存在（代码不捕获创建处的变量） |
| `x A`（x 不是代码值） | 拼接中只能使用代码值 |
| `A + 6` / `A * n`（n 是变量）/ `A + call ...` | '+' 连接的两个值必须都是代码值 / '*' 的重复次数必须是整型常量 |
| `A = 5`（把代码变量赋成普通值） | 'A' 已经是一个代码值，不能被赋成普通值 |
| `B = call valuable A for x` 之后再 `+ C` | call valuable ... for x 的结果是普通值，不能再当作代码使用 |
| `inject 5 valuable A for y`（片段不提 y） | inject 只能绑定自由变量 'y'（该代码片段里没有引用它） |
| `x = 1 + A`（把代码值当普通值用） | valuable: '+' 连接的两个值必须都是代码值 |
| `fn f(A) { A = 5 }` 后 `f(valuable {...})` | 代码值参数 'A' 不能被赋值（代码值没有运行期存储） |
| `f(valuable {...})` 与 `f(1)` 混用同一个 f | 函数 'f' 的参数 'A' 是代码值参数，所有调用都必须传入代码值 |
| `fn f(A) { t = 1  A }`，代码里也写 `t` | 传入的代码使用变量 't'，但函数 'f' 内部也有这个名字 |
| 工厂 `fn make(n) { return valuable { x = n } }` | 代码工厂 'make' 的参数 'n' 是运行期值，代码在使用点无法引用它 |
| 普通函数 `return <代码值>` | 函数 'make' 不是代码工厂，不能返回代码值 |
| `disposable fn f(A)` / `drift fn f(A)` 收到代码值 | valuable: disposable 函数 'f' 暂不支持代码值参数 / valuable: drift 函数 'f' 暂不支持代码值参数 |
| 自由名 `i` / `n` / `r` / `f` | 这些是保留记号，不能当变量名（与语言其余部分一致） |

## 边界 / 有意为之

- 一次运行是一个**独立的内部函数**：它有自己的局部变量，但会**闭包**住使用点传进来的变量指针，
  因此片段里的写回对使用点可见。运行内部的 `wrong` / `fate` / `zombie` / `interest` /
  `disposable` / `noclip` / `dual` 状态与使用点**互不干扰**（`codegen(Function*)` 的隔离规则）。
- 跨函数也是同一套办法：代码值参数得到一份**静态特化**的函数副本，使用点的变量按副本传入、
  返回时写回（`specializeCodeFunction` / `codeArgBindings`）。所以同一段代码从不同使用点传进来
  会得到不同的副本。
- 代码工厂是**内联**的：调用点在原地展开它返回的代码，因此工厂的普通参数只有在返回的代码
  完全不提它时才可以存在。
- `disposable` / `drift` 函数暂不支持代码值参数（这两种函数一个只能跑一次、一个参数是随机数，
  静态特化会把这两件事毁掉）。
- 记号拼接是**纯文本**层面的：`A * 2` 是把记号抄两遍（`x = 1 x = 1`），不是循环。
  片段里写 `for` 才是循环。
- 一个片段里可以引用另一个代码值：它在 `valuable` 存下来的时候就被展开成记号（同样不是延迟执行）。
- 实现位置：`src/llvm/xfawa_llvm_codegen.cpp`（`internFragment` / `expandCodeRefs` /
  `resolveCodeFragment` / `buildInjectFragment` / `emitValuableRun` / `collectFragmentRefs` /
  `collectCodeParamIndices` / `substituteCodeParams` / `specializeCodeFunction` /
  `codegenCodeArgumentCall` / `resolveFactoryCall` / `isCodeFactoryFunction`），
  语法在 `src/parser/parser.cpp`（`captureBraceBlock()` / `parseValuableUseStatement()`），
  AST 在 `src/include/xfawa_ast.h`（`Function::bodyTokens`）；测试见 `tests/exp/test_valuable_*.xf`
  （`pwsh -File tests/run_valuable_tests.ps1`）。
- `tests/exp/demo_valuable.xf` — 视频演示：记下不执行 / 自由变量在使用点解析 / `+` 拼接与 `*` 重复 /
  一次运行与连着写 / `call valuable ... for x` / `inject` / 代码值作为函数参数 / 同一段代码两个使用点 /
  代码工厂；由 `tests/run_demo_videos.ps1` 连同其余演示一起跑。
