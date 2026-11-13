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

**Étape 2 écrite, compilée, testée sur signaux de synthèse, puis écoutée par Pierre** (13 novembre 2026) : sur « Can of Monster », Rogus posé sur toutes les pistes avec leurs rôles, à volume égal (export sans Rogus −27,00 LUFS, avec −26,98), il préfère la version avec Rogus. L'effet sur le morceau complet est discret (au plus environ 1 dB par bande, quantité à 60 %). Validé sur un seul morceau : les cibles restent à affiner sur d'autres prises.

**Étape 3 écrite, compilée, testée hors FL Studio et installée** (13 novembre 2026). Vérifié dans FL Studio par une capture d'écran de Pierre (« Can of Monster », 10 instances) : la fenêtre s'affiche correctement, les instances se voient (« 9 autres pistes Rogus visibles »), et le démasquage agit (une guitare rythmique « cède 1,3 dB vers 100 Hz à : Kick »). **Pas encore vérifié** : que FL fournit bien la position de lecture (identité écoute / export), et le jugement à l'oreille. Voir « Fonctionnement du démasquage » plus bas.

Constat sur cette même capture (guitare électrique claire, rôle Rythmique, quantité 60 %) : la correction fixe monte le grave de +5,5 dB vers 60 Hz et l'aigu de +6 dB au-dessus de 10 kHz, les médiums restant à 0. Elle était donc en butée aux deux bouts. **Corrigé pour le grave** le même jour (`calculerCorrection`) : on ne monte plus une bande située à moins d'une octave au-dessus du coupe-bas (bord de la zone utile : pour une guitare, la bande de 100 Hz ne contient presque que de la ronflette), ni une bande presque vide. « Ne pas monter » se juge **après** la compensation de volume : ces bandes sont plafonnées au gain moyen de la piste pondéré par l'énergie, sinon la compensation les remonte quand même (c'était la cause des +5,5 dB). Basse et kick (coupe-bas à 30 Hz) ne sont pas concernés. **Reste ouvert** : la montée de l'aigu (+6 dB au-dessus de 10 kHz) n'est pas touchée, en attente du jugement de Pierre à l'oreille ; et cette butée montre que la cible « Rythmique » est loin de sa guitare claire.

**Étape 4 écrite, compilée, testée sur signaux de synthèse, installée puis écoutée par Pierre** (14 novembre 2026) : compression par rôle. Chaque piste est écoutée comme pour l'EQ ; l'écoute remplit en plus un histogramme des niveaux, qui sert à poser le seuil sous les passages forts de la piste. Deux réglages s'ajoutent : `compressionActive` (**non** par défaut : elle est surtout utile sur la voix et la basse, et se coche au cas par cas) et `compressionQuantite` (50 %). Les tests passent : à 100 %, un écart de 12 dB entre passages forts et faibles devient 7,7 dB, et le volume global ne bouge que de −0,46 dB. **Écoute du 14 novembre 2026 : sur la basse, Pierre préfère avec la compression.** La voix et les autres rôles n'ont pas encore été écoutés : les réglages par rôle restent des points de départ. La première version ne compressait presque pas : son détecteur lisait l'échantillon isolé au lieu de l'enveloppe de crête (la même que celle de l'écoute), il se croyait donc 6 à 8 dB sous le seuil ; corrigé avant l'écoute. Voir « Fonctionnement de la compression » plus bas.

| Fichier | Rôle |
|---|---|
| `CMakeLists.txt` | projet CMake ; télécharge JUCE 8.0.9 (version figée) à la première configuration |
| `Source/Correction.*` | cœur sans dépendance au plugin : tables des rôles, sources et sons, analyse d'une écoute, calcul de la correction, filtres, briques du démasquage |
| `Source/Orchestre.*` | mémoire partagée entre les instances du plugin : chacune y annonce ce qu'elle joue |
| `Source/PluginProcessor.*` | moteur audio : écoute de la piste (spectre et dynamique), filtrage, compression, annonces et démasquage, sauvegarde |
| `Source/PluginEditor.*` | fenêtre : trois menus, courbe appliquée, correction fixe, compression, démasquage, textes d'état |
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
5. **Compensation de volume** : un gain global (`compensationDb`, calculé dans `doser()`, plafonné à ±6 dB) rend exactement ce que la correction retire ou ajoute en énergie, en pondérant chaque bande par la part d'énergie de la piste écoutée (`poids`, déduits de la mesure : rien de plus à enregistrer dans le projet). Ce n'est qu'un gain : la forme de la courbe ne change pas. Raison : la moyenne des gains en dB retirée dans `calculerCorrection` ne conserve PAS le volume, car il dépend de l'endroit où est l'énergie. Mesuré le 13 novembre 2026 sur un export de « Can of Monster » avec Rogus sur toutes les pistes, avant la compensation : −1,8 LU en moyenne, de −0,4 dB (passages forts) à −3,4 dB (passages calmes) ; l'écart variant d'une piste à l'autre, il déplaçait aussi l'équilibre entre pistes. Après compensation, les tests (bruit en bandes d'octave, un cas plus dur que la musique) donnent moins de 0,5 dB d'écart, contre 1,1 à 3,4 dB sans. Limites : l'énergie sous 70 Hz et au-dessus de 9 kHz n'est pas pesée (sept bandes seulement) ; le calcul est en énergie, pas en sonie perçue ; une encoche de résonance très étroite est mal vue par l'évaluation en trois points par bande.

