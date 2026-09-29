param(
    [string]$Xfawac = "C:\Users\Administrator\Desktop\xfawa-exp\build\Release\xfawac.exe"
)

# Test harness for the EXP `zombie` feature (the arithmetic plague).
# Scenarios covered by tests/exp/test_zombie_*.xf:
#   1. basic    : `c = a + b` with infected a takes a's value, b stays healthy
#   2. ops      : + - * / % and unary minus all annihilate to the plague value
#   3. persist  : the plague is permanent and infects every later expression
#   4. logic    : ==, >, <, if and a healthy+plague sum keep their normal meaning
#   5. call     : the plague never crosses a function boundary
#   6. types    : float arithmetic carries the plague
#   7. list     : a plague value may not be stored into a list (compile error)
#   8. array_ok : a list of healthy numbers is untouched
#   9. loop     : a plague inside a loop body is a fixed point of its own sum
#   a. clean    : a clean expression never catches
#
# Run with: pwsh -NoProfile -File tests/run_zombie_tests.ps1

$ErrorActionPreference = "Continue"
$tests = Join-Path $PSScriptRoot "exp"
$workDir = Join-Path $env:TEMP "xfawa_zombie_test"
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
    param([string]$Name, [string]$Pattern = "zombie")
    # [zombie] ASCII prefix survives pwsh codepage capture.
    $src = Join-Path $tests "$Name.xf"
    $out = Join-Path $workDir "$Name.exe"
    Remove-Item -LiteralPath $out -ErrorAction SilentlyContinue
    $stdout = (& $Xfawac $src -o $out 2>&1) -join "`n"
    $code = $LASTEXITCODE
    $ok = $code -ne 0 -and $stdout -match $Pattern -and (-not (Test-Path -LiteralPath $out))
    $detail = if ($ok) { "" } else { "exit=$code matched=$($stdout -match $Pattern) exe=$(Test-Path -LiteralPath $out)" }
    return [pscustomobject]@{ Name = $Name; Ok = $ok; Detail = $detail }
}

$results.Add((Compile-Test -Name "test_zombie_basic" -ExpectExit 0))
$results.Add((Run-Test -Name "test_zombie_basic" -Expected @("5", "7", "5", "7", "5")))
$results.Add((Compile-Test -Name "test_zombie_ops" -ExpectExit 0))
$results.Add((Run-Test -Name "test_zombie_ops" -Expected @("10", "10", "10", "10", "10", "10", "10", "2")))
$results.Add((Compile-Test -Name "test_zombie_persist" -ExpectExit 0))
$results.Add((Run-Test -Name "test_zombie_persist" -Expected @("1", "2", "101", "100", "1", "100")))
$results.Add((Compile-Test -Name "test_zombie_logic" -ExpectExit 0))
$results.Add((Run-Test -Name "test_zombie_logic" -Expected @("1", "3", "0", "0", "1", "3", "7")))
$results.Add((Compile-Test -Name "test_zombie_call" -ExpectExit 0))
$results.Add((Run-Test -Name "test_zombie_call" -Expected @("6", "5", "7", "5")))
$results.Add((Compile-Test -Name "test_zombie_types" -ExpectExit 0))
$results.Add((Run-Test -Name "test_zombie_types" -Expected @("1.500000", "1.500000", "2.500000", "10.000000")))
$results.Add((Compile-Fail -Name "test_zombie_list" -Pattern "zombie"))
$results.Add((Compile-Test -Name "test_zombie_array_ok" -ExpectExit 0))
$results.Add((Run-Test -Name "test_zombie_array_ok" -Expected @("4", "6", "7")))
$results.Add((Compile-Test -Name "test_zombie_loop" -ExpectExit 0))
$results.Add((Run-Test -Name "test_zombie_loop" -Expected @("3", "7", "7")))
$results.Add((Compile-Test -Name "test_zombie_clean" -ExpectExit 0))
$results.Add((Run-Test -Name "test_zombie_clean" -Expected @("2", "3", "5")))

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
