param(
    [string]$Xfawac = "C:\Users\Administrator\Desktop\xfawa-exp\build\Release\xfawac.exe"
)

# Test harness for the EXP interest syntax: `¥[r] name = v` (simple interest,
# U+00A5 / U+FFE5) and `$[r] name = v` (compound interest). Rate defaults to
# 0.0001; every READ returns the current float value first, then settles the
# interest (fixed amount = principal * rate for ¥; a += a * rate for $), so
# multiple reads inside one expression each see a growing value. Covers
# tests/exp/test_interest.xf sections:
#   i1  three single reads of a ¥[0.1] variable -> 10,11,12
#   i2  a+a+a reads 11,12,13 -> 36, then next read 14
#   i3  default rate 0.0001 -> 100, 100.010002
#   i4..i6  $[0.1] compound -> 10, 11, 12.1
#   i7..i8  stacked ¥ + ¥ + $ rules -> 10 then 13.2
#   i9..i10  plain assignment then reads -> 20, 21 (rule keeps applying)
#   i11..i12  e = e + 5 read-then-grow -> 5, 5.5
# Plus four compile-error cases (already-typed float veto, string value,
# missing '=', bad rate token).

$ErrorActionPreference = "Continue"
$tests = Join-Path $PSScriptRoot "exp"
$workDir = Join-Path $env:TEMP "xfawa_interest_test"
New-Item -ItemType Directory -Force -Path $workDir | Out-Null

if (-not (Test-Path -LiteralPath $Xfawac)) {
    Write-Error "xfawac not found: $Xfawac (build first, then run this script)"
}

$results = [System.Collections.Generic.List[object]]::new()

# --- compile main interest test: must succeed ---
$src = Join-Path $tests "test_interest.xf"
$out = Join-Path $workDir "test_interest.exe"
Remove-Item -LiteralPath $out -ErrorAction SilentlyContinue
$cstdout = (& $Xfawac $src -o $out 2>&1) -join "`n"
$ccode = $LASTEXITCODE
$cFails = @()
if ($ccode -ne 0) { $cFails += "exit=$ccode expected=0" }
$results.Add([pscustomobject]@{ Name = "test_interest.compile"; Ok = ($cFails.Count -eq 0); Detail = ($cFails -join "; ") })

# --- runtime checks ---
$rFails = @()
if ($ccode -eq 0 -and (Test-Path -LiteralPath $out)) {
    $rout = @(& $out 2>&1 | ForEach-Object { $_.TrimEnd("`r") })

    # Section markers END the preceding section (same as run_disposable_tests.ps1).
    $sections = [System.Collections.Generic.Dictionary[string, System.Collections.Generic.List[string]]]::new()
    $pending = [System.Collections.Generic.List[string]]::new()
    foreach ($line in $rout) {
        if ($line -match '^i(1[0-2]|[1-9])$') {
            $sections[$line] = $pending
            $pending = [System.Collections.Generic.List[string]]::new()
        } else {
            $pending.Add($line)
        }
    }

    # i1: ¥[0.1]a=10 returns 10 (reads settle +1, stored 11).
    $i1 = @($sections["i1"])
    if (($i1 -join "|") -ne "10.000000") {
        $rFails += "[i1] expected '10.000000' got '$(($i1 -join '|'))'"
    }

    # i2: a is 11 now; a+a+a reads 11,12,13 -> 36; next read 14.
    $i2 = @($sections["i2"])
    if (($i2 -join "|") -ne "36.000000|14.000000") {
        $rFails += "[i2] expected '36.000000|14.000000' got '$(($i2 -join '|'))'"
    }

    # i3: default rate 0.0001 on principal 100 -> +0.01 per read.
    $i3 = @($sections["i3"])
    if (($i3 -join "|") -ne "100.000000|100.010002") {
        $rFails += "[i3] expected '100.000000|100.010002' got '$(($i3 -join '|'))'"
    }

    # i4..i6: $[0.1] compound on 10 -> 10, 11, 12.1.
    $i4 = @($sections["i4"]); $i5 = @($sections["i5"]); $i6 = @($sections["i6"])
    if (($i4 -join "|") -ne "10.000000") { $rFails += "[i4] expected '10.000000' got '$(($i4 -join '|'))'" }
    if (($i5 -join "|") -ne "11.000000") { $rFails += "[i5] expected '11.000000' got '$(($i5 -join '|'))'" }
    if (($i6 -join "|") -ne "12.100000") { $rFails += "[i6] expected '12.100000' got '$(($i6 -join '|'))'" }

    # i7..i8: stacked rules on base 10: ¥(+1) ¥(+1) $(*1.1) => 10 then 13.2.
    $i7 = @($sections["i7"]); $i8 = @($sections["i8"])
    if (($i7 -join "|") -ne "10.000000") { $rFails += "[i7] expected '10.000000' got '$(($i7 -join '|'))'" }
    if (($i8 -join "|") -ne "13.200000") { $rFails += "[i8] expected '13.200000' got '$(($i8 -join '|'))'" }

    # i9..i10: d=20 after def keeps the ¥[0.1] rule (amount frozen at 1).
    $i9 = @($sections["i9"]); $i10 = @($sections["i10"])
    if (($i9 -join "|") -ne "20.000000") { $rFails += "[i9] expected '20.000000' got '$(($i9 -join '|'))'" }
    if (($i10 -join "|") -ne "21.000000") { $rFails += "[i10] expected '21.000000' got '$(($i10 -join '|'))'" }

    # i11..i12: e = e + 5 reads 5 (0.5 growth is overwritten), stores 10.0;
    # then each read grows by the frozen ¥ amount 0.5 -> 10.0, 10.5.
    $i11 = @($sections["i11"]); $i12 = @($sections["i12"])
    if (($i11 -join "|") -ne "10.000000") { $rFails += "[i11] expected '10.000000' got '$(($i11 -join '|'))'" }
    if (($i12 -join "|") -ne "10.500000") { $rFails += "[i12] expected '10.500000' got '$(($i12 -join '|'))'" }
}
$results.Add([pscustomobject]@{ Name = "test_interest.runtime"; Ok = ($rFails.Count -eq 0); Detail = ($rFails -join "; ") })

