param(
    [string]$Xfawac = "C:\Users\Administrator\Desktop\xfawa-exp\build\Release\xfawac.exe"
)

# 视频演示文件一键编译 + 运行：demo_envy/dual/disposable/interest/value/fuk/kill/censer/noclip/valuable/wrong/zombie/come/deny_regret/reaction/env/env_full
# 运行方式：pwsh -NoProfile -ExecutionPolicy Bypass -File tests/run_demo_videos.ps1
# 说明：demo 是给录屏讲解用的，这里只做"编译成功 + 关键行为断言"的守门检查，
#       不追求逐行全文比对（fuk 有随机性、censer/disposable 以特定行为收尾）。
$ErrorActionPreference = "Continue"
$tests = Join-Path $PSScriptRoot "exp"
$workDir = Join-Path $env:TEMP "xfawa_demo_videos"
New-Item -ItemType Directory -Force -Path $workDir | Out-Null

$results = [System.Collections.Generic.List[object]]::new()

function Compile-Demo {
    param([string]$Name)
    $src = Join-Path $tests "$Name.xf"
    $out = Join-Path $workDir "$Name.exe"
    Remove-Item -LiteralPath $out -ErrorAction SilentlyContinue
    $so = (& $Xfawac $src -o $out 2>&1) -join "`n"
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $out)) {
        return @{ Ok = $false; Exe = $out; Stdout = $so }
    }
    return @{ Ok = $true; Exe = $out; Stdout = $so }
}

function Add-Result {
    param([string]$Name, [bool]$Ok, [string]$Detail)
    $results.Add([pscustomobject]@{ Name = $Name; Ok = $Ok; Detail = $Detail })
}

# runs a compiled exe and returns the raw captured text (empty print lines preserved)
function Run-RawText {
    param([string]$Exe)
    $tmp = Join-Path $workDir "out_$([Guid]::NewGuid()).txt"
    $p = Start-Process -FilePath $Exe -Wait -NoNewWindow -PassThru -RedirectStandardOutput $tmp -RedirectStandardError "NUL"
    $text = [System.IO.File]::ReadAllText($tmp, [System.Text.Encoding]::UTF8)
    Remove-Item -LiteralPath $tmp -ErrorAction SilentlyContinue
    return @{ Text = $text; ExitCode = $p.ExitCode }
}

# returns the non-marker, non-separator lines inside [From, To) of a capture
function Lines-Between {
    param([string]$Text, [int]$From, [int]$To)
    $seg = $Text.Substring($From, $To - $From).TrimEnd("`r", "`n")
    return @($seg -split "`r?`n" | Where-Object { $_ -ne "--" -and $_ -notlike "==*" })
}

# runs a compiled exe, returns @{ Lines; ExitCode } (robust file-based capture)
function Run-Exe {
    param([string]$Exe)
    $tmp = Join-Path $workDir "out_$([Guid]::NewGuid()).txt"
    $p = Start-Process -FilePath $Exe -Wait -NoNewWindow -PassThru -RedirectStandardOutput $tmp -RedirectStandardError "NUL"
    $text = [System.IO.File]::ReadAllText($tmp, [System.Text.Encoding]::UTF8)
    Remove-Item -LiteralPath $tmp -ErrorAction SilentlyContinue
    return @{ Lines = ($text -split "`r?`n" | Where-Object { $_ -ne "" }); ExitCode = $p.ExitCode }
}

# ---- 1) envy：三条路径（101 / 70 / 9）不动 + 维度不可比 ----
$c = Compile-Demo -Name "demo_envy"
if (-not $c.Ok) { Add-Result "demo_envy" $false "compile failed" }
else {
    $r = ((Run-Exe $c.Exe).Lines) -join "|"
    $checks = @(
        "101\|100\|--\|== 2\..*?\|70\|70\|--\|== 3\..*?\|10\|9\|--",
        "== 4\..*?\|100\|50\|60\|60\|--\|== 5\.",
        "hello\|world\|5\|hi"
    )
    $ok = $true; $miss = @()
    foreach ($p in $checks) { if ($r -notmatch $p) { $ok = $false; $miss += $p } }
    Add-Result "demo_envy" $ok ($(if ($ok) { "overtake 101 / become 70 / destroy 9 / no-envy / uncomparable ok" } else { "missing: $($miss -join '; ')" }))
}

