param(
    [string]$Xfawac = "C:\Users\Administrator\Desktop\xfawa-exp\build\Release\xfawac.exe"
)

# noclip 测试：变量跌入后室（不稳定读取、引用失败、传播、shuffleback 重排、错误路径）
# 运行方式：powershell -ExecutionPolicy Bypass -File tests/run_noclip_tests.ps1

$ErrorActionPreference = "Continue"
$tests = Join-Path $PSScriptRoot "exp"
$workDir = Join-Path $env:TEMP "xfawa_noclip_test"
New-Item -ItemType Directory -Force -Path $workDir | Out-Null

$results = [System.Collections.Generic.List[object]]::new()
$runCount = 25

function Compile-Test {
    param([string]$Name)
    $src = Join-Path $tests "$Name.xf"
    $out = Join-Path $workDir "$Name.exe"
    Remove-Item -LiteralPath $out -ErrorAction SilentlyContinue
    $stdout = (& $Xfawac $src -o $out 2>&1) -join "`n"
    if ($LASTEXITCODE -ne 0) {
        return @{ Ok = $false; Detail = "compile exit=$LASTEXITCODE"; Exe = $out }
    }
    if (-not (Test-Path -LiteralPath $out)) {
        return @{ Ok = $false; Detail = "no exe produced"; Exe = $out }
    }
    return @{ Ok = $true; Detail = ""; Exe = $out }
}

# Run the compiled exe $runCount times, collecting every output line.
function Run-Many {
    param([string]$Exe)
    $all = [System.Collections.Generic.List[string]]::new()
    for ($i = 0; $i -lt $runCount; $i++) {
        $run = @(& $Exe 2>&1)
        if ($LASTEXITCODE -ne 0) { $all.Add("<exit:$LASTEXITCODE>"); continue }
        foreach ($line in $run) { $all.Add(($line -replace "`r$", "")) }
    }
    return $all
}

function New-Fail {
    param([string]$Name, [string]$Detail)
    return [pscustomobject]@{ Name = $Name; Ok = $false; Detail = $Detail }
}

# ---- 1) 不稳定读取：int 变量 10 次读取，25 轮，必须同时出现正常值 7 与引用失败 0 ----
$c = Compile-Test -Name "test_noclip_basic"
if (-not $c.Ok) { $results.Add((New-Fail "test_noclip_basic" $c.Detail)) }
else {
    $out = Run-Many -Exe $c.Exe
    $okN = @($out | Where-Object { $_ -eq "7" }).Count
    $ok0 = @($out | Where-Object { $_ -eq "0" }).Count
    if ($okN -gt 0 -and $ok0 -gt 0) {
        $results.Add([pscustomobject]@{ Name = "test_noclip_basic"; Ok = $true; Detail = "normal=$okN corrupt=$ok0" })
    } else {
        $results.Add((New-Fail "test_noclip_basic" "need both '7' and '0'; got normal=$okN corrupt=$ok0 lines=$($out.Count)"))
    }
}

# ---- 2) 字符串不稳定读取：必须同时出现 "hello" 与空串（引用失败） ----
$c = Compile-Test -Name "test_noclip_string"
if (-not $c.Ok) { $results.Add((New-Fail "test_noclip_string" $c.Detail)) }
else {
    $out = Run-Many -Exe $c.Exe
    $okH = @($out | Where-Object { $_ -eq "hello" }).Count
    $okE = @($out | Where-Object { $_ -eq "" }).Count
    if ($okH -gt 0 -and $okE -gt 0) {
        $results.Add([pscustomobject]@{ Name = "test_noclip_string"; Ok = $true; Detail = "hello=$okH empty=$okE" })
    } else {
        $results.Add((New-Fail "test_noclip_string" "need both 'hello' and ''; got hello=$okH empty=$okE lines=$($out.Count)"))
    }
}

# ---- 3) 传播/漂移：读取 a 有时拿到 b 的值（11/22 交叉），b 同理 ----
$c = Compile-Test -Name "test_noclip_drift"
if (-not $c.Ok) { $results.Add((New-Fail "test_noclip_drift" $c.Detail)) }
else {
    $out = Run-Many -Exe $c.Exe
    # 每轮 10 行：a 在偶数位，b 在奇数位（交叉漂移：a 出现 22、b 出现 11）
    $aVals = [System.Collections.Generic.List[string]]::new()
    $bVals = [System.Collections.Generic.List[string]]::new()
    for ($i = 0; $i -lt $out.Count; $i++) {
        if ($i % 2 -eq 0) { $aVals.Add($out[$i]) } else { $bVals.Add($out[$i]) }
    }
    $aHas22 = ($aVals -contains "22"); $bHas11 = ($bVals -contains "11")
    $aHas11 = ($aVals -contains "11"); $bHas22 = ($bVals -contains "22")
    $aHas0 = ($aVals -contains "0"); $bHas0 = ($bVals -contains "0")
    if ($aHas11 -and $bHas22 -and ($aHas22 -or $bHas11) -and $aHas0 -and $bHas0) {
        $results.Add([pscustomobject]@{ Name = "test_noclip_drift"; Ok = $true; Detail = "a<-b=$aHas22 b<-a=$bHas11 corrupt=a:$aHas0/b:$bHas0" })
    } else {
        $results.Add((New-Fail "test_noclip_drift" "expected normal a=11 b=22, cross-drift, and corrupt; aHas11=$aHas11 bHas22=$bHas22 aHas22=$aHas22 bHas11=$bHas11 a0=$aHas0 b0=$bHas0"))
    }
}

