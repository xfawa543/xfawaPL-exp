# env（随身携带）

让一组函数都揣着一个叫"参数名"的值。这个值是每个函数的**携带参数**：组内互传时自动带上，不用谁操心；外部调用时需要显式交出。

一个函数可以同时出现在**多个** `env` 块里，于是同时携带多个值——这正是投稿原文的用法。

## 语法

```xf
env 类型 参数名 { 函数名1, 函数名2, ... }

# 把携带的值重新指向：
env 参数名 = 新值
```

配套的类型声明与结构体访问（投稿原文用到 `角色.血量` 这类写法）：

```xf
type 名字 { int 字段1, long 字段2, float 字段3, bool 字段4, string 字段5 }

类型名 变量 = { 值1, 值2, ... }   # 按字段顺序创建
变量.字段 = 值                    # 成员赋值
变量.字段 -= 值                   # 成员复合赋值（+= -×* /= %= 同理）
```

## 效果：投稿原文的例子

下面是把投稿原文逐条翻成 ASCII 名称后的完整写法（标识符是 ASCII-only）：

```xf
type Role { int action, int health }

#test {
    fn main() {
        Role hero = { 3, 100 }
        Role enemy = { 2, 80 }
        env int dmg { attack, takeDamage, defense }
        env Role sender { attack, defense, takeDamage }
        env Role receiver { attack }
        attack(5, hero, enemy)
        print(hero.action)   # 2：发出者.行动点 -= 1
        print(enemy.health)  # 78：接受伤害里改的是承受者的血量
    }

    fn attack() {
        sender.action -= 1     // 发出者.行动点 -= 1
        env sender = receiver  // env 发出者 = 承受者
        takeDamage()           // 接受伤害()：组内调用不写参数
    }

    fn takeDamage() {
        int actual = defense()
        health -= actual       // 血量 -= 实际伤害（省略 发出者. 的隐式 this）
    }

    fn defense() {
        return 2
    }
}
```

三个 `env` 块合起来说的是：`攻击` 同时携带 `伤害`、`发出者`、`承受者`；`接受伤害` 和 `防御` 携带 `伤害` 和 `发出者`。

- **多块携带**：`攻击` 出现在三个块里，所以它的携带参数依次是 `dmg`、`sender`、`receiver`（按块的声明顺序）。
- **成员复合赋值**：`sender.action -= 1` 写进 `发出者` 指向的那个结构体，外部 `hero.action` 看得见（结构体变量是引用语义）。
- **重新指向**：`env sender = receiver` 让 `发出者` 之后指向承受者，于是 `接受伤害` 里改的血量落在 `enemy` 上。
- **自动传递**：`takeDamage()` 和 `defense()` 只在 `env` 组里被列出，调用处不用写携带参数。
- **隐式 this**：`health -= actual` 里 `health` 不是局部变量，而是携带结构体 `Role` 的字段，等价于 `sender.health -= actual`。

## 单块携带

只有一个块时就是最朴素的"随身携带"：

```xf
fn main() {
    env int who { attack, take }
    attack(1, 2)      # 交出 who=1
    attack(3, 4)      # 交出 who=3
}

fn attack(who, victim) {
    print(who)        # 1：这就是携带的值
    env who = victim  # 重新指向：改掉携带的内容
    take()            # 内部调用不用写 who
}

fn take(who) {
    print(who)        # 2：已经被重新指向了
}
```

## 链条

携带的值可以跳很多层，谁也不用手动往下传：

```xf
fn main() {
    env int carrier { hop1, hop2, hop3 }
    hop1(99)
}

fn hop1() { hop2() }
fn hop2() { hop3() }
fn hop3(carrier) {
    print(carrier)    # 99：跳了三层，还是同一个值
}
```

## 规则

- `参数名` 会成为这组每个函数的**携带参数**，排在函数参数列表的**前面**，顺序按 `env` 块的声明顺序。
- `类型` 必须是 `int/long/float/bool/string`，或用 `type` 声明过的结构体名；写别的名字 → 编译报错（不会悄悄按整数处理）。
- 一个函数可以出现在多个块里，携带多个值；同名携带值重复列出只算一次。
- 组内互传时自动注入调用方也携带的同名值；调用方没携带的值由显式实参按顺序补上。
- `env 参数名 = 新值` 只能写在**携带它的函数**里（把携带的值重新指向）；写在别的函数里是编译错误。
- 携带参数的类型来自块的 `类型`：`int/long/float/bool/string` 按普通值传递，`type` 声明的结构体按引用（指针）传递，所以成员写入对外部可见。
- 结构体变量是引用语义：`a = b` 是重新指向，`a.字段 = v` 会写进被指向的对象。
- 裸字段名（`血量 -= 实际伤害`）等价于 `发出者.血量 -= 实际伤害`，前提是**没有同名局部变量**；有两个携带类型都含该字段时必须写全，编译报错。
- `env` 声明只能写在函数体内（例如 `fn main()` 里），不能放在模块顶层。

## 注意事项

- 被携带的函数必须存在；列出不存在的函数 → 编译报错。
- 同一个携带名只能声明一种类型。
- 如果函数自己声明了携带的参数，它必须落在携带列表对应的位置上（写在别处 → 编译报错）。
- 结构体字段只支持 `int/long/float/bool/string`，不支持嵌套结构体。
- 结构体只能通过 `env` 携带进函数：普通函数参数还不能声明成结构体类型（`fn show(r) { print(r.action) }` 目前会报错），要访问成员就用携带。
- 函数返回值目前是整数（`i64`），不能返回结构体。
- 标识符是 ASCII-only：示例里的中文（`角色`、`攻击`）是示意写法，实际代码请用 ASCII 名称。

## 测试

对应测试文件：`tests/exp/test_env_basic.xf`（携带 + 重新指向）、`tests/exp/test_env_chain.xf`（多层链路）、`tests/exp/test_env_full.xf`（投稿原文的多块携带 + 成员复合赋值 + 隐式 this）、`tests/exp/test_fail_env_ambiguous.xf`（字段歧义必须写全）。

视频演示：`tests/exp/demo_env.xf`（单块携带）、`tests/exp/demo_env_full.xf`（投稿原文全流程）。
