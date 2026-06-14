#Requires -Version 5.1
<#
.SYNOPSIS
    Open an SSH tunnel to a remote gempba center and live-tail its telemetry.

.DESCRIPTION
    Wraps "ssh -L <port>:127.0.0.1:<port> ... + telemetry_view.ps1" into one
    command. Spawns `ssh -N -L ...` as a child process, waits until the local
    end of the tunnel accepts TCP connections, then runs telemetry_view.ps1
    against it. Tearing this down (Ctrl+C, or the viewer returning) kills the
    SSH child.

    On a cluster, find the node running rank 0 yourself (`squeue` on the login
    node — the first node in the list), then pass that node as -SshHost and the
    login node as -JumpHost.

.PARAMETER SshHost
    The FINAL destination — the host actually running gempba. On a cluster that
    is the compute node where your job's rank 0 runs; at home it is just your
    server.

.PARAMETER JumpHost
    Optional. The host you ssh into FIRST to reach -SshHost — i.e. the cluster
    login / bastion node, when the gempba compute node is not reachable directly
    from your machine. Omit when you can ssh straight to -SshHost.

.PARAMETER LocalPort
    Local port the viewer connects to. Default 9000.

.PARAMETER RemotePort
    Remote loopback port gempba is listening on. Default 9000 — match this to
    the telemetry port the gempba binary was launched with.

.EXAMPLE
    # gempba on a host you can ssh into directly (home / LAN):
    scripts\telemetry_tunnel.ps1 -SshHost me@my-server

.EXAMPLE
    # gempba on a cluster compute node, reached by hopping through the login node:
    #   me@login-node   = the bastion you ssh into first  (-JumpHost)
    #   me@compute-node = the node running gempba          (-SshHost)
    scripts\telemetry_tunnel.ps1 -SshHost me@compute-node -JumpHost me@login-node
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [string]$SshHost,

    [string]$JumpHost,

    [int]$LocalPort = 9000,
    [int]$RemotePort = 9000
)

$ErrorActionPreference = 'Stop'

if (-not (Get-Command ssh -ErrorAction SilentlyContinue)) {
    Write-Host "ERROR: ssh client not found in PATH. Install OpenSSH.Client." -ForegroundColor Red
    exit 1
}

if ($JumpHost) {
    # Compute nodes only trust login-originated ssh, so nest instead of -J.
    $mid = Get-Random -Minimum 20000 -Maximum 60000
    $inner = "ssh -N -o ExitOnForwardFailure=yes -o StrictHostKeyChecking=accept-new -L ${mid}:127.0.0.1:${RemotePort} ${SshHost}"
    $q = [char]39
    # The remote cat gets EOF when the outer ssh dies, then kills the inner ssh.
    $remoteCmd = "sh -c ${q}${inner} & p=`$!; cat >/dev/null 2>&1; kill `$p 2>/dev/null${q}"
    # One string, not an array: Start-Process mangles array elements containing quotes.
    $argLine = "-o StrictHostKeyChecking=accept-new -L ${LocalPort}:localhost:${mid} ${JumpHost} `"$remoteCmd`""
    Write-Host ("Tunnel: {0} via {1} (login-originated hop)" -f $SshHost, $JumpHost) -ForegroundColor DarkGray
    $timeoutSec = 120
} else {
    $argLine = "-N -o ExitOnForwardFailure=yes -L ${LocalPort}:127.0.0.1:${RemotePort} ${SshHost}"
    Write-Host ("Tunnel: {0}" -f $SshHost) -ForegroundColor DarkGray
    $timeoutSec = 15
}

$ssh = Start-Process -FilePath ssh -ArgumentList $argLine -NoNewWindow -PassThru
try {
    $ready = $false
    $deadline = (Get-Date).AddSeconds($timeoutSec)
    while ((Get-Date) -lt $deadline) {
        if ($ssh.HasExited) {
            Write-Host ("ERROR: ssh exited (code {0}) before the tunnel was ready." -f $ssh.ExitCode) -ForegroundColor Red
            exit 1
        }
        # Read a byte, don't just connect: the local listener accepts before the inner hop is up.
        try {
            $probe = [System.Net.Sockets.TcpClient]::new('127.0.0.1', $LocalPort)
            try {
                $probe.ReceiveTimeout = 1500
                $buf = New-Object byte[] 1
                if ($probe.GetStream().Read($buf, 0, 1) -ge 1) { $ready = $true }
            } finally { $probe.Close() }
        } catch { }
        if ($ready) { break }
        Start-Sleep -Milliseconds 300
    }

    if (-not $ready) {
        Write-Host ("ERROR: tunnel did not become ready on 127.0.0.1:{0} within {1}s." -f $LocalPort, $timeoutSec) -ForegroundColor Red
        exit 1
    }

    & "$PSScriptRoot\telemetry_view.ps1" -Port $LocalPort
} finally {
    if (-not $ssh.HasExited) {
        Stop-Process -Id $ssh.Id -Force -ErrorAction SilentlyContinue
    }
}
