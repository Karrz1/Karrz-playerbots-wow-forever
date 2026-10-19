<#
.SYNOPSIS
    Extract WoW client data for TrinityCore (maps / vmaps / mmaps / dbc / Cameras).

.DESCRIPTION
    Runs the full extractor pipeline:
        mapextractor -> vmap4extractor -> vmap4assembler -> mmaps_generator
    in the correct order against a WoW client install.

    For the road-aware mmap workstream (P1.0b corpus dumping) pass -MapsOnly to
    stop after mapextractor.

    See src/modules/PlayerbotV2/docs/CLIENT_DATA_EXTRACTION.md for the full
    user-facing reference.

.PARAMETER ClientPath
    WoW install root containing .build.info / Data/ (e.g. "C:\Program Files (x86)\World of Warcraft\_retail_").

.PARAMETER OutputPath
    Where the extractor binaries live AND where the dbc/maps/vmaps/mmaps/Cameras
    output directories will be written.

.PARAMETER MapsOnly
    Skip vmap4extractor + vmap4assembler + mmaps_generator. Used for the road
    workflow which only needs maps/ + dbc/ + live CASC access.

.PARAMETER Threads
    Worker count for vmap4extractor and mmaps_generator. Default: 0 (all cores).

.PARAMETER Resume
    Safe to run again after an interruption. Each finished stage leaves a marker file (.extract-maps.done, .extract-vmaps.done,
    .extract-mmaps.done) in OutputPath and is skipped next time; an unfinished vmap stage is cleaned and redone; mmaps_generator itself
    skips tiles it already built, so the long stage continues where it stopped. At the end the intermediate Buildings/ and dir_bin/
    folders (about 40 GB) are removed and mmaps_generator runs with --silent (no key press at the end).

.PARAMETER Locale
    DBC locale (enUS, deDE, frFR, etc). Default: enUS.

.EXAMPLE
    .\extract_client_data.ps1 -ClientPath "C:\Program Files (x86)\World of Warcraft\_retail_" -OutputPath "I:\TrinityCore\playerbot\TrinityCore\build\bin\RelWithDebInfo"

.EXAMPLE
    .\extract_client_data.ps1 -ClientPath "C:\Program Files (x86)\World of Warcraft\_retail_" -OutputPath "I:\TrinityCore\playerbot\TrinityCore\build\bin\RelWithDebInfo" -MapsOnly
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ClientPath,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath,

    [switch]$MapsOnly,

    [int]$Threads = 0,

    [switch]$Resume,

    [string]$Locale = "enUS"
)

$ErrorActionPreference = "Stop"

function Write-Step($msg) {
    Write-Host ""
    Write-Host "==============================================================" -ForegroundColor Cyan
    Write-Host " $msg" -ForegroundColor Cyan
    Write-Host "==============================================================" -ForegroundColor Cyan
}

function Fail($msg) {
    Write-Host ""
    Write-Host "ERROR: $msg" -ForegroundColor Red
    exit 1
}

function Format-Size($bytes) {
    if ($bytes -ge 1GB) { return "{0:N2} GB" -f ($bytes / 1GB) }
    if ($bytes -ge 1MB) { return "{0:N1} MB" -f ($bytes / 1MB) }
    return "{0:N1} KB" -f ($bytes / 1KB)
}

function Show-DirSummary($root, $names) {
    Write-Host ""
    Write-Host "--- Output summary ---" -ForegroundColor Yellow
    $totalBytes = 0
    foreach ($name in $names) {
        $p = Join-Path $root $name
        if (Test-Path $p) {
            $files = Get-ChildItem -Recurse -File $p -ErrorAction SilentlyContinue
            $count = ($files | Measure-Object).Count
            $sum   = ($files | Measure-Object -Property Length -Sum).Sum
            if ($null -eq $sum) { $sum = 0 }
            $totalBytes += $sum
            Write-Host ("  {0,-12} files={1,-8} size={2}" -f $name, $count, (Format-Size $sum))
        } else {
            Write-Host ("  {0,-12} MISSING" -f $name) -ForegroundColor DarkGray
        }
    }
    Write-Host ("  {0,-12} {1}" -f "TOTAL", (Format-Size $totalBytes)) -ForegroundColor Green
}

# --- Validate output path + extractor binaries -----------------------------
$OutputPath = (Resolve-Path -LiteralPath $OutputPath -ErrorAction SilentlyContinue)
if (-not $OutputPath) {
    Fail "OutputPath does not exist."
}

