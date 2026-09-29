param(
    [string]$Xfawac = "C:\Users\Administrator\Desktop\xfawa-exp\build\Release\xfawac.exe"
)

# Test harness for the EXP `?!` random binary operator (docs/exp/randop.md).
# Covers the spec scenarios in tests/exp/test_randop.xf:
#   1. two ints      -> results must be in the int candidate result set
#   2. two floats    -> results must be in the float candidate result set
#   3. two bools     -> results are 0/1 only
#   4. mixed int/float operands -> promoted to float results
#   5. no legal candidates (string) -> [?!] warning + falls back to left operand
#   6. comparisons excluded: 5 ?! 3 must NEVER yield the comparison-only value 0
#   7. repeated runs: each 10 ?! 6 sample must be in its legal candidate set

$ErrorActionPreference = "Continue"
$tests = Join-Path $PSScriptRoot "exp"
$workDir = Join-Path $env:TEMP "xfawa_randop_test"
New-Item -ItemType Directory -Force -Path $workDir | Out-Null

if (-not (Test-Path -LiteralPath $Xfawac)) {
    Write-Error "xfawac not found: $Xfawac (build first, then run this script)"
}

$results = [System.Collections.Generic.List[object]]::new()

# Section marker -> legal result values for that section.
$IntSections = @{
    "t1" = @(0, 2, 3, 4, 6, 8, 12)
    "t3" = @(0, 1)
    "t6" = @(1, 2, 7, 8, 15)
    "t7" = @(1, 2, 4, 14, 16, 60)
}
$FloatSections = @{
    "t2" = @(0.5, 3.25, 4.5, 8.5, 13.0)
    "t4" = @(0.5, 1.2, 5.5, 7.5)
}

# Compile: must succeed, and there are exactly 3 `?!`-fallback warnings
# (st1?!nint, nint?!st1, st1?!st1). ASCII tag only: the Chinese body
# mojibakes under pwsh codepage capture.
$src = Join-Path $tests "test_randop.xf"
$out = Join-Path $workDir "test_randop.exe"
Remove-Item -LiteralPath $out -ErrorAction SilentlyContinue
$cstdout = (& $Xfawac $src -o $out 2>&1) -join "`n"
$ccode = $LASTEXITCODE
$cFails = @()
if ($ccode -ne 0) { $cFails += "exit=$ccode expected=0" }
$warnCount = ([regex]::Matches($cstdout, [regex]::Escape('[?!]'))).Count
if ($warnCount -lt 3) { $cFails += "found $warnCount warnings, expected >= 3" }
$results.Add([pscustomobject]@{ Name = "test_randop.compile"; Ok = ($cFails.Count -eq 0); Detail = ($cFails -join "; ") })

# Runtime: verify every sample falls inside its section's legal result set.
function Test-SampleValue {
    param([string]$Value, $LegalSet, [bool]$IsFloat)
    if ($IsFloat) {
        $d = 0.0
        [double]::TryParse($Value, [ref]$d) | Out-Null
        foreach ($v in $LegalSet) {
            if ([Math]::Abs($d - $v) -lt 0.00001) { return $true }
        }
        return $false
    }
    $iv = 0
    [int]::TryParse($Value, [ref]$iv) | Out-Null
    return $LegalSet -contains $iv
}

$rFails = @()
$rout = @(& $out 2>&1 | ForEach-Object { $_.TrimEnd("`r") })
if ($LASTEXITCODE -ne 0) {
    $rFails += "runtime exit=$($LASTEXITCODE)"
} else {
    # Split the output into sections at the "tN" markers. Each marker ENDS
    # the preceding section, so lines above "t1" belong to t1, lines between
    # "t1" and "t2" belong to t2, ..., and the trailing lines before "t7"
    # belong to t7.
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

    foreach ($tag in $IntSections.Keys) {
        $legal = $IntSections[$tag]
        $seen = @{}
        $bad = @()
        $lines = $sections[$tag]
        if ($lines.Count -lt 30) { $bad += "only $($lines.Count) samples" }
        foreach ($line in $lines) {
            if (-not (Test-SampleValue -Value $line -LegalSet $legal -IsFloat $false)) {
                $bad += "illegal value '$line' (legal: $($legal -join ','))"
                break
            }
            $seen[$line] = $true
        }
        if ($seen.Count -lt 2) { $bad += "all $($lines.Count) samples identical -> '?!' is not random" }
        if ($bad.Count -gt 0) { $rFails += "[$tag] $($bad -join '; ')" }
    }

    foreach ($tag in $FloatSections.Keys) {
        $legal = $FloatSections[$tag]
        $seen = @{}
        $bad = @()
        $lines = $sections[$tag]
        if ($lines.Count -lt 30) { $bad += "only $($lines.Count) samples" }
        foreach ($line in $lines) {
            if (-not (Test-SampleValue -Value $line -LegalSet $legal -IsFloat $true)) {
                $bad += "illegal value '$line' (legal: $($legal -join ','))"
                break
            }
            $seen[$line] = $true
        }
        if ($seen.Count -lt 2) { $bad += "all $($lines.Count) samples identical -> '?!' is not random" }
        if ($bad.Count -gt 0) { $rFails += "[$tag] $($bad -join '; ')" }
    }

    # t5: exact fallback outputs.
    $t5 = $sections["t5"]
    $expectedT5 = @("abc", "5", "abc")
    if ($null -eq $t5 -or ($t5.ToArray()) -join "|" -ne ($expectedT5 -join "|")) {
        $rFails += "[t5] expected fallback 'abc|5|abc' got '$((@($t5) -join '|'))'"
    }
}
$results.Add([pscustomobject]@{ Name = "test_randop.runtime"; Ok = ($rFails.Count -eq 0); Detail = ($rFails -join "; ") })

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