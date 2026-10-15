# Installs (or refreshes) the ForeverBots add-on in the WoW Forever game folder. No server, no administrator rights needed.
# Where the add-on comes from, in this order: a folder "ForeverBots" next to this script (the add-on zip from the Releases page),
# the module's own folder (src\modules\PlayerbotV2\Addon\ForeverBots, when run from a source checkout), or the newest release on GitHub.
#   .\Install-Addon.ps1                       -> finds the game folder, asks you to confirm it
#   .\Install-Addon.ps1 -Client "D:\WoW Forever"
#   .\Install-Addon.ps1 -Download             -> always fetch the newest add-on from GitHub
# Windows PowerShell 5.1.
param([string]$Client, [switch]$Download, [string]$Repo = 'Karrz1/Karrz-playerbots-wow-forever')
$ErrorActionPreference = 'Stop'
$build = '70338'
$memory = Join-Path $env:LOCALAPPDATA 'ForeverBots\client.txt'

function Find-ClientFolders([string[]]$roots, [int]$maxSeconds = 25) {
    $found = New-Object System.Collections.Generic.List[string]
    $clock = [Diagnostics.Stopwatch]::StartNew()
    foreach ($r in $roots) {
        if (-not (Test-Path $r)) { continue }
        $stack = New-Object System.Collections.Generic.Stack[object]; $stack.Push(@($r, 0))
        while ($stack.Count -and $clock.Elapsed.TotalSeconds -lt $maxSeconds) {
            $item = $stack.Pop(); $dir = $item[0]; $depth = $item[1]
            if (Test-Path (Join-Path $dir '.build.info')) {
                if ((Test-Path (Join-Path $dir '_classic_beta_')) -or @(Get-ChildItem $dir -Filter 'WowB*.exe' -ErrorAction SilentlyContinue).Count) {
                    if (-not $found.Contains($dir)) { $found.Add($dir) }
                    continue
                }
            }
            if ($depth -ge 3) { continue }
            foreach ($sub in Get-ChildItem $dir -Directory -ErrorAction SilentlyContinue) {
                if ($sub.Name -in 'Windows', '$Recycle.Bin', 'System Volume Information', 'ProgramData', 'node_modules', '.git') { continue }
                $stack.Push(@($sub.FullName, ($depth + 1)))
            }
        }
    }
    $withBuild = @($found | Where-Object { (Get-Content (Join-Path $_ '.build.info') -Raw -ErrorAction SilentlyContinue) -match $build })
    @($withBuild) + @($found | Where-Object { $withBuild -notcontains $_ })
}
function Pick-Folder($start) {
    Add-Type -AssemblyName System.Windows.Forms
    $d = New-Object System.Windows.Forms.FolderBrowserDialog
    if ($start) { $d.SelectedPath = $start }
    $d.Description = 'Select your WoW Forever game folder (the one that contains the file .build.info)'
    $d.ShowNewFolderButton = $false
    if ($d.ShowDialog() -eq 'OK') { return $d.SelectedPath }
    $null
}
function Addon-Version($dir) {
    $toc = Join-Path $dir 'ForeverBots.toc'
    if (-not (Test-Path $toc)) { return $null }
    $m = Select-String -Path $toc -Pattern '^##\s*Version:\s*(.+)$' | Select-Object -First 1
    if ($m) { $m.Matches[0].Groups[1].Value.Trim() } else { '?' }
}

# 1. where the add-on comes from
$src = $null
if (-not $Download) {
    foreach ($c in (Join-Path $PSScriptRoot 'ForeverBots'), (Join-Path $PSScriptRoot '..\..\src\modules\PlayerbotV2\Addon\ForeverBots')) {
        if (Test-Path (Join-Path $c 'ForeverBots.toc')) { $src = (Resolve-Path $c).Path; break }
    }
}
if (-not $src) {
    Write-Host 'Getting the newest add-on from GitHub...'
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $tmp = Join-Path $env:TEMP ('forever-bots-addon-' + [guid]::NewGuid().ToString('N')); New-Item -ItemType Directory -Force $tmp | Out-Null
    $zip = Join-Path $tmp 'addon.zip'; $url = $null
    try {
        $rel = Invoke-RestMethod "https://api.github.com/repos/$Repo/releases/latest" -Headers @{ 'User-Agent' = 'ForeverBots-addon-installer' }
        $asset = $rel.assets | Where-Object { $_.name -like 'ForeverBots-addon-*.zip' } | Select-Object -First 1
        if ($asset) { $url = $asset.browser_download_url }
    } catch { }
    if (-not $url) { $url = "https://raw.githubusercontent.com/$Repo/main/addon/ForeverBots-addon.zip" }
    Invoke-WebRequest $url -OutFile $zip -UseBasicParsing -Headers @{ 'User-Agent' = 'ForeverBots-addon-installer' }
    Expand-Archive $zip -DestinationPath $tmp -Force
    $toc = Get-ChildItem $tmp -Recurse -Filter 'ForeverBots.toc' | Select-Object -First 1
    if (-not $toc) { throw 'The download does not contain the add-on (no ForeverBots.toc).' }
    $src = $toc.DirectoryName
}

# 2. the game folder
if (-not $Client -and (Test-Path $memory)) { $Client = (Get-Content $memory -TotalCount 1).Trim() }
if ($Client -and -not (Test-Path (Join-Path $Client '.build.info'))) { Write-Warning "$Client has no .build.info; looking again."; $Client = $null }
if (-not $Client) {
    Write-Host 'Looking for your WoW Forever game folder (at most 25 seconds)...'
    $roots = @("$env:USERPROFILE\Desktop", "$env:USERPROFILE\Downloads", "$env:USERPROFILE\Documents", "$env:PUBLIC\Games", "$env:ProgramFiles", "${env:ProgramFiles(x86)}")
    foreach ($d in Get-PSDrive -PSProvider FileSystem) { $roots += $d.Root }
    $found = @(Find-ClientFolders $roots)
    $guess = if ($found.Count) { $found[0] } else { $null }
    if ($guess) { Write-Host "Found a likely game folder: $guess" } else { Write-Host 'Could not find it by itself.' }
    Write-Host 'A window opens: select your WoW Forever game folder (the one that contains the file .build.info).'
    $Client = Pick-Folder $guess
    if (-not $Client -or -not (Test-Path (Join-Path $Client '.build.info'))) { throw 'No valid game folder chosen (it must contain .build.info). Run this again.' }
}
New-Item -ItemType Directory -Force (Split-Path $memory -Parent) | Out-Null
Set-Content $memory $Client -Encoding ascii

# 3. install
$dest = $null
foreach ($sub in '_classic_beta_\Interface\AddOns', '_classic_\Interface\AddOns', 'Interface\AddOns') {
    $p = Join-Path $Client $sub
    if (Test-Path (Split-Path $p -Parent)) { $dest = $p; break }
}
if (-not $dest) { $dest = Join-Path $Client '_classic_beta_\Interface\AddOns' }
New-Item -ItemType Directory -Force $dest | Out-Null
$target = Join-Path $dest 'ForeverBots'
$old = Addon-Version $target
if (Test-Path $target) { Remove-Item $target -Recurse -Force }   # no stale files from an older version
Copy-Item $src $target -Recurse -Force
$new = Addon-Version $target
Write-Host ''
Write-Host "ForeverBots add-on $new installed to $target" -ForegroundColor Green
if ($old) { Write-Host "(it replaced version $old)" }
Write-Host 'Next: start the game. At the character select screen click AddOns, make sure ForeverBots is ticked (and "Load out of date AddOns" if shown).'
Write-Host 'In game: /reload, then /fb opens the bots window.'
