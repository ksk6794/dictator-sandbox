param([string[]]$Files, [string]$OutCsv)
# Every shell call that starts the engine (a project tool, or an agent's own battery script that calls them), with what it ran.
$engineTools = 'scenario|suite|test|getup-stand|getup-seeds|push-matrix|knockdowns|grab-pulls|natural|film|film-pair|compliance|sweep|record-replay|play|bootstrap|swing-lag|recording-report|surface-friction|clip-film|repeatability|pose-prior|gen-assets|bake-lighting'
$rows = New-Object System.Collections.ArrayList
foreach ($file in $Files) {
    $session = [System.IO.Path]::GetFileNameWithoutExtension($file).Substring(0, 8)
    $pending = @{}
    $reader = [System.IO.File]::OpenText($file)
    while ($null -ne ($line = $reader.ReadLine())) {
        if ($line.Length -lt 10) { continue }
        try { $o = $line | ConvertFrom-Json } catch { continue }
        if (-not $o.timestamp -or -not $o.message -or -not $o.message.content -or $o.message.content -is [string]) { continue }
        $ts = [datetime]$o.timestamp
        foreach ($c in $o.message.content) {
            if ($c.type -eq 'tool_use' -and ($c.name -eq 'PowerShell' -or $c.name -eq 'Bash')) {
                $pending[$c.id] = @{ ts = $ts; cmd = [string]$c.input.command; bg = [bool]$c.input.run_in_background }
            } elseif ($c.type -eq 'tool_result' -and $pending.ContainsKey($c.tool_use_id)) {
                $p = $pending[$c.tool_use_id]
                $pending.Remove($c.tool_use_id)
                $cmd = $p.cmd
                # A wait for something started earlier is not a launch.
                $isWait = ($cmd -match '^\s*(until|while)\b' -and $cmd -notmatch 'Tools[\\/]+(' + $engineTools + ')\.ps1')
                # Only a script that is run counts: after -File or the call operator, not one that is read with sed or grep.
                $run = '(?:-File\s+"?|&\s*"?)[^"\s]*?'
                $tools = @([regex]::Matches($cmd, $run + 'Tools[\\/]+(' + $engineTools + ')\.ps1') | ForEach-Object { $_.Groups[1].Value })
                $own = @([regex]::Matches($cmd, $run + '((?:Saved[\\/]+Recon|scratchpad|Saved[\\/]+Backups)[\\/]+[\w\-]+\.ps1)') | ForEach-Object { 'own:' + [System.IO.Path]::GetFileNameWithoutExtension($_.Groups[1].Value) } | Where-Object { $_ -ne 'own:backup' })
                $hasBuild = $cmd -match ($run + 'Tools[\\/]+build\.ps1')
                if ($tools.Count -eq 0 -and $own.Count -eq 0 -and -not $hasBuild) { continue }
                $loop = ($cmd -match '\bfor\b|foreach|ForEach-Object') -and ($tools -contains 'scenario')
                [void]$rows.Add([pscustomobject]@{
                    session = $session; at = $p.ts.ToString('yyyy-MM-dd HH:mm:ss'); bg = $p.bg; seconds = [math]::Round(($ts - $p.ts).TotalSeconds, 1)
                    build = $hasBuild; tools = (($tools | Select-Object -Unique) -join '+'); own = (($own | Select-Object -Unique) -join '+'); scenario_runs = @($tools | Where-Object { $_ -eq 'scenario' }).Count
                    loop = $loop; wait = $isWait })
            }
        }
    }
    $reader.Close()
}
$rows | Export-Csv -NoTypeInformation -Path $OutCsv
"rows: $($rows.Count)"
