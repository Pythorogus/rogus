"""Mastering automatique en ligne de commande.

Mode par défaut : amène le morceau à un volume perçu cible (LUFS) et passe un
limiteur pour que les crêtes ne dépassent pas le plafond. Les fréquences ne
sont pas modifiées.

Mode référence (--reference) : rapproche le morceau d'un titre de référence
(volume, équilibre des fréquences, largeur stéréo) via la bibliothèque
Matchering.

Exemples :
    python master.py monmix.wav
    python master.py monmix.wav -o master.wav --lufs -12
    python master.py monmix.wav --reference titre_pro.wav
"""

from __future__ import annotations

import argparse
import sys
import tempfile
from pathlib import Path

import numpy as np
import pyloudnorm as pyln
import soundfile as sf
from scipy.ndimage import maximum_filter1d, uniform_filter1d
from scipy.signal import resample_poly

LUFS_PAR_DEFAUT = -14.0  # cible des plateformes de streaming
SURECHANTILLONNAGE = 4   # pour détecter les crêtes entre deux échantillons
SOUS_TYPES = {"16": "PCM_16", "24": "PCM_24", "32f": "FLOAT"}


def db(valeur: float) -> float:
    """Convertit une amplitude linéaire en décibels."""
    return 20.0 * np.log10(max(valeur, 1e-12))


def lineaire(decibels: float) -> float:
    """Convertit des décibels en facteur d'amplitude."""
    return 10.0 ** (decibels / 20.0)


def enveloppe_cretes(audio: np.ndarray) -> np.ndarray:
    """Crête réelle (« true peak ») autour de chaque échantillon.

    Le signal est suréchantillonné 4x : une fois reconverti en analogique, il
    peut dépasser la valeur des échantillons eux-mêmes. On garde, pour chaque
    échantillon d'origine, le maximum tous canaux confondus.
    """
    n = audio.shape[0]
    cretes = np.zeros(n, dtype=np.float32)
    for canal in range(audio.shape[1]):
        sur = resample_poly(audio[:, canal].astype(np.float32), SURECHANTILLONNAGE, 1)
        sur = np.abs(sur[: n * SURECHANTILLONNAGE]).reshape(n, SURECHANTILLONNAGE)
        np.maximum(cretes, sur.max(axis=1), out=cretes)
    return cretes.astype(np.float64)


def reduction_limiteur(
    cretes: np.ndarray,
    frequence: int,
    plafond_db: float,
    anticipation_ms: float = 5.0,
    relachement_db_par_s: float = 80.0,
) -> np.ndarray:
    """Calcule la réduction de gain (en dB, >= 0) à appliquer à chaque échantillon.

    1. dépassement brut du plafond ;
    2. anticipation : la réduction commence avant la crête (pas de claquement) ;
    3. relâchement : le gain remonte progressivement après la crête ;
    4. lissage : supprime les cassures de la courbe de gain.
    Chaque étape ne fait qu'augmenter la réduction : le plafond reste garanti.
    """
    depassement = 20.0 * np.log10(np.maximum(cretes, 1e-12)) - plafond_db
    reduction = np.maximum(depassement, 0.0)
    if not reduction.any():
        return reduction

    fenetre = max(3, int(frequence * anticipation_ms / 1000)) | 1  # impaire
    reduction = maximum_filter1d(reduction, size=fenetre, mode="nearest")

    # Relâchement linéaire en dB : r[n] = max(r[n], r[n-1] - pas), vectorisé.
    pente = np.arange(len(reduction)) * (relachement_db_par_s / frequence)
    reduction = np.maximum.accumulate(reduction + pente) - pente

    return uniform_filter1d(reduction, size=fenetre, mode="nearest")


def mesurer(audio: np.ndarray, frequence: int, cretes: np.ndarray | None = None) -> dict:
    """Mesure le volume perçu, la crête réelle et la dynamique."""
    lufs = pyln.Meter(frequence).integrated_loudness(audio)
    if cretes is None:
        cretes = enveloppe_cretes(audio)
    crete_db = db(float(cretes.max()))
    return {
        "lufs": lufs,
        "crete": crete_db,
        # Écart crête / volume : plus il est grand, plus le morceau est dynamique.
        "dynamique": crete_db - lufs,
    }


def masteriser_volume(
    audio: np.ndarray, frequence: int, cible_lufs: float | None, plafond_db: float
) -> tuple[np.ndarray, float]:
    """Amène `audio` à `cible_lufs` sans dépasser `plafond_db`.

    Le limiteur fait un peu baisser le volume perçu, donc on ajuste le gain
    en plusieurs passes jusqu'à tomber sur la cible.
    Si `cible_lufs` vaut None, seul le plafond est appliqué (volume inchangé).
    Retourne le signal traité et la réduction maximale du limiteur (dB).
    """
    compteur = pyln.Meter(frequence)
    depart = compteur.integrated_loudness(audio)
    if not np.isfinite(depart):
        raise ValueError("le fichier est silencieux, rien à masteriser")

    cretes = enveloppe_cretes(audio)
    gain_db = 0.0 if cible_lufs is None else cible_lufs - depart
    sortie, reduction_max = audio, 0.0

    for _ in range(1 if cible_lufs is None else 25):
        reduction = reduction_limiteur(cretes * lineaire(gain_db), frequence, plafond_db)
        reduction_max = float(reduction.max())
        sortie = audio * (lineaire(gain_db) * 10.0 ** (-reduction / 20.0))[:, None]
        if cible_lufs is None:
            break
        ecart = cible_lufs - compteur.integrated_loudness(sortie)
        if abs(ecart) < 0.05:
            break
        gain_db += ecart

    # Sécurité : le gain variable peut déplacer très légèrement les crêtes.
    exces = db(float(enveloppe_cretes(sortie).max())) - plafond_db
    if exces > 0:
        sortie = sortie * lineaire(-exces)

    return sortie, reduction_max


