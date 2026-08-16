# Builds the Release configuration and packages the binaries into a zip
# named with the current git commit SHA, e.g. publish/winmempress-3f2a1b9.zip.
# A dirty working tree gets a "-dirty" suffix so the artifact is never
# mistaken for a clean build of that commit.

#Requires -Version 7
[CmdletBinding()]
param(
    [string]$OutputDir = 'publish'
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$cmake = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'

$sha = (git -C $repoRoot rev-parse --short HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Not a git repository or git is unavailable.' }
if ((git -C $repoRoot status --porcelain) | Where-Object { $_ }) {
    $sha += '-dirty'
    Write-Warning 'Working tree has uncommitted changes; artifact tagged as -dirty.'
}

& $cmake -S $repoRoot -B (Join-Path $repoRoot 'build')
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
& $cmake --build (Join-Path $repoRoot 'build') --config Release
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }

$releaseDir = Join-Path $repoRoot 'build\Release'
$stageName = "winmempress-$sha"
$stageDir = Join-Path $repoRoot (Join-Path $OutputDir $stageName)
if (Test-Path $stageDir) { Remove-Item -Recurse -Force $stageDir }
New-Item -ItemType Directory -Force $stageDir | Out-Null

foreach ($file in 'winmempress.exe', 'winmempress-cli.exe') {
    Copy-Item (Join-Path $releaseDir $file) $stageDir
}
Copy-Item (Join-Path $repoRoot 'LICENSE') $stageDir

$zipPath = Join-Path $repoRoot (Join-Path $OutputDir "$stageName.zip")
Compress-Archive -Path (Join-Path $stageDir '*') -DestinationPath $zipPath -Force

Write-Host "Published $zipPath"
