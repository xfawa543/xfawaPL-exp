param(
    [string]$Xfawac = "C:\Users\Administrator\Desktop\xfawa-exp\build\Release\xfawac.exe"
)

# Test harness for the EXP `disposable` keyword (docs/exp/disposable.md).
# Covers tests/exp/test_disposable.xf sections:
#   d1 shadow+fallback (a=1; disposable a=2 -> b=a+1 = 3)
#   d2 chained disposable value (c = a+1 with a already consumed by b -> c=2)
#   d3 LIFO stack over a base value (4,3,2,1,1)
#   d4 a normal assignment clears every layer (10,10)
#   d5 read-count parameter disposable[5] (100 x5)
#   d6 expression chained reads consume each use (g+g+g = 2+1+1 = 4)
#   d7 float layer (1.500000 then fallback 0.500000)
#   d8 bool layer (1 then fallback 0)
#   d9 re-push after exhaustion + assignment re-bases (9,5,5)
# Plus: a consumed disposable reads as "undefined/unavailable" at runtime,
# a `disposable fn` can only be called once, and three compile-error cases.

$ErrorActionPreference = "Continue"
$tests = Join-Path $PSScriptRoot "exp"
$workDir = Join-Path $env:TEMP "xfawa_disposable_test"
New-Item -ItemType Directory -Force -Path $workDir | Out-Null

if (-not (Test-Path -LiteralPath $Xfawac)) {
    Write-Error "xfawac not found: $Xfawac (build first, then run this script)"
}

$results = [System.Collections.Generic.List[object]]::new()

# --- compile main disposable test: must succeed ---
$src = Join-Path $tests "test_disposable.xf"
$out = Join-Path $workDir "test_disposable.exe"
Remove-Item -LiteralPath $out -ErrorAction SilentlyContinue
$cstdout = (& $Xfawac $src -o $out 2>&1) -join "`n"
$ccode = $LASTEXITCODE
$cFails = @()
if ($ccode -ne 0) { $cFails += "exit=$ccode expected=0" }
$results.Add([pscustomobject]@{ Name = "test_disposable.compile"; Ok = ($cFails.Count -eq 0); Detail = ($cFails -join "; ") })

# --- runtime checks ---
$rFails = @()
if ($ccode -eq 0 -and (Test-Path -LiteralPath $out)) {
    $rout = @(& $out 2>&1 | ForEach-Object { $_.TrimEnd("`r") })

    # Section markers END the preceding section (same as run_pinocchio_tests.ps1).
    $sections = [System.Collections.Generic.Dictionary[string, System.Collections.Generic.List[string]]]::new()
    $pending = [System.Collections.Generic.List[string]]::new()
    foreach ($line in $rout) {
        if ($line -match '^d[1-9]$') {
            $sections[$line] = $pending
            $pending = [System.Collections.Generic.List[string]]::new()
        } else {
            $pending.Add($line)
        }
    }

    # d1: b = a+1 reads the disposable 2 and consumes it -> 3.
    $d1 = @($sections["d1"])
    if (($d1 -join "|") -ne "3") {
        $rFails += "[d1] expected '3' got '$(($d1 -join '|'))'"
    }

    # d2: c = a+1 sees a already consumed by b -> base 1 -> c=2.
    $d2 = @($sections["d2"])
    if (($d2 -join "|") -ne "2") {
        $rFails += "[d2] expected '2' got '$(($d2 -join '|'))'"
    }

    # d3: LIFO 4,3,2 then base 1,1.
    $d3 = @($sections["d3"])
    if (($d3 -join "|") -ne "4|3|2|1|1") {
        $rFails += "[d3] expected '4|3|2|1|1' got '$(($d3 -join '|'))'"
    }

    # d4: assignment clears layers -> 10,10.
    $d4 = @($sections["d4"])
    if (($d4 -join "|") -ne "10|10") {
        $rFails += "[d4] expected '10|10' got '$(($d4 -join '|'))'"
    }

    # d5: disposable[5] yields its value five times.
    $d5 = @($sections["d5"])
    if (($d5 -join "|") -ne "100|100|100|100|100") {
        $rFails += "[d5] expected five 100s got '$(($d5 -join '|'))'"
    }

    # d6: value+value+value consumes one use per read -> 2+1+1 = 4.
    $d6 = @($sections["d6"])
    if (($d6 -join "|") -ne "4") {
        $rFails += "[d6] expected '4' got '$(($d6 -join '|'))'"
    }

    # d7: float layer 1.500000 then fallback 0.500000.
    $d7 = @($sections["d7"])
    if (($d7 -join "|") -ne "1.500000|0.500000") {
        $rFails += "[d7] expected '1.500000|0.500000' got '$(($d7 -join '|'))'"
    }

    # d8: bool layer 1 then fallback 0.
    $d8 = @($sections["d8"])
    if (($d8 -join "|") -ne "1|0") {
        $rFails += "[d8] expected '1|0' got '$(($d8 -join '|'))'"
    }

    # d9: disposable layer 9, assignment clears it -> 5,5.
    $d9 = @($sections["d9"])
    if (($d9 -join "|") -ne "9|5|5") {
        $rFails += "[d9] expected '9|5|5' got '$(($d9 -join '|'))'"
    }
}
$results.Add([pscustomobject]@{ Name = "test_disposable.runtime"; Ok = ($rFails.Count -eq 0); Detail = ($rFails -join "; ") })

