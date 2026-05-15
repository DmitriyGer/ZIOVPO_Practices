[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$AppOutputDir,

    [Parameter(Mandatory = $true)]
    [string]$StageDir,

    [Parameter(Mandatory = $true)]
    [string]$VcRedistUrl,

    [string]$GeneratedWxsPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not $GeneratedWxsPath) {
    $GeneratedWxsPath = Join-Path $StageDir "GeneratedFiles.wxs"
}

function Test-ExcludedFile {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.FileInfo]$File
    )

    $excludedNames = @(
        "AntivirusEngineTests.exe",
        "AntivirusEngineTests.pdb",
        "vc_redist.x64.exe"
    )

    $excludedExtensions = @(".pdb", ".ilk", ".ipdb", ".iobj")

    if ($excludedNames -contains $File.Name) {
        return $true
    }

    return $excludedExtensions -contains $File.Extension.ToLowerInvariant()
}

function Escape-XmlAttribute {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Value
    )

    return [System.Security.SecurityElement]::Escape($Value)
}

if (-not (Test-Path -LiteralPath $AppOutputDir)) {
    throw "App output directory does not exist: $AppOutputDir"
}

New-Item -Path $StageDir -ItemType Directory -Force | Out-Null

Get-ChildItem -LiteralPath $StageDir -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -ne "vc_redist.x64.exe" } |
    Remove-Item -Force

$runtimeFiles = @(
    Get-ChildItem -LiteralPath $AppOutputDir -File |
        Sort-Object Name |
        Where-Object { -not (Test-ExcludedFile -File $_) }
)

if (-not ($runtimeFiles.Name -contains "TrayApp.exe")) {
    throw "TrayApp.exe was not found in $AppOutputDir"
}

if (-not ($runtimeFiles.Name -contains "TrayService.exe")) {
    throw "TrayService.exe was not found in $AppOutputDir"
}

foreach ($file in $runtimeFiles) {
    Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $StageDir $file.Name) -Force
}

$vcRedistPath = Join-Path $StageDir "vc_redist.x64.exe"
if (-not (Test-Path -LiteralPath $vcRedistPath)) {
    Invoke-WebRequest -Uri $VcRedistUrl -OutFile $vcRedistPath
}

$supplementalFiles = @(
    Get-ChildItem -LiteralPath $StageDir -File |
        Sort-Object Name |
        Where-Object { $_.Name -notin @("TrayApp.exe", "TrayService.exe", "vc_redist.x64.exe") }
)

$xmlLines = New-Object System.Collections.Generic.List[string]
$xmlLines.Add('<Wix xmlns="http://wixtoolset.org/schemas/v4/wxs">')
$xmlLines.Add('  <Fragment>')
$xmlLines.Add('    <ComponentGroup Id="SupplementalRuntimeFiles">')

$index = 1
foreach ($file in $supplementalFiles) {
    $source = Escape-XmlAttribute -Value $file.FullName
    $componentId = "SupplementalRuntimeComponent$index"
    $fileId = "SupplementalRuntimeFile$index"

    $xmlLines.Add("      <Component Id=`"$componentId`" Directory=`"INSTALLFOLDER`">")
    $xmlLines.Add("        <File Id=`"$fileId`" Source=`"$source`" KeyPath=`"yes`" Checksum=`"yes`" />")
    $xmlLines.Add("      </Component>")

    $index++
}

$xmlLines.Add('    </ComponentGroup>')
$xmlLines.Add('  </Fragment>')
$xmlLines.Add('</Wix>')

$xmlLines | Set-Content -LiteralPath $GeneratedWxsPath -Encoding UTF8

Write-Host "Staged runtime files:"
$runtimeFiles | ForEach-Object { Write-Host " - $($_.Name)" }

if ($supplementalFiles.Count -eq 0) {
    Write-Host "No supplemental runtime files detected."
}
else {
    Write-Host "Supplemental runtime files:"
    $supplementalFiles | ForEach-Object { Write-Host " - $($_.Name)" }
}

Write-Host "VC++ redistributable payload: $vcRedistPath"
Write-Host "Generated WiX fragment: $GeneratedWxsPath"
