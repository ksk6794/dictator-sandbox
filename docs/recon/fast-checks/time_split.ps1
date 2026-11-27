param([string[]]$Files)
# Splits each session's wall time into: the model producing a message, tools running, and waiting for the person.
$out = New-Object System.Collections.ArrayList
foreach ($file in $Files) {
    $model = 0.0; $tool = 0.0; $human = 0.0; $yield = 0.0; $ask = 0.0
    $prev = $null
    $names = @{}
    $reader = [System.IO.File]::OpenText($file)
    while ($null -ne ($line = $reader.ReadLine())) {
        if ($line.Length -lt 10) { continue }
        try { $o = $line | ConvertFrom-Json } catch { continue }
        if (-not $o.timestamp -or -not $o.type) { continue }
        if ($o.type -ne 'user' -and $o.type -ne 'assistant') { continue }
        $ts = [datetime]$o.timestamp
        if ($prev) {
            $gap = ($ts - $prev).TotalSeconds
            if ($gap -lt 0) { $gap = 0 }
            if ($o.type -eq 'assistant') {
                $model += $gap
                if ($o.message.content -isnot [string]) { foreach ($c in $o.message.content) { if ($c.type -eq 'tool_use') { $names[$c.id] = $c.name } } }
            } else {
                $isResult = $false
                $toolName = ''
                if ($o.message.content -isnot [string]) { foreach ($c in $o.message.content) { if ($c.type -eq 'tool_result') { $isResult = $true; $toolName = $names[$c.tool_use_id] } } }
                if ($isResult) {
                    if ($toolName -eq 'Yield') { $yield += $gap } elseif ($toolName -eq 'AskUserQuestion') { $ask += $gap } else { $tool += $gap }
                } else { $human += $gap }
            }
        }
        $prev = $ts
    }
    $reader.Close()
    [void]$out.Add([pscustomobject]@{ session = [System.IO.Path]::GetFileNameWithoutExtension($file).Substring(0, 8); model_h = [math]::Round($model / 3600, 2); tools_h = [math]::Round($tool / 3600, 2); yield_h = [math]::Round($yield / 3600, 2); ask_owner_h = [math]::Round($ask / 3600, 2); owner_idle_h = [math]::Round($human / 3600, 2) })
}
$out | Format-Table -AutoSize | Out-String -Width 200
$sum = [pscustomobject]@{ model_h = ($out | Measure-Object model_h -Sum).Sum; tools_h = ($out | Measure-Object tools_h -Sum).Sum; yield_h = ($out | Measure-Object yield_h -Sum).Sum; ask_owner_h = ($out | Measure-Object ask_owner_h -Sum).Sum; owner_idle_h = ($out | Measure-Object owner_idle_h -Sum).Sum }
$sum | Format-List | Out-String