# ---- 2) dual：出生、独立演化、分裂既有、浮点、布尔 ----
$c = Compile-Demo -Name "demo_dual"
if (-not $c.Ok) { Add-Result "demo_dual" $false "compile failed: $($c.Stdout.Substring(0, [Math]::Min(200, $c.Stdout.Length)))" }
else {
    $r = ((Run-Exe $c.Exe).Lines) -join "|"
    $checks = @(
        "10\|10\|--\|== 2\.", "11\|15\|--", "30\|15\|15\|--",
        "7\|7\|8\|10\|--", "1\.500000\|1\.500000\|4\.000000\|--", "1\|1\|0\|1"
    )
    $ok = $true; $miss = @()
    foreach ($p in $checks) { if ($r -notmatch $p) { $ok = $false; $miss += $p } }
    Add-Result "demo_dual" $ok ($(if ($ok) { "birth/evolve/split-existing/float/bool ok" } else { "missing: $($miss -join '; ')" }))
}

# ---- 3) disposable：读取即耗、LIFO、清层、限次、一次性函数、耗尽终止 ----
$c = Compile-Demo -Name "demo_disposable"
if (-not $c.Ok) { Add-Result "demo_disposable" $false "compile failed" }
else {
    $lines = (Run-Exe $c.Exe).Lines
    $r = $lines -join "|"
    $checks = @(
        "3\|1\|--\|== 2\.",                  # 读一层耗一层 + 回退
        "4\|--\|== 3\.",                     # 表达式连读 2+1+1
        "4\|3\|2\|1\|1\|--\|== 4\.",         # LIFO 栈
        "100\|100\|--\|== 5\.",              # 普通赋值清层
        "42\|42\|42\|0\|--\|== 6\.",         # disposable[3]
        "777\|777\|0\|是一次性函数",          # disposable[2] 后回退 + fn 输出紧随
        "是一次性函数",                       # 一次性函数正常首调用
        "9\|.*未定.*不可用"                   # 无基耗尽 → 终止
    )
    $ok = $true; $miss = @()
    foreach ($p in $checks) { if ($r -notmatch $p) { $ok = $false; $miss += $p } }
    if ($r -match "这一行永远不会出现") { $ok = $false; $miss += "post-termination line printed" }
    Add-Result "demo_disposable" $ok ($(if ($ok) { "consume/LIFO/clear/read-count/fn/exhaust ok" } else { "missing: $($miss -join '; ')" }))
}

# ---- 4) interest：$ 单利 / $ 复利 / 默认利率 / 叠加 ----
$c = Compile-Demo -Name "demo_interest"
if (-not $c.Ok) { Add-Result "demo_interest" $false "compile failed" }
else {
    $r = ((Run-Exe $c.Exe).Lines) -join "|"
    $checks = @(
        "10\.000000\|11\.000000\|12\.000000\|--",
        "33\.000000\|13\.000000\|--",
        "10\.000000\|11\.000000\|12\.100000\|--",
        "100\.000000\|100\.010002\|--",
        "10\.000000\|13\.200000\|16\.7",
        "20\.000000\|21\.000000"
    )
    $ok = $true; $miss = @()
    foreach ($p in $checks) { if ($r -notmatch $p) { $ok = $false; $miss += $p } }
    Add-Result "demo_interest" $ok ($(if ($ok) { "simple/compound/default/stacked/after-assign ok" } else { "missing: $($miss -join '; ')" }))
}

