# Checks what the documents point at: every markdown link and its anchor, every path in backquotes to a file of the repo, and
# every scenario name, tuning name and script parameter a .md names in backquotes.
#   powershell -ExecutionPolicy Bypass -File Tools\check-links.ps1 [-Paths "docs/PRD.md,CLAUDE.md"] [-SelfTest]
# Prints LINKS OK or LINKS FAIL first, then every problem as file:line; writes Saved\Tools\links.json.
# Exit code: 0 nothing wrong, 1 problems found (or a self-test failed), 2 the check itself broke.
#
# What is held, and against what:
#   links    [text](target#anchor) and [name]: target, outside code; the target exists (relative to the file; under Saved\ not
#            checked; http, https and mailto not checked); an anchor in a .md is one of its headings by GitHub's rules
#            (lowercase, every character that is not a letter, a digit, a space, a hyphen or an underscore dropped, a space a
#            hyphen; the second heading of a name gets -1, the third -2) or an <a name>/<a id> in it
#   paths    `docs/...`, `Tools\...`, `Source/...`, `Specs/...`, `Scenarios/...`, `Config/...`, `Recordings/...` in
#            backquotes: the file or folder exists (a wildcard matches something; a :line after it is dropped), spelled in the
#            case git has it (a link's target too); `Content/...` is generated and not held
#   tuning   `group.name` in backquotes whose group is a top-level group of Config/Tuning/*.json: a value or a group of values
#            of those files (a * matches); `name=value` lists too
#   scenarios `word_word` in backquotes whose first word begins a scenario's name and that no code or tool spells: a scenario of
#            Scenarios/ (a * matches)
#   params   `-Name` after a Tools script in the same backquotes, or in a table row that names one Tools script: a parameter of
#            that script's param block
# A name a document calls removed is listed in it once, in a comment: <!-- removed: getup.carry getup.clip_* old_scenario -->;
# one it proposes for later, <!-- planned: grab_throw_* -->; a word that only looks like a scenario (a recording's or a label's
# name), <!-- not-names: vr_check3 -->; a script's parameter goes in such a list as -Name. Such a list holds for its whole file.
# docs/recon/ is the archive of plans as they were: only its links and anchors are held.
param(
    [string]$Paths = '',
    [switch]$SelfTest
)
. "$PSScriptRoot\common.ps1"

$resultFile = Join-Path $ToolsOut 'links.json'
# Content\ is left out: it is made by the fetch scripts and the generator, not kept in git, and a clean clone has none of it.
$PathPrefixes = 'docs|Tools|Source|Specs|Scenarios|Config|Recordings'
$CommonParameters = @('Verbose', 'Debug', 'ErrorAction', 'WarningAction', 'InformationAction', 'ErrorVariable', 'WarningVariable',
    'InformationVariable', 'OutVariable', 'OutBuffer', 'PipelineVariable', 'WhatIf', 'Confirm')

# ---------------------------------------------------------------------------------------------------------------------------
# Parsing: pure functions, held by the self-test.

function Get-HeadingSlug {
    # GitHub's anchor of a heading's text: the text as rendered (code marks, emphasis and link targets gone), lowercase, every
    # character but a letter, a mark, a digit, a connector (the underscore), a space or a hyphen dropped, a space a hyphen.
    param([string]$Text)
    $t = [regex]::Replace($Text, '!?\[([^\]]*)\]\([^)]*\)', '$1')
    $t = [regex]::Replace($t, '<[^>]+>', '')
    # Emphasis by underscores (_word_) is dropped as rendered, outside code only; an underscore inside a word stays.
    $parts = [regex]::Split($t, '(`+[^`]*`+)')
    $t = -join ($parts | ForEach-Object { if ($_ -match '^`') { $_ } else { [regex]::Replace($_, '(?<![\p{L}\p{N}_])_+([^_]+?)_+(?![\p{L}\p{N}_])', '$1') } })
    $t = $t -replace '[`*]', ''
    $t = $t.Trim().ToLowerInvariant()
    $t = [regex]::Replace($t, '[^\p{L}\p{M}\p{N}\p{Pc} -]', '')
    return ($t -replace ' ', '-')
}

