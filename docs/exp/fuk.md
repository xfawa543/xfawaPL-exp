# fu*k —— 列表合并（各取随机一半拼成新列表）

`A fu*k B` 从列表 `A` 随机取出 **floor(长度/2)** 个互不重复的元素，从列表 `B` 同样随机取出 **floor(长度/2)** 个互不重复的元素，拼接成**一张新列表**：

```
a = [1,2,3,4]
b = [10,20,30]
c = a fu*k b
print(c[0])     // 1..4 里的一个
print(c[1])     // 1..4 里的一个
print(c[2])     // 10 / 20 / 30 里的一个
```

## 语法

```
<列表1> fu*k <列表2>
```

运算结果是一张新列表（长度 = floor(lenA/2) + floor(lenB/2)，编译期已知），可以直接用下标读取，也可以赋值给变量：

```
c = a fu*k b       // c 被当作列表，长度已知，元素类型同 a/b
```

## 语义

- **随机取一半**：每边都做"partial Fisher-Yates 打乱"取一半下标，因此：
  - 一边 4 个 → 取 2 个；一边 3 个 → 取 1 个（floor）；一边 1 个 → 取 0 个。
  - 取出的元素互不重复；每次运行时随机，结果可能不同。
- **顺序**：左边选中的元素先入结果（按源顺序的第 i 个选中下标），右边随后；长度不变的前提下顺序随机。
- **元素类型必须一致**：两边元素类型不同 → 编译错误：
  ```
  [1,2] fu*k ["x","y"]    // [fu*k] both lists must hold the same type of elements
  ```
- **支持的类型**：int / long / string 数组（bool 按 int 存储也可参与 int 侧）；**float 数组不支持** → 编译错误 `[fu*k] float arrays are not supported yet`。
- **非列表操作数**：普通数值、未声明变量等 → 编译错误 `[fu*k] operands must be array literals or array variables`。
- **操作数**：可以是数组字面量（含 `[1...n]` 区间），也可以是已赋值的数组变量（运行时读取其指针）。
- 结果数组分配在堆上（malloc），长度和元素类型在编译期落入 `arrayLengths` / `localTypes`，后续下标读取/遍历沿用普通数组语义，负数下标同样按已知长度回绕。

## 实现位置

- Lexer：`fu*k` 在 tokenize 中做**特殊关键字前瞻**（精确串 `fu*k` 后跟非字母数字），产生 `PUNCTUATOR_FU_K`。
- Parser：`parseExpression` 在 `?!` 相同位置把 `A fu*k B` 构造成 `BinaryOp(BinaryOpType::FU_K)`。
- Codegen：
  - `codegen(BinaryOp*)` 分派 `FU_K` → `codegenFuK`：分析两侧长度/元素类型（变量查 `arrayLengths`/`localTypes`，字面量按首元素判型），生成 partial Fisher-Yates 打乱 + 逐元素拷贝到新 malloc 缓冲。
  - `codegen(AssignmentStatement*)` 对 `a = x fu*k y` 预计算 `arrayLengths[a]` 与 `localTypes[a]`（按操作数元素类型取 ARRAY_INT/ARRAY_LONG/ARRAY_STRING）。
- 每次运行随机：与 `randop`/`?!`/`drift` 共用 `rand` 与一次性播种。

## 边界说明（已定）

- float 列表**编译期拒绝**（实现只覆盖 int/long/string 三种元素宽度）。
- 元素"取一半"是**编译期定死**的数量（floor(n/2)），即使 n 为奇数也按地板取；n=1 的一侧取 0 个。
- 未做越界下标检查：`c[长度]` 超出范围的行为与其他数组一致（不设防线）。

## 测试

- `tests/exp/test_fuk.xf` — f1 两侧字面量各 4（2+2）；f2 变量 + `[1...n]` 区间（2+1，元素落入各自取值域）；f3 奇数各取 floor/2（1+1）；f4 字符串列表（2+2）；f5 长整型列表（1+1）。
- `tests/exp/test_fuk_fail_mismatch.xf`（类型不一致）、`test_fuk_fail_notlist.xf`（非列表）、`test_fuk_fail_float.xf`（float 列表）— 编译必须失败。
- `tests/run_exp_new_tests.ps1` — 按标记切段校验每段的取值域与长度（随机结果只在声明的集合里断言）。
- `tests/exp/demo_fuk.xf` - 视频演示：四幕（固定 + 随机 / 两侧随机 / 分区各 1 / 随机取字符串）。
