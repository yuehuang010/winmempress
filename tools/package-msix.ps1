#requires -Version 7.0

<#
.SYNOPSIS
    Builds mempressmonitor.exe and packs it into an MSIX for the Microsoft Store.

.DESCRIPTION
    Produces packaging/out/MemPressMonitor-<version>-<arch>.msix. Run once per
    architecture you want to publish and upload each .msix to the same Partner
    Center submission.

    For a Store submission, pass the three identity values Partner Center shows
    under Product identity:

        tools\package-msix.ps1 -IdentityName 12345Publisher.MemPressMonitor `
            -Publisher 'CN=ABCD1234-...' -PublisherDisplayName 'Your Name'

    With no identity parameters the script uses development placeholders, which
    is what you want for a local sideload test (add -Sign for that).

.NOTES
    -Sign produces a self-signed test signature so the package can be installed
    locally. The Store re-signs submitted packages, so never ship that cert.
#>

[CmdletBinding()]
param(
    [ValidateSet('x64', 'arm64')]
    [string]$Architecture = 'x64',

    [ValidatePattern('^\d+\.\d+\.\d+\.0$')]
    [string]$Version = '1.0.0.0',

    [string]$IdentityName = 'MemPressMonitor.Development',

    [ValidatePattern('^CN=')]
    [string]$Publisher = 'CN=MemPressMonitor Development',

    [string]$PublisherDisplayName = 'MemPressMonitor Development',

    [switch]$Sign
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent ([System.IO.Path]::GetFullPath($PSScriptRoot))
$packagingDir = Join-Path $repoRoot 'packaging'
$assetDir = Join-Path $packagingDir 'Assets'
$outDir = Join-Path $packagingDir 'out'
$stageDir = Join-Path $outDir "stage-$Architecture"
$buildDir = Join-Path $repoRoot "build-$Architecture"

$cmake = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) { throw "cmake.exe not found at $cmake" }

# Pick the newest Windows SDK that has the packaging tools.
$sdkBin = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin\*\x64\makeappx.exe' -ErrorAction SilentlyContinue |
    Sort-Object { [version](Split-Path -Leaf (Split-Path -Parent (Split-Path -Parent $_.FullName))) } |
    Select-Object -Last 1 |
    ForEach-Object { Split-Path -Parent $_.FullName }
if (-not $sdkBin) { throw 'No Windows SDK with makeappx.exe was found. Install the Windows 10/11 SDK.' }

$makeappx = Join-Path $sdkBin 'makeappx.exe'
$makepri = Join-Path $sdkBin 'makepri.exe'
$signtool = Join-Path $sdkBin 'signtool.exe'
foreach ($tool in $makepri, $signtool) {
    if (-not (Test-Path -LiteralPath $tool)) { throw "Required SDK tool is missing: $tool" }
}

function Invoke-Tool {
    param([string]$FilePath, [string[]]$Arguments, [string]$FailureMessage)

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) { throw $FailureMessage }
}

# --- Build -----------------------------------------------------------------

$cmakePlatform = if ($Architecture -eq 'x64') { 'x64' } else { 'ARM64' }
Invoke-Tool $cmake @('-S', $repoRoot, '-B', $buildDir, '-A', $cmakePlatform) 'CMake configure failed.'
Invoke-Tool $cmake @('--build', $buildDir, '--config', 'Release', '--target', 'mempressmonitor') 'Build failed.'

$exePath = Join-Path $buildDir 'Release\mempressmonitor.exe'
if (-not (Test-Path -LiteralPath $exePath)) { throw "Build did not produce $exePath" }

# --- Assets ----------------------------------------------------------------

if (-not (Test-Path -LiteralPath (Join-Path $assetDir 'StoreLogo.scale-100.png'))) {
    & (Join-Path $PSScriptRoot 'generate-store-assets.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'Store asset generation failed.' }
}

# --- Stage -----------------------------------------------------------------

if (Test-Path -LiteralPath $stageDir) { Remove-Item -Recurse -Force -LiteralPath $stageDir }
$null = New-Item -ItemType Directory -Path $stageDir -Force

Copy-Item -LiteralPath $exePath -Destination $stageDir
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE') -Destination $stageDir
Copy-Item -LiteralPath $assetDir -Destination $stageDir -Recurse

$manifest = [xml](Get-Content -LiteralPath (Join-Path $packagingDir 'AppxManifest.xml') -Raw)
$identity = $manifest.Package.Identity
$identity.Name = $IdentityName
$identity.Publisher = $Publisher
$identity.Version = $Version
$identity.ProcessorArchitecture = $Architecture
$manifest.Package.Properties.PublisherDisplayName = $PublisherDisplayName

$stagedManifest = Join-Path $stageDir 'AppxManifest.xml'
$manifest.Save($stagedManifest)

# --- Resource index --------------------------------------------------------

$priConfig = Join-Path $outDir 'priconfig.xml'
$priFile = Join-Path $outDir "resources-$Architecture.pri"
Invoke-Tool $makepri @('createconfig', '/cf', $priConfig, '/dq', 'lang-en-US', '/pv', '10.0.0', '/o') 'makepri createconfig failed.'

# The default config splits scale and language candidates into separate
# resource-pack .pri files. This is a single self-contained package, so drop
# the split and keep every candidate in one resources.pri.
$priConfigXml = [xml](Get-Content -LiteralPath $priConfig -Raw)
$packagingNode = $priConfigXml.resources.packaging
foreach ($node in @($packagingNode.ChildNodes)) {
    $null = $packagingNode.RemoveChild($node)
}
$priConfigXml.Save($priConfig)

Invoke-Tool $makepri @('new', '/pr', $stageDir, '/cf', $priConfig, '/mn', $stagedManifest, '/of', $priFile, '/o') 'makepri new failed.'
Copy-Item -LiteralPath $priFile -Destination (Join-Path $stageDir 'resources.pri')

# --- Pack ------------------------------------------------------------------

$msixPath = Join-Path $outDir "MemPressMonitor-$Version-$Architecture.msix"
Invoke-Tool $makeappx @('pack', '/d', $stageDir, '/p', $msixPath, '/o') 'makeappx pack failed.'

if ($Sign) {
    $cert = Get-ChildItem 'Cert:\CurrentUser\My' |
        Where-Object { $_.Subject -eq $Publisher } |
        Sort-Object NotAfter |
        Select-Object -Last 1
    if (-not $cert) {
        $cert = New-SelfSignedCertificate -Type Custom -Subject $Publisher `
            -KeyUsage DigitalSignature -FriendlyName 'MemPressMonitor MSIX test signing' `
            -CertStoreLocation 'Cert:\CurrentUser\My' `
            -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')
        Write-Host "Created test signing certificate $($cert.Thumbprint)"
    }

    Invoke-Tool $signtool @('sign', '/fd', 'SHA256', '/sha1', $cert.Thumbprint, $msixPath) 'signtool sign failed.'
    Write-Host 'Signed with a test certificate. To install locally, first trust it (elevated):'
    Write-Host "  Export-Certificate -Cert Cert:\CurrentUser\My\$($cert.Thumbprint) -FilePath `$env:TEMP\mempressmonitor-test.cer"
    Write-Host "  Import-Certificate -FilePath `$env:TEMP\mempressmonitor-test.cer -CertStoreLocation Cert:\LocalMachine\TrustedPeople"
}

Write-Host "Packaged $msixPath"
