# 编译器红温机制（rage + sorry）

"编译器红温"是 xfawa-exp 中的娱乐性编译器状态系统：当编译器多次成功捕获 `try...expect` 错误时，它的"红温值"（`rage`）会逐渐升高，进入"红温"甚至"极度红温"状态，并改变编译器的输出语气和吐槽风格。

这是一个纯编译期的娱乐机制：它**完全不影响程序的生成代码和运行时语义**，只改变编译器在编译过程中打印的提示信息。

## rage（红温值）

### 生命周期（持久化）

`rage` 是 **xfawac 自己的持久化状态**，不是单次编译的临时变量：

| 事件 | 行为 |
|---|---|
| 首次安装（无状态文件） | `rage = 0` |
| 每次编译开始 | 从用户级状态目录读取上一次的 `rage` |
| 成功捕获一次可捕获错误 | `rage += 1`（上限 5） |
| 执行 `sorry` 语句 | `rage -= delta`（`delta ∈ [0, rage]`，随机，下限 0） |
| 编译结束 | 若 `rage` 有变化，安全写回状态文件 |
| `xfawac rage reset` | 主动清零并写回 |

**rage 永不随编译任务结束或进程退出而重置。** 多次独立的 `xfawac` 启动之间，红温值持续累积——这是 xfawac 自己"被气成什么样"的指标，与具体项目无关（项目 A 与项目 B 共享同一个 rage）。

### 状态存储位置

rage 状态文件就放在**编译器可执行文件所在的目录**，与 `xfawac` 本体同在：

| 平台 | 路径 |
|---|---|
| Windows | `<xfawac.exe 所在目录>\rage`（例如 `build\Release\rage`） |
| Linux | `<xfawac 所在目录>/rage`（经 `/proc/self/exe` 解析） |
| macOS | `<xfawac 所在目录>/rage`（经 `_NSGetExecutablePath` 解析） |

- 永不写入当前工作目录或用户项目目录。
- **每一份 xfawac 副本各有一份自己的 rage**：把 `xfawac.exe` 拷到哪里，红温就跟着走到哪里。
- 文件内容就是一个纯整数（如 `3`），不引入数据库。
- 写入采用"临时文件 → flush/close → 原子替换正式文件"（Windows `MoveFileExW` / POSIX `rename`），不会把状态文件原地截断，中途退出也不会损坏。
- **损坏恢复策略**：文件缺失、不可读、内容非合法整数、带尾随垃圾、或数值超出 `0..5` 时，一律安全回退为 `0`，不会让状态破坏整个编译器。
- **并发**：多个 xfawac 实例同时读写时，采用 pid 唯一临时文件 + 原子替换，状态文件本身不会损坏；对同一份状态的并发修改是"最后写入者胜"（第一版仅保证单进程下严格正确）。

### CLI 查看 / 重置

```
xfawac rage          → rage: 3/5
xfawac rage reset    → rage reset: 0/5
```

### 数值范围与等级

| rage 范围 | 等级 | 编译器行为 |
|---|---|---|
| 0–2 | 正常 | 标准提示语气 |
| 3–4 | **红温**（RAGE） | 编译器输出带"红温"标记的吐槽语气 |
| 5 | **极度红温**（MAX RAGE） | 编译器输出带"极度红温"标记的极端吐槽语气 |

即使在 `rage = 5` 后继续发生错误，rage 仍然保持 5，不会继续增加。

### 观察方式

每次 `rage` 发生变化时，编译器在编译输出中打印一行警告：

```
[warning:syntax] [rage +1] 哼，小错误而已……人家才没有生气呢 rage = 3/5 (红温 RAGE)
[warning:syntax] [rage -3] o……o…ok rage = 2/5
```

（catch 台词每级 5 句、随机抽取且连续两次不重复；sorry 台词每级 2 句随机。文案随红温等级调整语气，始终包含 rage 数值变化。）

程序本身的运行输出（`print` 的内容）不受任何影响。

## sorry（道歉）

