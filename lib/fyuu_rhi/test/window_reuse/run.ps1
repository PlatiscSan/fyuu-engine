param(
    [string]$BuildDirectory = 'cmake-build/Clang_Ninja_vcpkg',
    [string]$OutputDirectory = 'cmake-build/rhi-window-reuse',
    [int]$Repeats = 3,
    [switch]$FreshWindow
)

$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
$executable = Join-Path $repository "$BuildDirectory/bin/FyuuRHIWindowReuse.exe"
$resultsPath = Join-Path $repository $OutputDirectory
New-Item -ItemType Directory -Force -Path $resultsPath | Out-Null
$env:PATH = 'C:\vcpkg\installed\x64-windows\bin;' + $env:PATH
$backends = @('d3d12', 'vulkan', 'opengl', 'webgpu')
$results = @()

for ($round = 1; $round -le $Repeats; ++$round) {
    foreach ($first in $backends) {
        $sequences = @(@($first))
        foreach ($second in $backends) {
            $sequences += ,@($first, $second, $first)
        }
        foreach ($sequence in $sequences) {
            $caseName = $sequence -join '-'
            $arguments = $sequence
            if ($FreshWindow) {
                $caseName = "fresh-$caseName"
                $arguments = @('--fresh') + $sequence
            }
            $stdout = Join-Path $resultsPath "$round-$caseName.out.log"
            $stderr = Join-Path $resultsPath "$round-$caseName.err.log"
            $probe = Start-Process -FilePath $executable -ArgumentList $arguments `
                -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout `
                -RedirectStandardError $stderr
            $timedOut = -not $probe.WaitForExit(45000)
            if ($timedOut) {
                $probe.Kill()
                $probe.WaitForExit()
            }
            $output = Get-Content -LiteralPath $stdout
            $result = [pscustomobject]@{
                round = $round
                sequence = $caseName
                exit_code = $probe.ExitCode
                timeout = $timedOut
                phases = @($output | Where-Object { $_ -match '^(PASS|FAIL) ' })
                stdout = $stdout
                stderr = $stderr
            }
            $results += $result
            Write-Output "Round $round $caseName exit=$($probe.ExitCode) timeout=$timedOut $($result.phases -join '; ')"
        }
    }
}
$summaryName = if ($FreshWindow) { 'fresh-results.json' } else { 'results.json' }
$results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $resultsPath $summaryName) -Encoding utf8
