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
- Pas de tests automatisés pour `master.py` (le plugin en a, voir plus bas).

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

### État du plugin (dossier `plugin/`)

**Étape 1 terminée** (13 novembre 2026) : compilée avec MSVC 19.44 et CMake 4.4.4, chargée par Pierre dans FL Studio sur une piste de guitare.

**Étape 2 écrite, compilée et testée sur signaux de synthèse** (même jour), mais **pas encore écoutée dans FL Studio**. Elle n'est terminée qu'après écoute sur de vraies prises.

| Fichier | Rôle |
|---|---|
| `CMakeLists.txt` | projet CMake ; télécharge JUCE 8.0.9 (version figée) à la première configuration |
| `Source/Correction.*` | cœur sans dépendance au plugin : table des rôles, analyse d'une écoute, calcul de la correction, filtres |
| `Source/PluginProcessor.*` | moteur audio : écoute de la piste, filtrage, sauvegarde |
| `Source/PluginEditor.*` | fenêtre : rôle, courbe appliquée, quantité, bouton d'écoute, texte d'état |
| `Tests/tests.cpp` | tests du moteur hors FL Studio |
| `compiler.ps1` | configure et compile en Release |
| `tester.ps1` | compile et lance les tests |
| `installer.ps1` | copie le `.vst3` dans `C:\Program Files\Common Files\VST3` (emplacement choisi par Pierre) ; se relance avec les droits administrateur, Windows demande confirmation |

Fonctionnement de l'EQ fixe :

1. **Trois menus** décrivent la piste (tables dans `Correction.cpp`) :
   - **Rôle** = fonction dans le mix : Lead, Rythmique, Arpège, Basse, Kick, Snare, Hits, Voix, Nappe. Il donne la zone à garder lisible, un coupe-bas et l'équilibre de base visé sur sept bandes d'octave (100 Hz à 6,4 kHz).
   - **Source** = ce qui produit le son : Guitare électrique, Guitare acoustique, Voix, Basse, Batterie, Synthé. Elle décide de la correction fixe : son propre coupe-bas, présence ou non de résonances à corriger (un synthé n'a pas les défauts d'un enregistrement), léger décalage de la cible (acoustique).
   - **Son** = caractère voulu : Clair, Saturé, Sombre, Brillant. Il déplace la cible pour que le plugin ne « corrige » pas ce qui est voulu (un son sombre : il ne remonte pas les aigus).
   - Coupe-bas final = le plus haut de celui du rôle et de celui de la source. Résonances recherchées seulement si le rôle ET la source s'y prêtent.
2. **Écoute** : sur demande, le plugin accumule le spectre de l'entrée pendant 10 s de son réel (les silences ne comptent pas, 3 s au minimum). Il en tire une *mesure* : niveau par bande et jusqu'à trois résonances étroites (200 Hz à 5 kHz).
3. **Correction** = écart entre la cible (rôle + source + son) et la mesure, moyenne retirée (le volume ne change pas), bornée à -6 / +4 dB, réalisée par un coupe-bas, sept filtres larges et des encoches de 2 à 4 dB sur les résonances. Sans écoute, seul le coupe-bas agit.
4. **Quantité** (0 à 100 %) dose le tout. À 0 % ou correction désactivée, le son traverse le plugin sans aucun calcul.

La mesure est enregistrée dans le projet de l'hôte, pas la correction : changer de rôle, de source ou de son, ou améliorer le calcul, ne demande pas de réécoute. La courbe affichée utilise les mêmes filtres que l'audio.

Limites connues de l'étape 2 :

