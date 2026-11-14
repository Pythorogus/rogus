# Copie les plugins compilés dans le dossier VST3 commun de Windows :
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
$racine = $PSScriptRoot
$cible  = Join-Path $env:CommonProgramFiles 'VST3'

# Les deux plugins du projet. Les noms de dossier sont aussi les noms des
# binaires à l'intérieur (Rogus.vst3, Rogus Master.vst3).
$sources = @(
    (Join-Path $racine 'build\Rogus_artefacts\Release\VST3\Rogus.vst3'),
    (Join-Path $racine 'build\RogusMaster_artefacts\Release\VST3\Rogus Master.vst3')
)

foreach ($source in $sources)
{
    if (-not (Test-Path $source))
    {
        throw "Plugin introuvable : $source. Lancer d'abord .\plugin\compiler.ps1"
    }
}

$identite = [Security.Principal.WindowsIdentity]::GetCurrent()
$estAdmin = ([Security.Principal.WindowsPrincipal] $identite).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $estAdmin)
{
    # Relance dans une fenêtre élevée, puis vérifie le résultat depuis ici :
    # la fenêtre élevée se ferme toute seule et ses messages ne sont pas visibles.
    Start-Process powershell -Verb RunAs -Wait `
        -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`""

    $toutInstalle = $true

    foreach ($source in $sources)
    {
        $nom = Split-Path $source -Leaf
        $compile  = Join-Path $source 'Contents\x86_64-win'
        $compile  = Join-Path $compile $nom
        $installe = Join-Path $cible (Join-Path $nom 'Contents\x86_64-win')
        $installe = Join-Path $installe $nom

        $dejaAJour = (Test-Path $installe) -and
                     ((Get-Item $installe).LastWriteTime -ge (Get-Item $compile).LastWriteTime)

        if (-not $dejaAJour)
        {
            $toutInstalle = $false
        }
    }

    if ($toutInstalle)
    {
        Write-Host "Plugins installés dans : $cible"
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

    foreach ($source in $sources)
    {
        Copy-Item $source $cible -Recurse -Force
        Write-Host "Copié : $(Split-Path $source -Leaf)"
    }
}
catch
{
    Write-Host "Échec de la copie : $_" -ForegroundColor Red
    Read-Host 'Appuyer sur Entrée pour fermer'
    exit 1
}
