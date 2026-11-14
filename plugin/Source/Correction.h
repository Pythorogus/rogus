#pragma once

#include <array>
#include <vector>
#include <juce_core/juce_core.h>
#include "Textes.h"

// Cœur de l'EQ fixe par rôle, sans dépendance au plugin ni à l'interface :
//   - la table des rôles (ce qu'on attend de chaque instrument) ;
//   - l'analyse d'une écoute (spectre moyen -> mesure) ;
//   - le calcul de la correction (mesure + rôle -> réglages de filtres) ;
//   - les filtres eux-mêmes, partagés par l'audio, l'affichage et les tests.
// Tout ce qui est marqué noexcept ci-dessous n'alloue pas de mémoire et peut
// donc être appelé depuis le fil audio.
namespace rogus
{

// Les textes accentués doivent être décodés explicitement en UTF-8.

// --- Découpage en fréquences -------------------------------------------------

// Sept bandes larges espacées d'une octave. L'oreille juge l'équilibre d'un
// instrument à cette échelle : plus fin, on corrigerait les notes jouées
// plutôt que le timbre.
constexpr int nbBandes = 7;
inline constexpr std::array<double, nbBandes> frequencesBandes
    { 100.0, 200.0, 400.0, 800.0, 1600.0, 3200.0, 6400.0 };

constexpr int nbResMax  = 3;                              // résonances atténuées au maximum
constexpr int nbFiltres = 1 + nbBandes + nbResMax;        // coupe-bas + bandes + résonances

// Analyse : 8192 points donnent une précision d'environ 6 Hz à 48 kHz,
// nécessaire pour distinguer les bandes dans le grave.
constexpr int ordreFft  = 13;
constexpr int tailleFft = 1 << ordreFft;

// --- Rôles -------------------------------------------------------------------

// Trois choix indépendants décrivent une piste :
//   - le RÔLE : sa fonction dans le morceau (lead, rythmique, basse...). Il donne
//     la zone de fréquences à garder lisible et l'équilibre de base visé ;
//   - la SOURCE : ce qui produit le son (guitare, voix, synthé...). Elle décide
//     de la correction fixe : un instrument enregistré a des défauts (grondement,
//     résonances du corps ou de l'ampli) qu'un synthé n'a pas ;
//   - le SON : le caractère voulu (clair, saturé, sombre, brillant). Il déplace
//     la cible pour que le plugin ne « corrige » pas ce qui est voulu.

using Equilibre = std::array<float, nbBandes>;      // dB par bande d'octave

// Réglages du compresseur propres à un rôle (étape 4).
struct ReglageCompression
{
    float ratio;                            // 3 = au-dessus du seuil, 3 dB en entrée n'en donnent plus qu'1
    float attaqueMs;                        // temps avant d'agir : long, il laisse passer l'attaque des notes
    float relachementMs;                    // temps pour relâcher
    float profondeurDb;                     // le seuil est placé à cette distance sous les passages forts
};

struct Role
{
    const char* nom;
    float coupeBasHz;                       // en dessous, rien d'utile pour ce rôle
    float importantMin, importantMax;       // zone où la piste doit rester lisible
    Equilibre cible;                        // équilibre visé
    bool resonances;                        // chercher des résonances étroites ?
    int priorite;                           // priorité par défaut dans le mix, de 1 (fond) à 5 (devant)
    ReglageCompression compression;
};

struct Source
{
    const char* nom;
    float coupeBasHz;                       // coupe-bas propre à la source
    bool resonances;                        // cette source a-t-elle des résonances à corriger ?
    Equilibre decalage;                     // ajustement de la cible, en dB
};

struct Son
{
    const char* nom;
    Equilibre decalage;                     // ajustement de la cible, en dB
};

constexpr int nbRoles = 9, nbSources = 6, nbSons = 7;
const Role& role (int index) noexcept;
const Source& source (int index) noexcept;
const Son& son (int index) noexcept;
juce::StringArray nomsRoles();
juce::StringArray nomsSources();
juce::StringArray nomsSons();

// --- Données -----------------------------------------------------------------

// Histogramme des niveaux de la piste : la part du temps passée à chaque niveau
// (48 cases de 1,5 dB, de -72 à 0 dBFS). Il dit à quel point la piste est
// dynamique, donc où placer le seuil du compresseur et combien de volume rendre.
constexpr int nbHisto = 48;
constexpr float histoMinDb = -72.0f, histoPasDb = 1.5f;
using Histogramme = std::array<float, nbHisto>;

inline float niveauDeLaCase (int k) noexcept { return histoMinDb + ((float) k + 0.5f) * histoPasDb; }
inline int caseDuNiveau (float db) noexcept
{
    const int k = (int) ((db - histoMinDb) / histoPasDb);
    return k < 0 ? 0 : (k >= nbHisto ? nbHisto - 1 : k);
}

// Ce qu'une écoute a appris sur la piste. Indépendant du rôle : on peut
// changer de rôle après coup sans réécouter.
struct Mesure
{
    bool valide = false;
    std::array<float, nbBandes> niveaux {};      // dB par bande d'octave
    int nbRes = 0;
    std::array<float, nbResMax> resHz {};
    std::array<float, nbResMax> resExces {};     // dépassement en dB au-dessus du voisinage