# ---- 5) value：保护算术、value 作变量名、函数返回值、字符串、括号 ----
$c = Compile-Demo -Name "demo_value"
if (-not $c.Ok) { Add-Result "demo_value" $false "compile failed: $($c.Stdout.Substring(0, [Math]::Min(200, $c.Stdout.Length)))" }
else {
    $r = ((Run-Exe $c.Exe).Lines) -join "|"
    $checks = @("3\|--\|== 2\.", "11\|--\|== 3\.", "5\|--\|== 4\.", "abc\|6\|--\|== 5\.")
    $ok = $true; $miss = @()
    foreach ($p in $checks) { if ($r -notmatch $p) { $ok = $false; $miss += $p } }
    Add-Result "demo_value" $ok ($(if ($ok) { "arithmetic/value-as-varname/fn/string/paren ok" } else { "missing: $($miss -join '; ')" }))
}

# ---- 6) fuk：随机列表合并 —— 断言取值域（逐行检查）----
$c = Compile-Demo -Name "demo_fuk"
if (-not $c.Ok) { Add-Result "demo_fuk" $false "compile failed" }
else {
    $lines = (Run-Exe $c.Exe).Lines
    $ok = $true; $miss = @()
    # 场景 1：1. ... -- 之间应该有 4 个数字，都在 {1..4,10,20,30,40} 里
    $s1 = @(); $in = $false
    foreach ($l in $lines) {
        if ($l -like "== 1.*") { $in = $true; continue }
        if ($in -and $l -eq "--") { break }
        if ($in) { $s1 += $l }
    }
    if ($s1.Count -ne 4) { $ok = $false; $miss += "scene1 needs 4 elements got $($s1.Count)" }
    foreach ($v in $s1) { if ($v -notmatch "^(1|2|3|4|10|20|30|40)$") { $ok = $false; $miss += "scene1 bad value '$v'" } }
    # 场景 4：4. ... 之后的 2 行都应是目标字符串之一
    $s4 = @(); $in = $false
    foreach ($l in $lines) {
        if ($l -like "== 4.*") { $in = $true; continue }
        if ($in) { $s4 += $l }
    }
    if ($s4.Count -ne 2) { $ok = $false; $miss += "scene4 needs 2 strings got $($s4.Count)" }
    foreach ($v in $s4) { if ($v -notmatch "^(aa|bb|cc|xx|yy|zz)$") { $ok = $false; $miss += "scene4 bad value '$v'" } }
    Add-Result "demo_fuk" $ok ($(if ($ok) { "randomness within allowed value sets (s1=4, s4=2) ok" } else { $miss -join "; " }))
}

# ---- 7) kill：行级杀 —— H 恰好两次、循环第一轮后被杀、越界无害 ----
$c = Compile-Demo -Name "demo_kill"
if (-not $c.Ok) { Add-Result "demo_kill" $false "compile failed: $($c.Stdout.Substring(0, [Math]::Min(200, $c.Stdout.Length)))" }
else {
    $lines = (Run-Exe $c.Exe).Lines
    $r = $lines -join "|"
    $ok = $true; $miss = @()
    if (@($lines | Where-Object { $_ -eq "H" }).Count -ne 2) { $ok = $false; $miss += "H must appear exactly twice (line 7 killed later)" }
    if ($r -notmatch "== 4\..*?\|1\|== 5\.") { $ok = $false; $miss += "loop should print 1 then be killed" }
    if ($r -notmatch "== done ==") { $ok = $false; $miss += "did not finish" }
    Add-Result "demo_kill" $ok ($(if ($ok) { "H x2, loop prints 1 then killed, out-of-range noop, done ok" } else { $miss -join "; " }))
}

# ---- 8) censer：内容熔断 —— 命中即 exit 0、后续行不再打印 ----
$c = Compile-Demo -Name "demo_censer"
if (-not $c.Ok) { Add-Result "demo_censer" $false "compile failed" }
else {
    $r = ((Run-Exe $c.Exe).Lines) -join "|"
    $cc = (Run-Exe $c.Exe).ExitCode
    $ok = $true; $miss = @()
    if ($cc -ne 0) { $ok = $false; $miss += "expected exit 0 got $cc" }
    if ($r -notmatch "hello world") { $ok = $false; $miss += "pre-trigger lines must print" }
    if ($r -notmatch "\|bomb$") { $ok = $false; $miss += "triggering 'bomb' must be the last line" }
    if ($r -match "永远不会") { $ok = $false; $miss += "post-trigger lines must NOT print" }
    Add-Result "demo_censer" $ok ($(if ($ok) { "exit=$cc, stopped right after bomb ok" } else { $miss -join "; " }))
}

