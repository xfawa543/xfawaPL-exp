param(
    [string]$Xfawac = "C:\Users\Administrator\Desktop\xfawa-exp\build\Release\xfawac.exe"
)

# Test harness for the four EXP syntaxes added together: `deny`, `regret`/`doubt`,
# `a <->[K] b` and `env`. Scenarios covered by tests/exp/test_*.xf:
#   1. deny_basic   : `deny answer = 41` keeps the value, stops acknowledging it
#                      (answer == 41 -> 0, answer != 41 -> 1, other compares intact)
#   2. deny_believe : `deny "2 + 2 = 5"` takes a believe rule back (2 + 2 -> 4)
#   3. regret_basic : `regret "1 + 1 = 3"` undoes one rule, `regret all` undoes the rest
#   4. doubt_basic  : `doubt answer` inside a lie breaks it for the rest of the block
#   5. reaction     : `a <->[1] b` closes half the gap each step, a + b conserved
#   6. reaction ratio: K = 2 balances at the fractional point 100/3
#   7. env_basic    : the carried value rides along on its own, `env who = v` re-points it
#   8. env_chain    : the value survives a two-hop chain nobody passes by hand
#   f1. deny noeq   : `deny answer` without a value is a parse error
#   f2. regret none : regretting a belief the program never had is an error
#   f3. doubt none  : doubting a variable no lie covers is an error
#   f4. reaction same: a variable cannot react with itself
#   f5. reaction float: a float variable cannot take part in a reaction
#   f6. reaction name : an undeclared variable is an error
#   f7. env unknown : an env block may only list functions that exist
#   f8. env notfirst: the carried value must be the function's first parameter
#   f9. env assign  : `env x = v` only works inside a function that carries x
#
# Run with: pwsh -NoProfile -File tests/run_deny_regret_env_tests.ps1

$ErrorActionPreference = "Continue"
# The compiler writes UTF-8; without this the piped output is decoded with the
# console's own code page and the Chinese parts of a message cannot be matched.
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }
$tests = Join-Path $PSScriptRoot "exp"
$workDir = Join-Path $env:TEMP "xfawa_deny_regret_env_test"
New-Item -ItemType Directory -Force -Path $workDir | Out-Null

if (-not (Test-Path -LiteralPath $Xfawac)) {
    Write-Error "xfawac not found: $Xfawac (build first, then run this script)"
}

$results = [System.Collections.Generic.List[object]]::new()

function Compile-Test {
    param(
        [string]$Name,
        [int]$ExpectExit
    )
    $src = Join-Path $tests "$Name.xf"
    $out = Join-Path $workDir "$Name.exe"
    Remove-Item -LiteralPath $out -ErrorAction SilentlyContinue
    $stdout = (& $Xfawac $src -o $out 2>&1) -join "`n"
    $code = $LASTEXITCODE
    $failures = @()
    if ($code -ne $ExpectExit) { $failures += "exit=$code expected=$ExpectExit" }
    return [pscustomobject]@{ Name = $Name; Ok = ($failures.Count -eq 0); Detail = ($failures -join "; ") }
}

function Run-Test {
    param([string]$Name, [string[]]$Expected, [int]$ExpectExit = 0)
    $exe = Join-Path $workDir "$Name.exe"
    if (-not (Test-Path -LiteralPath $exe)) {
        return [pscustomobject]@{ Name = "$Name.runtime"; Ok = $false; Detail = "no exe produced" }
    }
    $rout = (& $exe 2>&1) | ForEach-Object { $_ -replace "`r`n", "`n" }
    $actual = @($rout | ForEach-Object { $_.TrimEnd("`r") })
    $failures = @()
    if ($LASTEXITCODE -ne $ExpectExit) { $failures += "exit=$($LASTEXITCODE) expected=$ExpectExit" }
    if ($ExpectExit -eq 0) {
        if ($actual.Count -ne $Expected.Count) {
            $failures += "lines=$($actual.Count) expected=$($Expected.Count)"
            for ($i = 0; $i -lt [Math]::Min($actual.Count, $Expected.Count); $i++) {
                if ($actual[$i] -ne $Expected[$i]) { $failures += "line $($i+1): got '$($actual[$i])' expected '$($Expected[$i])'"; break }
            }
        } elseif ($null -ne (Compare-Object $actual $Expected)) {
            $m = Compare-Object $actual $Expected | Select-Object -First 1
            $failures += "output differs: '$($m.InputObject)'"
        }
    }
    return [pscustomobject]@{ Name = "$Name.runtime"; Ok = ($failures.Count -eq 0); Detail = ($failures -join "; ") }
}

