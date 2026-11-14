# Compile et lance les tests des deux plugins, sans FL Studio.
#
#   .\plugin\tester.ps1

$ErrorActionPreference = 'Stop'
$racine = $PSScriptRoot
$build  = Join-Path $racine 'build'

# Dans un terminal ouvert avant l'installation de CMake, il n'est pas dans le PATH.
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { $env:Path = "C:\Program Files\CMake\bin;" + $env:Path }

cmake -S $racine -B $build | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'La configuration CMake a échoué.' }

cmake --build $build --config Release --target RogusTests RogusMasterTests --parallel
if ($LASTEXITCODE -ne 0) { throw 'La compilation des tests a échoué.' }

& (Join-Path $build 'RogusTests_artefacts\Release\RogusTests.exe')
$testsPiste = $LASTEXITCODE

& (Join-Path $build 'RogusMasterTests_artefacts\Release\RogusMasterTests.exe')
$testsMaster = $LASTEXITCODE

if ($testsPiste -ne 0 -or $testsMaster -ne 0) { exit 1 }
exit 0
