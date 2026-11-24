# Takes a clean clone to a verified build: stock content, the clips of Epic's Game Animation Sample, compile, asset generation, lighting bake, unit tests,
# scenario suite, push matrix, knockdowns, self-test of the interactive session.
#   powershell -ExecutionPolicy Bypass -File Tools\bootstrap.ps1
# Needs: Epic's Game Animation Sample as a project of its own (a manual download; Tools\fetch-sample-anims.ps1 says
# where it is looked for), Unreal Engine 5.8 (Epic Games Launcher), Visual Studio 2026 with the MSVC 14.50 toolset and
# Windows SDK 10.0.22621. Stops at the first failing step. Writes Saved\Tools\bootstrap.json.
. "$PSScriptRoot\common.ps1"

$steps = @(
    @{ name = 'fetch-stock-content'; script = 'fetch-stock-content.ps1' },
    @{ name = 'fetch-sample-anims';  script = 'fetch-sample-anims.ps1' },
    @{ name = 'build';               script = 'build.ps1' },
    @{ name = 'gen-assets';          script = 'gen-assets.ps1' },
    @{ name = 'bake-lighting';       script = 'bake-lighting.ps1' },
    @{ name = 'test';                script = 'test.ps1' },
    @{ name = 'suite';               script = 'suite.ps1' },
    @{ name = 'push-matrix';         script = 'push-matrix.ps1' },
    @{ name = 'knockdowns';          script = 'knockdowns.ps1' },
    @{ name = 'play-smoke';          script = 'play.ps1'; arguments = @('-Smoke') }
)

$results = @()
$ok = $true
foreach ($step in $steps) {
    Write-Output "=== $($step.name) ==="
    $started = Get-Date
    $stepArguments = @()
    if ($step.arguments) { $stepArguments = $step.arguments }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot $step.script) @stepArguments | ForEach-Object { Write-Output $_ }
    $code = $LASTEXITCODE
    $results += [ordered]@{ step = $step.name; ok = ($code -eq 0); exit_code = $code; seconds = [math]::Round(((Get-Date) - $started).TotalSeconds, 1) }
    if ($code -ne 0) { $ok = $false; break }
}

Write-JsonFile (Join-Path $ToolsOut 'bootstrap.json') ([ordered]@{ ok = $ok; steps = $results })
if ($ok) {
    Write-Output "BOOTSTRAP OK: all $($steps.Count) steps passed"
    exit 0
}
Write-Output "BOOTSTRAP FAILED at step '$($results[-1].step)'"
exit 1
