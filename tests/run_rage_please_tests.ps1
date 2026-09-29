param(
    [string]$Xfawac = "C:\Users\Administrator\Desktop\xfawa-exp\build\Release\xfawac.exe"
)

# Test harness for the "every-5-lines please" red-hot rule + please/sorry/cross-file
# rage semantics (docs/exp/rage.md, docs/exp/try_expect.md). Relies on the persistent
# rage file next to xfawac.exe; resets it before each non-cross-file scenario.

$ErrorActionPreference = "Continue"
$tests = Join-Path $PSScriptRoot "exp"
$workDir = Join-Path $env:TEMP "xfawa_rp_test"
New-Item -ItemType Directory -Force -Path $workDir | Out-Null

if (-not (Test-Path -LiteralPath $Xfawac)) {
    Write-Error "xfawac not found: $Xfawac (build first, then run this script)"
}

$results = [System.Collections.Generic.List[object]]::new()

function Get-Rage {
    $out = (& $Xfawac rage 2>&1) -join "`n"
    if ($LASTEXITCODE -ne 0) { throw "rage read failed: $out" }
    $m = [regex]::Match($out, "rage:\s*(\d+)/5")
    if (-not $m.Success) { throw "unexpected rage output: $out" }
    return [int]$m.Groups[1].Value
}

function Reset-Rage {
    $null = & $Xfawac rage reset 2>&1
    if ($LASTEXITCODE -ne 0) { throw "rage reset failed" }
}

function Compile {
    param(
        [string]$Name,
        [int]$ExpectExit,
        [string]$ExpectText = "",
        [string]$ExpectNoText = ""
    )
    $src = Join-Path $tests "$Name.xf"
    $out = Join-Path $workDir "$Name.exe"
    Remove-Item -LiteralPath $out -ErrorAction SilentlyContinue
    $stdout = (& $Xfawac $src -o $out 2>&1) -join "`n"
    $code = $LASTEXITCODE
    $failures = @()
    if ($code -ne $ExpectExit) { $failures += "exit=$code expected=$ExpectExit" }
    if ($ExpectText -and $stdout -notmatch $ExpectText) { $failures += "missing text: '$ExpectText'" }
    if ($ExpectNoText -and $stdout -match $ExpectNoText) { $failures += "unexpected text: '$ExpectNoText'" }
    return [pscustomobject]@{ Name = $Name; Ok = ($failures.Count -eq 0); Detail = ($failures -join "; ") }
}

function Expect-Rage {
    param([int]$Value, [string]$Label)
    $v = Get-Rage
    return [pscustomobject]@{ Name = $Label; Ok = ($v -eq $Value); Detail = "rage=$v expected=$Value" }
}

function Expect-RageIn {
    param([int]$Min, [int]$Max, [string]$Label)
    $v = Get-Rage
    $inRange = ($v -ge $Min) -and ($v -le $Max)
    return [pscustomobject]@{ Name = $Label; Ok = $inRange; Detail = "rage=$v expected in [$Min..$Max]" }
}

function Run-Binary {
    param([string]$Name, [string]$ExpectText = "")
    $exe = Join-Path $workDir "$Name.exe"
    if (-not (Test-Path -LiteralPath $exe)) {
        return [pscustomobject]@{ Name = "$Name.runtime"; Ok = $false; Detail = "no exe produced" }
    }
    $rout = (& $exe 2>&1) -join "`n"
    $ok = $LASTEXITCODE -eq 0 -and (($ExpectText -eq "") -or ($rout -match $ExpectText))
    $detail = if ($LASTEXITCODE -ne 0) { "exit=$($LASTEXITCODE)" } elseif (($ExpectText -ne "") -and ($rout -notmatch $ExpectText)) { "output missing: '$ExpectText'" } else { "" }
    return [pscustomobject]@{ Name = "$Name.runtime"; Ok = $ok; Detail = $detail }
}

# ---- Test 1: normal try/expect, not red-hot -> no please required ----
Reset-Rage
$results.Add((Compile -Name "test_rage_please_t1_basic" -ExpectExit 0 -ExpectText "rage"))
$results.Add((Run-Binary -Name "test_rage_please_t1_basic" -ExpectText "caught"))
$results.Add((Expect-Rage -Value 1 -Label "t1.rage"))

# ---- Test 2: red-hot -> every-5-lines please rule is enforced (bare please only) ----
Reset-Rage
$results.Add((Compile -Name "test_rage_please_t2_redhot_no_please" -ExpectExit 1 -ExpectText "required within every 5 code lines"))
$results.Add((Expect-Rage -Value 3 -Label "t2a.rage"))
Reset-Rage
$results.Add((Compile -Name "test_rage_please_t2_redhot_comply" -ExpectExit 0))
$results.Add((Run-Binary -Name "test_rage_please_t2_redhot_comply" -ExpectText "ok"))
$results.Add((Expect-Rage -Value 4 -Label "t2b.rage"))

# ---- Test 3: bare please only cools while red-hot (4 catches then 3 pleases -> 4->3->2) ----
Reset-Rage
$results.Add((Compile -Name "test_rage_please_t3_please_minus_one" -ExpectExit 0))
$results.Add((Expect-Rage -Value 2 -Label "t3.rage"))

# ---- Test 4: sorry -> random drop within [0, rage], never negative ----
# 5 catches + 5 sorries (random end [0,5]) + 5 bare pleases -> [0,2] deterministic compile
Reset-Rage
$results.Add((Compile -Name "test_rage_please_t4_sorry_random" -ExpectExit 0))
$results.Add((Expect-RageIn -Min 0 -Max 2 -Label "t4.rage"))
$results.Add((Run-Binary -Name "test_rage_please_t4_sorry_random"))

# ---- Test 5: cross-file rage persists (not cleared between files) ----
# a: 6 catches + 3 bare pleases (first useless at rage 0) -> 4
Reset-Rage
$results.Add((Compile -Name "test_rage_please_t5_cross_a" -ExpectExit 0))
$results.Add((Expect-Rage -Value 4 -Label "t5a.rage"))
$results.Add((Compile -Name "test_rage_please_t5_cross_b" -ExpectExit 1 -ExpectText "required within every 5 code lines"))
$results.Add((Expect-Rage -Value 4 -Label "t5b.rage"))
# c: from 4, three bare pleases -> 4->3->2 (third useless), stops at 2
$results.Add((Compile -Name "test_rage_please_t5_cross_c" -ExpectExit 0))
$results.Add((Expect-Rage -Value 2 -Label "t5c.rage"))
$results.Add((Run-Binary -Name "test_rage_please_t5_cross_c" -ExpectText "cooled"))

# ---- Test 6: not red-hot -> no every-5-lines please error at all ----
Reset-Rage
$results.Add((Compile -Name "test_rage_please_t6_normal_no_please" -ExpectExit 0 -ExpectNoText "required within every 5 code lines"))
$results.Add((Run-Binary -Name "test_rage_please_t6_normal_no_please" -ExpectText "10"))

# ---- Test 7: catches don't directly trigger the please rule ----
Reset-Rage
$results.Add((Compile -Name "test_rage_please_t7_catch_not_trigger" -ExpectExit 0 -ExpectNoText "required within every 5 code lines"))
$results.Add((Run-Binary -Name "test_rage_please_t7_catch_not_trigger" -ExpectText "d"))

# leave a fresh state
Reset-Rage

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