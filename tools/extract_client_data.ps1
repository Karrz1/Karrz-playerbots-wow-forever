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
    Worker count for vmap4extractor and mmaps_generator. Default: 0 = chosen automatically
    (two cores less than the CPU has, and fewer when the RAM is small: a heavy tile build on
    every core at once commits gigabytes per worker and can freeze the whole PC).

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

# 0 = choose the worker count automatically: leave two cores for the desktop and size the rest by
# the RAM. mmaps_generator defaults to one worker per logical core, and a heavy tile build commits
# gigabytes per worker; on a 16 GB PC with all 12 workers at once that starved the interactive
# session for tens of minutes (black screen, input dead, only a hard reset helps) while tiles kept
# being written in the background.
$ramGb = [math]::Floor((Get-CimInstance Win32_ComputerSystem -ErrorAction SilentlyContinue).TotalPhysicalMemory / 1GB)
if ($Threads -le 0) {
    $Threads = [int][math]::Max(1, [math]::Min([Environment]::ProcessorCount - 2, [math]::Floor($ramGb / 2.5)))
}
# memory cap for mmaps_generator (a Windows job object): when a heavy tile needs more, the
# allocation fails inside the generator and its per-tile SEH guard skips just that tile, instead
# of the whole machine dying. 75% of the RAM, at least 4 GB.
$genMemLimitBytes = [long][math]::Max(4, [math]::Floor($ramGb * 0.75)) * 1GB
# the rescue pass after the main one uses fewer workers on purpose: a worker then commits far less,
# so tiles the cap skipped in the main pass can still be built (finished tiles are skipped anyway)
$rescueThreads = [int][math]::Max(1, [math]::Floor($Threads / 2))

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
Write-Host "Threads: $Threads"
Write-Host "Mode   : $(if ($MapsOnly) { 'MAPS-ONLY (road workflow)' } else { 'FULL' })$(if ($Resume) { ' (resumable)' })"

function Test-Done($name) { $Resume -and (Test-Path (Join-Path $OutputPath ".extract-$name.done")) }
function Set-Done($name) { if ($Resume) { Set-Content (Join-Path $OutputPath ".extract-$name.done") (Get-Date -Format s) } }
$clock = [Diagnostics.Stopwatch]::StartNew()

