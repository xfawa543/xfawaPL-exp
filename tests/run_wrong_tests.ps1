param(
    [string]$Xfawac = "C:\Users\Administrator\Desktop\xfawa-exp\build\Release\xfawac.exe"
)

# Test harness for the EXP `wrong` feature (this session's spec; kept +1-first).
# Scenarios covered by tests/exp/test_wrong_*.xf:
#   1. basic   : wrong x == 10 escapes to x = 11..15 (int probe: random 1..5 nudge)
#   2. multi   : three sequential wrongs (10/11/12) all escape, x past all three
#   3. range   : wrong x > 5 lands exactly on the boundary 5
#   4. derived : wrong z == 30 (z = x + y) bumps root x (+1..5), z recomputes x + y
#   5. probe   : wrong x + y == 30 bumps the first root (+1..5), y untouched
#   6. persist : assignment re-testing keeps evasion alive (x = 10 -> 11..15)
#   7. nomatch : non-triggering wrong is a no-op
#   8. float   : wrong v == 10.0 -> 10.1 (10% of |v| floored at 0.01)
#   9. bool    : wrong b toggles true -> false (prints 0)
#   a. constant: wrong with no variables is a compile error
#   b. lie     : lie-shadowed variable -> constant -> same compile error
#   c. blocked : no escape exists -> prints the message and exit(1)
#
# Run with: pwsh -NoProfile -File tests/run_wrong_tests.ps1

$ErrorActionPreference = "Continue"
$tests = Join-Path $PSScriptRoot "exp"
$workDir = Join-Path $env:TEMP "xfawa_wrong_test"
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
    if ($expectExit -eq 0) {
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
    param([string]$Name)
    # [wrong:] ASCII prefix survives pwsh codepage capture.
    $src = Join-Path $tests "$Name.xf"
    $out = Join-Path $workDir "$Name.exe"
    Remove-Item -LiteralPath $out -ErrorAction SilentlyContinue
    $stdout = (& $Xfawac $src -o $out 2>&1) -join "`n"
    $code = $LASTEXITCODE
    $ok = $code -ne 0 -and $stdout -match "wrong:" -and (-not (Test-Path -LiteralPath $out))
    $detail = if ($ok) { "" } else { "exit=$code matched=$($stdout -match 'wrong:') exe=$(Test-Path -LiteralPath $out)" }
    return [pscustomobject]@{ Name = $Name; Ok = $ok; Detail = $detail }
}

$results.Add((Compile-Test -Name "test_wrong_basic" -ExpectExit 0))
$results.Add((Run-Test -Name "test_wrong_basic" -Expected @("1", "1", "1")))
$results.Add((Compile-Test -Name "test_wrong_multi" -ExpectExit 0))
$results.Add((Run-Test -Name "test_wrong_multi" -Expected @("1", "1", "1", "1")))
$results.Add((Compile-Test -Name "test_wrong_range" -ExpectExit 0))
$results.Add((Run-Test -Name "test_wrong_range" -Expected @("5")))
$results.Add((Compile-Test -Name "test_wrong_derived" -ExpectExit 0))
$results.Add((Run-Test -Name "test_wrong_derived" -Expected @("1", "1", "20", "1", "1")))
$results.Add((Compile-Test -Name "test_wrong_probe" -ExpectExit 0))
$results.Add((Run-Test -Name "test_wrong_probe" -Expected @("1", "1", "20")))
$results.Add((Compile-Test -Name "test_wrong_persist" -ExpectExit 0))
$results.Add((Run-Test -Name "test_wrong_persist" -Expected @("1", "1")))
$results.Add((Compile-Test -Name "test_wrong_nomatch" -ExpectExit 0))
$results.Add((Run-Test -Name "test_wrong_nomatch" -Expected @("11")))
$results.Add((Compile-Test -Name "test_wrong_float" -ExpectExit 0))
$results.Add((Run-Test -Name "test_wrong_float" -Expected @("10.100000")))
$results.Add((Compile-Test -Name "test_wrong_bool" -ExpectExit 0))
$results.Add((Run-Test -Name "test_wrong_bool" -Expected @("0")))
$results.Add((Compile-Fail -Name "test_wrong_constant"))
$results.Add((Compile-Fail -Name "test_wrong_lie"))
$results.Add((Compile-Test -Name "test_wrong_blocked" -ExpectExit 0))
$results.Add((Run-Test -Name "test_wrong_blocked" -Expected @() -ExpectExit 1))

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