- **Les cibles (rôles, sources, sons) sont des points de départ choisis sans mesure sur de vraies prises** (tableaux dans `Correction.cpp`). Les rôles Arpège, Snare, Hits et Nappe et les décalages de source et de son sont les plus devinés. C'est le premier réglage à affiner à l'oreille. Pour cela il faut des pistes isolées de Pierre : ses 7 morceaux analysés (13 novembre 2026) sont des mixes complets, inutilisables pour caler une cible par instrument.
- **Résonances et persistance** : une bosse du spectre moyen n'est retenue que si elle est aussi apparue dans au moins 80 % des trames de l'écoute (`seuilPersistance` dans `Correction.cpp`). Constat sur 7 mixes de Pierre (13 novembre 2026) : sans ce filtre, le détecteur trouvait des « résonances » dans presque toutes les fenêtres de 10 s, mais ces bosses n'étaient présentes que dans environ la moitié des trames : c'étaient des notes jouées. Le seuil de 80 % est une valeur de départ, **à confirmer sur une vraie prise**. Limite restante : une note tenue pendant toute l'écoute (bourdon, pad) reste prise pour une résonance. Recherche désactivée pour la basse et le kick.
- Le coupe-bas n'est plus réglable à la main : il dépend du rôle et de la source. Les paramètres de l'étape 1 (`coupeBasActif`, `coupeBasFreq`) n'existent plus ; à l'ouverture d'un projet de l'étape 1 seul l'interrupteur est repris (la fréquence réglée à la main est perdue).
- La fenêtre (trois menus) n'a pas été vue à l'écran : mise en page à vérifier dans FL Studio.

Outils nécessaires (installés sur ce poste avec winget) : Visual Studio Build Tools 2022 (charge de travail « Développement Desktop en C++ ») et CMake 3.22 ou plus. Dans un terminal ouvert avant l'installation, `cmake` n'est pas dans le PATH : il est dans `C:\Program Files\CMake\bin`.

```powershell
.\plugin\tester.ps1
.\plugin\compiler.ps1
.\plugin\installer.ps1
```

Les tests vérifient des propriétés mesurables (réponse des filtres, bornes de la correction, neutralité à 0 %, déroulement d'une écoute, sauvegarde). Ils ne disent rien de la qualité sonore. Les relancer après toute modification du moteur.

Fermer FL Studio avant d'installer (un plugin chargé est verrouillé). Ce dossier est l'emplacement VST3 standard : dans FL Studio, il suffit de relancer la recherche (Options > Manage plugins > Find installed plugins).

Règles du code audio : pas d'allocation mémoire ni d'attente sur un verrou dans `processBlock` (le calcul de la correction se fait sur le fil des messages, puis passe au fil audio par un verrou qu'il ne fait qu'essayer) ; les textes accentués passent par `rogus::fr` (décodage UTF-8).

**Règle de compatibilité (exigence de Pierre) : une nouvelle version doit toujours reprendre les réglages enregistrés par la précédente.** Charger la nouvelle version dans un projet existant ne doit rien faire perdre.

- Ne jamais supprimer ni renommer un identifiant de paramètre (`role`, `source`, `son`, `correctionActive`, `quantite`), ni les codes du plugin (`Rogs` / `Rgs1`).
- Les listes de choix ne s'enregistrent que par leur numéro : on n'en réordonne ni n'en retire jamais une entrée ; les nouvelles s'ajoutent **à la fin**.
- Si un changement incompatible est vraiment nécessaire : augmenter `versionEtat` dans `PluginProcessor.cpp`, écrire la conversion depuis la version précédente dans `migrerEtat`, et ajouter un test qui fabrique un faux ancien état (voir `etatEtape2` dans `Tests/tests.cpp`) et vérifie ce qui est repris. Les tests doivent échouer si on retire la conversion.
- Formats déjà connus : étape 1 et étape 2 (sans numéro de version, converties à l'ouverture), version 3 actuelle (trois menus). L'ancien rôle de l'étape 2 (6 choix) est converti en rôle + source.
- Ce n'est vérifié que par les tests, sur des états fabriqués : **pas encore sur un vrai projet FL enregistré avec l'étape 2**.

**Licence** : JUCE est sous AGPL v3 (ou licence commerciale). Sans conséquence en usage personnel, contraignant en cas de distribution.

Points techniques à traiter à l'étape 3 : synchroniser les instances sur la position de lecture de l'hôte (et non l'horloge), pour que l'export hors temps réel donne le même résultat que l'écoute.

Abandonné pour l'instant : profils de style et détection de genre pour le mastering, interface web locale.

## Conventions

- Code, commentaires et messages utilisateur en français.
- Commentaires pédagogiques : expliquer le pourquoi des étapes audio.
- Dépôt git local (branche `main`). Le dossier `plugin/build/` et les fichiers audio ne sont pas suivis.
