param(
    [string]$Xfawac = "C:\Users\Administrator\Desktop\xfawa-exp\build\Release\xfawac.exe"
)

# Test harness for the EXP `pinocchio` self-referential-proposition keyword
# (docs/exp/pinocchio.md). Covers tests/exp/test_pinocchio.xf sections:
#   p1 stable true  (P true, state untouched)
#   p2 stable false (P false, no else, state untouched)
#   p3 converging   (state changes until P turns false; prints 1 2 3)
#   p4 oscillation  (truth alternates T,F,T,F -> oscillation)
#   p5 max iterations (P always true, state never settles -> timeout)
#   p6 unrelated write is not state change (outputs 99)
# Plus three compile-error cases (string variable / string literal
# proposition / limit:0).

$ErrorActionPreference = "Continue"
$tests = Join-Path $PSScriptRoot "exp"
$workDir = Join-Path $env:TEMP "xfawa_pinocchio_test"
New-Item -ItemType Directory -Force -Path $workDir | Out-Null

if (-not (Test-Path -LiteralPath $Xfawac)) {
    Write-Error "xfawac not found: $Xfawac (build first, then run this script)"
}

$results = [System.Collections.Generic.List[object]]::new()

# --- compile main pinocchio test: must succeed ---
$src = Join-Path $tests "test_pinocchio.xf"
$out = Join-Path $workDir "test_pinocchio.exe"
Remove-Item -LiteralPath $out -ErrorAction SilentlyContinue
$cstdout = (& $Xfawac $src -o $out 2>&1) -join "`n"
$ccode = $LASTEXITCODE
$cFails = @()
if ($ccode -ne 0) { $cFails += "exit=$ccode expected=0" }
$results.Add([pscustomobject]@{ Name = "test_pinocchio.compile"; Ok = ($cFails.Count -eq 0); Detail = ($cFails -join "; ") })

# --- runtime checks ---
$rFails = @()
if ($ccode -eq 0 -and (Test-Path -LiteralPath $out)) {
    $rout = @(& $out 2>&1 | ForEach-Object { $_.TrimEnd("`r") })

    # Section markers END the preceding section (same as run_drift_tests.ps1).
    $sections = [System.Collections.Generic.Dictionary[string, System.Collections.Generic.List[string]]]::new()
    $pending = [System.Collections.Generic.List[string]]::new()
    foreach ($line in $rout) {
        if ($line -match '^p[1-6]$') {
            $sections[$line] = $pending
            $pending = [System.Collections.Generic.List[string]]::new()
        } else {
            $pending.Add($line)
        }
    }

    # p1: stable true -> exactly "[pinocchio] stable true".
    $p1 = @($sections["p1"])
    if (($p1 -join "|") -ne "[pinocchio] stable true") {
        $rFails += "[p1] expected '[pinocchio] stable true' got '$(($p1 -join '|'))'"
    }

    # p2: stable false -> exactly "[pinocchio] stable false".
    $p2 = @($sections["p2"])
    if (($p2 -join "|") -ne "[pinocchio] stable false") {
        $rFails += "[p2] expected '[pinocchio] stable false' got '$(($p2 -join '|'))'"
    }

    # p3: converging -> 1 2 3 then "[pinocchio] stable false".
    $p3 = @($sections["p3"])
    $expectedP3 = @("1", "2", "3", "[pinocchio] stable false")
    if (($p3 -join "|") -ne ($expectedP3 -join "|")) {
        $rFails += "[p3] expected '$(($expectedP3 -join '|'))' got '$(($p3 -join '|'))'"
    }

    # p4: oscillation -> exactly "[pinocchio] oscillation".
    $p4 = @($sections["p4"])
    if (($p4 -join "|") -ne "[pinocchio] oscillation") {
        $rFails += "[p4] expected '[pinocchio] oscillation' got '$(($p4 -join '|'))'"
    }

    # p5: max iterations -> "[pinocchio] no stable solution (max iterations 5)".
    $p5 = @($sections["p5"])
    if (($p5 -join "|") -ne "[pinocchio] no stable solution (max iterations 5)") {
        $rFails += "[p5] expected 'max iterations 5' got '$(($p5 -join '|'))'"
    }

    # p6: unrelated write is not state change -> pinocchio prints stable true,
    # then main prints y (99).
    $p6 = @($sections["p6"])
    $expectedP6 = @("[pinocchio] stable true", "99")
    if (($p6 -join "|") -ne ($expectedP6 -join "|")) {
        $rFails += "[p6] expected '$(($expectedP6 -join '|'))' got '$(($p6 -join '|'))'"
    }
}
$results.Add([pscustomobject]@{ Name = "test_pinocchio.runtime"; Ok = ($rFails.Count -eq 0); Detail = ($rFails -join "; ") })

# --- compile-error: proposition references a STRING variable ---
$failVar = Join-Path $tests "test_pinocchio_fail_string_var.xf"
$f1out = Join-Path $workDir "fail_string_var.exe"
Remove-Item -LiteralPath $f1out -ErrorAction SilentlyContinue
$s1 = (& $Xfawac $failVar -o $f1out 2>&1) -join "`n"
$f1 = @()
if ($LASTEXITCODE -eq 0) { $f1 += "compile unexpectedly succeeded" }
if ($s1 -notmatch 'pinocchio.*numeric/bool variables') { $f1 += "missing 'numeric/bool variables' in output" }
$results.Add([pscustomobject]@{ Name = "test_pinocchio.fail_string_var"; Ok = ($f1.Count -eq 0); Detail = ($f1 -join "; ") })

# --- compile-error: proposition is a STRING LITERAL expression ---
$failLit = Join-Path $tests "test_pinocchio_fail_string_lit.xf"
$f2out = Join-Path $workDir "fail_string_lit.exe"
Remove-Item -LiteralPath $f2out -ErrorAction SilentlyContinue
$s2 = (& $Xfawac $failLit -o $f2out 2>&1) -join "`n"
$f2 = @()
if ($LASTEXITCODE -eq 0) { $f2 += "compile unexpectedly succeeded" }
if ($s2 -notmatch 'pinocchio.*bool/numeric expression') { $f2 += "missing 'bool/numeric expression' in output" }
$results.Add([pscustomobject]@{ Name = "test_pinocchio.fail_string_lit"; Ok = ($f2.Count -eq 0); Detail = ($f2 -join "; ") })

# --- compile-error: limit: 0 ---
$failLimit = Join-Path $tests "test_pinocchio_fail_limit0.xf"
$f3out = Join-Path $workDir "fail_limit0.exe"
Remove-Item -LiteralPath $f3out -ErrorAction SilentlyContinue
$s3 = (& $Xfawac $failLimit -o $f3out 2>&1) -join "`n"
$f3 = @()
if ($LASTEXITCODE -eq 0) { $f3 += "compile unexpectedly succeeded" }
if ($s3 -notmatch 'limit must be a positive integer') { $f3 += "missing 'limit must be a positive integer' in output" }
$results.Add([pscustomobject]@{ Name = "test_pinocchio.fail_limit0"; Ok = ($f3.Count -eq 0); Detail = ($f3 -join "; ") })

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