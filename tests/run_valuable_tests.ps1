param(
    [string]$Xfawac = "C:\Users\Administrator\Desktop\xfawa-exp\build\Release\xfawac.exe"
)

# Test harness for the EXP `valuable` feature (values that are code).
# Scenarios covered by tests/exp/test_valuable_*.xf:
#   1. basic        : for/in/x+item are code, a juxtaposition splices a block
#   2. lazy         : creating a code value runs nothing, only a use point runs it
#   3. cat          : `+` joins code at compile time, `* n` repeats it
#   4. call         : `call valuable A for x` returns an ordinary value
#   5. inject       : `inject` substitutes a free name at the token level
#   6. jux          : juxtaposed code runs left to right, print renders `code`
#   7. reuse        : the same code value runs many times, also inside a loop
#   8. empty        : `* 0` is a no-op, a code run never catches the plague
#   9. scopes       : the same code value name resolves per function scope
#  10. arg          : a code value passed as an argument, compiled into the callee
#  11. arg_scope    : the same code over two use points, one copy each
#  12. arg_mixed    : a code argument beside ordinary ones, over a list
#  13. factory      : a code factory: `return <code>` is the code it builds
#  14. factory_mix  : a factory builds code in pieces and hands a code value back
#   f1. missing var : a free name nobody has is an error
#   f2. not code    : only code values may be juxtaposed
#   f3. bad inject  : inject may only bind a name the fragment mentions
#   f4. plain value : an ordinary value is not code
#   f5. arg_assign  : a code value parameter cannot be assigned to
#   f6. arg_ordinary: a code value parameter stays a code value at every call
#   f7. factory param: a factory's code cannot mention a runtime parameter
#   f8. factory ret : only a code factory may return a code value
#   f9. factory rec: a factory cannot call itself
#  f10. arg collision: the code and the function share one name space
#  f11. disposable   : a one-shot function cannot be copied per call site
#  f12. drift        : a drifting function's random argument has nowhere to go
#
# Run with: pwsh -NoProfile -File tests/run_valuable_tests.ps1

$ErrorActionPreference = "Continue"
# The compiler writes UTF-8; without this the piped output is decoded with the
# console's own code page and the Chinese parts of a message cannot be matched.
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }
$tests = Join-Path $PSScriptRoot "exp"
$workDir = Join-Path $env:TEMP "xfawa_valuable_test"
New-Item -ItemType Directory -Force -Path $workDir | Out-Null

if (-not (Test-Path -LiteralPath $Xfawac)) {
    Write-Error "xfawac not found: $Xfawac (build first, then run this script)"
}

$results = [System.Collections.Generic.List[object]]::new()

function Run-Test {
    param([string]$Name, [string[]]$Expected)
    $src = Join-Path $tests "$Name.xf"
    $out = Join-Path $workDir "$Name.exe"
    Remove-Item -LiteralPath $out -ErrorAction SilentlyContinue
    $cstdout = (& $Xfawac $src -o $out 2>&1) -join "`n"
    $ccode = $LASTEXITCODE
    if ($ccode -ne 0) {
        $line = ($cstdout -split "`n" | Where-Object { $_ -match "\[error" } | Select-Object -First 1)
        return [pscustomobject]@{ Name = $Name; Ok = $false; Detail = "compile failed: $line" }
    }
    $rout = (& $out 2>&1) | ForEach-Object { $_ -replace "`r`n", "`n" }
    $actual = @($rout | ForEach-Object { $_.TrimEnd("`r") })
    $failures = @()
    if ($LASTEXITCODE -ne 0) { $failures += "exit=$($LASTEXITCODE) expected=0" }
    if ($actual.Count -ne $Expected.Count) {
        $failures += "lines=$($actual.Count) expected=$($Expected.Count) (got: $($actual -join '|'))"
    } else {
        for ($i = 0; $i -lt $Expected.Count; $i++) {
            if ($actual[$i] -ne $Expected[$i]) {
                $failures += "line $($i+1): got '$($actual[$i])' expected '$($Expected[$i])'"
                break
            }
        }
    }
    return [pscustomobject]@{ Name = $Name; Ok = ($failures.Count -eq 0); Detail = ($failures -join "; ") }
}