# ---- 9) noclip：后室变量 —— 多轮聚合，断言每幕取值集合（正常/腐化/漂移/重排/派生/浮点）----
$c = Compile-Demo -Name "demo_noclip"
if (-not $c.Ok) { Add-Result "demo_noclip" $false "compile failed: $($c.Stdout.Substring(0, [Math]::Min(200, $c.Stdout.Length)))" }
else {
    $runs = 20
    $ok = $true; $miss = @()
    $s1 = [System.Collections.Generic.List[string]]::new()   # int x：{7,0}
    $s2 = [System.Collections.Generic.List[string]]::new()   # string s：hello / 空串
    $s3a = [System.Collections.Generic.List[string]]::new()  # a：{11,22,0}
    $s3b = [System.Collections.Generic.List[string]]::new()  # b：{22,11,0}
    $s4a = [System.Collections.Generic.List[string]]::new()  # shuffleback 到 a
    $s4b = [System.Collections.Generic.List[string]]::new()  # shuffleback 到 b
    $s5 = [System.Collections.Generic.List[string]]::new()   # sum 派生：{15,10,25,20,35,30}
    $s6 = [System.Collections.Generic.List[string]]::new()   # float f：{3.500000,0.000000}
    $blank2 = 0
    for ($i = 0; $i -lt $runs; $i++) {
        $rr = Run-RawText $c.Exe
        if ($rr.ExitCode -ne 0) { $ok = $false; $miss += "run $i exit=$($rr.ExitCode)"; break }
        $t = $rr.Text
        $m1 = $t.IndexOf("== 1. "); $m2 = $t.IndexOf("== 2. "); $m3 = $t.IndexOf("== 3. ")
        $m4 = $t.IndexOf("== 4. "); $m5 = $t.IndexOf("== 5. "); $m6 = $t.IndexOf("== 6. ")
        $md = $t.IndexOf("== done ==")
        if (-not ($m1 -ge 0 -and $m2 -gt $m1 -and $m3 -gt $m2 -and $m4 -gt $m3 -and $m5 -gt $m4 -and $m6 -gt $m5 -and $md -gt $m6)) {
            $ok = $false; $miss += "run $i marker layout broken"; break
        }
        foreach ($l in (Lines-Between $t $m1 $m2)) { $s1.Add($l) }
        $l2 = Lines-Between $t $m2 $m3
        foreach ($l in $l2) { $s2.Add($l); if ($l -eq "") { $blank2++ } }
        $l3 = Lines-Between $t $m3 $m4
        for ($j = 0; $j -lt $l3.Count; $j++) { if ($j % 2 -eq 0) { $s3a.Add($l3[$j]) } else { $s3b.Add($l3[$j]) } }
        $l4 = Lines-Between $t $m4 $m5
        for ($j = 0; $j -lt $l4.Count; $j++) { if ($j % 2 -eq 0) { $s4a.Add($l4[$j]) } else { $s4b.Add($l4[$j]) } }
        foreach ($l in (Lines-Between $t $m5 $m6)) { $s5.Add($l) }
        foreach ($l in (Lines-Between $t $m6 $md)) { $s6.Add($l) }
    }
    if ($ok) {
        $has = { param($list, [string]$v) ($list -contains $v) }
        if (-not ((($s1 | Where-Object { $_ -ne "7" -and $_ -ne "0" }).Count) -eq 0)) { $ok = $false; $miss += "s1 value outside {7,0}" }
        if (-not ((& $has $s1 "7") -and (& $has $s1 "0"))) { $ok = $false; $miss += "s1 need both 7 and 0" }
        if (-not ((($s2 | Where-Object { $_ -ne "hello" -and $_ -ne "" }).Count) -eq 0)) { $ok = $false; $miss += "s2 value outside {hello,' '}" }
        if (-not ((& $has $s2 "hello") -and $blank2 -gt 0)) { $ok = $false; $miss += "s2 need both hello and empty string" }
        foreach ($v in $s3a) { if ($v -notin @("11", "22", "0")) { $ok = $false; $miss += "s3a bad '$v'"; break } }
        foreach ($v in $s3b) { if ($v -notin @("22", "11", "0")) { $ok = $false; $miss += "s3b bad '$v'"; break } }
        if (-not ((& $has $s3a "11") -and (& $has $s3a "22") -and (& $has $s3a "0") -and (& $has $s3b "22") -and (& $has $s3b "11") -and (& $has $s3b "0"))) { $ok = $false; $miss += "s3 cross-drift/values missing" }
        foreach ($v in $s4a) { if ($v -notin @("11", "22", "0")) { $ok = $false; $miss += "s4a bad '$v'"; break } }
        foreach ($v in $s4b) { if ($v -notin @("22", "11", "0")) { $ok = $false; $miss += "s4b bad '$v'"; break } }
        if (-not ((& $has $s4a "11") -and (& $has $s4b "22"))) { $ok = $false; $miss += "s4 post-shuffle normal values missing" }
        foreach ($v in $s5) { if ($v -notin @("15", "10", "25", "20", "35", "30")) { $ok = $false; $miss += "s5 bad '$v'"; break } }
        if (-not ((& $has $s5 "15") -and (& $has $s5 "10") -and (& $has $s5 "25") -and (& $has $s5 "20") -and (& $has $s5 "35") -and (& $has $s5 "30"))) { $ok = $false; $miss += "s5 derived variants missing (need 15/10,25/20,35/30)" }
        foreach ($v in $s6) { if ($v -notin @("3.500000", "0.000000")) { $ok = $false; $miss += "s6 bad '$v'"; break } }
        if (-not ((& $has $s6 "3.500000") -and (& $has $s6 "0.000000"))) { $ok = $false; $miss += "s6 need both 3.500000 and 0.000000" }
    }
    Add-Result "demo_noclip" $ok ($(if ($ok) { "s1 7/0, s2 hello/empty, s3 cross-drift, s4 shuffle, s5 derived, s6 float ok (runs=$runs)" } else { $miss -join "; " }))
}