$required = @("mapextractor.exe")
if (-not $MapsOnly) {
    $required += @("vmap4extractor.exe", "vmap4assembler.exe", "mmaps_generator.exe")
}

foreach ($bin in $required) {
    $p = Join-Path $OutputPath $bin
    if (-not (Test-Path $p)) {
        Fail "Required binary not found: $p
Build the worldserver project first (cmake --build build --config RelWithDebInfo)."
    }
}

# --- Validate client install -----------------------------------------------
if (-not (Test-Path $ClientPath)) {
    Fail "ClientPath does not exist: $ClientPath"
}
$buildInfo = Join-Path $ClientPath ".build.info"
$dataDir   = Join-Path $ClientPath "Data"
if (-not (Test-Path $buildInfo) -and -not (Test-Path $dataDir)) {
    Fail "ClientPath '$ClientPath' has no .build.info or Data\ - not a valid WoW install root.
Did you point at _retail_/Data/ instead of _retail_/ ?"
}

Write-Host "Client : $ClientPath"
Write-Host "Output : $OutputPath"
Write-Host "Locale : $Locale"
Write-Host "Threads: $(if ($Threads -le 0) { '(all cores)' } else { $Threads })"
Write-Host "Mode   : $(if ($MapsOnly) { 'MAPS-ONLY (road workflow)' } else { 'FULL' })$(if ($Resume) { ' (resumable)' })"

function Test-Done($name) { $Resume -and (Test-Path (Join-Path $OutputPath ".extract-$name.done")) }
function Set-Done($name) { if ($Resume) { Set-Content (Join-Path $OutputPath ".extract-$name.done") (Get-Date -Format s) } }
$clock = [Diagnostics.Stopwatch]::StartNew()