function Remove-CodeSpans {
    # The line with every `code` span blanked (same length), so that links inside code are not taken for links.
    param([string]$Line)
    return [regex]::Replace($Line, '(`+)(.+?)\1', { param($m) ' ' * $m.Length })
}

function Get-CodeSpans {
    param([string]$Line)
    return @([regex]::Matches($Line, '(`+)(.+?)\1') | ForEach-Object { $_.Groups[2].Value.Trim() })
}

function Get-MarkdownLinks {
    # Targets of inline links and images, and of reference definitions, in a line (code spans left out).
    param([string]$Line)
    $plain = Remove-CodeSpans $Line
    $targets = New-Object System.Collections.Generic.List[string]
    foreach ($m in [regex]::Matches($plain, '!?\[(?:[^\[\]]|\[[^\]]*\])*\]\(\s*<?([^)\s>]+)>?(?:\s+"[^"]*")?\s*\)')) {
        $targets.Add($m.Groups[1].Value)
    }
    $def = [regex]::Match($plain, '^\s{0,3}\[[^\]]+\]:\s*<?(\S+?)>?(\s|$)')
    if ($def.Success) { $targets.Add($def.Groups[1].Value) }
    return $targets.ToArray()
}

function Get-DocumentStructure {
    # The anchors of a .md (headings by GitHub's rules, with -1, -2 for repeats; <a name> and <a id>) and its lines, each
    # marked as inside a fenced code block or not.
    param([string[]]$Lines)
    $anchors = New-Object 'System.Collections.Generic.HashSet[string]'
    $seen = @{}
    $inFence = $false
    $fence = ''
    $code = New-Object bool[] $Lines.Count
    for ($i = 0; $i -lt $Lines.Count; $i++) {
        $line = $Lines[$i]
        # A fence opens with three or more backquotes or tildes (an info string may follow) and closes with as many or more of the
        # same character and nothing after them.
        $f = [regex]::Match($line, '^\s{0,3}(```+|~~~+)(.*)$')
        if ($f.Success) {
            if (-not $inFence) { $inFence = $true; $fence = $f.Groups[1].Value; $code[$i] = $true; continue }
            if ($f.Groups[1].Value[0] -eq $fence[0] -and $f.Groups[1].Value.Length -ge $fence.Length -and $f.Groups[2].Value.Trim() -eq '') {
                $inFence = $false; $code[$i] = $true; continue
            }
        }
        $code[$i] = $inFence
        if ($inFence) { continue }
        $text = $null
        $h = [regex]::Match($line, '^\s{0,3}#{1,6}\s+(.*?)\s*#*\s*$')
        if ($h.Success) { $text = $h.Groups[1].Value }
        elseif ($i -gt 0 -and $line -match '^\s{0,3}(=+|-+)\s*$' -and $Lines[$i - 1].Trim() -ne '' -and
                -not $code[$i - 1] -and $Lines[$i - 1] -notmatch '^\s*([|>*+-]|\d+\.|#)') {
            $text = $Lines[$i - 1]
        }
        if ($null -ne $text) {
            $slug = Get-HeadingSlug $text
            if ($seen.ContainsKey($slug)) { $seen[$slug]++; $slug = "$slug-$($seen[$slug])" } else { $seen[$slug] = 0 }
            [void]$anchors.Add($slug)
        }
        foreach ($a in [regex]::Matches($line, '<a\s+(?:name|id)\s*=\s*"([^"]+)"')) { [void]$anchors.Add($a.Groups[1].Value) }
    }
    return @{ Anchors = $anchors; Code = $code }
}

function Split-CodeWords {
    # The words of a code span as a shell would split them (quotes keep spaces; the quotes are dropped).
    param([string]$Span)
    return @([regex]::Matches($Span, '"[^"]*"|''[^'']*''|\S+') | ForEach-Object { $_.Value.Trim('"', "'") })
}

function Get-RepoPathMentions {
    # Paths to repo files in a code span: the first word, or any word, that starts with a known top folder.
    param([string]$Span)
    $found = @()
    foreach ($w in (Split-CodeWords $Span)) {
        if ($w -match "^($PathPrefixes)[\\/]") {
            $p = $w -replace ':\d+(-\d+)?$', '' -replace '[,;:.)]+$', ''
            $found += $p
        }
    }
    return $found
}

