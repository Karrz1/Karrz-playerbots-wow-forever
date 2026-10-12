# Summarises dungeon/raid fights from the worldserver bot log: boss kills, who died to what, wipes.
# Reads the PlayerbotV2 lines [bot_death] (killer, spell, damage, grp_alive=alive/total of the dying player's group)
# and [boss_kill] (raiders_alive=alive/total). Needs Logger.playerbot.v2=3 in worldserver.conf.
# Usage: .\Fight-Report.ps1 [-Hours 3] [-Log <path>]   (default: newest *.log under server-bot\logs, last 3 hours)
param(
    [double]$Hours = 3,
    [string]$Log = ''
)
if (-not $Log) {
    $dir = Join-Path $env:USERPROFILE 'wow forever\server-bot\logs'
    $Log = (Get-ChildItem -Path $dir -Filter *.log -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1).FullName
}
if (-not $Log -or -not (Test-Path $Log)) { Write-Host "No log found (pass -Log <path>)"; exit 1 }
$since = [DateTimeOffset]::UtcNow.AddHours(-$Hours).ToUnixTimeSeconds()

$deaths = New-Object System.Collections.Generic.List[object]
$bosses = New-Object System.Collections.Generic.List[object]
foreach ($line in Get-Content $Log) {
    if ($line -match '\[boss_kill\] ts=(\d+) map=(\d+) (.+?) \((\d+)\) raiders_alive=(\d+)/(\d+)') {
        if ([long]$Matches[1] -lt $since) { continue }
        $bosses.Add([pscustomobject]@{ Ts = [long]$Matches[1]; Map = $Matches[2]; Boss = $Matches[3]; Alive = [int]$Matches[5]; Total = [int]$Matches[6] })
    }
    elseif ($line -match '\[bot_death\] ts=(\d+) (\S+) \((bot|player)\) map=(\d+) .*?killer=(.+?) spell=(.+?) dmg=(-?\d+) .*?grp_alive=(\d+)/(\d+)') {
        if ([long]$Matches[1] -lt $since) { continue }
        $deaths.Add([pscustomobject]@{ Ts = [long]$Matches[1]; Who = $Matches[2]; Bot = ($Matches[3] -eq 'bot'); Map = [int]$Matches[4]
                                       Killer = $Matches[5]; Spell = $Matches[6]; Dmg = [int]$Matches[7]; Alive = [int]$Matches[8]; Total = [int]$Matches[9] })
    }
}
# Only deaths inside a group of 5+ (dungeon / raid); battleground and solo deaths are not fights to tune.
$deaths = @($deaths | Where-Object { $_.Total -ge 5 })
function T($ts) { [DateTimeOffset]::FromUnixTimeSeconds($ts).ToLocalTime().ToString('HH:mm:ss') }

Write-Host "=== Fight report, last $Hours h ($Log) ===" -ForegroundColor Cyan
Write-Host ("Boss kills: {0}   Group deaths: {1} ({2} bots, {3} human)" -f $bosses.Count, $deaths.Count, @($deaths | Where-Object Bot).Count, @($deaths | Where-Object { -not $_.Bot }).Count)
if ($bosses.Count) { Write-Host "`nBoss kills:"; $bosses | ForEach-Object { "  {0}  {1}  ({2}/{3} alive)" -f (T $_.Ts), $_.Boss, $_.Alive, $_.Total } }
if ($deaths.Count) {
    Write-Host "`nDeaths by killer / spell (what to fix first):"
    $deaths | Group-Object { "$($_.Killer) | $($_.Spell)" } | Sort-Object Count -Descending | Select-Object -First 10 | ForEach-Object { "  {0,3}x  {1}" -f $_.Count, $_.Name }
    Write-Host "`nDeaths by player:"
    $deaths | Group-Object Who | Sort-Object Count -Descending | ForEach-Object { "  {0,3}x  {1}" -f $_.Count, $_.Name }
    $wipes = @($deaths | Where-Object { $_.Alive -eq 0 })
    Write-Host ("`nWipes (last standing member died): {0}" -f $wipes.Count)
    $wipes | ForEach-Object { "  {0}  {1} killed by {2} ({3})" -f (T $_.Ts), $_.Who, $_.Killer, $_.Spell }
}
$raid = (Select-String -Path $Log -Pattern '\[raid_' -SimpleMatch:$false | Measure-Object).Count
Write-Host ("`nRaid-brain lines in the log: {0}" -f $raid)
