# Copie le plugin compilé dans le dossier VST3 commun de Windows :
#   C:\Program Files\Common Files\VST3
# C'est l'emplacement standard : FL Studio et les autres hôtes y cherchent
# les plugins sans réglage particulier.
#
#   .\plugin\installer.ps1
#
# Ce dossier est protégé : le script se relance lui-même avec les droits
# administrateur, et Windows affiche une demande de confirmation.
# Fermer FL Studio avant : un plugin chargé est verrouillé et ne peut pas être remplacé.

$ErrorActionPreference = 'Stop'
$source = Join-Path $PSScriptRoot 'build\Rogus_artefacts\Release\VST3\Rogus.vst3'
$cible  = Join-Path $env:CommonProgramFiles 'VST3'

if (-not (Test-Path $source)) { throw "Plugin introuvable : lancer d'abord .\plugin\compiler.ps1" }

$identite = [Security.Principal.WindowsIdentity]::GetCurrent()
$estAdmin = ([Security.Principal.WindowsPrincipal] $identite).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $estAdmin)
{
    # Relance dans une fenêtre élevée, puis vérifie le résultat depuis ici :
    # la fenêtre élevée se ferme toute seule et ses messages ne sont pas visibles.
    Start-Process powershell -Verb RunAs -Wait `
        -ArgumentList '-NoProfile', '-File', "`"$PSCommandPath`""

    $installe = Join-Path $cible 'Rogus.vst3\Contents\x86_64-win\Rogus.vst3'
    if ((Test-Path $installe) -and ((Get-Item $installe).LastWriteTime -ge (Get-Item (Join-Path $source 'Contents\x86_64-win\Rogus.vst3')).LastWriteTime))
    {
        Write-Host "Plugin installé dans : $cible"
    }
    else
    {
        throw "L'installation n'a pas abouti (demande refusée, ou FL Studio encore ouvert)."
    }
    return
}

try
{
    New-Item -ItemType Directory -Force $cible | Out-Null
    Copy-Item $source $cible -Recurse -Force
}
catch
{
    Write-Host "Échec de la copie : $_" -ForegroundColor Red
    Read-Host 'Appuyer sur Entrée pour fermer'
    exit 1
}