function Get-TuningMentions {
    # Tuning names in a code span: group.name, also inside name=value,name=value lists. A word with < or [ is a pattern of a
    # result's field, and a word that ends in a file's extension is a file, not a name.
    param([string]$Span, [string[]]$Groups)
    $alt = ($Groups | ForEach-Object { [regex]::Escape($_) }) -join '|'
    $found = @()
    foreach ($m in [regex]::Matches($Span, "(?<![\w.\\/<>-])(?:$alt)\.[A-Za-z0-9_*]+(?:\.[A-Za-z0-9_*]+)*(?![\w(<\[]|\.[<\[])")) {
        $v = $m.Value.TrimEnd('.')
        if ($v -match '\.(json|md|ps1|cpp|h|csv|txt|png|mp4|uasset|umap)$') { continue }
        $found += $v
    }
    return $found
}

function Get-ScenarioMentions {
    # Words in a code span that look like a scenario's name: lowercase words joined by underscores, standing alone.
    param([string]$Span)
    $found = @()
    # A word that ends in a unit is a field or a metric (`step_lag_cm`), not a scenario.
    foreach ($m in [regex]::Matches($Span, '(?<![\w.\\/<>=-])[a-z][a-z0-9]*(?:_[a-z0-9*]+)+(?![\w.\\/(<>=-])')) {
        if ($m.Value -match '_(s|ms|cm|mm|m|ns|mps|deg|n|nm|kg|pct|hz|rad|j|w)$') { continue }
        $found += $m.Value
    }
    return $found
}

function Get-ScriptParameterMentions {
    # In one code span: the Tools script it runs and the -Names after it, up to a pipe, a command's end or a redirection (what
    # follows is another command's). A second script in the span is not judged.
    param([string]$Span)
    $words = Split-CodeWords $Span
    $script = $null
    $params = @()
    foreach ($w in $words) {
        if ($null -eq $script) {
            $s = [regex]::Match($w, '(?:^|[\\/])([\w-]+)\.ps1$')
            if ($s.Success) { $script = $s.Groups[1].Value }
            continue
        }
        if ($w -match '^(\||;|&&|\|\||\d?\*?>>?.*|&)$' -or $w -match '\.ps1$') { break }
        if ($w -match '^-([A-Za-z]\w*)$') { $params += $Matches[1] }
        if ($w.EndsWith(';')) { break }
    }
    return @{ Script = $script; Params = $params }
}

function Join-OpenSpans {
    # The lines of a document with every code span that runs on over a line break joined to the line it begins on (that line
    # takes the text of the next ones, which are left empty), so that a span is read whole. A line inside a fence is not joined,
    # nor one with no open span; five lines at most.
    param([string[]]$Lines, [bool[]]$Code)
    $out = [string[]]$Lines.Clone()
    for ($i = 0; $i -lt $out.Count; $i++) {
        if ($Code[$i]) { continue }
        $joined = 0
        while ((([regex]::Matches(($out[$i] -replace '``+', ''), '`')).Count % 2) -eq 1 -and $joined -lt 5 -and ($i + $joined + 1) -lt $out.Count) {
            $next = $i + $joined + 1
            # A code span never crosses a block: a blank line, a table row, a list item, a heading or a quote ends it.
            if ($Code[$next] -or $out[$next].Trim() -eq '' -or $out[$next] -match '^\s*\||^\s*([-*+]|\d+[.)])\s|^\s{0,3}#|^\s*>') { break }
            $out[$i] = $out[$i] + ' ' + $out[$next].Trim()
            $out[$next] = ''
            $joined++
        }
    }
    return $out
}

function Test-NameIn {
    # A name (it may have a * of its own) against a set of names that may have * too.
    param([string]$Name, $Set, [string[]]$Patterns)
    foreach ($p in $Patterns) { if ($Name -like $p) { return $true } }
    if ($Name.Contains('*')) {
        $rx = '^' + ([regex]::Escape($Name) -replace '\\\*', '.*') + '$'
        foreach ($n in $Set) { if ($n -match $rx) { return $true } }
        return $false
    }
    return $Set.Contains($Name)
}

