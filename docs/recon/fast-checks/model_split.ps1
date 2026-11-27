param([string[]]$Files)
# Model time (gaps that end in an assistant line) attributed to what the model was producing: the tool call it led to, by kind.
$buckets = @{}
$counts = @{}
function Get-Kind($c) {
    $n = $c.name
    if ($n -eq 'Edit' -or $n -eq 'Write' -or $n -eq 'MultiEdit') {
        $p = [string]$c.input.file_path
        if ($p -match '\\scratchpad\\|/scratchpad/|Saved[\\/]') { return 'edit: scratch/Saved' }
        if ($p -match '\.(cpp|h|cs)$') { return 'edit: C++ source' }
        if ($p -match 'CLAUDE\.md$') { return 'edit: CLAUDE.md' }
        if ($p -match '\.md$') { return 'edit: docs *.md' }
        if ($p -match 'Scenarios[\\/].*\.json$') { return 'edit: scenario json' }
        if ($p -match 'Config[\\/]') { return 'edit: config/tuning' }
        if ($p -match '\.ps1$') { return 'edit: tool scripts' }
        if ($p -match 'memory') { return 'edit: memory' }
        return 'edit: other'
    }
    if ($n -like 'mcp__claude_ai_Claude_Docs__*') { return 'Claude Docs (plan mirror)' }
    if ($n -eq 'PowerShell' -or $n -eq 'Bash') {
        $cmd = [string]$c.input.command
        if ($cmd -match 'Tools[\\/]+[\w\-]+\.ps1') { return 'shell: project tool run' }
        if ($cmd -match '<<|python|\.py') { return 'shell: ad-hoc analysis script' }
        return 'shell: other'
    }
    return "tool: $n"
}
foreach ($file in $Files) {
    $prev = $null
    $acc = 0.0
    $reader = [System.IO.File]::OpenText($file)
    while ($null -ne ($line = $reader.ReadLine())) {
        if ($line.Length -lt 10) { continue }
        try { $o = $line | ConvertFrom-Json } catch { continue }
        if (-not $o.timestamp -or ($o.type -ne 'user' -and $o.type -ne 'assistant')) { continue }
        $ts = [datetime]$o.timestamp
        if ($prev -and $o.type -eq 'assistant') {
            $gap = [Math]::Max(0, ($ts - $prev).TotalSeconds)
            $acc += $gap
            $uses = @()
            if ($o.message.content -isnot [string]) { $uses = @($o.message.content | Where-Object { $_.type -eq 'tool_use' }) }
            if ($uses.Count -gt 0) {
                $kind = Get-Kind $uses[0]
                $buckets[$kind] += $acc; $counts[$kind] += 1
                $acc = 0.0
            }
        } elseif ($o.type -eq 'user') {
            if ($acc -gt 0) { $buckets['text reply to the owner'] += $acc; $counts['text reply to the owner'] += 1 }
            $acc = 0.0
        }
        $prev = $ts
    }
    $reader.Close()
}
$total = ($buckets.Values | Measure-Object -Sum).Sum
$buckets.GetEnumerator() | Sort-Object Value -Descending | ForEach-Object { [pscustomobject]@{ kind = $_.Key; hours = [math]::Round($_.Value / 3600, 2); share = [math]::Round(100 * $_.Value / $total, 1); calls = $counts[$_.Key]; mean_s = [math]::Round($_.Value / $counts[$_.Key], 1) } } | Format-Table -AutoSize | Out-String -Width 200