### 语法

```
sorry
```

### 规则

- `sorry` 使 `rage -= delta`，其中 **`delta` 是在 `[0, rage]` 内均匀随机**的整数：
  - `rage = 5` 时一次 `sorry` 可能得到 `5→5`、`5→4` … `5→0` 任意结果；
  - `rage = 0` 时 `delta = 0`，`sorry` 后仍为 0（永不产生非法随机范围、永不为负）。
- `sorry` **不能跳过错误**（语法错误/语义错误照常发生）。
- `sorry` **不能关闭 warning**。
- `sorry` **不能改变程序正常语义**（生成代码中 `sorry` 位置不产生任何运行时操作）。

### 编译器响应

收到道歉后，编译器在编译输出中打印该次实际的随机下降量：

```
[warning:syntax] [rage -4] 哼……这次就大发慈悲放过你，只有一次哦！ rage = 1/5
```

## 与 try...expect 的关系

每次 `try...expect` 成功拦截一个可捕获错误时（`try` 块有真实编译错误，`expect` 块被执行），`rage += 1`。一次编译中每次成功捕获都是一次递增事件（最终值永远钳制在 5）。因此 try...expect 是 rage 上升的主要来源，sorry 是其随机下降的调剂——二者共同构成 rage 的动态变化。

红温（`rage >= 3`）状态下，编译器会对编译结果施加**强制规则**，详见 [try_expect.md](try_expect.md)。

## 红温对编译结果的影响

`rage` 状态**绝不**改变运行层面：

- 不修改用户源代码
- 不修改编译器永久配置
- 不随机修改程序行为
- 不阻止链接
- 生成的程序在运行时的行为与 rage 无关（编译期状态不注入任何运行时行为）

红温状态下对编译结果的强制规则见 [try_expect.md](try_expect.md)。除此之外，红温只改变编译器打印的警告语气与"红温"标签。

**注意**：既然是持久状态，录视频前如需从零演示，先执行 `xfawac rage reset`。

## 实现位置

- **rage 状态**：`SemanticAnalyzer` 成员变量（`int rage`），构造时接收 `main.cpp` 从 `loadRageState()` 读到的持久化起始值（钳制到 `0..5`）。
- **持久化存取**：`main.cpp` 的 `RageStore` 辅助函数（`compilerDir` / `rageStateDir` / `loadRageState` / `saveRageState`），状态文件存放于可执行文件所在目录；`RagePersist` RAII 守卫在语义分析之后把所有退出路径上的 rage 变化写回。
- **CLI**：`main.cpp` 参数循环中的 `rage` / `rage reset` 分支。
- **rage 递增**：`TRY_EXPECT_STATEMENT` 语义分析中的捕获逻辑（`bumpRage()`）。
- **rage 随机递减**：`SORRY_STATEMENT` 语义分析（`compilerRand(rage + 1)`）。
- **红温强制规则**：见 [try_expect.md](try_expect.md) 的实现位置。
- **quip 随机抽取**：`SemanticAnalyzer::compilerRand`（`std::random_device` 播种的一次性 `std::mt19937`）+ `nextCaughtQuipIndex`（5 句池子且避免连续重复）。
- **消息输出**：将 rage 变化信息加入 `SemanticAnalyzer::warnings`，由 `main.cpp` 转发至 `ErrorReporter`，在编译成功时打印。

## 测试

- `tests/exp/test_rage.xf` — 一份完整覆盖：catch + sorry 增减、多次 catch 钳制到 5、多次 sorry 随机下降、降温后重新升温、消息文案随机抽取、rage 达到 5 时程序输出与 rage=0 时一致（文件在结尾把 rage 降到红温线以下，避免触发红温强制规则）。
- 跨进程持久化 / 上限 5 / sorry 随机区间的 CLI 实测：本仓库测试不依赖持久化值，测试脚本始终先 `rage reset` 再断言运行输出。
- 红温强制规则与"每五行合规字"的专项测试见 [try_expect.md](try_expect.md)。