function Compile-Fail {
    param([string]$Name, [string]$Pattern = "valuable")
    $src = Join-Path $tests "$Name.xf"
    $out = Join-Path $workDir "$Name.exe"
    Remove-Item -LiteralPath $out -ErrorAction SilentlyContinue
    $cstdout = (& $Xfawac $src -o $out 2>&1) -join "`n"
    $ccode = $LASTEXITCODE
    $ok = $ccode -ne 0 -and $cstdout -match $Pattern -and (-not (Test-Path -LiteralPath $out))
    $detail = if ($ok) { "" } else { "exit=$ccode matched=$($cstdout -match $Pattern) exe=$(Test-Path -LiteralPath $out)" }
    return [pscustomobject]@{ Name = $Name; Ok = $ok; Detail = $detail }
}

$results.Add((Run-Test -Name "test_valuable_basic" -Expected @("10")))
$results.Add((Run-Test -Name "test_valuable_lazy"  -Expected @("0", "7")))
$results.Add((Run-Test -Name "test_valuable_cat"   -Expected @("1", "2", "hi", "hi", "hi", "11")))
$results.Add((Run-Test -Name "test_valuable_call"  -Expected @("3", "12", "2")))
$results.Add((Run-Test -Name "test_valuable_inject" -Expected @("15", "25")))
$results.Add((Run-Test -Name "test_valuable_jux"   -Expected @("one", "two", "three", "one", "two", "code")))
$results.Add((Run-Test -Name "test_valuable_reuse" -Expected @("3", "9")))
$results.Add((Run-Test -Name "test_valuable_empty" -Expected @("3", "4")))
$results.Add((Run-Test -Name "test_valuable_scopes" -Expected @("11", "101")))
$results.Add((Run-Test -Name "test_valuable_arg" -Expected @("10")))
$results.Add((Run-Test -Name "test_valuable_arg_scope" -Expected @("1", "101")))
$results.Add((Run-Test -Name "test_valuable_arg_mixed" -Expected @("8", "1", "0", "4")))
$results.Add((Run-Test -Name "test_valuable_factory" -Expected @("1", "2")))
$results.Add((Run-Test -Name "test_valuable_factory_mix" -Expected @("b", "t", "t", "d", "d")))
$results.Add((Compile-Fail -Name "test_valuable_fail_missing" -Pattern "valuable"))
$results.Add((Compile-Fail -Name "test_valuable_fail_notcode" -Pattern "valuable"))
$results.Add((Compile-Fail -Name "test_valuable_fail_inject"  -Pattern "valuable"))
$results.Add((Compile-Fail -Name "test_valuable_fail_value"   -Pattern "valuable"))
$results.Add((Compile-Fail -Name "test_valuable_fail_arg_assign" -Pattern "代码值参数"))
$results.Add((Compile-Fail -Name "test_valuable_fail_arg_ordinary" -Pattern "代码值参数"))
$results.Add((Compile-Fail -Name "test_valuable_fail_factory_param" -Pattern "代码工厂"))
$results.Add((Compile-Fail -Name "test_valuable_fail_factory_return" -Pattern "代码工厂"))
$results.Add((Compile-Fail -Name "test_valuable_fail_factory_recursive" -Pattern "不能调用自己"))
$results.Add((Compile-Fail -Name "test_valuable_fail_arg_collision" -Pattern "valuable"))
$results.Add((Compile-Fail -Name "test_valuable_fail_disposable_arg" -Pattern "disposable"))
$results.Add((Compile-Fail -Name "test_valuable_fail_drift_arg" -Pattern "drift"))

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
