# value —— 保护前缀（显式表明"这里有值"）

`value` 加在赋值右侧的表达式前面，明示这个表达式是"必须产生一个值"的运算（同时照常求值它）：

```
a = value 1 + 2
print(a)        // 3 —— 保护前缀不改变求值结果，只是显式标注
```

## 语法

```
<变量> = value <表达式>
```

`value` 后跟一个普通表达式（数值、字符串、括号表达式、函数调用……均可）。

## 语义

- `value` 是**纯标注并照常求值**：`a = value 1 + 2` 与 `a = 1 + 2` 结果完全相同。
- **void 守卫**（核心规则）：如果 `value` 后面是函数调用，而被调函数**没有 return**（void），编译直接报错：
  ```
  y = value hello()    // [value] the function 'hello' does not return a value (void), ...
  fn hello() { print("hi") }   // void → 被 value 点名即报错
  ```
  这不只是普通"无返回值赋给变量"错误——`value` 恰好把这类"空值"暴露出来。
- **`value` 本身仍是普通标识符**（关键字保留的取舍）：因为既有测试把 `value` 当变量名使用（`value = 1; disposable value = 2; ...`），编译器用**上下文判定**区分两种用法：
  - `a = value 1 + 2`（`value` 后紧跟一个可做表达式起点的记号）→ 保护前缀。
  - `a = value + 1`（`value` 后是运算符 `+`）→ 普通变量 `value` 的算术。
- 判断"是否保护前缀"的下一记号集合：数字 / 长整型数字 / 浮点 / 字符串 / 标识符（函数调用、变量）/ `true` / `false` / `input` / `(` / o-字面量。
- 被判定为 void 的函数是**编译期静态判定**（函数体内是否（递归地）存在 `return` 语句），不依赖函数清单/运行时。

## 与下面特性的关系

- `value` 与 `?.` 之类未来运算符无关；它就是标识保护意图的**值前缀**。
- `value` 对字符串、`(` 括号表达式、变量读取、`input` 均可用；`input` 恒返回字符串，不受 void 守卫影响。

## 实现位置

- Parser：赋值语句的 RHS 处做**前看一个记号**的上下文判定（`value` 后是否"可作表达式起点"），构造 `ValueExpression`（内层为目标表达式）。
- AST：`NodeType::VALUE_EXPRESSION` / `ValueExpression`。
- Codegen：`codegen(ValueExpression*)` 校验内层是否为 void 函数调用（查 `collectVoidFunctions` 的结果表），是则 `[value]` 报错；否则照常生成内层表达式代码。void 判定表在 `codegenProgram` 里一次性建立：凡是函数体内（含嵌套块/分支/循环/函数/`try-expect`）**不存在 return** 就算 void。

## 边界说明（已定）

- `value` 后直接跟变量名的写法（`x = value y`）视为"保护变量读取"，不是调用，不触发 void 守卫。
- 有意保留 `value` 作普通变量名 → 任何"以 value 收尾选普通变量语法"的情况（如 `value + 1`）都不是保护前缀（文档如此，测试如此）。
- 只做"RHS 是否有值"的显式标注：不对被保护表达式做类型变换。

## 测试

- `tests/exp/test_value.xf` — v1 保护算术（3）；v2 `value` 作普通变量（11）；v3 有返回值函数（5）；v4 字符串（abc）；v5 括号表达式（6）。
- `tests/exp/test_value_fail_void.xf` — `value hello()` 报 void 错，编译必须失败。
- `tests/run_exp_new_tests.ps1` — 完整编译 + 运行比对。
- `tests/exp/demo_value.xf` - 视频演示：五幕（保护算术 / value 作变量 / 有返回值函数 / 字符串 / 括号）。
