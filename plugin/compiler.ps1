# Compile les plugins Rogus en VST3 (version optimisée).
# La première fois, CMake télécharge JUCE : compter plusieurs minutes.
#
#   .\plugin\compiler.ps1

$ErrorActionPreference = 'Stop'
$racine = $PSScriptRoot
$build  = Join-Path $racine 'build'

# Dans un terminal ouvert avant l'installation de CMake, il n'est pas dans le PATH.
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { $env:Path = "C:\Program Files\CMake\bin;" + $env:Path }

cmake -S $racine -B $build
if ($LASTEXITCODE -ne 0) { throw 'La configuration CMake a échoué.' }

cmake --build $build --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw 'La compilation a échoué.' }

$vst3 = Join-Path $build 'Rogus_artefacts\Release\VST3\Rogus.vst3'
$vst3Master = Join-Path $build 'RogusMaster_artefacts\Release\VST3\Rogus Master.vst3'

Write-Host ''
Write-Host "Plugins compilés :"
Write-Host "  $vst3"
Write-Host "  $vst3Master"
Write-Host 'Pour les rendre visibles dans FL Studio : .\plugin\installer.ps1'
