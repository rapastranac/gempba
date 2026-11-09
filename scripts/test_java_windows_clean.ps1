# Run the Java tests for one variant with no MSYS2 or Git directory on PATH, as
# on a Windows machine with only a JDK and Maven, plus MS-MPI for MP, so they
# fail when the Windows native library needs anything else.
#
# Usage:
#   pwsh scripts/test_java_windows_clean.ps1 MT
#   pwsh scripts/test_java_windows_clean.ps1 MP
#
# Expects the variant's gempba_jni.dll under
# bindings/java/src/main/resources/natives/win-x86_64/, and JAVA_HOME set.

param(
    [Parameter(Mandatory)]
    [ValidateSet('MT', 'MP')]
    [string]$Variant
)

$ErrorActionPreference = 'Stop'

if (-not $env:JAVA_HOME) {
    throw 'JAVA_HOME is not set.'
}

# A runner service can start before Maven lands on the machine's PATH, so fall
# back to the PATH stored in the registry.
$mvn = (Get-Command mvn.cmd -ErrorAction SilentlyContinue).Source
if (-not $mvn) {
    $stored = [Environment]::GetEnvironmentVariable('PATH', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('PATH', 'User')
    $mvn = ($stored -split ';') | Where-Object { $_ } | ForEach-Object { Join-Path $_ 'mvn.cmd' } |
        Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (-not $mvn) {
    throw "Maven's mvn.cmd is on neither PATH nor the PATH stored in the registry."
}

$env:PATH = (($env:PATH -split ';') | Where-Object { $_ -and $_ -notlike '*msys64*' -and $_ -notlike '*\Git\*' }) -join ';'
$env:PATH = "$env:JAVA_HOME\bin;$(Split-Path -Parent $mvn);$env:PATH"

$name = if ($Variant -eq 'MT') { 'multithreading' } else { 'multiprocessing' }
Set-Location (Join-Path $PSScriptRoot '..\bindings\java')
& $mvn -B test "-Dgempba.variant=$name"
exit $LASTEXITCODE
