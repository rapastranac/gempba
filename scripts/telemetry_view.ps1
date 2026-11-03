#Requires -Version 5.1
<#
.SYNOPSIS
    Live-tails the GemPBA telemetry stream of a run on this machine.

.DESCRIPTION
    Connects to the center's TCP socket on 127.0.0.1 and prints, for each
    broadcast, one row per rank: its role, tasks started, sent and received,
    thread count, CPU% and RSS (KB). A paused stream prints a "paused" row per
    broadcast, and each run marker is printed once, when it first appears.
    Press Ctrl+C to stop.

    The gempba center binds loopback only, so the viewer connects to
    127.0.0.1. To watch a run on another machine, use the GemPBA Dashboard,
    which opens the SSH tunnel itself.

.PARAMETER Port
    TCP port to connect to on 127.0.0.1. Defaults to 9000, the gempba default.
    Pass the port the program set with gempba::telemetry::configure_port.

.PARAMETER Raw
    Print full pretty-printed JSON for each broadcast instead of one row per
    rank. Useful when you want to inspect topology or node fields.

.EXAMPLE
    scripts\telemetry_view.ps1
    # Tails 127.0.0.1:9000, one row per rank.

.EXAMPLE
    scripts\telemetry_view.ps1 -Port 9100 -Raw
    # Connects to a non-default port, prints full JSON broadcasts.
#>

[CmdletBinding()]
param(
    [int]$Port = 9000,
    [switch]$Raw
)

$ErrorActionPreference = 'Stop'

# 127.0.0.1 (not "localhost"): Windows may resolve localhost to ::1 (IPv6)
# first while the gempba center binds INADDR_LOOPBACK (IPv4 only).
$Ip = '127.0.0.1'

try {
    $client = [System.Net.Sockets.TcpClient]::new($Ip, $Port)
} catch {
    Write-Host ("ERROR: cannot connect to {0}:{1}" -f $Ip, $Port) -ForegroundColor Red
    Write-Host "  - Is the gempba program running on this machine?"
    Write-Host "  - If it set another port with gempba::telemetry::configure_port, pass -Port <n> here too."
    exit 1
}

$reader = [System.IO.StreamReader]::new($client.GetStream())
$epoch  = [DateTime]'1970-01-01Z'
$invariant = [System.Globalization.CultureInfo]::InvariantCulture
$rowFormat = '{0,8}  {1,12}  {2,4}  {3,-6}  {4,13}  {5,10}  {6,10}  {7,7}  {8,6}  {9,10}'

function Format-Elapsed([TimeSpan]$Span, [bool]$WithMilliseconds) {
    $text = '{0}:{1:00}:{2:00}' -f [math]::Floor($Span.TotalHours), $Span.Minutes, $Span.Seconds
    if ($WithMilliseconds) {
        $text += '.{0:000}' -f $Span.Milliseconds
    }
    $text
}

function Format-Label([string]$Label) {
    $Label.Replace('\', '\\').Replace("`t", '\t').Replace("`r", '\r').Replace("`n", '\n')
}

Write-Host ("Connected to {0}:{1} - streaming broadcasts (Ctrl+C to stop)" -f $Ip, $Port) -ForegroundColor Green

if (-not $Raw) {
    Write-Host ""
    Write-Host ($rowFormat -f "time", "elapsed", "rank", "role", "tasks_started", "tasks_sent", "tasks_recv", "threads", "cpu%", "rss(KB)") -ForegroundColor DarkGray
    Write-Host ("-" * 104) -ForegroundColor DarkGray
}

$runId = ''
$seen = 0
try {
    while (-not $reader.EndOfStream) {
        $line = $reader.ReadLine()
        if ([string]::IsNullOrWhiteSpace($line)) { continue }

        try {
            $j = $line | ConvertFrom-Json
        } catch {
            continue
        }

        if ($Raw) {
            $j | ConvertTo-Json -Depth 10
            Write-Host ""
            continue
        }

        $time = $epoch.AddMilliseconds([double]$j.ts).ToLocalTime().ToString('HH:mm:ss')
        $elapsed = Format-Elapsed ([TimeSpan]::FromSeconds([double]$j.elapsed_seconds)) $false

        if ($j.paused -eq $true -and $null -eq $j.workers) {
            '{0,8}  {1,12}  paused' -f $time, $elapsed
            continue
        }

        $roles = @{}
        $markers = @()
        if ($null -ne $j.topology) {
            foreach ($identity in @($j.topology.identities)) {
                if ($null -ne $identity) {
                    $roles[[string]$identity.worker_id] = if ($identity.role) { $identity.role } else { '-' }
                }
            }
            if ($null -ne $j.topology.run -and $null -ne $j.topology.run.markers) {
                $markers = @($j.topology.run.markers)
            }
        }

        $from = if ($j.run_id -eq $runId) { $seen } else { 0 }
        for ($k = $from; $k -lt $markers.Count; $k++) {
            $marker = $markers[$k]
            '{0,8}  {1,12}  marker: {2} ({3})' -f '', (Format-Elapsed ([TimeSpan]::FromMilliseconds([double]$marker.elapsed_ms)) $true), (Format-Label $marker.label), $marker.source
        }
        $runId = $j.run_id
        $seen = $markers.Count

        $first = $true
        foreach ($w in @($j.workers | Sort-Object { [int]$_.worker_id })) {
            $role = $roles[[string]$w.worker_id]
            if (-not $role) { $role = '-' }
            $cpu = ([double]$w.process_cpu_pct).ToString('0.0', $invariant) + '%'
            $rss = [math]::Floor([double]$w.process_rss_bytes / 1KB)
            $rowFormat -f ($(if ($first) { $time } else { '' })), ($(if ($first) { $elapsed } else { '' })), $w.worker_id, $role, $w.tasks_started_total, $w.tasks_sent_total, $w.tasks_recv_total, $w.process_threads, $cpu, $rss
            $first = $false
        }
    }
} finally {
    $reader.Dispose()
    $client.Close()
}
