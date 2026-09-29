param(
    [string]$Xfawac = "C:\Users\Administrator\Desktop\xfawa-exp\build\Release\xfawac.exe"
)

# Test harness for the EXP `deja` feature (docs/exp/deja.md).
# Covers the 8 spec scenarios in tests/exp/test_deja.xf plus the compile-time
# warnings for futures that are not constants / behind control flow.

$ErrorActionPreference = "Continue"
$tests = Join-Path $PSScriptRoot "exp"
$workDir = Join-Path $env:TEMP "xfawa_deja_test"
New-Item -ItemType Directory -Force -Path $workDir | Out-Null

if (-not (Test-Path -LiteralPath $Xfawac)) {
    Write-Error "xfawac not found: $Xfawac (build first, then run this script)"
}

$results = [System.Collections.Generic.List[object]]::new()

# Expected stdout of the compiled binary, exactly in this order.
$ExpectedOutput = @(
    "42", "t1",
    "99", "t2",
    "1", "7", "t3",
    "5", "6", "t4",
    "15", "9", "9", "t5a",
    "2#", "8", "8", "t5b",
    "t6a", "3", "t6b",
    "5", "1", "t7",
    "200", "100", "t8"
)

function Compile-Deja {
    param(
        [string]$Name,
        [int]$ExpectExit,
        [string]$ExpectText = "",
        [int]$ExpectWarnCount = 0
    )
    $src = Join-Path $tests "$Name.xf"
    $out = Join-Path $workDir "$Name.exe"
    Remove-Item -LiteralPath $out -ErrorAction SilentlyContinue
    $stdout = (& $Xfawac $src -o $out 2>&1) -join "`n"
    $code = $LASTEXITCODE
    $failures = @()
    if ($code -ne $ExpectExit) { $failures += "exit=$code expected=$ExpectExit" }
    if ($ExpectText -and ($stdout -notmatch $ExpectText)) { $failures += "missing text: '$ExpectText'" }
    if ($ExpectWarnCount -gt 0) {
        $count = ([regex]::Matches($stdout, [regex]::Escape($ExpectText))).Count
        if ($count -lt $ExpectWarnCount) { $failures += "found $count warnings, expected >= $ExpectWarnCount" }
    }
    return [pscustomobject]@{ Name = $Name; Ok = ($failures.Count -eq 0); Detail = ($failures -join "; ") }
}

function Run-Deja {
    param([string]$Name)
    $exe = Join-Path $workDir "$Name.exe"
    if (-not (Test-Path -LiteralPath $exe)) {
        return [pscustomobject]@{ Name = "$Name.runtime"; Ok = $false; Detail = "no exe produced" }
    }
    $rout = (& $exe 2>&1) | ForEach-Object { $_ -replace "`r`n", "`n" }
    $actual = @($rout | ForEach-Object { $_.TrimEnd("`r") })
    $ok = $LASTEXITCODE -eq 0 -and $actual.Count -eq $ExpectedOutput.Count
    $detail = ""
    if ($LASTEXITCODE -ne 0) {
        $detail = "exit=$($LASTEXITCODE)"
    } elseif ($actual.Count -ne $ExpectedOutput.Count) {
        $detail = "lines=$($actual.Count) expected=$($ExpectedOutput.Count)"
        for ($i = 0; $i -lt [Math]::Min($actual.Count, $ExpectedOutput.Count); $i++) {
            if ($actual[$i] -ne $ExpectedOutput[$i]) { $detail += "; line $($i+1): got '$($actual[$i])' expected '$($ExpectedOutput[$i])'"; break }
        }
    } elseif ($null -ne (Compare-Object $actual $ExpectedOutput)) {
        $ok = $false
        $mismatch = Compare-Object $actual $ExpectedOutput | Select-Object -First 1
        $detail = "output differs from expected: $($mismatch.InputObject)"
    }
    return [pscustomobject]@{ Name = "$Name.runtime"; Ok = $ok; Detail = $detail }
}

# t1-t8 in one file: premonition constant futures, first-future-wins, orthogonal
# stacking with wrath/paradox, function-boundary and control-flow-limited scans,
# no-side-effect guarantee. Exactly 5 futures fail and must each warn.
# NOTE: the Chinese warning body is mojibake under pwsh codepage capture, so we
# assert on the ASCII "[deja]" tag (one occurrence per failing future).
$results.Add((Compile-Deja -Name "test_deja" -ExpectExit 0 -ExpectText "[deja]" -ExpectWarnCount 5))
$results.Add((Run-Deja -Name "test_deja"))

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