function Compile-Fail {
    param([string]$Name, [string]$Pattern)
    $src = Join-Path $tests "$Name.xf"
    $out = Join-Path $workDir "$Name.exe"
    Remove-Item -LiteralPath $out -ErrorAction SilentlyContinue
    $stdout = (& $Xfawac $src -o $out 2>&1) -join "`n"
    $code = $LASTEXITCODE
    $ok = $code -ne 0 -and $stdout -match $Pattern -and (-not (Test-Path -LiteralPath $out))
    $detail = if ($ok) { "" } else { "exit=$code matched=$($stdout -match $Pattern) exe=$(Test-Path -LiteralPath $out)" }
    return [pscustomobject]@{ Name = $Name; Ok = $ok; Detail = $detail }
}

$results.Add((Compile-Test -Name "test_deny_basic" -ExpectExit 0))
$results.Add((Run-Test -Name "test_deny_basic" -Expected @("1", "41", "0", "1", "0", "1")))
$results.Add((Compile-Test -Name "test_deny_believe" -ExpectExit 0))
$results.Add((Run-Test -Name "test_deny_believe" -Expected @("5", "4", "6")))
$results.Add((Compile-Test -Name "test_regret_basic" -ExpectExit 0))
$results.Add((Run-Test -Name "test_regret_basic" -Expected @("3", "2", "7", "6")))
$results.Add((Compile-Test -Name "test_doubt_basic" -ExpectExit 0))
$results.Add((Run-Test -Name "test_doubt_basic" -Expected @("42", "41", "41")))
$results.Add((Compile-Test -Name "test_reaction_basic" -ExpectExit 0))
$results.Add((Run-Test -Name "test_reaction_basic" -Expected @("30", "70", "40", "60", "45", "55")))
$results.Add((Compile-Test -Name "test_reaction_ratio" -ExpectExit 0))
$results.Add((Run-Test -Name "test_reaction_ratio" -Expected @("16", "84", "24", "76", "28", "72")))
$results.Add((Compile-Test -Name "test_env_basic" -ExpectExit 0))
$results.Add((Run-Test -Name "test_env_basic" -Expected @("1", "2", "3", "4")))
$results.Add((Compile-Test -Name "test_env_chain" -ExpectExit 0))
$results.Add((Run-Test -Name "test_env_chain" -Expected @("7")))
# The submission's own example: three env blocks feed 攻击 (attack) three carried
# values, it re-points 发出者 (sender) to 承受者 (receiver) and 接受伤害
# (takeDamage) writes the bare field 血量 (health) through the implicit this.
$results.Add((Compile-Test -Name "test_env_full" -ExpectExit 0))
$results.Add((Run-Test -Name "test_env_full" -Expected @("2", "78")))
$results.Add((Compile-Fail -Name "test_fail_deny_noeq" -Pattern "deny"))
$results.Add((Compile-Fail -Name "test_fail_regret_none" -Pattern "regret"))
$results.Add((Compile-Fail -Name "test_fail_doubt_none" -Pattern "doubt"))
$results.Add((Compile-Fail -Name "test_fail_reaction_same" -Pattern "different variables"))
$results.Add((Compile-Fail -Name "test_fail_reaction_float" -Pattern "int/long"))
$results.Add((Compile-Fail -Name "test_fail_reaction_unknown" -Pattern "nosuch"))
$results.Add((Compile-Fail -Name "test_fail_env_unknown" -Pattern "nosuch"))
$results.Add((Compile-Fail -Name "test_fail_env_notfirst" -Pattern "携带列表的第 1 位"))
$results.Add((Compile-Fail -Name "test_fail_env_assign" -Pattern "env"))
# Two carried types own the same field: the bare name is ambiguous.
$results.Add((Compile-Fail -Name "test_fail_env_ambiguous" -Pattern "不能省略"))
# The type on an env block must be a basic type or a declared struct.
$results.Add((Compile-Fail -Name "test_fail_env_badtype" -Pattern "未知类型"))

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
