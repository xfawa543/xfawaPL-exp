param(
    [string]$Xfawac = "C:\Users\Administrator\Desktop\xfawa-exp\build\Release\xfawac.exe"
)

# Test harness for the EXP `dual` one-existence-into-two-self keyword
# (docs/exp/dual.md). Covers tests/exp/test_dual.xf sections:
#   d1 birth         (dual x = 10 -> both selves 10)
#   d2 independence  (x = x+1 touches only the original, x[1]+5 the second)
#   d3 divergence    (multiplication on second self, x[0] == original self)
#   d4 split current (dual x on an existing variable, both grow independently)
#   d5 float         (fractional split and independent evolution)
#   d6 bool          (true split; x[1] = false flips only the second self)
# Plus five compile-error cases (string split / write to non-dual / index
# out of range / splitting an undeclared variable / type-mismatch re-split).

$ErrorActionPreference = "Continue"
$tests = Join-Path $PSScriptRoot "exp"
$workDir = Join-Path $env:TEMP "xfawa_dual_test"
New-Item -ItemType Directory -Force -Path $workDir | Out-Null

if (-not (Test-Path -LiteralPath $Xfawac)) {
    Write-Error "xfawac not found: $Xfawac (build first, then run this script)"
}

$results = [System.Collections.Generic.List[object]]::new()

# --- compile main dual test: must succeed ---
$src = Join-Path $tests "test_dual.xf"
$out = Join-Path $workDir "test_dual.exe"
Remove-Item -LiteralPath $out -ErrorAction SilentlyContinue
$cstdout = (& $Xfawac $src -o $out 2>&1) -join "`n"
$ccode = $LASTEXITCODE
$cFails = @()
if ($ccode -ne 0) { $cFails += "exit=$ccode expected=0" }
$results.Add([pscustomobject]@{ Name = "test_dual.compile"; Ok = ($cFails.Count -eq 0); Detail = ($cFails -join "; ") })

# --- runtime checks ---
$rFails = @()
if ($ccode -eq 0 -and (Test-Path -LiteralPath $out)) {
    $rout = @(& $out 2>&1 | ForEach-Object { $_.TrimEnd("`r") })

    # Section markers END the preceding section (same as run_pinocchio_tests.ps1).
    $sections = [System.Collections.Generic.Dictionary[string, System.Collections.Generic.List[string]]]::new()
    $pending = [System.Collections.Generic.List[string]]::new()
    foreach ($line in $rout) {
        if ($line -match '^d[1-6]$') {
            $sections[$line] = $pending
            $pending = [System.Collections.Generic.List[string]]::new()
        } else {
            $pending.Add($line)
        }
    }

    # d1: birth -> both selves 10.
    $d1 = @($sections["d1"])
    if (($d1 -join "|") -ne "10|10") {
        $rFails += "[d1] expected '10|10' got '$(($d1 -join '|'))'"
    }

    # d2: independence -> original 11, second 15.
    $d2 = @($sections["d2"])
    if (($d2 -join "|") -ne "11|15") {
        $rFails += "[d2] expected '11|15' got '$(($d2 -join '|'))'"
    }

    # d3: second self doubled to 30, original reduced to 8, x[0] == x.
    $d3 = @($sections["d3"])
    if (($d3 -join "|") -ne "30|8|8") {
        $rFails += "[d3] expected '30|8|8' got '$(($d3 -join '|'))'"
    }

    # d4: split current value -> 7 7, then independent growth -> 8 10.
    $d4 = @($sections["d4"])
    if (($d4 -join "|") -ne "7|7|8|10") {
        $rFails += "[d4] expected '7|7|8|10' got '$(($d4 -join '|'))'"
    }

    # d5: float split -> 1.500000 twice, second self +2.5 -> 4.000000.
    $d5 = @($sections["d5"])
    if (($d5 -join "|") -ne "1.500000|1.500000|4.000000") {
        $rFails += "[d5] expected '1.500000|1.500000|4.000000' got '$(($d5 -join '|'))'"
    }

    # d6: bool split true -> 1 1, flag[1]=false flips only the second -> 0 1.
    $d6 = @($sections["d6"])
    if (($d6 -join "|") -ne "1|1|0|1") {
        $rFails += "[d6] expected '1|1|0|1' got '$(($d6 -join '|'))'"
    }
}
$results.Add([pscustomobject]@{ Name = "test_dual.runtime"; Ok = ($rFails.Count -eq 0); Detail = ($rFails -join "; ") })

