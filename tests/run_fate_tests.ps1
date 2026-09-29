param(
    [string]$Xfawac = "C:\Users\Administrator\Desktop\xfawa-exp\build\Release\xfawac.exe"
)

# Test harness for the EXP `fate` feature (docs/exp/fate.md).
# Covers the spec scenarios in tests/exp/test_fate.xf:
#   1. basic destiny birth + immediate read
#   2. repeated rebellions approach the destiny value (never landing on it)
#   3. each recovery result becomes the persistent floor; floor never drops
#   4. ordinary variables are untouched by the fate mechanism
#   5. hitting the destiny exactly causes no displacement
#   6. non-integer destiny degrades to a plain assignment (with a warning)

$ErrorActionPreference = "Continue"
$tests = Join-Path $PSScriptRoot "exp"
$workDir = Join-Path $env:TEMP "xfawa_fate_test"
New-Item -ItemType Directory -Force -Path $workDir | Out-Null

if (-not (Test-Path -LiteralPath $Xfawac)) {
    Write-Error "xfawac not found: $Xfawac (build first, then run this script)"
}

$results = [System.Collections.Generic.List[object]]::new()

# Expected stdout of the compiled binary, exactly in this order.
$ExpectedOutput = @(
    "t1", "100", "60", "t1done",
    "60", "80", "90", "95", "t2done",
    "60", "80", "90", "t3done",
    "-7", "2", "t4done",
    "7", "8", "t5done",
    "hi", "t6done"
)

function Compile-Fate {
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

function Run-Fate {
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

# The non-integer destiny (t6) must emit one [fate] warning (ASCII tag only:
# the Chinese warning body is mojibake under pwsh codepage capture).
$results.Add((Compile-Fate -Name "test_fate" -ExpectExit 0 -ExpectText "[fate]" -ExpectWarnCount 1))
$results.Add((Run-Fate -Name "test_fate"))

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