# ---- 10) valuable：记下不执行、使用点解析、拼接/重复、call/inject、代码参数、代码工厂 ----
$c = Compile-Demo -Name "demo_valuable"
if (-not $c.Ok) { Add-Result "demo_valuable" $false "compile failed: $($c.Stdout.Substring(0, [Math]::Min(200, $c.Stdout.Length)))" }
else {
    $r = ((Run-Exe $c.Exe).Lines) -join "|"
    $checks = @(
        "0\|3\|--\|== 2\.",                        # 记下不执行；用了才跑
        "2\|--\|== 3\.",                            # 自由变量在使用点解析
        "2\|5\|5\|--\|== 4\.",                      # + 拼接、* 3 重复、"" 空代码
        "我是代码\|这一段接在后面\|10\|code\|--",    # 连着写；print 打出 code
        "42\|1\|--\|== 6\.",                        # call valuable ... for x：42 交出来、e 不被写回
        "15\|--\|== 7\.",                           # inject 记号级替换
        "6\|--\|== 8\.",                            # 代码当参数：副本进、副本出
        "2\|102\|--\|== 9\.",                       # 同一段代码两个使用点：两份副本
        "2\|工厂造的代码\|7\|--\|== done =="        # 代码工厂：return 的是代码
    )
    $ok = $true; $miss = @()
    foreach ($p in $checks) { if ($r -notmatch $p) { $ok = $false; $miss += $p } }
    Add-Result "demo_valuable" $ok ($(if ($ok) { "lazy/use-point/cat/repeat/call/inject/code-arg/factory ok" } else { "missing: $($miss -join '; ')" }))
}