function Get-TuningNames {
    # Every value of the tuning files by its dotted name, and every group above a value.
    param([string]$Dir)
    $names = New-Object 'System.Collections.Generic.HashSet[string]'
    $groups = New-Object 'System.Collections.Generic.HashSet[string]'
    $walk = {
        param($obj, [string]$prefix)
        foreach ($p in $obj.PSObject.Properties) {
            $n = if ($prefix) { "$prefix.$($p.Name)" } else { $p.Name }
            [void]$names.Add($n)
            if (-not $prefix) { [void]$groups.Add($p.Name) }
            if ($p.Value -is [System.Management.Automation.PSCustomObject]) { & $walk $p.Value $n }
        }
    }
    foreach ($f in (Get-ChildItem $Dir -Filter *.json)) {
        & $walk (Get-Content $f.FullName -Raw -Encoding UTF8 | ConvertFrom-Json) ''
    }
    return @{ Names = $names; Groups = @($groups) }
}

function Get-ScriptParameters {
    # The parameters of every Tools script's param block, read by PowerShell's own parser.
    param([string]$Dir)
    $map = @{}
    foreach ($f in (Get-ChildItem $Dir -Filter *.ps1)) {
        $tokens = $null; $errors = $null
        $ast = [System.Management.Automation.Language.Parser]::ParseFile($f.FullName, [ref]$tokens, [ref]$errors)
        if (-not $ast.ParamBlock) { continue }   # a library of functions (common.ps1), not a script that is run
        $set = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
        foreach ($p in $ast.ParamBlock.Parameters) { [void]$set.Add($p.Name.VariablePath.UserPath) }
        $map[$f.BaseName] = $set
    }
    return $map
}

function Get-CodeWords {
    # Every snake_case word that the code or the tools spell in a string, or that a json file of the repo has as a key: names of
    # fields, metrics, events and folders, which are not scenarios.
    param([string]$Root)
    $words = New-Object 'System.Collections.Generic.HashSet[string]'
    $files = @(Get-ChildItem (Join-Path $Root 'Source') -Recurse -Include *.cpp, *.h) + @(Get-ChildItem (Join-Path $Root 'Tools') -Filter *.ps1)
    foreach ($f in $files) {
        $text = [System.IO.File]::ReadAllText($f.FullName)
        foreach ($m in [regex]::Matches($text, '"[^"\r\n]*"|''[^''\r\n]*''')) {
            foreach ($w in [regex]::Matches($m.Value, '[a-z][a-z0-9]*(?:_[a-z0-9]+)+')) { [void]$words.Add($w.Value) }
        }
    }
    foreach ($dir in 'Config\Tuning', 'Specs', 'Scenarios', 'Tools') {
        foreach ($f in (Get-ChildItem (Join-Path $Root $dir) -Filter *.json -ErrorAction SilentlyContinue)) {
            foreach ($m in [regex]::Matches([System.IO.File]::ReadAllText($f.FullName), '"([a-z][a-z0-9]*(?:_[a-z0-9]+)+)"\s*:')) {
                [void]$words.Add($m.Groups[1].Value)
            }
        }
    }
    return $words
}

# ---------------------------------------------------------------------------------------------------------------------------
# The self-test: the parsing above held against cases whose answers are known.

