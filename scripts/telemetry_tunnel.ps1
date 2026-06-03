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

$sshArgs = @('-N')
if ($JumpHost) {
    $sshArgs += @('-J', $JumpHost)
    Write-Host ("Tunnel: {0} via {1}" -f $SshHost, $JumpHost) -ForegroundColor DarkGray
} else {
    Write-Host ("Tunnel: {0}" -f $SshHost) -ForegroundColor DarkGray
}
$sshArgs += @('-L', "${LocalPort}:127.0.0.1:${RemotePort}", $SshHost)

$ssh = Start-Process -FilePath ssh -ArgumentList $sshArgs -NoNewWindow -PassThru
try {
    $deadline = (Get-Date).AddSeconds(15)
    while ((Get-Date) -lt $deadline) {
        if ($ssh.HasExited) {
            Write-Host ("ERROR: ssh exited (code {0}) before the tunnel was ready." -f $ssh.ExitCode) -ForegroundColor Red
            exit 1
        }
        try {
            $probe = [System.Net.Sockets.TcpClient]::new('127.0.0.1', $LocalPort)
            $probe.Close()
            break
        } catch {
            Start-Sleep -Milliseconds 200
        }
    }

    & "$PSScriptRoot\telemetry_view.ps1" -Port $LocalPort
} finally {
    if (-not $ssh.HasExited) {
        Stop-Process -Id $ssh.Id -Force -ErrorAction SilentlyContinue
    }
}