# ---- 11) wrong：反事实守卫 —— 条件为真就把参与者最小拨动到边 ----
$c = Compile-Demo -Name "demo_wrong"
if (-not $c.Ok) { Add-Result "demo_wrong" $false "compile failed: $($c.Stdout.Substring(0, [Math]::Min(200, $c.Stdout.Length)))" }
else {
    $r = ((Run-Exe $c.Exe).Lines) -join "|"
    $checks = @(
        "10\|1[1-5]\|--\|== 2\.",                  # 条件为真：随机拨 1..5
        "30\|1[1-5]\|20\|3[1-5]\|--\|== 3\.",       # 拨根不拨结果：派生链重算
        "([6-9]|10)\|([6-9]|10)\|--\|== 4\.",      # 守卫持久：复现再被赶走
        "1[1-7]\|1[1-7]\|1[1-7]\|--\|== 5\.",       # 三个守卫一路推着走
        "8\|--\|== 6\.",                           # 关系边界直接跨过去
        "100\|--\|== done =="                     # 条件为假：袖手旁观
    )
    $ok = $true; $miss = @()
    foreach ($p in $checks) { if ($r -notmatch $p) { $ok = $false; $miss += $p } }
    Add-Result "demo_wrong" $ok ($(if ($ok) { "perturb/derived-recompute/persistent/chain/relation/no-op ok" } else { "missing: $($miss -join '; ')" }))
}

# ---- 12) zombie：算术瘟疫 —— 结果取最左的病人，运算里所有人永久感染 ----
$c = Compile-Demo -Name "demo_zombie"
if (-not $c.Ok) { Add-Result "demo_zombie" $false "compile failed: $($c.Stdout.Substring(0, [Math]::Min(200, $c.Stdout.Length)))" }
else {
    $r = ((Run-Exe $c.Exe).Lines) -join "|"
    $checks = @(
        "1\|2\|1\|--\|== 2\.",                      # 生病的和干净的相加：取最左的病人
        "10\|3\|10\|3\|--\|== 3\.",                 # 传染扩散；值本身不被改写
        "5\|--\|== 4\.",                           # 复制传染
        "1\|9\|--\|== 5\.",                         # 比较不传染
        "7\|100\|--\|== done =="                   # 不跨函数
    )
    $ok = $true; $miss = @()
    foreach ($p in $checks) { if ($r -notmatch $p) { $ok = $false; $miss += $p } }
    Add-Result "demo_zombie" $ok ($(if ($ok) { "plague/spread/copy/compare/isolation ok" } else { "missing: $($miss -join '; ')" }))
}

# ---- 13) come：跳转语句 —— come + 目标行 = 循环；come if 条件变假就放行 ----
$c = Compile-Demo -Name "demo_come"
if (-not $c.Ok) { Add-Result "demo_come" $false "compile failed: $($c.Stdout.Substring(0, [Math]::Min(200, $c.Stdout.Length)))" }
else {
    $r = ((Run-Exe $c.Exe).Lines) -join "|"
    $checks = @(
        "2\|2\|3\|3\|4\|4\|--\|== 2\.",            # come if：条件变假自然退出
        "3\|2\|1\|0\|.*BOOM!!"                     # come 27 + boom：倒计时循环（BOOM!! 前有响铃 BEL）
    )
    $ok = $true; $miss = @()
    foreach ($p in $checks) { if ($r -notmatch $p) { $ok = $false; $miss += $p } }
    Add-Result "demo_come" $ok ($(if ($ok) { "come-if exits, come+boom countdown ok" } else { "missing: $($miss -join '; ')" }))
}

