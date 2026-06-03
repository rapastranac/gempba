#Requires -Version 5.1
<#
.SYNOPSIS
    Open an SSH tunnel to a remote gempba center and live-tail its telemetry.

.DESCRIPTION
    Wraps the two-step "ssh -L <port>:127.0.0.1:<port> ... + telemetry_view.ps1"
    workflow into a single command. Spawns `ssh -N -L ...` as a child process,
    waits until the local end of the tunnel accepts TCP connections, then runs
    telemetry_view.ps1 against it. Tearing down this script (Ctrl+C, or the
    watch script returning) kills the SSH child.

    Two modes:
      HPC     -Login <user@login> -Job <slurm-id>
              Resolves the compute node hostname from `squeue -j <id>` on the
              login node, then opens `ssh -J <login> -L ... <user>@<compute>`.
      Direct  -SshHost <user@host>
              Single-hop tunnel — for home LAN, or a host you can ssh into
              without a jump.

.PARAMETER Login
    HPC mode only. SSH destination of the cluster login node, e.g.
    user@login.cluster.edu.

.PARAMETER Job
    HPC mode only. SLURM job id whose compute node should be tunnelled to.

.PARAMETER SshHost
    Direct mode only. SSH destination running gempba, or any host whose loopback
    can reach gempba.

.PARAMETER LocalPort
    Local laptop port the watch script will connect to. Default 9000.

.PARAMETER RemotePort
    Remote loopback port gempba is listening on. Default 9000 — match this to
    whatever -telemetry_port the gempba binary was launched with.

.EXAMPLE
    scripts\connect_telemetry.ps1 -Login user@login.cluster.edu -Job 1234567

.EXAMPLE
    scripts\connect_telemetry.ps1 -SshHost user@desktop
#>

[CmdletBinding(DefaultParameterSetName='Direct')]
param(
    [Parameter(ParameterSetName='HPC', Mandatory=$true)]
    [string]$Login,

    [Parameter(ParameterSetName='HPC', Mandatory=$true)]
    [string]$Job,

    [Parameter(ParameterSetName='Direct', Mandatory=$true)]
    [string]$SshHost,

    [int]$LocalPort = 9000,
    [int]$RemotePort = 9000
)

$ErrorActionPreference = 'Stop'

if (-not (Get-Command ssh -ErrorAction SilentlyContinue)) {
    Write-Host "ERROR: ssh client not found in PATH. Install OpenSSH.Client." -ForegroundColor Red
    exit 1
}

if ($PSCmdlet.ParameterSetName -eq 'HPC') {
    Write-Host ("Resolving compute node for job {0} via {1}..." -f $Job, $Login)
    $nodeList = & ssh $Login "squeue -j $Job -h -o '%N'" 2>$null
    if ([string]::IsNullOrWhiteSpace($nodeList)) {
        Write-Host ("ERROR: squeue returned no node for job {0}." -f $Job) -ForegroundColor Red
        Write-Host "  - Is the job actually running? (ssh $Login squeue -u `$USER)"
        Write-Host ("  - Does your SSH key authenticate to {0}?" -f $Login)
        exit 1
    }
    # SLURM node lists can be ranges (compute-[12-15]); take the first concrete name.
    $computeNode = ($nodeList.Trim() -split ',')[0] -replace '\[.*$', ''
    $userPart = if ($Login -match '^([^@]+)@') { $Matches[1] + '@' } else { '' }
    $sshDestination = "${userPart}${computeNode}"
    $sshArgs = @('-N', '-J', $Login, '-L', "${LocalPort}:127.0.0.1:${RemotePort}", $sshDestination)
    Write-Host ("Tunnel: {0} via {1}" -f $sshDestination, $Login) -ForegroundColor DarkGray
} else {
    $sshArgs = @('-N', '-L', "${LocalPort}:127.0.0.1:${RemotePort}", $SshHost)
    Write-Host ("Tunnel: {0}" -f $SshHost) -ForegroundColor DarkGray
}

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
