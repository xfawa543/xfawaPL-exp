param(
    [string]$Xfawac = "C:\Users\Administrator\Desktop\xfawa-exp\build\Release\xfawac.exe"
)

# Test harness for the EXP `drift` random-recursive-parameter keyword
# (docs/exp/drift.md). Covers tests/exp/test_drift.xf sections:
#   1. single int param, explicit range drift(-5,5) + natural termination
#   2. default int range [0,100], built-in depth cap ends the recursion
#   3. multi-param recursion, independent random per param
#   4. float param with float range drift(0.0,1.0)
#   5. user depth limit drift depth:50
#   6. original termination condition still fires (returns 42)
#   7. ordinary recursion (no drift) is untouched: exact 5 4 3 2 1 0
# Plus two compile-error cases (string param / uninferable type).

$ErrorActionPreference = "Continue"
$tests = Join-Path $PSScriptRoot "exp"
$workDir = Join-Path $env:TEMP "xfawa_drift_test"
New-Item -ItemType Directory -Force -Path $workDir | Out-Null

if (-not (Test-Path -LiteralPath $Xfawac)) {
    Write-Error "xfawac not found: $Xfawac (build first, then run this script)"
}

$results = [System.Collections.Generic.List[object]]::new()

# --- compile main drift test: must succeed ---
$src = Join-Path $tests "test_drift.xf"
$out = Join-Path $workDir "test_drift.exe"
Remove-Item -LiteralPath $out -ErrorAction SilentlyContinue
$cstdout = (& $Xfawac $src -o $out 2>&1) -join "`n"
$ccode = $LASTEXITCODE
$cFails = @()
if ($ccode -ne 0) { $cFails += "exit=$ccode expected=0" }
$results.Add([pscustomobject]@{ Name = "test_drift.compile"; Ok = ($cFails.Count -eq 0); Detail = ($cFails -join "; ") })