La mesure est enregistrée dans le projet de l'hôte, pas la correction : changer de rôle, de source ou de son, ou améliorer le calcul, ne demande pas de réécoute. La courbe affichée utilise les mêmes filtres que l'audio.

Fonctionnement du démasquage (étape 3) :

1. **Annonce** : chaque instance mesure en continu l'énergie de sa piste (entrée, avant correction) dans les sept bandes, par « ticks » de 1024 échantillons. Pour chaque bande elle publie une *activité* de 0 à 1 : la piste joue-t-elle là, par rapport à son propre niveau habituel (0 à 20 dB en dessous, 1 à moins de 8 dB) ? Ce niveau habituel (`refDb`) vient de l'écoute : **une piste jamais écoutée, ou écoutée avant l'étape 3, n'annonce rien et ne reçoit donc aucune place**.
2. **Orchestre** (`Orchestre.*`) : un tableau de 64 places partagé par toutes les instances chargées dans le même programme, lu et écrit sans verrou (atomiques). Aucun routage dans l'hôte.
3. **Décision** : une piste ne cède qu'aux pistes **strictement plus prioritaires**, dans les bandes de leur zone « à garder lisible » (celle du rôle), et seulement pendant qu'elles y jouent. Réduction = 6 dB au plus × quantité × 0,25 par cran d'écart de priorité × activité × poids de la zone. Plusieurs demandes sur une bande : la plus forte, pas la somme. À priorité égale personne ne cède.
4. **Priorité** : menu Auto (celle du rôle) ou 1 à 5. Par défaut : Kick 5, Voix 5, Lead 4, Snare 4, Basse 4, Rythmique 3, Arpège 3, Hits 2, Nappe 1. Kick devant basse dans la bande de 100 Hz = le « sidechain » sans routage.
5. **Application** : sept filtres (mêmes formes que la correction fixe) après la correction fixe. **Réactivité** : attaque de 200 à 15 ms, relâchement de 800 à 80 ms. Pas de compensation de volume ici : la baisse est voulue.
6. **Même résultat à l'écoute et à l'export** : les ticks sont numérotés d'après la position de lecture de l'hôte, et une instance ne lit que le tick terminé avant le début de son bloc. L'ordre de traitement des pistes et la taille des blocs ne changent donc rien (testé : sortie identique à l'échantillon près pour l'ordre inversé et pour des blocs de 256 contre 512 ; 0,1 dB près pour des blocs de 480, non alignés sur les ticks). Si l'hôte ne donne pas la position (ou lecture arrêtée) : repli sur la dernière annonce de chaque piste, ignorée après 250 ms, sans garantie d'identité à l'export.
7. **Paramètres ajoutés** (à la suite, valeurs par défaut pour les anciens projets) : `priorite` (Auto), `demasquageActif` (oui), `demasquageQuantite` (50 %), `reactivite` (50 %). La mesure gagne les champs `ref0` à `ref6`, facultatifs : pas de changement de `versionEtat`.

Fonctionnement de la compression (étape 4) :

