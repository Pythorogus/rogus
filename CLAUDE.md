# rogus

Outil personnel de mastering audio automatique en ligne de commande, inspiré de LANDR.
Tout tourne en local : aucun fichier audio n'est envoyé à un service externe.

## État actuel

Un seul script, `master.py`, avec deux modes :

- **Mode par défaut** : amène le morceau à un volume perçu cible (LUFS) et applique un limiteur. Les fréquences ne sont pas modifiées.
- **Mode `--reference`** : rapproche le morceau d'un titre de référence (volume, équilibre des fréquences, largeur stéréo) via Matchering, puis applique le limiteur.

Il n'y a **pas d'IA** pour l'instant : c'est du traitement de signal déterministe.

## Utilisation

Python n'a les bibliothèques que dans l'environnement virtuel `.venv` du projet.

```powershell
.\.venv\Scripts\python.exe master.py monmix.wav
.\.venv\Scripts\python.exe master.py monmix.wav -o final.wav --lufs -11 --bits 16
.\.venv\Scripts\python.exe master.py monmix.wav --reference titre_pro.wav
```

| Option | Rôle | Défaut |
|---|---|---|
| `-o` | fichier de sortie | `<nom>_master.wav` |
| `--lufs` | volume perçu visé | -14 (ou celui de la référence) |
| `--plafond` | crête réelle maximale (dBTP) | -1 |
| `--reference` | morceau à imiter | aucun |
| `--bits` | `16`, `24` ou `32f` | 24 |

Le fichier d'entrée n'est jamais modifié. Le script affiche un tableau avant/après (LUFS, crête réelle, dynamique).

## Fonctionnement de `master.py`

1. **Lecture** du fichier avec `soundfile` (WAV, FLAC, AIFF, MP3...).
2. **Mesure** : volume intégré (LUFS, norme ITU-R BS.1770 via `pyloudnorm`) et crête réelle (« true peak ») par suréchantillonnage 4x.
3. **Mode référence** (facultatif) : Matchering écrit un fichier temporaire, relu ensuite.
4. **Volume + limiteur** (`masteriser_volume`) : le gain est ajusté en plusieurs passes, car le limiteur fait baisser le volume perçu. Sans cible LUFS (référence seule), seul le plafond est appliqué.
5. **Écriture** en WAV, avec dither TPDF en 16 bits.

Le limiteur (`reduction_limiteur`) est écrit en numpy/scipy, sans boucle Python :
dépassement du plafond → anticipation (filtre maximum) → relâchement linéaire en dB → lissage.
Chaque étape ne fait qu'augmenter la réduction, donc le plafond reste garanti.
L'enveloppe de crêtes n'est calculée qu'une fois : elle est proportionnelle au gain.

## Dépendances

Python 3.13, bibliothèques listées dans `requirements.txt` :

| Bibliothèque | Rôle |
|---|---|
| `numpy`, `scipy` | calcul, filtres, suréchantillonnage |
| `soundfile` | lecture et écriture des fichiers audio |
| `pyloudnorm` | mesure du volume perçu (LUFS) |
| `matchering` | mode `--reference` uniquement, importé à la demande |

Installation sur un nouveau poste :

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
```

**Licence** : Matchering est sous GPL v3. Sans conséquence en usage personnel, contraignant en cas de distribution de l'outil.

## Limites connues

- Limiteur simple, large bande : transparent pour quelques dB de réduction, pas au-delà. Le script avertit au-dessus de 6 dB.
- Sur un morceau très percussif, une cible élevée peut ne pas être atteinte (avertissement affiché).
- Entrée : WAV, FLAC, AIFF, OGG, MP3. Sortie : WAV uniquement.
- Matchering travaille en 44,1 kHz : en mode référence, la sortie est à cette fréquence.
- Validé sur des signaux de synthèse (cible LUFS et plafond respectés). Des morceaux réels ont aussi été traités (fichiers de test du dossier), mais l'écoute à l'oreille n'est pas encore documentée.
- Pas de tests automatisés.

## Objectif du projet

Pierre veut un « ingé son » qui fait à sa place la partie technique du mix (EQ, compression, sidechain, master), tout en gardant le pouvoir de décision artistique. Il enregistre de la guitare électrique, veut essayer l'acoustique, et parfois de la voix. Il travaille dans FL Studio.

## Suite prévue : plugin VST3 d'assistance au mix

Un plugin (C++ / JUCE) à poser sur chaque piste, dans un sous-dossier du projet. `master.py` reste l'outil de mastering.

Principes décidés :

- **Rôle par piste** choisi par l'utilisateur (guitare lead, rythmique, voix, basse, kick...) : il fixe les bandes de fréquences importantes pour l'instrument et une priorité par défaut.
- **Deux couches d'EQ** : une correction fixe apprise à l'écoute (coupe-bas, résonances, équilibre), et un démasquage dynamique qui suit le morceau.
- **Orchestration entre instances** : chaque instance publie son énergie par bande, les pistes moins prioritaires s'effacent dans les bandes où une piste prioritaire joue, et seulement pendant qu'elle joue. Cela couvre aussi le sidechain kick/basse sans routage dans FL.
- **Contrôle utilisateur** : l'utilisateur décide de l'intention (qui est devant, de combien), le plugin gère les fréquences. Réglages peu nombreux : rôle, priorité, quantité, réactivité, activation de chaque couche. Le plugin montre toujours ce qu'il fait.

Étapes :

1. **Squelette** : plugin VST3 minimal (coupe-bas) qui se compile et se charge dans FL Studio.
2. **EQ fixe par rôle** : écoute, cible par instrument, dosage.
3. **Orchestration** : communication entre instances, priorités, démasquage dynamique.
4. **Compression** par rôle.
5. Plus tard : contrôle des prises (saturation, bruit, ronflette), explications générées par un modèle de langage.
6. **Optionnel** : plugin sur la piste master, en lecture seule au départ. Il reçoit l'énergie publiée par toutes les instances et affiche la carte des collisions entre pistes, ainsi que le volume perçu (LUFS) et la crête réelle en direct. Il ne modifie pas les pistes. Une action sur le bus (limiteur, cible globale) ne sera envisagée qu'après, pour éviter deux systèmes de décision concurrents.

Points techniques à traiter à l'étape 3 : synchroniser les instances sur la position de lecture de l'hôte (et non l'horloge), pour que l'export hors temps réel donne le même résultat que l'écoute.

Abandonné pour l'instant : profils de style et détection de genre pour le mastering, interface web locale.

## Conventions

- Code, commentaires et messages utilisateur en français.
- Commentaires pédagogiques : expliquer le pourquoi des étapes audio.
- Pas de git pour l'instant dans ce dossier.