# --- runtime: a consumed disposable reads as undefined/unavailable ---
$srcUnd = Join-Path $tests "test_disposable_undefined.xf"
$undOut = Join-Path $workDir "test_disposable_undefined.exe"
Remove-Item -LiteralPath $undOut -ErrorAction SilentlyContinue
$u1 = @()
$uout = (& $Xfawac $srcUnd -o $undOut 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0) {
    $u1 += "compile failed"
} elseif (Test-Path -LiteralPath $undOut) {
    $ur = @(& $undOut 2>&1 | ForEach-Object { $_.TrimEnd("`r") }) -join "|"
    if ($ur -notmatch '^3\|2\|' -or $ur -notmatch 'disposable') {
        $u1 += "expected '3|2|<disposable undefined error>' got '$ur'"
    }
}
$results.Add([pscustomobject]@{ Name = "test_disposable.undefined"; Ok = ($u1.Count -eq 0); Detail = ($u1 -join "; ") })

# --- runtime: a disposable fn can only be called once ---
$srcFn = Join-Path $tests "test_disposable_fn.xf"
$fnOut = Join-Path $workDir "test_disposable_fn.exe"
Remove-Item -LiteralPath $fnOut -ErrorAction SilentlyContinue
$f1 = @()
$fout = (& $Xfawac $srcFn -o $fnOut 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0) {
    $f1 += "compile failed"
} elseif (Test-Path -LiteralPath $fnOut) {
    $fr = @(& $fnOut 2>&1 | ForEach-Object { $_.TrimEnd("`r") }) -join "|"
    if ($fr -notmatch '^42\|' -or $fr -notmatch 'disposable') {
        $f1 += "expected '42|<disposable unavailable error>' got '$fr'"
    }
}
$results.Add([pscustomobject]@{ Name = "test_disposable.fn_one_shot"; Ok = ($f1.Count -eq 0); Detail = ($f1 -join "; ") })

# --- compile-error: read count must be >= 1 ---
$failCount = Join-Path $tests "test_disposable_fail_count.xf"
$fcOut = Join-Path $workDir "fail_count.exe"
Remove-Item -LiteralPath $fcOut -ErrorAction SilentlyContinue
$sc1 = (& $Xfawac $failCount -o $fcOut 2>&1) -join "`n"
$fc1 = @()
if ($LASTEXITCODE -eq 0) { $fc1 += "compile unexpectedly succeeded" }
if ($sc1 -notmatch 'read count must be >= 1') { $fc1 += "missing 'read count must be >= 1' in output" }
$results.Add([pscustomobject]@{ Name = "test_disposable.fail_count"; Ok = ($fc1.Count -eq 0); Detail = ($fc1 -join "; ") })

# --- compile-error: layer base type mismatch ---
$failMismatch = Join-Path $tests "test_disposable_fail_mismatch.xf"
$fmOut = Join-Path $workDir "fail_mismatch.exe"
Remove-Item -LiteralPath $fmOut -ErrorAction SilentlyContinue
$sc2 = (& $Xfawac $failMismatch -o $fmOut 2>&1) -join "`n"
$fc2 = @()
if ($LASTEXITCODE -eq 0) { $fc2 += "compile unexpectedly succeeded" }
if ($sc2 -notmatch 'must keep the same base type') { $fc2 += "missing 'must keep the same base type' in output" }
$results.Add([pscustomobject]@{ Name = "test_disposable.fail_mismatch"; Ok = ($fc2.Count -eq 0); Detail = ($fc2 -join "; ") })

# --- compile-error: disposable layer needs a value ---
$failNoEq = Join-Path $tests "test_disposable_fail_noeq.xf"
$fnqOut = Join-Path $workDir "fail_noeq.exe"
Remove-Item -LiteralPath $fnqOut -ErrorAction SilentlyContinue
$sc3 = (& $Xfawac $failNoEq -o $fnqOut 2>&1) -join "`n"
$fc3 = @()
if ($LASTEXITCODE -eq 0) { $fc3 += "compile unexpectedly succeeded" }
if ($sc3 -notmatch "Expected '=' after the disposable variable name") { $fc3 += "missing 'Expected = after the disposable variable name' in output" }
$results.Add([pscustomobject]@{ Name = "test_disposable.fail_noeq"; Ok = ($fc3.Count -eq 0); Detail = ($fc3 -join "; ") })

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