1. **Mesure de la dynamique** : pendant l'écoute, chaque échantillon met à jour une enveloppe de crête (montée immédiate, descente en 100 ms). Une valeur sur 32 est rangée dans un histogramme de 48 cases de 1,5 dB, de −72 à 0 dBFS ; le silence sous −55 dBFS n'est pas compté. L'histogramme part avec la mesure dans le projet de l'hôte.
2. **Seuil** : le 90e centile de l'histogramme donne le niveau des passages forts. Le seuil est posé `profondeurDb` sous ce niveau (4 à 8 dB selon le rôle). Une piste enregistrée bas est donc compressée comme une piste enregistrée fort : le réglage est relatif, pas une valeur absolue.
3. **Courbe** : genou de 6 dB centré sur le seuil, puis le ratio du rôle ; en dessous du genou, rien n'est changé. La quantité (0 à 100 %) dose la réduction.
4. **Détecteur** : le niveau est lu par une enveloppe de crête, **la même que celle de l'écoute**. C'est indispensable : avec un niveau plus lissé, le compresseur se croit 6 à 8 dB sous le seuil et ne fait presque rien (c'était le défaut de la première version). L'attaque et le relâchement du rôle lissent le **gain**, pas l'enveloppe : une attaque lente laisse passer le début des notes (le claquement du kick), un relâchement lent fait revenir le son en douceur.
5. **Volume rendu** : un gain fixe, calculé d'après l'histogramme, rend l'énergie retirée (plafonné à 12 dB, lissé sur 50 ms). Il ne « pompe » pas : c'est le compresseur qui bouge, pas lui. L'histogramme étant mesuré avant la correction fixe, la compensation globale de celle-ci est retirée du niveau lu ; l'effet spectral de l'EQ, lui, n'est pas compensé.
6. **Place dans le trajet** : correction fixe → compression → démasquage. La place cédée aux autres pistes n'est donc pas « rattrapée » par le gain rendu.
7. **Écoute obligatoire** : sans histogramme (piste jamais écoutée, ou projet d'avant l'étape 4), la compression ne fait rien, même cochée. Une nouvelle écoute la rend possible ; l'interrupteur reste à cocher, et il est **à l'arrêt par défaut**.
8. **Paramètres ajoutés** (à la suite) : `compressionActive` (non), `compressionQuantite` (50 %). La mesure gagne les champs `histo0` à `histo47`, facultatifs : pas de changement de `versionEtat`. Raison du défaut à l'arrêt : la compression est surtout utile sur la voix et la basse ; sur une guitare déjà compressée par l'ampli ou sur des drums dont on veut garder les transitoires, elle peut faire plus de mal que de bien.

Réglages par rôle (table `tableRoles` de `Correction.cpp`) : la basse est validée à l'oreille ; les autres restent à affiner.

| Rôle | Ratio | Attaque | Relâchement | Seuil sous les passages forts |
|---|---|---|---|---|
| Lead | 3 | 15 ms | 150 ms | 6 dB |
| Rythmique | 2,5 | 20 ms | 150 ms | 5 dB |
| Arpège | 2,5 | 15 ms | 200 ms | 6 dB |
| Basse | 4 | 25 ms | 200 ms | 8 dB |
| Kick | 4 | 30 ms | 100 ms | 4 dB |
| Snare | 3 | 20 ms | 120 ms | 4 dB |
| Hits | 2 | 10 ms | 150 ms | 4 dB |
| Voix | 3 | 10 ms | 120 ms | 8 dB |
| Nappe | 2 | 40 ms | 300 ms | 5 dB |

Limites connues de l'étape 3 :

- Les instances se voient dans FL Studio tant qu'elles sont chargées normalement. Si le texte d'état affichait « aucune autre piste Rogus visible » avec plusieurs instances (plugin « bridgé » par FL, par exemple), elles seraient isolées dans des programmes séparés et il faudrait une vraie communication entre programmes.
- Le plugin ne voit pas les faders du mixeur (il est placé avant) : il sait qu'une piste joue, pas à quel volume elle sort. La priorité remplace cette information.
- Réglages devinés, à affiner à l'oreille : profondeur maximale (6 dB), seuils d'activité (−20 / −8 dB), temps d'attaque et de relâchement, priorités par défaut, zones des rôles.
- Il faut refaire une écoute sur chaque piste pour qu'elle soit prise en compte.
- Dans les tests, plusieurs instances coexistent dans le même programme et se voient : garder des priorités égales entre elles, ou les détruire, pour qu'un test n'en influence pas un autre.

Limites connues de l'étape 4 :

- **Réglages par rôle partiellement devinés** : la basse est validée à l'oreille. Ratio, attaque, relâchement et profondeur des autres rôles n'ont pas encore été validés ; la voix est le prochain cas à écouter.
- Compresseur simple, large bande, sans lookahead : un seul seuil et un seul ratio par piste. Il ne remplace pas un compresseur multibande sur une basse ou une voix difficile.
- Le niveau est une enveloppe de crête : un son très percussif fait baisser le gain à chaque coup. C'est voulu, mais peut s'entendre sur une piste déjà compressée.
- Le relâchement effectif est un peu plus long que celui du rôle : l'enveloppe de niveau retombe en 100 ms **et** le gain a son propre relâchement. À affiner à l'écoute.
- Le volume rendu est calculé en énergie, pas en sonie perçue, et seule la compensation globale de l'EQ est retirée du niveau : le volume peut bouger de quelques dixièmes de dB sur un morceau réel.
- L'histogramme ne compte pas le silence sous −55 dBFS : une piste très faible est mesurée sur peu d'événements, et une piste entièrement sous ce seuil n'a pas de compression.
- La fenêtre et la compression ont été vérifiées dans FL Studio sur la basse. La voix, les autres rôles et l'identité écoute / export ne sont pas encore vérifiés.

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

Les tests vérifient des propriétés mesurables (réponse des filtres, bornes de la correction, neutralité à 0 %, déroulement d'une écoute, sauvegarde, courbe et volume du compresseur). Ils ne disent rien de la qualité sonore. Les relancer après toute modification du moteur.

Fermer FL Studio avant d'installer (un plugin chargé est verrouillé). Ce dossier est l'emplacement VST3 standard : dans FL Studio, il suffit de relancer la recherche (Options > Manage plugins > Find installed plugins).

Règles du code audio : pas d'allocation mémoire ni d'attente sur un verrou dans `processBlock` (le calcul de la correction se fait sur le fil des messages, puis passe au fil audio par un verrou qu'il ne fait qu'essayer) ; les textes accentués passent par `rogus::fr` (décodage UTF-8).

**Règle de compatibilité (exigence de Pierre) : une nouvelle version doit toujours reprendre les réglages enregistrés par la précédente.** Charger la nouvelle version dans un projet existant ne doit rien faire perdre.

- Ne jamais supprimer ni renommer un identifiant de paramètre (`role`, `source`, `son`, `correctionActive`, `quantite`, `priorite`, `demasquageActif`, `demasquageQuantite`, `reactivite`, `compressionActive`, `compressionQuantite`), ni les codes du plugin (`Rogs` / `Rgs1`).
- Les listes de choix ne s'enregistrent que par leur numéro : on n'en réordonne ni n'en retire jamais une entrée ; les nouvelles s'ajoutent **à la fin**.
- Si un changement incompatible est vraiment nécessaire : augmenter `versionEtat` dans `PluginProcessor.cpp`, écrire la conversion depuis la version précédente dans `migrerEtat`, et ajouter un test qui fabrique un faux ancien état (voir `etatEtape2` dans `Tests/tests.cpp`) et vérifie ce qui est repris. Les tests doivent échouer si on retire la conversion.
- Formats déjà connus : étape 1 et étape 2 (sans numéro de version, converties à l'ouverture), version 3 actuelle (trois menus). L'ancien rôle de l'étape 2 (6 choix) est converti en rôle + source.
- Ce n'est vérifié que par les tests, sur des états fabriqués : **pas encore sur un vrai projet FL enregistré avec l'étape 2**.

**Licence** : JUCE est sous AGPL v3 (ou licence commerciale). Sans conséquence en usage personnel, contraignant en cas de distribution.

Abandonné pour l'instant : profils de style et détection de genre pour le mastering, interface web locale.

## Conventions

- Code, commentaires et messages utilisateur en français.
- Commentaires pédagogiques : expliquer le pourquoi des étapes audio.
- Dépôt git local (branche `main`). Le dossier `plugin/build/` et les fichiers audio ne sont pas suivis.