def masteriser_reference(source: Path, reference: Path, destination: Path) -> None:
    """Mastering par référence avec Matchering (résultat en WAV 24 bits)."""
    try:
        import matchering as mg
    except ImportError:
        raise SystemExit(
            "Le mode --reference nécessite Matchering : pip install matchering"
        )
    mg.log(warning_handler=lambda message: print(f"  [matchering] {message}"))
    mg.process(
        target=str(source),
        reference=str(reference),
        results=[mg.pcm24(str(destination))],
    )


def ecrire(chemin: Path, audio: np.ndarray, frequence: int, bits: str) -> None:
    """Écrit le fichier audio, avec dither en 16 bits."""
    if bits == "16":
        # Dither TPDF : un bruit très faible qui masque l'arrondi à 16 bits.
        alea = np.random.default_rng()
        bruit = alea.random(audio.shape) - alea.random(audio.shape)
        audio = audio + bruit / 32768.0
    sf.write(chemin, np.clip(audio, -1.0, 1.0), frequence, subtype=SOUS_TYPES[bits])


def afficher_rapport(avant: dict, apres: dict) -> None:
    lignes = [
        ("Volume perçu (LUFS)", "lufs"),
        ("Crête réelle (dBTP)", "crete"),
        ("Dynamique (dB)", "dynamique"),
    ]
    print(f"\n{'':24}{'Avant':>10}{'Après':>10}")
    for libelle, cle in lignes:
        print(f"{libelle:24}{avant[cle]:>10.1f}{apres[cle]:>10.1f}")


def analyser_arguments() -> argparse.Namespace:
    analyseur = argparse.ArgumentParser(
        description="Mastering automatique : volume cible + limiteur, ou par référence."
    )
    analyseur.add_argument("entree", type=Path, help="fichier à masteriser (WAV, FLAC, AIFF, MP3...)")
    analyseur.add_argument("-o", "--sortie", type=Path, help="fichier de sortie (défaut : <nom>_master.wav)")
    analyseur.add_argument(
        "--lufs", type=float,
        help=f"volume perçu cible (défaut : {LUFS_PAR_DEFAUT:g}, ou celui de la référence)",
    )
    analyseur.add_argument("--plafond", type=float, default=-1.0, help="crête maximale en dBTP (défaut : -1)")
    analyseur.add_argument("--reference", type=Path, help="morceau de référence à imiter (EQ compris)")
    analyseur.add_argument("--bits", choices=SOUS_TYPES, default="24", help="format de sortie (défaut : 24)")
    return analyseur.parse_args()


def main() -> int:
    for flux in (sys.stdout, sys.stderr):
        flux.reconfigure(encoding="utf-8")
    args = analyser_arguments()

    for fichier in (args.entree, args.reference):
        if fichier is not None and not fichier.is_file():
            print(f"Erreur : fichier introuvable : {fichier}", file=sys.stderr)
            return 1
    if args.plafond > 0:
        print("Erreur : --plafond doit être négatif ou nul (ex. -1).", file=sys.stderr)
        return 1

    sortie = args.sortie or args.entree.with_name(f"{args.entree.stem}_master.wav")
    if sortie.suffix.lower() != ".wav":
        print("Erreur : la sortie doit être un fichier .wav.", file=sys.stderr)
        return 1
    if sortie.resolve() == args.entree.resolve():
        print("Erreur : la sortie écraserait le fichier d'entrée.", file=sys.stderr)
        return 1

    audio, frequence = sf.read(args.entree, always_2d=True, dtype="float64")
    if audio.shape[0] < frequence:
        print("Erreur : le fichier dure moins d'une seconde.", file=sys.stderr)
        return 1
    print(f"Entrée : {args.entree.name} ({audio.shape[0] / frequence:.0f} s, {frequence} Hz)")
    try:
        avant = mesurer(audio, frequence)
        if not np.isfinite(avant["lufs"]):
            raise ValueError("le fichier est silencieux, rien à masteriser")

        cible = args.lufs
        if args.reference is not None:
            print(f"Mastering par référence : {args.reference.name}")
            with tempfile.TemporaryDirectory() as dossier:
                temporaire = Path(dossier) / "matchering.wav"
                masteriser_reference(args.entree, args.reference, temporaire)
                audio, frequence = sf.read(temporaire, always_2d=True, dtype="float64")
        elif cible is None:
            cible = LUFS_PAR_DEFAUT

        if cible is None:
            # Matchering ne surveille pas les crêtes entre échantillons.
            print(f"Plafond : {args.plafond:g} dBTP")
        else:
            print(f"Volume cible : {cible:g} LUFS, plafond : {args.plafond:g} dBTP")
        audio, reduction = masteriser_volume(audio, frequence, cible, args.plafond)
        if reduction > 6:
            print(
                f"Attention : le limiteur réduit jusqu'à {reduction:.1f} dB, le son "
                "risque d'être écrasé. Essayez une cible plus basse."
            )
    except ValueError as erreur:
        print(f"Erreur : {erreur}", file=sys.stderr)
        return 1

    ecrire(sortie, audio, frequence, args.bits)
    apres = mesurer(audio, frequence)
    if cible is not None and abs(apres["lufs"] - cible) > 0.2:
        print(
            f"Attention : cible non atteinte ({apres['lufs']:.1f} LUFS au lieu de "
            f"{cible:g}), le morceau a trop de crêtes pour ce volume."
        )
    afficher_rapport(avant, apres)
    print(f"\nSortie : {sortie}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