# --- runtime checks ---
$rFails = @()
if ($ccode -eq 0 -and (Test-Path -LiteralPath $out)) {
    $rout = @(& $out 2>&1 | ForEach-Object { $_.TrimEnd("`r") })

    # Section markers END the preceding section (same as run_randop_tests.ps1).
    $sections = [System.Collections.Generic.Dictionary[string, System.Collections.Generic.List[string]]]::new()
    $pending = [System.Collections.Generic.List[string]]::new()
    foreach ($line in $rout) {
        if ($line -match '^t[1-7]$') {
            $sections[$line] = $pending
            $pending = [System.Collections.Generic.List[string]]::new()
        } else {
            $pending.Add($line)
        }
    }

    function Is-IntInRange {
        param([string]$Value, [int]$Lo, [int]$Hi)
        $iv = 0
        if (-not [int]::TryParse($Value, [ref]$iv)) { return $false }
        return ($iv -ge $Lo -and $iv -le $Hi)
    }
    function Is-FloatInRange {
        param([string]$Value, [double]$Lo, [double]$Hi)
        $d = 0.0
        if (-not [double]::TryParse($Value, [ref]$d)) { return $false }
        return ($d -ge $Lo -and $d -le $Hi)
    }

    # t1 walk(-5,5): first = written external arg 3; all non-last in [-5,5]; final 42.
    $t1 = @($sections["t1"])
    if (-not (Is-IntInRange $t1[0] -5 100) -and $t1[0] -ne "3") { $rFails += "[t1] first sample '$($t1[0])' should be written arg 3" }
    if ($t1[0] -ne "3") { $rFails += "[t1] external call must pass the written arg, got '$($t1[0])'" }
    if ($t1.Count -lt 2) { $rFails += "[t1] too few samples ($($t1.Count))" }
    $t1NoLast = $t1 | Select-Object -SkipLast 1
    foreach ($s in $t1NoLast) {
        if (-not (Is-IntInRange $s -5 5)) { $rFails += "[t1] value '$s' outside drift(-5,5)"; break }
    }
    if ($t1[$t1.Count - 1] -ne "42") { $rFails += "[t1] final should be 42 (natural termination), got '$($t1[$t1.Count - 1])'" }

    # t2 driftd default [0,100], no termination -> depth cap 1000 -> final 0.
    $t2 = @($sections["t2"])
    if ($t2[$t2.Count - 1] -ne "0") { $rFails += "[t2] final should be 0 (depth cap), got '$($t2[$t2.Count - 1])'" }
    if ($t2.Count -lt 1000 -or $t2.Count -gt 1004) {
        $rFails += "[t2] expected ~1002 samples (depth 1000), got $($t2.Count)"
    }
    foreach ($s in $t2) {
        if (-not (Is-IntInRange $s 0 100)) { $rFails += "[t2] value '$s' outside default [0,100]"; break }
    }

    # t3 two(-5,5): all non-last in [-5,5]; final 9 (n+m<=0).
    $t3 = @($sections["t3"])
    if ($t3.Count -lt 3 -or ($t3.Count % 2) -ne 1) { $rFails += "[t3] expected odd count >=3, got $($t3.Count)" }
    $t3NoLast = $t3 | Select-Object -SkipLast 1
    foreach ($s in $t3NoLast) {
        if (-not (Is-IntInRange $s -5 5)) { $rFails += "[t3] value '$s' outside drift(-5,5)"; break }
    }
    if ($t3[$t3.Count - 1] -ne "9") { $rFails += "[t3] final should be 9, got '$($t3[$t3.Count - 1])'" }

    # t4 fv float drift(0.0,1.0): non-last floats in [0,1); first is written 0.5; final 3.
    $t4 = @($sections["t4"])
    if ($t4[0] -ne "0.500000") { $rFails += "[t4] first sample should be written arg 0.500000, got '$($t4[0])'" }
    $t4NoLast = $t4 | Select-Object -SkipLast 1
    foreach ($s in $t4NoLast) {
        if (-not (Is-FloatInRange $s 0.0 0.999999)) { $rFails += "[t4] value '$s' outside [0,1)"; break }
    }
    if ($t4[$t4.Count - 1] -ne "3") { $rFails += "[t4] final should be 3, got '$($t4[$t4.Count - 1])'" }

    # t5 cap depth:50, no satisfiable termination -> capped, final 0; ~52 samples.
    $t5 = @($sections["t5"])
    if ($t5[0] -ne "1") { $rFails += "[t5] first sample should be written arg 1, got '$($t5[0])'" }
    if ($t5[$t5.Count - 1] -ne "0") { $rFails += "[t5] final should be 0 (user depth cap), got '$($t5[$t5.Count - 1])'" }
    if ($t5.Count -lt 50 -or $t5.Count -gt 54) { $rFails += "[t5] expected ~52 samples, got $($t5.Count)" }
    foreach ($s in $t5) {
        if (-not (Is-IntInRange $s 0 100)) { $rFails += "[t5] value '$s' outside [0,100]"; break }
    }

    # t6 ok(-5,5): original termination fires; all non-last in [-5,5]; final 42.
    $t6 = @($sections["t6"])
    foreach ($s in ($t6 | Select-Object -SkipLast 1)) {
        if (-not (Is-IntInRange $s -5 5)) { $rFails += "[t6] value '$s' outside drift(-5,5)"; break }
    }
    if ($t6[$t6.Count - 1] -ne "42") { $rFails += "[t6] final should be 42, got '$($t6[$t6.Count - 1])'" }

    # t7 norm(5) ordinary recursion: exact 5 4 3 2 1 0.
    $t7 = @($sections["t7"])
    $expectedT7 = @("5", "4", "3", "2", "1", "0")
    if (($t7 -join "|") -ne ($expectedT7 -join "|")) {
        $rFails += "[t7] expected '$($expectedT7 -join '|')' got '$(($t7 -join '|'))'"
    }
}
$results.Add([pscustomobject]@{ Name = "test_drift.runtime"; Ok = ($rFails.Count -eq 0); Detail = ($rFails -join "; ") })

# --- compile-error: string parameter cannot be randomized ---
$failString = Join-Path $tests "test_drift_fail_string.xf"
$f1out = Join-Path $workDir "fail_string.exe"
Remove-Item -LiteralPath $f1out -ErrorAction SilentlyContinue
$s1 = (& $Xfawac $failString -o $f1out 2>&1) -join "`n"
$f1 = @()
if ($LASTEXITCODE -eq 0) { $f1 += "compile unexpectedly succeeded" }
if ($s1 -notmatch '\[drift\] cannot randomize') { $f1 += "missing '[drift] cannot randomize' in output" }
$results.Add([pscustomobject]@{ Name = "test_drift.fail_string"; Ok = ($f1.Count -eq 0); Detail = ($f1 -join "; ") })

# --- compile-error: uninferable parameter type ---
$failUnknown = Join-Path $tests "test_drift_fail_unknown.xf"
$f2out = Join-Path $workDir "fail_unknown.exe"
Remove-Item -LiteralPath $f2out -ErrorAction SilentlyContinue
$s2 = (& $Xfawac $failUnknown -o $f2out 2>&1) -join "`n"
$f2 = @()
if ($LASTEXITCODE -eq 0) { $f2 += "compile unexpectedly succeeded" }
if ($s2 -notmatch '\[drift\] cannot infer') { $f2 += "missing '[drift] cannot infer' in output" }
$results.Add([pscustomobject]@{ Name = "test_drift.fail_unknown"; Ok = ($f2.Count -eq 0); Detail = ($f2 -join "; ") })

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