#Requires -Version 5.1
<#
.SYNOPSIS
    Live-tails the GemPBA telemetry stream from a running benchmark.

.DESCRIPTION
    Connects to the center's TCP socket on 127.0.0.1 and prints one formatted
    line per broadcast frame: timestamp, tasks_local_total, process CPU,
    tasks_sent_total, and process RSS. Press Ctrl+C to stop.

    The gempba center always binds loopback only — the connection target is
    therefore always 127.0.0.1, either directly when running on the same host
    or as the local end of an SSH tunnel for remote dashboards. Pass -Port to
    match whatever port the gempba binary was launched with (-telemetry_port).

    The gempba binary must call gempba::telemetry::install() at startup; with
    no install call, no center is running and there is nothing to connect to.

.PARAMETER Port
    TCP port to connect to on 127.0.0.1. Defaults to 9000, the gempba default.
    Override when the benchmark was launched with `-telemetry_port <n>` or when
    your SSH tunnel uses a non-default local port.

.PARAMETER Raw
    Print full pretty-printed JSON for each frame instead of the formatted
    one-line summary. Useful when you want to inspect topology or node fields.

.EXAMPLE
    scripts\watch_telemetry.ps1
    # Tails 127.0.0.1:9000 with the formatted summary.

.EXAMPLE
    scripts\watch_telemetry.ps1 -Port 9100 -Raw
    # Connects to a non-default port, prints full JSON frames.

.EXAMPLE
    # In one shell, open the SSH tunnel:
    #   ssh -J user@login.cluster.edu -L 9000:127.0.0.1:9000 user@compute-12
    # In a second shell, run the watch script:
    scripts\watch_telemetry.ps1
    # SSH carries the traffic to the compute node; nothing else to configure.
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
    Write-Host "  - Is the benchmark running and the SSH tunnel (if any) still up?"
    Write-Host "  - Did the benchmark call gempba::telemetry::install() at startup?"
    Write-Host "  - If the benchmark was launched with -telemetry_port <n>, pass -Port <n> here too."
    exit 1
}

$reader = [System.IO.StreamReader]::new($client.GetStream())
$epoch  = [DateTime]'1970-01-01Z'

Write-Host ("Connected to {0}:{1} - streaming frames (Ctrl+C to stop)" -f $Ip, $Port) -ForegroundColor Green

if (-not $Raw) {
    Write-Host ""
    Write-Host ("{0,8}  {1,10}  {2,-14}  {3,-14}  {4,7}  {5,7}  {6,12}" -f "time","elapsed","tasks_local","tasks_remote","threads","cpu%","rss(KB)") -ForegroundColor DarkGray
    Write-Host ("-" * 95) -ForegroundColor DarkGray
}

try {
    while (-not $reader.EndOfStream) {
        $line = $reader.ReadLine()
        if ([string]::IsNullOrWhiteSpace($line)) { continue }

        $j = $line | ConvertFrom-Json

        if ($Raw) {
            $j | ConvertTo-Json -Depth 10
            Write-Host ""
            continue
        }

        $w = $j.workers | Select-Object -First 1
        if (-not $w) {
            # No worker frames yet. Center has just started; skip until the
            # first publish lands in the aggregator.
            continue
        }

        $ts      = $epoch.AddMilliseconds($j.ts).ToLocalTime().ToString('HH:mm:ss')
        $rss     = [math]::Round($w.process_rss_bytes / 1KB, 0)

        # Format elapsed_seconds as H:MM:SS so the eye can read it at a glance.
        $elapsed = [TimeSpan]::FromSeconds([double]$j.elapsed_seconds)
        $elapsedStr = "{0:0}:{1:00}:{2:00}" -f [int]$elapsed.TotalHours, $elapsed.Minutes, $elapsed.Seconds

        "{0,8}  {1,10}  {2,-14}  {3,-14}  {4,7}  {5,6:N1}%  {6,12:N0}" -f $ts, $elapsedStr, $w.tasks_local_total, $w.tasks_sent_total, $w.process_threads, $w.process_cpu_pct, $rss
    }
} finally {
    $reader.Dispose()
    $client.Close()
}
