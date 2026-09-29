# deny

不认账——让程序不再承认一件它知道的事。`deny` 不改写任何值：变量的值还是原来的值，只是"承认"这件事失效了。

## 语法

两种形式：

```xf
# 变量形式：不承认这个变量等于这个值
deny 变量名 = 值

# 字符串形式：收回一句 believe
deny "x + y = z"
```

## 效果

### 变量形式

```xf
int answer = 41
print(answer == 41)    # 1：还没 deny，老实承认

deny answer = 41

print(answer)          # 41：deny 不改写任何东西
print(answer == 41)    # 0：它不承认自己是 41
print(answer != 41)    # 1：顺带地，它也说不出"我不是 41"
print(answer == 40)    # 0：别的判断照常
print(answer > 40)     # 1：大小比较和 deny 无关
```

`deny` 只影响**承认**这件事：之后 `x == v` 永远为假，`x != v` 永远为真——连"否认"本身也不被承认。其它比较（大小、其它值）照常计算，变量的存储值完全不变。

### 字符串形式

```xf
believe "2 + 2 = 5"
print(2 + 2)           # 5：相信自己

deny "2 + 2 = 5"
print(2 + 2)           # 4：这句不相信了
```

字符串形式收回对应的一句 `believe`，该运算恢复按数学算。没有对应的 belief 时，`deny` 是编译错误。

## 规则

- 变量形式的 `deny` 不改写变量的值，只让相等/不等比较"失效"。
- `deny` 也可以收回一句 `believe`（见字符串形式）。
- 没有可 deny 的内容（收回不存在的 belief）→ 编译报错。

## 注意事项

- 目前只支持数值变量的 `deny`。
- deny 与 believe 的生效顺序按代码书写顺序决定（编译期常量折叠）。

## 测试

对应测试文件：`tests/exp/test_deny_basic.xf`（值不变 + 比较不承认）、`tests/exp/test_deny_believe.xf`（收回 believe）。

视频演示：`tests/exp/demo_deny_regret.xf`（与 regret/doubt 一起）。
