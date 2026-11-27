param([string[]]$Files, [string]$OutCsv)
# For every shell tool call in the transcripts: which Tools\*.ps1 it ran, when it was asked for and when its result came.
$rows = New-Object System.Collections.ArrayList
$sessions = New-Object System.Collections.ArrayList
foreach ($file in $Files) {
    $pending = @{}
    $firstTs = $null; $lastTs = $null
    $reader = [System.IO.File]::OpenText($file)
    while ($null -ne ($line = $reader.ReadLine())) {
        if ($line.Length -lt 10) { continue }
        try { $o = $line | ConvertFrom-Json } catch { continue }
        if (-not $o.timestamp) { continue }
        $ts = [datetime]$o.timestamp
        if (-not $firstTs) { $firstTs = $ts }
        $lastTs = $ts
        if (-not $o.message -or -not $o.message.content) { continue }
        $content = $o.message.content
        if ($content -is [string]) { continue }
        foreach ($c in $content) {
            if ($c.type -eq 'tool_use') {
                $cmd = ''
                if ($c.input.command) { $cmd = [string]$c.input.command }
                $bg = [bool]$c.input.run_in_background
                $pending[$c.id] = @{ ts = $ts; name = $c.name; cmd = $cmd; bg = $bg }
            } elseif ($c.type -eq 'tool_result' -and $pending.ContainsKey($c.tool_use_id)) {
                $p = $pending[$c.tool_use_id]
                $script = ''
                $m = [regex]::Matches($p.cmd, 'Tools[\\/]+([\w\-]+)\.ps1')
                if ($m.Count -gt 0) { $script = ($m | ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique) -join '+' }
                [void]$rows.Add([pscustomobject]@{ session = [System.IO.Path]::GetFileNameWithoutExtension($file).Substring(0, 8); tool = $p.name; script = $script; bg = $p.bg; seconds = [math]::Round(($ts - $p.ts).TotalSeconds, 1); at = $p.ts.ToString('MM-dd HH:mm:ss'); cmd = $p.cmd.Substring(0, [Math]::Min(160, $p.cmd.Length)).Replace("`n", ' ') })
                $pending.Remove($c.tool_use_id)
            }
        }
    }
    $reader.Close()
    if ($firstTs) { [void]$sessions.Add([pscustomobject]@{ session = [System.IO.Path]::GetFileNameWithoutExtension($file).Substring(0, 8); span_h = [math]::Round(($lastTs - $firstTs).TotalHours, 2); from = $firstTs.ToString('MM-dd HH:mm'); to = $lastTs.ToString('MM-dd HH:mm') }) }
}
$rows | Export-Csv -NoTypeInformation -Path $OutCsv
$sessions | Format-Table -AutoSize | Out-String -Width 200