Push-Location $OutputPath
try {
    # --- Step 1: mapextractor ----------------------------------------------
    Write-Step "Step 1/4: mapextractor (maps + dbc + Cameras + gt)"
    if (Test-Done 'maps') { Write-Host "Already done (resuming)." }
    else {
        $eFlag = if ($MapsOnly) { "3" } else { "15" }
        & ".\mapextractor.exe" -i "$ClientPath" -o "$OutputPath" -e $eFlag -f 1 -l $Locale
        if ($LASTEXITCODE -ne 0) {
            Fail "mapextractor failed with exit code $LASTEXITCODE.
Check stdout above; usual culprits: CASC init failure, wrong client path, antivirus."
        }
        Set-Done 'maps'
    }

    if ($MapsOnly) {
        Write-Step "Maps-only mode: skipping vmap4/mmaps generation."
        Show-DirSummary $OutputPath @("dbc", "maps", "Cameras", "gt")
        Write-Host ""
        Write-Host "DONE (maps-only)." -ForegroundColor Green
        return
    }

    # --- Step 2: vmap4extractor --------------------------------------------
    Write-Step "Step 2/4: vmap4extractor (CASC -> Buildings/)"
    if (Test-Done 'vmaps') { Write-Host "Already done (resuming)." }
    else {
    if ($Resume) { foreach ($junk in 'Buildings', 'dir_bin') { Remove-Item (Join-Path $OutputPath $junk) -Recurse -Force -ErrorAction SilentlyContinue } }   # a half-finished earlier run
    $vmapArgs = @("-d", "$ClientPath")
    if ($Threads -gt 0) { $vmapArgs += @("--threads", "$Threads") }
    & ".\vmap4extractor.exe" @vmapArgs
    if ($LASTEXITCODE -ne 0) {
        Fail "vmap4extractor failed with exit code $LASTEXITCODE.
Common cause: leftover Buildings/dir_bin/ from a previous run. Delete it and retry."
    }

    # --- Step 3: vmap4assembler --------------------------------------------
    Write-Step "Step 3/4: vmap4assembler (Buildings/ -> vmaps/)"
    $asmArgs = @("Buildings", "vmaps")
    if ($Threads -gt 0) { $asmArgs += @("--threads", "$Threads") }
    & ".\vmap4assembler.exe" @asmArgs
    if ($LASTEXITCODE -ne 0) {
        Fail "vmap4assembler failed with exit code $LASTEXITCODE."
    }
    Set-Done 'vmaps'
    }

    # --- Step 4: mmaps_generator -------------------------------------------
    Write-Step "Step 4/4: mmaps_generator (dbc+maps+vmaps -> mmaps/)"
    $mmapArgs = @("--input", "$OutputPath", "--output", "$OutputPath", "--silent")
    if ($Threads -gt 0) { $mmapArgs += @("--threads", "$Threads") }
    # run it as a process so the progress can be shown: one line per minute with the number of maps finished so far
    $expect = @{}
    foreach ($f in Get-ChildItem (Join-Path $OutputPath 'maps') -Filter '*.tilelist' -ErrorAction SilentlyContinue) { if ($f.Name -match '^(\d{4})') { $expect[[int]$Matches[1]] = 1 } }
    foreach ($d in Get-ChildItem (Join-Path $OutputPath 'vmaps') -Directory -ErrorAction SilentlyContinue) { if ($d.Name -match '^\d+$') { $expect[[int]$d.Name] = 1 } }
    $gen = Start-Process (Join-Path (Get-Location).Path 'mmaps_generator.exe') -ArgumentList $mmapArgs -WorkingDirectory (Get-Location).Path -NoNewWindow -PassThru
    while (-not $gen.WaitForExit(60000)) {
        $done = @(Get-ChildItem (Join-Path $OutputPath 'mmaps') -Filter '*.mmap' -ErrorAction SilentlyContinue).Count
        Write-Host ("Pathfinding data: {0} of about {1} maps done, still working ({2} minutes so far)..." -f $done, $expect.Count, [math]::Round($clock.Elapsed.TotalMinutes))
    }
    $genCode = $gen.ExitCode
    if ($genCode -ne 0) {
        Fail "mmaps_generator failed with exit code $genCode.
Check that dbc/, maps/, and vmaps/0000/*.vmtree exist in OutputPath."
    }
    # Safety net: a map that has game files but still no navmesh (the generator skips "junk"/dev maps by default) is built on its own,
    # because naming a map id builds it whatever the skip options say. Without one the server logs "Could not load MMAP ... result: 2".
    $ids = @()
    foreach ($f in Get-ChildItem (Join-Path $OutputPath 'maps') -Filter '*.tilelist' -ErrorAction SilentlyContinue) { if ($f.Name -match '^(\d{4})') { $ids += [int]$Matches[1] } }
    foreach ($d in Get-ChildItem (Join-Path $OutputPath 'vmaps') -Directory -ErrorAction SilentlyContinue) { if ($d.Name -match '^\d+$') { $ids += [int]$d.Name } }
    $missing = @($ids | Sort-Object -Unique | Where-Object { -not (Test-Path (Join-Path $OutputPath ('mmaps\{0:D4}.mmap' -f $_))) })
    foreach ($id in $missing) {
        Write-Host "Building the navigation mesh of map $id on its own..."
        $one = @("--input", "$OutputPath", "--output", "$OutputPath", "--silent", "$id")
        if ($Threads -gt 0) { $one += @("--threads", "$Threads") }
        & ".\mmaps_generator.exe" @one
    }
    $still = @($missing | Where-Object { -not (Test-Path (Join-Path $OutputPath ('mmaps\{0:D4}.mmap' -f $_))) })
    $have = @(Get-ChildItem (Join-Path $OutputPath 'mmaps') -Filter '*.mmap' -ErrorAction SilentlyContinue).Count
    Write-Host "Navigation meshes: $have maps built; all steps took $([math]::Round($clock.Elapsed.TotalMinutes)) minutes."
    if ($still.Count) { Write-Warning ("No navigation mesh could be built for map(s): " + ($still -join ', ') + ". Usually these maps have no ground to walk on.") }
    if ($have -lt 1) { Fail "No navigation meshes were produced (mmaps/ is empty)." }
    Set-Done 'mmaps'
    if ($Resume) { foreach ($junk in 'Buildings', 'dir_bin') { Remove-Item (Join-Path $OutputPath $junk) -Recurse -Force -ErrorAction SilentlyContinue } }

    Show-DirSummary $OutputPath @("dbc", "maps", "Cameras", "gt", "vmaps", "mmaps", "Buildings")
    Write-Host ""
    Write-Host "DONE (full extraction)." -ForegroundColor Green
    Write-Host "Tip: 'Buildings/' is intermediate and can be deleted to reclaim ~40 GB:" -ForegroundColor DarkGray
    Write-Host "    Remove-Item -Recurse -Force '$OutputPath\Buildings','$OutputPath\dir_bin'" -ForegroundColor DarkGray
}
finally {
    Pop-Location
}
