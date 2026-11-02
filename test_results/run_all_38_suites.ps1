$tests = Get-ChildItem -Path build -Filter 'test_*.exe' | Sort-Object Name
$total = $tests.Count
$passed = 0
$failed = 0
Write-Host "=== Running Full Project Regression Matrix ($total Suites) ==="

foreach ($t in $tests) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $p = Start-Process -FilePath $t.FullName -WorkingDirectory (Resolve-Path 'build') -NoNewWindow -PassThru -Wait
    $sw.Stop()
    $code = $p.ExitCode
    if ($code -eq 0) {
        $passed++
        Write-Host ('[{0,2}/{1}] {2,-42} PASS ({3} ms)' -f ($passed + $failed), $total, $t.Name, $sw.ElapsedMilliseconds)
    } else {
        $failed++
        Write-Host ('[{0,2}/{1}] {2,-42} FAIL (code {3}, {4} ms)' -f ($passed + $failed), $total, $t.Name, $code, $sw.ElapsedMilliseconds)
    }
}

Write-Host "========================================================"
Write-Host (' REGRESSION SUMMARY: {0} PASSED, {1} FAILED (TOTAL {2})' -f $passed, $failed, $total)
Write-Host "========================================================"
if ($failed -ne 0) { exit 1 } else { exit 0 }