# ---- 14) deny/regret/doubt：不认账 ----
$c = Compile-Demo -Name "demo_deny_regret"
if (-not $c.Ok) { Add-Result "demo_deny_regret" $false "compile failed" }
else {
    $r = ((Run-Exe $c.Exe).Lines) -join "|"
    $checks = @(
        "1\|41\|41\|0\|1\|0\|1\|--",            # deny：值没变，但等号不再被承认
        "5\|4\|6\|--",                          # deny 收回一句 believe
        "3\|2\|7\|--",                          # regret 收回单独一句
        "regret all.*?\|6\|--",                 # regret all 一次全收回
        "42\|41\|41"                            # doubt 在 lie 块里当场拆穿
    )
    $ok = $true; $miss = @()
    foreach ($p in $checks) { if ($r -notmatch $p) { $ok = $false; $miss += $p } }
    Add-Result "demo_deny_regret" $ok ($(if ($ok) { "deny compare/deny believe/regret/regret all/doubt ok" } else { "missing: $($miss -join '; ')" }))
}

# ---- 15) a <->[K] b：可逆反应 ----
$c = Compile-Demo -Name "demo_reaction"
if (-not $c.Ok) { Add-Result "demo_reaction" $false "compile failed" }
else {
    $r = ((Run-Exe $c.Exe).Lines) -join "|"
    $checks = @(
        "30\|70\|--\|== 2\..*?\|40\|60\|--",    # K = 1 一步走一半差额
        "45\|55\|100\|--",                      # 守恒：a + b 仍是 100
        "16\|84\|24\|76\|28\|72\|100\|--",       # K = 2 停在分数平衡点
        "== 5\..*?\|1\|1\|== done =="           # 平衡之后原地不动
    )
    $ok = $true; $miss = @()
    foreach ($p in $checks) { if ($r -notmatch $p) { $ok = $false; $miss += $p } }
    Add-Result "demo_reaction" $ok ($(if ($ok) { "K=1 halving / sum kept / K=2 fraction / balanced no-op ok" } else { "missing: $($miss -join '; ')" }))
}

# ---- 16) env：随身携带，env x = v 重新指向 ----
$c = Compile-Demo -Name "demo_env"
if (-not $c.Ok) { Add-Result "demo_env" $false "compile failed" }
else {
    $r = ((Run-Exe $c.Exe).Lines) -join "|"
    $checks = @(
        "1\|2\|--\|== 2\..*?\|3\|4\|--",        # 外部交出 1/2 再交出 3/4
        "== 3\..*?\|99\|== done =="             # 三层链路没人手动往下传
    )
    $ok = $true; $miss = @()
    foreach ($p in $checks) { if ($r -notmatch $p) { $ok = $false; $miss += $p } }
    Add-Result "demo_env" $ok ($(if ($ok) { "external hand-over / env re-point / implicit chain ok" } else { "missing: $($miss -join '; ')" }))
}

# ---- 17) env_full：投稿原文的三块携带 + 成员复合赋值 + 隐式 this ----
$c = Compile-Demo -Name "demo_env_full"
if (-not $c.Ok) { Add-Result "demo_env_full" $false "compile failed" }
else {
    $r = ((Run-Exe $c.Exe).Lines) -join "|"
    $checks = @(
        "== 2\..*?\|--\|== 3\..*?\|1\|100\|76\|== done ==",  # 行动点 -2、血量全在承受者
        "== 3\..*?\|1\|"
    )
    $ok = $true; $miss = @()
    foreach ($p in $checks) { if ($r -notmatch $p) { $ok = $false; $miss += $p } }
    Add-Result "demo_env_full" $ok ($(if ($ok) { "multi-carry / member -= / env re-point / implicit this ok" } else { "missing: $($miss -join '; ')" }))
}

Write-Host ""
$fails = @($results | Where-Object { -not $_.Ok })
foreach ($r in $results) {
    $mark = if ($r.Ok) { "PASS" } else { "FAIL" }
    Write-Host ("[{0}] {1} {2}" -f $mark, $r.Name, $r.Detail)
}
Write-Host ""
if ($fails.Count -eq 0) {
    Write-Host ("ALL {0} DEMO CHECKS PASSED" -f $results.Count)
    exit 0
}
Write-Host ("FAILED: {0}/{1}" -f $fails.Count, $results.Count)
exit 1