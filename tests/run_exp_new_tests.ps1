param(
    [string]$Xfawac = "C:\Users\Administrator\Desktop\xfawa-exp\build\Release\xfawac.exe"
)

# 新 EXP 测试：value（保护前缀）/ fu*k（列表合并）/ kill（行杀手）/ censer（内容熔断）
# 运行方式：powershell -ExecutionPolicy Bypass -File tests/run_exp_new_tests.ps1

$ErrorActionPreference = "Continue"
$tests = Join-Path $PSScriptRoot "exp"
$workDir = Join-Path $env:TEMP "xfawa_exp_new_test"
New-Item -ItemType Directory -Force -Path $workDir | Out-Null

$results = [System.Collections.Generic.List[object]]::new()

function Compile-Run {
    param([string]$Name)
    $src = Join-Path $tests "$Name.xf"
    $out = Join-Path $workDir "$Name.exe"
    Remove-Item -LiteralPath $out -ErrorAction SilentlyContinue
    $stdout = (& $Xfawac $src -o $out 2>&1) -join "`n"
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        return [pscustomobject]@{ Name = $Name; Ok = $false; Detail = "compile exit=$code" }
    }
    if (-not (Test-Path -LiteralPath $out)) {
        return [pscustomobject]@{ Name = $Name; Ok = $false; Detail = "no exe produced" }
    }
    $rout = (& $out 2>&1) | ForEach-Object { $_ -replace "`r`n", "`n" }
    $rune = $LASTEXITCODE
    $actual = @($rout | ForEach-Object { $_.TrimEnd("`r") })
    return [pscustomobject]@{ Name = $Name; Ok = $true; Detail = ""; RunExit = $rune; Actual = $actual }
}

function Expect-Lines {
    param($Item, [string[]]$Expected, [string]$Sep)
    if (-not $Item.Ok) { return $Item }
    if ($Item.RunExit -ne 0) {
        $Item.Detail = "run exit=$($Item.RunExit)"; $Item.Ok = $false; return $Item
    }
    if ($Item.Actual.Count -ne $Expected.Count) {
        $Item.Detail = "lines=$($Item.Actual.Count) expected=$($Expected.Count)"
        for ($i = 0; $i -lt [Math]::Min($Item.Actual.Count, $Expected.Count); $i++) {
            if ($Item.Actual[$i] -ne $Expected[$i]) { $Item.Detail += "; line $($i+1): got '$($Item.Actual[$i])' expected '$($Expected[$i])'"; break }
        }
        $Item.Ok = $false
        return $Item
    }
    if ($null -ne (Compare-Object $Item.Actual $Expected)) {
        $m = Compare-Object $Item.Actual $Expected | Select-Object -First 1
        $Item.Detail = "output differs: '$($m.InputObject)'"
        $Item.Ok = $false
    }
    return $Item
}

function Expect-CompileFail {
    param([string]$Name, [string]$Tag)
    $src = Join-Path $tests "$Name.xf"
    $out = Join-Path $workDir "$Name.exe"
    Remove-Item -LiteralPath $out -ErrorAction SilentlyContinue
    $stdout = (& $Xfawac $src -o $out 2>&1) -join "`n"
    $code = $LASTEXITCODE
    $escaped = [regex]::Escape($Tag)
    $ok = $code -ne 0 -and $stdout -match $escaped -and (-not (Test-Path -LiteralPath $out))
    $detail = if ($ok) { "" } else { "exit=$code tag='$Tag' matched=$($stdout -match $escaped) exe=$(Test-Path -LiteralPath $out)" }
    return [pscustomobject]@{ Name = $Name; Ok = $ok; Detail = $detail }
}