# ---- 4) shuffleback：全部返回并重排后，a 有时显示 22、b 有时显示 11 ----
$c = Compile-Test -Name "test_noclip_shuffleback"
if (-not $c.Ok) { $results.Add((New-Fail "test_noclip_shuffleback" $c.Detail)) }
else {
    $out = Run-Many -Exe $c.Exe
    $swapA22 = 0; $swapB11 = 0
    # 每轮有 10 行。shuffleback 之后的 a 输出在第 2、6 行（a）与第 3、7 行（b）。
    for ($run = 0; $run -lt $runCount; $run++) {
        $r0 = $out[$run * 10 + 2]; $r1 = $out[$run * 10 + 3]
        $r2 = $out[$run * 10 + 6]; $r3 = $out[$run * 10 + 7]
        if ($r0 -eq "22" -or $r2 -eq "22") { $swapA22++ }
        if ($r1 -eq "11" -or $r3 -eq "11") { $swapB11++ }
    }
    if ($swapA22 -gt 0 -and $swapB11 -gt 0) {
        $results.Add([pscustomobject]@{ Name = "test_noclip_shuffleback"; Ok = $true; Detail = "swapA22=$swapA22 swapB11=$swapB11" })
    } else {
        $results.Add((New-Fail "test_noclip_shuffleback" "expected post-shuffle a=22/b=11 sometimes; got swapA22=$swapA22 swapB11=$swapB11"))
    }
}

# ---- 5) long 不稳定读取 ----
$c = Compile-Test -Name "test_noclip_long"
if (-not $c.Ok) { $results.Add((New-Fail "test_noclip_long" $c.Detail)) }
else {
    $out = Run-Many -Exe $c.Exe
    $okN = @($out | Where-Object { $_ -eq "10000000000" }).Count
    $ok0 = @($out | Where-Object { $_ -eq "0" }).Count
    if ($okN -gt 0 -and $ok0 -gt 0) {
        $results.Add([pscustomobject]@{ Name = "test_noclip_long"; Ok = $true; Detail = "normal=$okN corrupt=$ok0" })
    } else {
        $results.Add((New-Fail "test_noclip_long" "need both '10000000000' and '0'; got normal=$okN corrupt=$ok0"))
    }
}

# ---- 6) 混合传播到派生变量：sum/k 从 base+const 得到不稳定和 ----
$c = Compile-Test -Name "test_noclip_mixed"
if (-not $c.Ok) { $results.Add((New-Fail "test_noclip_mixed" $c.Detail)) }
else {
    $out = Run-Many -Exe $c.Exe
    $valid = $true; $detail = ""
    # 行布局：base, sum15, sum25, sum35, k, msg x5
    $allowedBase = @("5", "0")
    $allowedSum1 = @("15", "10")
    $allowedSum2 = @("25", "20")
    $allowedSum3 = @("35", "30")
    $allowedK   = @("6", "1")
    $allowedMsg = @("ok", "")
    for ($run = 0; $run -lt $runCount; $run++) {
        $base = $out[$run * 10 + 0]
        $s1   = $out[$run * 10 + 1]
        $s2   = $out[$run * 10 + 2]
        $s3   = $out[$run * 10 + 3]
        $k    = $out[$run * 10 + 4]
        $m0   = $out[$run * 10 + 5]
        if ($base -notin $allowedBase -or $s1 -notin $allowedSum1 -or $s2 -notin $allowedSum2 -or
            $s3 -notin $allowedSum3 -or $k -notin $allowedK -or $m0 -notin $allowedMsg) {
            $valid = $false; $detail = "run=$run base=$base s1=$s1 s2=$s2 s3=$s3 k=$k m0='$m0'"; break
        }
    }
    if ($valid) {
        $results.Add([pscustomobject]@{ Name = "test_noclip_mixed"; Ok = $true; Detail = "all derived values within backrooms ranges" })
    } else {
        $results.Add((New-Fail "test_noclip_mixed" $detail))
    }
}

# ---- 7) 未定义变量：必须编译失败 ----
$c = Compile-Test -Name "test_noclip_error"
if ($c.Ok) {
    $results.Add((New-Fail "test_noclip_error" "expected compile failure for undefined noclip target"))
} else {
    $results.Add([pscustomobject]@{ Name = "test_noclip_error"; Ok = $true; Detail = "compile rejected undefined noclip target" })
}

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