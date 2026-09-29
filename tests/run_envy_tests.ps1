param(
    [string]$Xfawac = "C:\Users\Administrator\Desktop\xfawa-exp\build\Release\xfawac.exe"
)

# Test harness for the EXP `envy` feature (docs/exp/envy.md).
# Covers the spec scenarios in tests/exp/test_envy.xf:
#   1. target clearly better than self -> path chosen by disparity intensity
#   2. self already not worse -> no jealousy at all (greater AND equal)
#   3. surpass path (gap <= self): receiver jumps to target + 1
#   4. become/mimic path (self < gap <= 2*self): receiver copies target
#   5. extreme envy destroy path (gap > 2*self): target dragged below self
#   6. unrelated variables are untouched
#   7. incomparable dimensions (string/string, int/string) -> no-op + warning

$ErrorActionPreference = "Continue"
$tests = Join-Path $PSScriptRoot "exp"
$workDir = Join-Path $env:TEMP "xfawa_envy_test"
New-Item -ItemType Directory -Force -Path $workDir | Out-Null

if (-not (Test-Path -LiteralPath $Xfawac)) {
    Write-Error "xfawac not found: $Xfawac (build first, then run this script)"
}

$results = [System.Collections.Generic.List[object]]::new()

# Expected stdout of the compiled binary, exactly in this order.
$ExpectedOutput = @(
    "101", "100", "t1",
    "100", "50", "60", "60", "t2",
    "61", "60", "t3",
    "70", "70", "t4",
    "10", "9", "t5",
    "10", "9", "5", "6", "7", "t6",
    "hello", "world", "5", "hi", "t7"
)

function Compile-Envy {
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

function Run-Envy {
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

# t7's two incomparable envies must each emit a warning (ASCII "[envy]" tag:
# the Chinese warning body is mojibake under pwsh codepage capture).
$results.Add((Compile-Envy -Name "test_envy" -ExpectExit 0 -ExpectText "[envy]" -ExpectWarnCount 2))
$results.Add((Run-Envy -Name "test_envy"))

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