# --- compile-error: splitting a string variable ---
$failString = Join-Path $tests "test_dual_fail_string.xf"
$f1out = Join-Path $workDir "fail_string.exe"
Remove-Item -LiteralPath $f1out -ErrorAction SilentlyContinue
$s1 = (& $Xfawac $failString -o $f1out 2>&1) -join "`n"
$f1 = @()
if ($LASTEXITCODE -eq 0) { $f1 += "compile unexpectedly succeeded" }
if ($s1 -notmatch 'cannot be split into two independent existences') { $f1 += "missing 'cannot be split...' in output" }
$results.Add([pscustomobject]@{ Name = "test_dual.fail_string"; Ok = ($f1.Count -eq 0); Detail = ($f1 -join "; ") })

# --- compile-error: writing to a non-dual variable's index ---
$failNotDual = Join-Path $tests "test_dual_fail_not_dual.xf"
$f2out = Join-Path $workDir "fail_not_dual.exe"
Remove-Item -LiteralPath $f2out -ErrorAction SilentlyContinue
$s2 = (& $Xfawac $failNotDual -o $f2out 2>&1) -join "`n"
$f2 = @()
if ($LASTEXITCODE -eq 0) { $f2 += "compile unexpectedly succeeded" }
if ($s2 -notmatch 'is not a dual variable') { $f2 += "missing 'is not a dual variable' in output" }
$results.Add([pscustomobject]@{ Name = "test_dual.fail_not_dual"; Ok = ($f2.Count -eq 0); Detail = ($f2 -join "; ") })

# --- compile-error: index out of range (must be 0 or 1) ---
$failRange = Join-Path $tests "test_dual_fail_range.xf"
$f3out = Join-Path $workDir "fail_range.exe"
Remove-Item -LiteralPath $f3out -ErrorAction SilentlyContinue
$s3 = (& $Xfawac $failRange -o $f3out 2>&1) -join "`n"
$f3 = @()
if ($LASTEXITCODE -eq 0) { $f3 += "compile unexpectedly succeeded" }
if ($s3 -notmatch 'exactly two selves') { $f3 += "missing 'exactly two selves' in output" }
$results.Add([pscustomobject]@{ Name = "test_dual.fail_range"; Ok = ($f3.Count -eq 0); Detail = ($f3 -join "; ") })

# --- compile-error: splitting an undeclared variable ---
$failUndeclared = Join-Path $tests "test_dual_fail_undeclared.xf"
$f4out = Join-Path $workDir "fail_undeclared.exe"
Remove-Item -LiteralPath $f4out -ErrorAction SilentlyContinue
$s4 = (& $Xfawac $failUndeclared -o $f4out 2>&1) -join "`n"
$f4 = @()
if ($LASTEXITCODE -eq 0) { $f4 += "compile unexpectedly succeeded" }
if ($s4 -notmatch 'has no existence to split yet') { $f4 += "missing 'has no existence to split yet' in output" }
$results.Add([pscustomobject]@{ Name = "test_dual.fail_undeclared"; Ok = ($f4.Count -eq 0); Detail = ($f4 -join "; ") })

# --- compile-error: re-splitting with a different base type ---
$failMismatch = Join-Path $tests "test_dual_fail_mismatch.xf"
$f5out = Join-Path $workDir "fail_mismatch.exe"
Remove-Item -LiteralPath $f5out -ErrorAction SilentlyContinue
$s5 = (& $Xfawac $failMismatch -o $f5out 2>&1) -join "`n"
$f5 = @()
if ($LASTEXITCODE -eq 0) { $f5 += "compile unexpectedly succeeded" }
if ($s5 -notmatch 'must keep the same base type') { $f5 += "missing 'must keep the same base type' in output" }
$results.Add([pscustomobject]@{ Name = "test_dual.fail_mismatch"; Ok = ($f5.Count -eq 0); Detail = ($f5 -join "; ") })

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