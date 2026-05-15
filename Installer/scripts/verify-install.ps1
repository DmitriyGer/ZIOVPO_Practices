[CmdletBinding()]
param(
    [string]$InstallerPath,
    [string]$InstallDir = "${env:ProgramFiles}\ZIOVPO Antivirus",
    [string]$ServiceName = "TrayService",
    [string]$ProgramDataDir = "${env:ProgramData}\TrayApp\Antivirus"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Assert-Admin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw "verify-install.ps1 must be run from an elevated PowerShell session."
    }
}

function Wait-ForPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [int]$TimeoutSeconds = 30
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if (Test-Path -LiteralPath $Path) {
            return
        }

        Start-Sleep -Seconds 1
    }

    throw "Timed out waiting for path: $Path"
}

function Wait-ForNoPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [int]$TimeoutSeconds = 30
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if (-not (Test-Path -LiteralPath $Path)) {
            return
        }

        Start-Sleep -Seconds 1
    }

    throw "Timed out waiting for path removal: $Path"
}

function Wait-ForServiceState {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [string]$State,
        [int]$TimeoutSeconds = 30
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $service = Get-CimInstance Win32_Service -Filter "Name='$Name'" -ErrorAction SilentlyContinue
        if ($null -ne $service -and $service.State -eq $State) {
            return $service
        }

        Start-Sleep -Seconds 1
    }

    throw "Timed out waiting for service '$Name' to reach state '$State'."
}

function Get-InstallerPath {
    param(
        [string]$ExplicitPath
    )

    if ($ExplicitPath) {
        return (Resolve-Path -LiteralPath $ExplicitPath).Path
    }

    $scriptRoot = Split-Path -Parent $PSCommandPath
    $installerRoot = Split-Path -Parent $scriptRoot
    $outputDir = Join-Path $installerRoot "artifacts\Release\x64\output"

    $bundle = Get-ChildItem -LiteralPath $outputDir -Filter *.exe -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if ($bundle) {
        return $bundle.FullName
    }

    $msi = Get-ChildItem -LiteralPath $outputDir -Filter *.msi -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if ($msi) {
        return $msi.FullName
    }

    throw "Installer artifact was not found. Pass -InstallerPath explicitly."
}

Assert-Admin

$resolvedInstallerPath = Get-InstallerPath -ExplicitPath $InstallerPath
$installerExtension = [System.IO.Path]::GetExtension($resolvedInstallerPath).ToLowerInvariant()

Write-Host "Using installer: $resolvedInstallerPath"

if ($installerExtension -eq ".exe") {
    $installArgs = '/quiet /norestart'
    $uninstallArgs = '/uninstall /quiet /norestart'
}
elseif ($installerExtension -eq ".msi") {
    $installArgs = "/i `"$resolvedInstallerPath`" /qn /norestart"
    $uninstallArgs = "/x `"$resolvedInstallerPath`" /qn /norestart"
}
else {
    throw "Unsupported installer extension: $installerExtension"
}

if ($installerExtension -eq ".exe") {
    $installProcess = Start-Process -FilePath $resolvedInstallerPath -ArgumentList $installArgs -Wait -PassThru
}
else {
    $installProcess = Start-Process -FilePath "msiexec.exe" -ArgumentList $installArgs -Wait -PassThru
}

if ($installProcess.ExitCode -ne 0) {
    throw "Install failed with exit code $($installProcess.ExitCode)."
}

Wait-ForPath -Path (Join-Path $InstallDir "TrayApp.exe")
Wait-ForPath -Path (Join-Path $InstallDir "TrayService.exe")
$service = Wait-ForServiceState -Name $ServiceName -State "Running"
$expectedServiceBinaryPath = '"' + (Join-Path $InstallDir "TrayService.exe") + '"'

if ($service.StartMode -ne "Auto") {
    throw "Service '$ServiceName' is installed but StartMode is '$($service.StartMode)' instead of 'Auto'."
}

if ($service.PathName -ne $expectedServiceBinaryPath) {
    throw "Service '$ServiceName' binary path is '$($service.PathName)' instead of '$expectedServiceBinaryPath'."
}

Wait-ForPath -Path $ProgramDataDir
Wait-ForPath -Path (Join-Path $ProgramDataDir "default.avdb")

Write-Host "Install verification passed."

if ($installerExtension -eq ".exe") {
    $uninstallProcess = Start-Process -FilePath $resolvedInstallerPath -ArgumentList $uninstallArgs -Wait -PassThru
}
else {
    $uninstallProcess = Start-Process -FilePath "msiexec.exe" -ArgumentList $uninstallArgs -Wait -PassThru
}

if ($uninstallProcess.ExitCode -ne 0) {
    throw "Uninstall failed with exit code $($uninstallProcess.ExitCode)."
}

Wait-ForNoPath -Path $InstallDir
Wait-ForNoPath -Path $ProgramDataDir

$removedService = Get-CimInstance Win32_Service -Filter "Name='$ServiceName'" -ErrorAction SilentlyContinue
if ($null -ne $removedService) {
    throw "Service '$ServiceName' still exists after uninstall."
}

Write-Host "Uninstall verification passed."