function Invoke-SelfTest {
    $failures = New-Object System.Collections.Generic.List[string]
    function Expect([string]$what, $got, $want) {
        $g = ($got | ForEach-Object { "$_" }) -join '|'
        $w = ($want | ForEach-Object { "$_" }) -join '|'
        if ($g -cne $w) { $failures.Add("$what`: got '$g', want '$w'") }
    }
    Expect 'slug of a numbered heading' (Get-HeadingSlug '8. Order of work and plan of parts') '8-order-of-work-and-plan-of-parts'
    Expect 'slug with an apostrophe' (Get-HeadingSlug "R10's gate") 'r10s-gate'
    # Cyrillic made of its code points: Windows PowerShell 5.1 reads a script without a byte-order mark in the ANSI code page.
    $ru = { param([int[]]$c) -join ($c | ForEach-Object { [char]$_ }) }
    $heading = (& $ru 0x41F, 0x440, 0x438, 0x432, 0x435, 0x442) + ', ' + (& $ru 0x41C, 0x438, 0x440) + '! ' + (& $ru 0x42D, 0x442, 0x430, 0x43F) + ' 2'
    $slug = (& $ru 0x43F, 0x440, 0x438, 0x432, 0x435, 0x442) + '-' + (& $ru 0x43C, 0x438, 0x440) + '-' + (& $ru 0x44D, 0x442, 0x430, 0x43F) + '-2'
    Expect 'slug of Cyrillic' (Get-HeadingSlug $heading) $slug
    Expect 'slug with code, bold and a link' (Get-HeadingSlug '`getup_held` and **bold** [link](x.md)') 'getup_held-and-bold-link'
    Expect 'slug keeps hyphens and drops other marks' (Get-HeadingSlug 'SP-4: the pool (decision 13)') 'sp-4-the-pool-decision-13'
    $s = Get-DocumentStructure @('# A', 'text', '## A', '```', '# not a heading', '```', '### A', 'Setext', '------', '<a name="here"></a>')
    Expect 'repeated headings' (@($s.Anchors) | Sort-Object) (@('a', 'a-1', 'a-2', 'here', 'setext') | Sort-Object)
    Expect 'fenced lines marked' ($s.Code[3], $s.Code[4], $s.Code[5], $s.Code[6]) ($true, $true, $true, $false)
    Expect 'links outside code only' (Get-MarkdownLinks 'see [x](docs/PRD.md#status) and `[y](z.md)` and ![i](a.png "t")') ('docs/PRD.md#status', 'a.png')
    Expect 'a reference definition' (Get-MarkdownLinks '[ref]: ../docs/testing.md#r10s-gate') '../docs/testing.md#r10s-gate'
    Expect 'a link with brackets in its text' (Get-MarkdownLinks '[a [b] c](f.md)') 'f.md'
    Expect 'paths with backslashes and a line' (Get-RepoPathMentions 'Tools\check-links.ps1 -Paths docs/PRD.md:13') ('Tools\check-links.ps1', 'docs/PRD.md')
    Expect 'not a path' (Get-RepoPathMentions 'Saved\Tools\links.json') @()
    Expect 'tuning in a list' (Get-TuningMentions '-Tune "behaviour.fear=1,getup.fit_*"' @('behaviour', 'getup')) ('behaviour.fear', 'getup.fit_*')
    Expect 'tuning: a file and a field pattern are not names' (Get-TuningMentions 'poses.json effectors.list.<hand>.x control.fall.sense' @('poses', 'effectors', 'fall')) @()
    Expect 'scenarios' (Get-ScenarioMentions 'scenario.ps1 -Name drop_passive getup_laid_* Saved\Films\r10_a x=b_c step_lag_cm') ('drop_passive', 'getup_laid_*')
    $p = Get-ScriptParameterMentions 'powershell -ExecutionPolicy Bypass -File Tools\getup-stand.ps1 -Near 70 -Tune "a=-1"'
    Expect 'script and its parameters' ($p.Script, ($p.Params -join ',')) ('getup-stand', 'Near,Tune')
    Expect 'slug drops emphasis by underscores, not a snake_case word' (Get-HeadingSlug 'The _first_ part of `snake_case` and snake_case') 'the-first-part-of-snake_case-and-snake_case'
    $s = Get-DocumentStructure @('```json', '# in code', '```text', '# still in code', '```', '# out')
    Expect 'a fence closes only on a bare fence' (@($s.Anchors)) @('out')
    $p = Get-ScriptParameterMentions 'suite.ps1 -SaveBaseline x | Select-Object -First 15'
    Expect 'parameters stop at a pipe' ($p.Params -join ',') 'SaveBaseline'
    $p = Get-ScriptParameterMentions 'suite.ps1 -Label a; Tools\baseline.ps1 -CompareMatrix x'
    Expect 'parameters stop at a command end' ($p.Params -join ',') 'Label'
    $joined = Join-OpenSpans @('text `Tools\suite.ps1', '-Long` more', 'next') @($false, $false, $false)
    Expect 'a code span over a line break is read whole' ($joined[0], $joined[1], $joined[2]) ('text `Tools\suite.ps1 -Long` more', '', 'next')
    $joined = Join-OpenSpans @('- item one `open', '- item two `code` here', 'next') @($false, $false, $false)
    Expect 'a code span does not cross a list item' ($joined[0], $joined[1]) ('- item one `open', '- item two `code` here')
    $set = New-Object 'System.Collections.Generic.HashSet[string]'
    [void]$set.Add('getup.fit_surface'); [void]$set.Add('getup')
    Expect 'a wildcard name' (Test-NameIn 'getup.fit_*' $set @()) $true
    Expect 'a removed pattern' (Test-NameIn 'getup.clip_speed' $set @('getup.clip_*')) $true
    Expect 'a removed wildcard' (Test-NameIn 'getup.clip_*' $set @('getup.clip_*')) $true
    Expect 'a missing name' (Test-NameIn 'getup.carry' $set @()) $false
    return $failures
}