    // Niveau moyen de la piste dans chaque bande pendant qu'elle joue (dB),
    // mesuré avec les mêmes filtres que le démasquage. Il sert de repère pour
    // savoir, en direct, si la piste « joue » dans une bande. Absent des écoutes
    // faites avant l'étape 3 : la piste ne peut alors pas demander de place.
    bool refValide = false;
    std::array<float, nbBandes> refDb {};

    // Histogramme des niveaux (somme = 1), pour le compresseur. Absent des écoutes
    // faites avant l'étape 4 : la piste n'est alors pas compressée.
    bool histoValide = false;
    Histogramme histo {};
};

// Le compresseur tel qu'il est réglé pour cette piste (à quantité 100 %).
struct Compression
{
    bool valide = false;                         // faux sans histogramme : pas de compression
    float seuilDb = 0.0f;
    float ratio = 1.0f;
    float attaqueMs = 10.0f;
    float relachementMs = 100.0f;
};

// Les réglages de filtres. Calculés pour une quantité de 100 %.
struct Correction
{
    float coupeBasHz = 10.0f;                    // 10 Hz = coupe-bas inaudible
    std::array<float, nbBandes> gains {};        // dB
    int nbRes = 0;
    std::array<float, nbResMax> resHz {};
    std::array<float, nbResMax> resGain {};      // dB, toujours négatifs

    // Compensation de volume. Le volume perçu dépend surtout de LÀ OÙ se trouve
    // l'énergie de la piste : baisser une bande riche et en remonter une pauvre
    // fait baisser le volume, même si les gains « s'annulent » en moyenne.
    // `poids` donne la part d'énergie de la piste dans chaque bande (somme = 1,
    // tout à zéro = pas de mesure) ; `compensationDb` est le gain global qui en
    // découle, calculé par doser(). Un simple gain : la forme de la courbe ne change pas.
    std::array<float, nbBandes> poids {};
    float compensationDb = 0.0f;

    // Repère de niveau de la piste (copie de la mesure), pour le démasquage.
    bool refValide = false;
    std::array<float, nbBandes> refDb {};