# ---- fu*k 随机输出校验：按分隔标记切分，逐段检查取值区间 ----
function Test-FukGroup {
    param([string]$Name)
    $item = Compile-Run -Name $Name
    if (-not $item.Ok) { return $item }
    $a = $item.Actual
    # 段一：两字面量 [1..8] 四个元素
    $idx = [Array]::IndexOf($a, "f1")
    if ($idx -ne 4) { $item.Detail = "marker f1 at $idx expected 4"; $item.Ok = $false; return $item }
    for ($k = 0; $k -lt 4; $k++) { if ([int]$a[$k] -notin 1..8) { $item.Detail = "fuk f1 line$($k+1)='$($a[$k])' not in 1..8"; $item.Ok = $false; return $item } }
    # 段二：前两个 in 1..4，第三个 in {10,20,30}
    $idx = [Array]::IndexOf($a, "f2")
    if ($idx -ne 8) { $item.Detail = "marker f2 at $idx expected 8"; $item.Ok = $false; return $item }
    for ($k = 0; $k -lt 2; $k++) { if ([int]$a[5 + $k] -notin 1..4) { $item.Detail = "fuk f2 line$($k+1)='$($a[5+$k])' not in 1..4"; $item.Ok = $false; return $item } }
    if ([int]$a[7] -notin @(10,20,30)) { $item.Detail = "fuk f2 line3='$($a[7])' not in 10/20/30"; $item.Ok = $false; return $item }
    # 段三：{1,2,3} 与 {40,50}
    $idx = [Array]::IndexOf($a, "f3")
    if ($idx -ne 11) { $item.Detail = "marker f3 at $idx expected 11"; $item.Ok = $false; return $item }
    if ([int]$a[9] -notin 1..3) { $item.Detail = "fuk f3 line1='$($a[9])' not in 1..3"; $item.Ok = $false; return $item }
    if ([int]$a[10] -notin @(40,50)) { $item.Detail = "fuk f3 line2='$($a[10])' not in 40/50"; $item.Ok = $false; return $item }
    # 段四：四个字符串都在两边的取值集合
    $idx = [Array]::IndexOf($a, "f4")
    if ($idx -ne 16) { $item.Detail = "marker f4 at $idx expected 16"; $item.Ok = $false; return $item }
    $set = @("aa","bb","cc","dd","ee","ff","gg","hh")
    for ($k = 0; $k -lt 4; $k++) { if ($a[12 + $k] -notin $set) { $item.Detail = "fuk f4 line$($k+1)='$($a[12+$k])' not in letter set"; $item.Ok = $false; return $item } }
    # 段五：两个长整数都在四值集合
    $idx = [Array]::IndexOf($a, "f5")
    if ($idx -ne 19) { $item.Detail = "marker f5 at $idx expected 19"; $item.Ok = $false; return $item }
    $lset = @("10000000000","20000000000","30000000000","40000000000")
    for ($k = 0; $k -lt 2; $k++) { if ($a[17 + $k] -notin $lset) { $item.Detail = "fuk f5 line$($k+1)='$($a[17+$k])' not in long set"; $item.Ok = $false; return $item } }
    if ($item.Actual.Count -ne 20) { $item.Detail = "total lines=$($item.Actual.Count) expected 20"; $item.Ok = $false }
    return $item
}

# ---- value ----
$v = Compile-Run -Name "test_value"
$results.Add((Expect-Lines -Item $v -Expected @("3","v1","11","v2","5","v3","abc","v4","6","v5")))

# ---- value 无返回值守卫：必须编译失败 ----
$results.Add((Expect-CompileFail -Name "test_value_fail_void" -Tag "[value]"))

# ---- fu*k ----
$results.Add((Test-FukGroup -Name "test_fuk"))

# ---- fu*k 错误路径：必须编译失败 ----
$results.Add((Expect-CompileFail -Name "test_fuk_fail_mismatch" -Tag "[fu*k] both lists must hold the same type"))
$results.Add((Expect-CompileFail -Name "test_fuk_fail_notlist" -Tag "[fu*k] operands must be array literals"))
$results.Add((Expect-CompileFail -Name "test_fuk_fail_float" -Tag "[fu*k] float arrays are not supported"))

# ---- kill ----
$k = Compile-Run -Name "test_kill"
$results.Add((Expect-Lines -Item $k -Expected @("kH","k3a","k3b","kL","k2","k4","k5")))

# ---- censer ----
$c = Compile-Run -Name "test_censer"
$results.Add((Expect-Lines -Item $c -Expected @("words","a word","plain","word")))

$cn = Compile-Run -Name "test_censer_num"
$results.Add((Expect-Lines -Item $cn -Expected @("c2a","666")))

Write-Host ""
$fails = @($results | Where-Object { -not $_.Ok })
foreach ($r in $results) {
    $mark = if ($r.Ok) { "PASS" } else { "FAIL" }
    Write-Host ("[{0}] {1} {2}" -f $mark, $r.Name, $r.Detail)
}
Write-Host ""
if ($fails.Count -eq 0) {
    Write-Host ("ALL {0} CHECKS PASSED" -f $results.Count)
    exit 0
}
Write-Host ("FAILED: {0}/{1}" -f $fails.Count, $results.Count)
exit 1