# --- compile-error: re-declaring a non-float variable as an interest var ---
$failRedef = Join-Path $tests "test_interest_fail_redef.xf"
$frOut = Join-Path $workDir "fail_redef.exe"
Remove-Item -LiteralPath $frOut -ErrorAction SilentlyContinue
$sc1 = (& $Xfawac $failRedef -o $frOut 2>&1) -join "`n"
$fc1 = @()
if ($LASTEXITCODE -eq 0) { $fc1 += "compile unexpectedly succeeded" }
if ($sc1 -notmatch 'already exists as a non-float variable') { $fc1 += "missing 'already exists as a non-float variable' in output" }
$results.Add([pscustomobject]@{ Name = "test_interest.fail_redef"; Ok = ($fc1.Count -eq 0); Detail = ($fc1 -join "; ") })

# --- compile-error: string value is not allowed ---
$failStr = Join-Path $tests "test_interest_fail_string.xf"
$fsOut = Join-Path $workDir "fail_string.exe"
Remove-Item -LiteralPath $fsOut -ErrorAction SilentlyContinue
$sc2 = (& $Xfawac $failStr -o $fsOut 2>&1) -join "`n"
$fc2 = @()
if ($LASTEXITCODE -eq 0) { $fc2 += "compile unexpectedly succeeded" }
if ($sc2 -notmatch "cannot be an interest variable") { $fc2 += "missing 'cannot be an interest variable' in output" }
$results.Add([pscustomobject]@{ Name = "test_interest.fail_string"; Ok = ($fc2.Count -eq 0); Detail = ($fc2 -join "; ") })

# --- compile-error: missing '=' after the variable name ---
$failNoEq = Join-Path $tests "test_interest_fail_noeq.xf"
$fnqOut = Join-Path $workDir "fail_noeq.exe"
Remove-Item -LiteralPath $fnqOut -ErrorAction SilentlyContinue
$sc3 = (& $Xfawac $failNoEq -o $fnqOut 2>&1) -join "`n"
$fc3 = @()
if ($LASTEXITCODE -eq 0) { $fc3 += "compile unexpectedly succeeded" }
if ($sc3 -notmatch "Expected '=' after the interest variable name") { $fc3 += "missing 'Expected = after the interest variable name' in output" }
$results.Add([pscustomobject]@{ Name = "test_interest.fail_noeq"; Ok = ($fc3.Count -eq 0); Detail = ($fc3 -join "; ") })

# --- compile-error: non-numeric rate ---
$failRate = Join-Path $tests "test_interest_fail_rate.xf"
$fr2Out = Join-Path $workDir "fail_rate.exe"
Remove-Item -LiteralPath $fr2Out -ErrorAction SilentlyContinue
$sc4 = (& $Xfawac $failRate -o $fr2Out 2>&1) -join "`n"
$fc4 = @()
if ($LASTEXITCODE -eq 0) { $fc4 += "compile unexpectedly succeeded" }
if ($sc4 -notmatch "Expected an interest rate number") { $fc4 += "missing 'Expected an interest rate number' in output" }
$results.Add([pscustomobject]@{ Name = "test_interest.fail_rate"; Ok = ($fc4.Count -eq 0); Detail = ($fc4 -join "; ") })

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