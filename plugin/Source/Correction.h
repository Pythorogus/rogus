#pragma once

#include <array>
#include <vector>
#include <juce_core/juce_core.h>

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
inline juce::String fr (const char* texte) { return juce::String::fromUTF8 (texte); }

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

struct Role
{
    const char* nom;
    float coupeBasHz;                       // en dessous, rien d'utile pour ce rôle
    float importantMin, importantMax;       // zone où la piste doit rester lisible
    Equilibre cible;                        // équilibre visé
    bool resonances;                        // chercher des résonances étroites ?
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

constexpr int nbRoles = 9, nbSources = 6, nbSons = 4;
const Role& role (int index) noexcept;
const Source& source (int index) noexcept;
const Son& son (int index) noexcept;
juce::StringArray nomsRoles();
juce::StringArray nomsSources();
juce::StringArray nomsSons();

// --- Données -----------------------------------------------------------------

// Ce qu'une écoute a appris sur la piste. Indépendant du rôle : on peut
// changer de rôle après coup sans réécouter.
struct Mesure
{
    bool valide = false;
    std::array<float, nbBandes> niveaux {};      // dB par bande d'octave
    int nbRes = 0;
    std::array<float, nbResMax> resHz {};
    std::array<float, nbResMax> resExces {};     // dépassement en dB au-dessus du voisinage
};

// Les réglages de filtres. Calculés pour une quantité de 100 %.
struct Correction
{
    float coupeBasHz = 10.0f;                    // 10 Hz = coupe-bas inaudible
    std::array<float, nbBandes> gains {};        // dB
    int nbRes = 0;
    std::array<float, nbResMax> resHz {};
    std::array<float, nbResMax> resGain {};      // dB, toujours négatifs
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

} // namespace rogus