# ---------------------------------------------------------------------------------------------------------------------------

$selfFailures = Invoke-SelfTest
if ($SelfTest -or $selfFailures.Count -gt 0) {
    if ($selfFailures.Count -gt 0) {
        Write-Output "LINKS FAIL: the self-test failed in $($selfFailures.Count) case(s)"
        $selfFailures | ForEach-Object { Write-Output "  $_" }
        exit 1
    }
    if ($SelfTest) { Write-Output 'LINKS OK: the self-test passed'; exit 0 }
}

try {
    Push-Location $RepoRoot
    $excluded = '^(Saved|Intermediate|Binaries|DerivedDataCache)[\\/]'
    if ($Paths) {
        $files = @()
        foreach ($p in ($Paths -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })) {
            $full = Join-Path $RepoRoot $p
            if (Test-Path $full -PathType Container) {
                $files += @(Get-ChildItem $full -Recurse -Filter *.md | ForEach-Object { $_.FullName.Substring($RepoRoot.Length + 1) })
            } elseif (Test-Path $full) { $files += $p } else { throw "no such file: $p" }
        }
    } else {
        $files = @(& git ls-files --cached --others --exclude-standard -- '*.md')
    }
    # A file deleted from the tree but still in git's index is not read.
    $files = @($files | ForEach-Object { $_ -replace '/', '\' } | Where-Object { $_ -notmatch $excluded -and (Test-Path -LiteralPath (Join-Path $RepoRoot $_)) } | Sort-Object -Unique)

    # The repo's files and folders as git spells them: a link or path that names one in another case works on Windows and is broken
    # on GitHub or a clone on Linux. What git does not know (generated content) is only looked for.
    $exactPaths = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
    $anyCasePaths = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($tracked in @(& git ls-files --cached --others --exclude-standard)) {
        $p = $tracked
        while ($p) {
            [void]$exactPaths.Add($p); [void]$anyCasePaths.Add($p)
            $cut = $p.LastIndexOf('/')
            if ($cut -lt 0) { break }
            $p = $p.Substring(0, $cut)
        }
    }
    $caseProblem = {
        param([string]$fullPath)
        $root = $RepoRoot.TrimEnd('\') + '\'
        if (-not $fullPath.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) { return $null }
        $relative = $fullPath.Substring($root.Length).TrimEnd('\') -replace '\\', '/'
        if ($relative -and $anyCasePaths.Contains($relative) -and -not $exactPaths.Contains($relative)) { return "not as git spells it (case)" }
        return $null
    }

    $tuning = Get-TuningNames (Join-Path $RepoRoot 'Config\Tuning')
    $scenarioNames = New-Object 'System.Collections.Generic.HashSet[string]'
    Get-ChildItem (Join-Path $RepoRoot 'Scenarios') -Filter *.json | ForEach-Object { [void]$scenarioNames.Add($_.BaseName) }
    $scenarioFirstWords = New-Object 'System.Collections.Generic.HashSet[string]'
    foreach ($n in $scenarioNames) { [void]$scenarioFirstWords.Add(($n -split '_')[0]) }
    $codeWords = Get-CodeWords $RepoRoot
    $scriptParams = Get-ScriptParameters (Join-Path $RepoRoot 'Tools')

    $structures = @{}
    $getStructure = {
        param([string]$full)
        $key = $full.ToLowerInvariant()
        if (-not $structures.ContainsKey($key)) {
            $structures[$key] = Get-DocumentStructure ([System.IO.File]::ReadAllLines($full))
        }
        return $structures[$key]
    }

    $problems = New-Object System.Collections.Generic.List[object]
    $counts = [ordered]@{ links = 0; anchors = 0; paths = 0; tuning = 0; scenarios = 0; params = 0 }
    $add = { param($file, $line, $kind, $what, $detail) $problems.Add([pscustomobject][ordered]@{ file = [string]$file; line = [int]$line; kind = [string]$kind; what = [string]$what; detail = [string]$detail }) }

    foreach ($rel in $files) {
        $full = Join-Path $RepoRoot $rel
        $structure = & $getStructure $full
        $lines = Join-OpenSpans ([System.IO.File]::ReadAllLines($full)) $structure.Code
        $dir = Split-Path -Parent $full
        $archive = $rel -match '^docs\\recon\\'
        $removed = @()
        foreach ($m in [regex]::Matches([System.IO.File]::ReadAllText($full), '<!--\s*(?:removed|planned|not-names):\s*([^>]*?)\s*-->')) {
            $removed += @($m.Groups[1].Value -split '\s+' | Where-Object { $_ })
        }
        $row = ''
        $rowStart = 0
        for ($i = 0; $i -lt $lines.Count; $i++) {
            if ($structure.Code[$i]) { continue }
            $line = $lines[$i]
            $n = $i + 1

            foreach ($target in (Get-MarkdownLinks $line)) {
                if ($target -match '^(https?|mailto|ftp):' -or $target -match '^[a-z]+://') { continue }
                $counts.links++
                $parts = $target -split '#', 2
                $pathPart = [uri]::UnescapeDataString($parts[0]) -replace '/', '\'
                $anchor = if ($parts.Count -gt 1) { [uri]::UnescapeDataString($parts[1]) } else { $null }
                if ($pathPart) {
                    $targetFull = [System.IO.Path]::GetFullPath((Join-Path $dir $pathPart))
                    if ($targetFull.StartsWith((Join-Path $RepoRoot 'Saved'), [StringComparison]::OrdinalIgnoreCase)) { continue }
                    if (-not (Test-Path -LiteralPath $targetFull)) { & $add $rel $n 'link' $target 'no such file'; continue }
                    $why = & $caseProblem $targetFull
                    if ($why) { & $add $rel $n 'link' $target $why; continue }
                } else { $targetFull = $full }
                if ($anchor -and $targetFull -match '\.md$') {
                    $counts.anchors++
                    if (-not (& $getStructure $targetFull).Anchors.Contains($anchor)) { & $add $rel $n 'anchor' $target 'no heading with that anchor' }
                }
            }
            if ($archive) { continue }

            $spans = Get-CodeSpans $line
            foreach ($span in $spans) {
                foreach ($p in (Get-RepoPathMentions $span)) {
                    if ($p -match '[<>]') { continue }
                    if (Test-NameIn ($p -replace '\\', '/') (New-Object 'System.Collections.Generic.HashSet[string]') @($removed | ForEach-Object { $_ -replace '\\', '/' })) { continue }
                    $counts.paths++
                    $pf = Join-Path $RepoRoot ($p -replace '/', '\')
                    $exists = if ($p.Contains('*')) { [bool](Get-ChildItem $pf -ErrorAction SilentlyContinue) } else { Test-Path -LiteralPath $pf }
                    if (-not $exists) { & $add $rel $n 'path' $p 'no such file or folder' }
                    elseif (-not $p.Contains('*')) {
                        $why = & $caseProblem ([System.IO.Path]::GetFullPath($pf))
                        if ($why) { & $add $rel $n 'path' $p $why }
                    }
                }
                foreach ($t in (Get-TuningMentions $span $tuning.Groups)) {
                    $counts.tuning++
                    if (-not (Test-NameIn $t $tuning.Names $removed)) { & $add $rel $n 'tuning' $t 'not a value or group of Config/Tuning/*.json' }
                }
                foreach ($s in (Get-ScenarioMentions $span)) {
                    if (-not $scenarioFirstWords.Contains(($s -split '_')[0])) { continue }
                    if ($codeWords.Contains($s) -or $tuning.Names.Contains($s)) { continue }
                    $counts.scenarios++
                    if (-not (Test-NameIn $s $scenarioNames $removed)) { & $add $rel $n 'scenario' $s 'no such scenario in Scenarios/' }
                }
                $sp = Get-ScriptParameterMentions $span
                if ($sp.Script -and $scriptParams.ContainsKey($sp.Script)) {
                    foreach ($param in $sp.Params) {
                        $counts.params++
                        if (-not $scriptParams[$sp.Script].Contains($param) -and $CommonParameters -notcontains $param -and $removed -notcontains "-$param") {
                            & $add $rel $n 'param' "$($sp.Script).ps1 -$param" 'not a parameter of the script'
                        }
                    }
                }
            }

            # A table row with a command (`powershell ... -File Tools\x.ps1 ...`, as the rows of CLAUDE.md's commands have) that
            # names no other script: every -Name that begins a code span of the row is a parameter of that script. A row may run
            # on over several lines until one ends with "|".
            if ($line -match '^\s*\|') { $row = $line; $rowStart = $n } elseif ($row) { $row += ' ' + $line }
            if ($row -and $line -match '\|\s*$') {
                $rowSpans = Get-CodeSpans $row
                $scripts = @($rowSpans | ForEach-Object { [regex]::Matches($_, '([\w-]+)\.ps1\b') } | ForEach-Object { $_.Groups[1].Value } |
                    Sort-Object -Unique)
                $command = @($rowSpans | Where-Object { $_ -match '^powershell\s' }).Count -gt 0
                if ($command -and $scripts.Count -eq 1 -and $scriptParams.ContainsKey($scripts[0])) {
                    foreach ($span in (Get-CodeSpans $row)) {
                        if ((Get-ScriptParameterMentions $span).Script) { continue }
                        $first = (Split-CodeWords $span) | Select-Object -First 1
                        if ($first -match '^-([A-Za-z]\w*)$') {
                            $counts.params++
                            if (-not $scriptParams[$scripts[0]].Contains($Matches[1]) -and $CommonParameters -notcontains $Matches[1] -and $removed -notcontains "-$($Matches[1])") {
                                & $add $rel $rowStart 'param' "$($scripts[0]).ps1 -$($Matches[1])" 'not a parameter of the script (named in its table row)'
                            }
                        }
                    }
                }
                $row = ''
            }
        }
    }
    Pop-Location
} catch {
    Write-Output "LINKS ERROR: $($_.Exception.Message) ($($_.InvocationInfo.PositionMessage))"
    exit 2
}

$ok = $problems.Count -eq 0
$byKind = [ordered]@{}
foreach ($p in $problems) { if ($byKind.Contains($p.kind)) { $byKind[$p.kind]++ } else { $byKind[$p.kind] = 1 } }
Write-JsonFile $resultFile ([ordered]@{ ok = $ok; files = $files.Count; checked = $counts; problems_by_kind = $byKind; problems = $problems.ToArray() })
$summary = "$($files.Count) file(s); " + (($counts.Keys | ForEach-Object { "$($counts[$_]) $_" }) -join ', ')
if ($ok) {
    Write-Output "LINKS OK: $summary"
    exit 0
}
$kinds = ($byKind.Keys | ForEach-Object { "$($byKind[$_]) $_" }) -join ', '
Write-Output "LINKS FAIL: $($problems.Count) problem(s) ($kinds) in $summary"
foreach ($p in $problems) { Write-Output ("  {0}:{1} [{2}] {3}: {4}" -f $p.file, $p.line, $p.kind, $p.what, $p.detail) }
Write-Output "  (every problem in $resultFile)"
exit 1