    // Compresseur (étape 4) et histogramme dont il a besoin pour rendre le volume.
    Compression compression;
    Histogramme histo {};
};

// Un filtre du 2e ordre, déjà normalisé (a0 = 1).
struct Coefs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

using Banc = std::array<Coefs, nbFiltres>;

// --- Fonctions ---------------------------------------------------------------

// Applique le bouton « Quantité » (0 à 1) à une correction.
Correction doser (const Correction&, float quantite) noexcept;

// Traduit une correction en filtres. Une seule fonction pour l'audio et pour
// la courbe affichée : ce qu'on voit est exactement ce qu'on entend.
Banc construireFiltres (const Correction&, double frequenceEch) noexcept;

// Gain total du banc de filtres à une fréquence donnée, en dB.
double reponseDb (const Banc&, double frequenceHz, double frequenceEch) noexcept;

// --- Persistance des bosses ---------------------------------------------------
// Une vraie résonance est présente en permanence, quelle que soit la note jouée.
// Une note de la mélodie, elle, n'est là que par moments. Pour les distinguer,
// l'écoute compte, pour chaque fréquence de la grille ci-dessous, dans combien
// de trames une bosse est apparue.

// Grille de recherche : 112 points espacés d'un 1/24 d'octave, de 200 Hz à 5 kHz.
constexpr int nbGrille = 112;
double frequenceGrille (int index) noexcept;

using Compteurs = std::array<int, nbGrille>;

// Examine UNE trame (sommes cumulées de son spectre de puissance, nbCases + 1
// valeurs, la première étant 0) et incrémente les compteurs des points où une
// bosse apparaît. Sans allocation : appelée depuis le fil audio.
void marquerBosses (const double* cumul, int nbCases, double frequenceEch, Compteurs& compteurs) noexcept;

// Spectre de puissance moyen (tailleFft / 2 + 1 valeurs) -> mesure.
// Avec les compteurs de l'écoute, seules les bosses présentes dans la grande
// majorité des trames sont retenues comme résonances.
Mesure analyser (const std::vector<double>& spectre, double frequenceEch,
                 const Compteurs* compteurs = nullptr, int nbTrames = 0);

// Mesure + rôle + source + son -> correction. Sans mesure valide : seulement
// le coupe-bas, le plus haut de celui du rôle et de celui de la source.
Correction calculerCorrection (const Role&, const Source&, const Son&, const Mesure&);

// --- Compression (étape 4) -----------------------------------------------------
// Un compresseur réduit l'écart entre les passages forts et les passages faibles
// d'une piste : elle reste mieux à sa place dans le mix. Au-dessus d'un seuil,
// il baisse le son selon un ratio ; puis il remonte le tout pour garder le volume.

// Largeur du « genou » : la zone autour du seuil où la compression s'installe
// en douceur au lieu de démarrer d'un coup.
constexpr float genouDb = 6.0f;

// Gain du compresseur (dB, <= 0) pour un niveau d'entrée donné, à quantité 100 %.
float gainCompression (const Compression&, float niveauDb) noexcept;

// Réglages du rôle + histogramme de la piste -> compresseur. Le seuil est posé
// sous les passages forts de la piste (son 90e centile), pas à une valeur fixe :
// une piste enregistrée bas est compressée comme une piste enregistrée fort.
Compression calculerCompression (const ReglageCompression&, const Mesure&) noexcept;

// Gain à rendre (dB) pour que la piste garde son énergie malgré la compression,
// d'après son histogramme. Un simple gain fixe : il ne « pompe » pas.
float rattrapageCompression (const Compression&, const Histogramme&, float quantite) noexcept;

// --- Démasquage dynamique (étape 3) -------------------------------------------
// Quand deux pistes jouent dans la même zone de fréquences, la moins prioritaire
// cède un peu de place à l'autre, seulement dans cette zone et seulement pendant
// que l'autre joue. Les fonctions ci-dessous sont les briques de ce calcul.

using Reductions = std::array<float, nbBandes>;      // dB par bande, toujours <= 0
using BancBandes = std::array<Coefs, nbBandes>;

// Réduction maximale d'une bande, à quantité 100 % et écart de priorité maximal.
constexpr float profondeurMaxDb = 6.0f;

// Sept passe-bande d'une octave : ils mesurent en direct l'énergie de la piste par bande.
BancBandes construireFiltresAnalyse (double frequenceEch) noexcept;

// Les sept filtres qui appliquent la réduction (mêmes formes que la correction fixe).
BancBandes construireFiltresDynamiques (const Reductions&, double frequenceEch) noexcept;
double reponseDb (const BancBandes&, double frequenceHz, double frequenceEch) noexcept;

// Part de chaque bande dans la zone « à garder lisible » d'un rôle : 1 dans la
// zone, puis décroît jusqu'à 0 une demi-octave plus loin.
Equilibre poidsZone (const Role&) noexcept;

// La piste joue-t-elle dans cette bande ? 0 = non (20 dB sous son niveau habituel),
// 1 = pleinement (à moins de 8 dB de son niveau habituel).
float activite (float niveauDb, float refDb) noexcept;

// Poids de l'écart de priorité : 0 si la piste qui demande n'est pas strictement
// plus prioritaire, puis 0,25 par cran d'écart.
float facteurPriorite (int prioriteQuiDemande, int prioriteQuiCede) noexcept;

// Réduction (dB, <= 0) qu'une piste applique dans une bande pour une autre piste.
float reductionBande (float poidsZone, float activite, int prioriteQuiDemande,
                      int prioriteQuiCede, float quantite) noexcept;

} // namespace rogus
