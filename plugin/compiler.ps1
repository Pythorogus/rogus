# Compile le plugin Rogus en VST3 (version optimisée).
# La première fois, CMake télécharge JUCE : compter plusieurs minutes.
#
#   .\plugin\compiler.ps1

$ErrorActionPreference = 'Stop'
$racine = $PSScriptRoot
$build  = Join-Path $racine 'build'

cmake -S $racine -B $build
if ($LASTEXITCODE -ne 0) { throw 'La configuration CMake a échoué.' }

cmake --build $build --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw 'La compilation a échoué.' }

$vst3 = Join-Path $build 'Rogus_artefacts\Release\VST3\Rogus.vst3'
Write-Host ''
Write-Host "Plugin compilé : $vst3"
Write-Host 'Pour le rendre visible dans FL Studio : .\plugin\installer.ps1'