# --- mmaps_generator runner: memory cap, captured output, stall watchdog -----------------------
# The generator's own lines (and its unbuffered per-tile [tile-begin] breadcrumb on stderr) tell
# which tile is in flight; a Windows job object caps its memory so a heavy tile fails inside it
# instead of committing the whole PC to a freeze; below-normal priority keeps the desktop usable.
try {
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class WowfJobLimit
{
    [StructLayout(LayoutKind.Sequential)]
    public struct JOBOBJECT_BASIC_LIMIT_INFORMATION
    {
        public long PerProcessUserTimeLimit;
        public long PerJobUserTimeLimit;
        public uint LimitFlags;
        public UIntPtr MinimumWorkingSetSize;
        public UIntPtr MaximumWorkingSetSize;
        public uint ActiveProcessLimit;
        public UIntPtr Affinity;
        public uint PriorityClass;
        public uint SchedulingClass;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct IO_COUNTERS
    {
        public ulong ReadOperationCount;
        public ulong WriteOperationCount;
        public ulong OtherOperationCount;
        public ulong ReadTransferCount;
        public ulong WriteTransferCount;
        public ulong OtherTransferCount;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct JOBOBJECT_EXTENDED_LIMIT_INFORMATION
    {
        public JOBOBJECT_BASIC_LIMIT_INFORMATION BasicLimitInformation;
        public IO_COUNTERS IoInfo;
        public UIntPtr ProcessMemoryLimit;
        public UIntPtr JobMemoryLimit;
        public UIntPtr PeakProcessMemoryUsed;
        public UIntPtr PeakJobMemoryUsed;
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr CreateJobObject(IntPtr a, string name);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool SetInformationJobObject(IntPtr job, int infoClass, IntPtr info, int len);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool CloseHandle(IntPtr job);

    public const int JobObjectExtendedLimitInformation = 9;
    public const uint JOB_OBJECT_LIMIT_PROCESS_MEMORY = 0x100;
    public const uint JOB_OBJECT_LIMIT_JOB_MEMORY = 0x200;

    // returns the job handle, or IntPtr.Zero when the cap could not be set.
    // CPU rate control was tried and dropped on purpose: on Windows 11 (build 26100, Hyper-V/VBS
    // on) a job with CPU rate control leaves the assigned process stuck at startup with zero CPU,
    // which was verified against mmaps_generator. Below-normal priority does that job instead.
    public static IntPtr Create(long memoryLimitBytes)
    {
        IntPtr job = CreateJobObject(IntPtr.Zero, null);
        if (job == IntPtr.Zero) return IntPtr.Zero;

        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = new JOBOBJECT_EXTENDED_LIMIT_INFORMATION();
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_JOB_MEMORY;
        limits.ProcessMemoryLimit = new UIntPtr((ulong)memoryLimitBytes);
        limits.JobMemoryLimit = new UIntPtr((ulong)memoryLimitBytes);
        int size = Marshal.SizeOf(typeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION));
        IntPtr ptr = Marshal.AllocHGlobal(size);
        Marshal.StructureToPtr(limits, ptr, false);
        bool ok = SetInformationJobObject(job, JobObjectExtendedLimitInformation, ptr, size);
        Marshal.FreeHGlobal(ptr);
        if (!ok) { CloseHandle(job); return IntPtr.Zero; }
        return job;
    }
}
"@ -ErrorAction Stop
    $script:JobApiOk = $true
} catch {
    Write-Warning "The memory cap is not available ($($_.Exception.Message)); the generator runs without it (the stall watchdog still applies)."
    $script:JobApiOk = $false
}

function Start-Generator {
    # starts mmaps_generator with captured output, below-normal priority (the desktop stays
    # responsive even with all workers busy) and, when the job object could be created, a memory
    # cap so a heavy tile cannot commit the whole PC
    param([string[]]$GenArgs, [string]$StdoutLog, [string]$StderrLog, [long]$MemoryLimitBytes)
    $run = Start-Process -FilePath (Join-Path (Get-Location).Path 'mmaps_generator.exe') -ArgumentList $GenArgs `
        -WorkingDirectory (Get-Location).Path -NoNewWindow -PassThru `
        -RedirectStandardOutput $StdoutLog -RedirectStandardError $StderrLog
    $null = $run.Handle   # Windows PowerShell 5.1 leaves ExitCode empty unless the handle is read right after the start
    try { $run.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::BelowNormal } catch { }
    $job = [IntPtr]::Zero
    if ($script:JobApiOk) {
        $job = [WowfJobLimit]::Create($MemoryLimitBytes)
        if ($job -eq [IntPtr]::Zero) {
            Write-Warning "The memory cap could not be created; the generator runs without it."
        } elseif (-not [WowfJobLimit]::AssignProcessToJobObject($job, $run.Handle)) {
            Write-Warning "The generator could not be attached to the memory cap (error $([Runtime.InteropServices.Marshal]::GetLastWin32Error()))."
            [void][WowfJobLimit]::CloseHandle($job)
            $job = [IntPtr]::Zero
        }
    }
    @{ Proc = $run; Job = $job }
}

function Watch-Generator {
    # one progress line per minute with the real tile count (the .mmap header of a map is written
    # when the map STARTS, so counting those told nothing). If no new tile file appears for
    # $StallMinutes minutes the generator is stopped: a healthy run writes tiles continuously,
    # so 20 quiet minutes mean it is stuck on a tile it will never finish. Finished tiles are
    # kept and the missing ones are built by the later passes.
    param($Run, [string]$StderrLog, [Diagnostics.Stopwatch]$Clock, [int]$TotalExpected, [int]$StallMinutes)
    $run = $Run.Proc
    $last = -1; $stall = 0
    while (-not $run.WaitForExit(60000)) {
        $tiles = @(Get-ChildItem (Join-Path $OutputPath 'mmaps') -Filter '*.mmtile' -File -ErrorAction SilentlyContinue).Count
        $inFlight = ''
        try { $inFlight = (Get-Content $StderrLog -Tail 1 -ErrorAction Stop) } catch { }
        if ($tiles -gt $last) { $last = $tiles; $stall = 0 } else { $stall++ }
        Write-Host ("Pathfinding data: {0} of about {1} tiles built, still working ({2} minutes so far){3}..." -f $tiles, $TotalExpected, [math]::Round($Clock.Elapsed.TotalMinutes), $(if ($inFlight) { " [$inFlight]" } else { '' }))
        if ($stall -ge $StallMinutes) {
            Write-Warning "No new tiles for $StallMinutes minutes - stopping the generator (it is stuck on a tile it cannot finish)."
            try { $run.Kill() } catch { }
            try { $run.WaitForExit(30000) | Out-Null } catch { }
            return $true
        }
    }
    return $false
}

function Get-IncompleteMaps {
    # map ids whose .tilelist tiles are not all present as .mmtile files; maps with an empty tile
    # bitmap (junk maps without game data) never appear here, and the .mmap header of a map is
    # written when the map STARTS, so tile files are the only honest completeness check
    $present = @{}
    foreach ($f in Get-ChildItem (Join-Path $OutputPath 'mmaps') -Filter '*.mmtile' -File -ErrorAction SilentlyContinue) {
        if ($f.Name -match '^(\d{4})_(\d{2})_(\d{2})\.mmtile$') {
            $key = "{0}:{1}:{2}" -f ([int]$Matches[1]), ([int]$Matches[2]), ([int]$Matches[3])
            $present[$key] = $true
        }
    }
    $missing = @()
    foreach ($id in $expectedTiles.Keys) {
        foreach ($c in $expectedTiles[$id]) {
            $p = $c -split ','
            $key = "{0}:{1}:{2}" -f $id, [int]$p[0], [int]$p[1]
            if (-not $present.ContainsKey($key)) { $missing += $id; break }
        }
    }
    $missing | Sort-Object
}

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
    $vmapArgs = @("-d", "$ClientPath", "--threads", "$Threads")
    & ".\vmap4extractor.exe" @vmapArgs
    if ($LASTEXITCODE -ne 0) {
        Fail "vmap4extractor failed with exit code $LASTEXITCODE.
Common cause: leftover Buildings/dir_bin/ from a previous run. Delete it and retry."
    }

    # --- Step 3: vmap4assembler --------------------------------------------
    Write-Step "Step 3/4: vmap4assembler (Buildings/ -> vmaps/)"
    $asmArgs = @("Buildings", "vmaps", "--threads", "$Threads")
    & ".\vmap4assembler.exe" @asmArgs
    if ($LASTEXITCODE -ne 0) {
        Fail "vmap4assembler failed with exit code $LASTEXITCODE."
    }
    Set-Done 'vmaps'
    }

    # --- Step 4: mmaps_generator -------------------------------------------
    Write-Step "Step 4/4: mmaps_generator (dbc+maps+vmaps -> mmaps/)"
    # What to build: the 64x64 tile bitmap inside every .tilelist (a binary file: 4 bytes magic,
    # 4 bytes version, 4 bytes build, then one 0/1 byte per tile at index 4095 - (tileX*64 + tileY),
    # the same layout MapBuilder::discoverTiles reads). Maps with an empty bitmap (junk maps without
    # game data) are not counted at all, so the total is the honest denominator.
    $expectedTiles = @{}
    $totalExpected = 0
    foreach ($f in Get-ChildItem (Join-Path $OutputPath 'maps') -Filter '*.tilelist' -File -ErrorAction SilentlyContinue) {
        if ($f.Name -notmatch '^(\d{4})\.tilelist$') { continue }
        $bytes = [IO.File]::ReadAllBytes($f.FullName)
        if ($bytes.Length -lt 12 + 4096) { continue }
        $id = [int]$Matches[1]
        $coords = New-Object System.Collections.Generic.List[string]
        for ($i = 0; $i -lt 4096; $i++) {
            if ($bytes[12 + $i] -eq 0x31) {
                $rev = 4095 - $i
                $coords.Add(('{0},{1}' -f [math]::Floor($rev / 64), ($rev % 64)))
            }
        }
        if ($coords.Count) { $expectedTiles[$id] = $coords; $totalExpected += $coords.Count }
    }
    $haveTiles = @(Get-ChildItem (Join-Path $OutputPath 'mmaps') -Filter '*.mmtile' -File -ErrorAction SilentlyContinue).Count
    if ($totalExpected -gt 0 -and $haveTiles -ge $totalExpected) {
        Write-Host "All $totalExpected pathfinding tiles are already built."
    } else {
        Write-Host "Pathfinding data: $haveTiles of about $totalExpected tiles are built; the rest is built now."
    }

    $logsDir = Join-Path $OutputPath 'logs'
    New-Item -ItemType Directory -Force $logsDir | Out-Null
    $genOutLog = Join-Path $logsDir 'mmaps-generator.out.log'
    $genErrLog = Join-Path $logsDir 'mmaps-generator.err.log'
    # a generator left over from an interrupted run would hold the log files: stop it first
    Get-Process mmaps_generator -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

    # main pass: several workers, capped by the job object (see Start-Generator). A heavy tile that
    # hits the cap is skipped by the generator's own per-tile guard; the rescue pass builds it.
    $mmapArgs = @("--input", "$OutputPath", "--output", "$OutputPath", "--silent", "--threads", "$Threads")
    $run = Start-Generator -GenArgs $mmapArgs -StdoutLog $genOutLog -StderrLog $genErrLog -MemoryLimitBytes $genMemLimitBytes
    $killedMain = Watch-Generator -Run $run -StderrLog $genErrLog -Clock $clock -TotalExpected $totalExpected -StallMinutes 20
    if ($killedMain) { Write-Warning "The main generator pass was stopped by the stall watchdog; finished tiles are kept and the rest is built below." }
    $mainExit = if ($run.Proc.HasExited) { $run.Proc.ExitCode } else { -1 }

    # rescue pass: whatever is still missing (tiles the memory cap skipped, maps the watchdog left,
    # or a generator that died mid-run) is built with half the workers - each worker then commits
    # far less, and finished tiles are skipped in seconds.
    $incomplete = @(Get-IncompleteMaps)
    $visitedAll = (-not $killedMain) -and $mainExit -eq 0
    if ($incomplete.Count) {
        Write-Host ("Tiles are still missing on {0} map(s) ({1}) - building them with fewer workers..." -f $incomplete.Count, ((@($incomplete | Select-Object -First 15) | ForEach-Object { '{0:D4}' -f $_ }) -join ', ') + $(if ($incomplete.Count -gt 15) { ', ...' } else { '' }))
        $rescueArgs = @("--input", "$OutputPath", "--output", "$OutputPath", "--silent", "--threads", "$rescueThreads")
        $rescue = Start-Generator -GenArgs $rescueArgs -StdoutLog $genOutLog -StderrLog $genErrLog -MemoryLimitBytes $genMemLimitBytes
        $killedRescue = Watch-Generator -Run $rescue -StderrLog $genErrLog -Clock $clock -TotalExpected $totalExpected -StallMinutes 20
        $rescueExit = if ($rescue.Proc.HasExited) { $rescue.Proc.ExitCode } else { -1 }
        # a full pass that drained its queue visited every tile: whatever is still missing has no
        # walkable content (no terrain and no models there - the tilelist lists them, but there is
        # nothing to mesh, and no run can build a file for them)
        $visitedAll = ($rescueExit -eq 0) -and (-not $killedRescue)
    }

    $incomplete = @(Get-IncompleteMaps)
    $haveTiles = @(Get-ChildItem (Join-Path $OutputPath 'mmaps') -Filter '*.mmtile' -File -ErrorAction SilentlyContinue).Count
    Write-Host "Navigation meshes: $haveTiles of about $totalExpected tiles built; all steps took $([math]::Round($clock.Elapsed.TotalMinutes)) minutes."
    if ($incomplete.Count) {
        if ($visitedAll) {
            Write-Host ("The tilelist lists {0} tiles without walkable content on map(s) {1}: no terrain and no models there, so no navmesh file is written for them (this is normal)." -f ($totalExpected - $haveTiles), ((@($incomplete) | ForEach-Object { '{0:D4}' -f $_ }) -join ', ')) -ForegroundColor DarkGray
        } else {
            Write-Warning ("Some pathfinding tiles could not be built on map(s): " + ((@($incomplete) | ForEach-Object { '{0:D4}' -f $_ }) -join ', ') + ". The generator's tile breadcrumbs are in $genErrLog. The server runs and bots walk where the mesh exists. Run this script again to retry them (finished tiles are skipped).")
            try { Set-Content (Join-Path $logsDir 'mmaps-incomplete.txt') @(($incomplete | ForEach-Object { '{0:D4}' -f $_ })) -Encoding ascii } catch { }
        }
    }
    if ($haveTiles -lt 1) { Fail "No navigation meshes were produced (mmaps/ is empty)." }
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
# give a caller that checks $LASTEXITCODE a defined exit code: a script that only runs PowerShell
# cmdlets never sets it, so a resumed run with every stage done would fail the check
exit 0
