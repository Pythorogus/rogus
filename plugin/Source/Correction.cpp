#include "Correction.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace rogus
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    // Équilibres visés : niveau par bande d'octave, en dB, pour
    // 100 / 200 / 400 / 800 / 1600 / 3200 / 6400 Hz. Seule la forme compte
    // (les écarts entre bandes), pas le niveau absolu.
    //
    // ATTENTION : ce sont des points de départ raisonnables, pas des vérités.
    // Ils sont à affiner à l'oreille sur les prises de Pierre.
    // Rôles : la fonction de la piste dans le mix.
    // Le dernier nombre est la priorité par défaut (1 = fond, 5 = devant) : à
    // priorité égale personne ne cède. Le kick passe devant la basse dans le
    // grave (le « sidechain » classique), la voix devant le lead.
    const std::array<Role, nbRoles> tableRoles
    {{
        // Lead : peu de grave (il laisse la place à la basse et à la rythmique),
        // l'essentiel entre 800 Hz et 3 kHz, là où la mélodie se détache.
        { "Lead",       100.0f, 1000.0f,  4000.0f, { -14.0f,  -6.0f,  -2.0f,   0.0f,   0.0f,  -2.0f,  -8.0f }, true,  4 },
        // Rythmique : le corps (200 à 800 Hz) porte le morceau, les aigus restent en retrait.
        { "Rythmique",   80.0f,  200.0f,  2000.0f, {  -8.0f,  -2.0f,   0.0f,  -1.0f,  -3.0f,  -6.0f, -12.0f }, true,  3 },
        // Arpège : entre les deux. Il doit rester net (attaque des notes) sans prendre le corps de la rythmique.
        { "Arpège",      90.0f,  500.0f,  3500.0f, { -12.0f,  -4.0f,  -1.0f,   0.0f,  -1.0f,  -3.0f,  -9.0f }, true,  3 },
        // Basse et kick : l'énergie est dans le grave et décroît régulièrement.
        // Pas de recherche de résonances : leur note fondamentale ressemble
        // à une résonance, et il ne faut surtout pas l'atténuer.
        { "Basse",       30.0f,   40.0f,   250.0f, {   0.0f,  -3.0f,  -8.0f, -13.0f, -19.0f, -26.0f, -34.0f }, false, 4 },
        { "Kick",        30.0f,   40.0f,   120.0f, {   0.0f,  -6.0f, -12.0f, -16.0f, -19.0f, -21.0f, -26.0f }, false, 5 },
        // Snare : le corps vers 200 Hz et le claquement des timbres entre 2 et 5 kHz.
        { "Snare",       80.0f,  150.0f,  5000.0f, { -12.0f,  -2.0f,  -5.0f,  -7.0f,  -6.0f,  -4.0f,  -6.0f }, true,  4 },
        // Hits (percussions, cymbales, samples) : brillants, sans grave utile. Pas de
        // recherche de résonances : le timbre métallique d'une cymbale en est justement fait.
        { "Hits",       150.0f, 3000.0f, 10000.0f, { -16.0f, -10.0f,  -8.0f,  -6.0f,  -4.0f,  -2.0f,  -3.0f }, false, 2 },
        // Voix : le corps vers 200-400 Hz, l'intelligibilité entre 1 et 5 kHz.
        { "Voix",        90.0f, 1000.0f,  5000.0f, { -12.0f,  -3.0f,   0.0f,  -1.0f,  -4.0f,  -7.0f, -11.0f }, true,  5 },
        // Nappe (pads, synthés de fond) : doit rester en retrait, sans grave qui
        // encombre la basse. Pas de recherche de résonances : une note tenue
        // pendant toute l'écoute ressemblerait à une résonance.
        { "Nappe",      120.0f,  300.0f,  2000.0f, { -10.0f,  -3.0f,   0.0f,  -2.0f,  -5.0f,  -8.0f, -13.0f }, false, 1 },
    }};

    // Sources : ce qui produit le son, donc les défauts possibles.
    // Le coupe-bas final est le plus haut de celui du rôle et de celui de la source.
    const std::array<Source, nbSources> tableSources
    {{
        { "Guitare électrique",  70.0f, true,  {  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f } },
        // Acoustique : moins de médiums que l'électrique, aigus plus ouverts (attaque des cordes).
        { "Guitare acoustique",  80.0f, true,  {  0.0f, -1.0f, -3.0f, -4.0f, -4.0f, -3.0f,  0.0f } },
        { "Voix",                90.0f, true,  {  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f } },
        { "Basse",               25.0f, false, {  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f } },
        { "Batterie",            30.0f, true,  {  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f } },
        // Synthé : signal numérique, ni grondement de micro ni résonance de caisse.
        { "Synthé",              25.0f, false, {  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f } },
    }};

    // Sons : ce qu'on veut garder tel quel. Un son sombre décale la cible vers
    // le bas dans les aigus : le plugin ne cherchera pas à les remonter.
    const std::array<Son, nbSons> tableSons
    {{
        { "Clair",    {  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f } },
        // Saturé : la distorsion gonfle le bas-médium (boue) et les aigus (grésillement).
        { "Saturé",   {  0.0f, -3.0f, -3.0f,  0.0f,  0.0f, -3.0f, -3.0f } },
        { "Sombre",   {  0.0f,  0.0f,  0.0f,  0.0f, -2.0f, -4.0f, -6.0f } },
        { "Brillant", {  0.0f,  0.0f,  0.0f,  0.0f,  1.0f,  2.0f,  3.0f } },
    }};

    // Limites de la correction large. On coupe plus volontiers qu'on ne monte :
    // monter une bande fait aussi monter le bruit qui s'y trouve.
    constexpr double coupeMax  = -6.0;
    constexpr double monteeMax =  4.0;

    // Facteur de qualité des bandes larges (environ une octave de largeur)
    // et des résonances (étroites : on ne veut toucher qu'elles).
    constexpr double qBande     = 1.2;
    constexpr double qResonance = 5.0;

    // Une résonance n'est retenue que si elle dépasse son voisinage de ce seuil.
    constexpr double seuilResonanceDb = 5.0;

    // ... et d'être présente dans au moins 80 % des trames où quelqu'un joue.
    // Mesuré sur sept mixes complets : les bosses venant des notes jouées
    // apparaissent dans environ la moitié des trames, jamais en continu.
    // 80 % est une valeur de départ, à confirmer sur une vraie prise.
    constexpr double seuilPersistance = 0.8;

    // --- Filtres du 2e ordre (formules « RBJ », la référence du domaine) ---

    struct Brut { double b0, b1, b2, a0, a1, a2; };

    Coefs normaliser (const Brut& c) noexcept
    {
        return { c.b0 / c.a0, c.b1 / c.a0, c.b2 / c.a0, c.a1 / c.a0, c.a2 / c.a0 };
    }

    // Au-delà de 45 % de la fréquence d'échantillonnage, les formules se dégradent.
    double pulsation (double f, double fs) noexcept
    {
        return 2.0 * pi * std::clamp (f, 1.0, fs * 0.45) / fs;
    }

    Coefs passeHaut (double f, double q, double fs) noexcept
    {
        const double w = pulsation (f, fs), c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        return normaliser ({ (1.0 + c) * 0.5, -(1.0 + c), (1.0 + c) * 0.5, 1.0 + alpha, -2.0 * c, 1.0 - alpha });
    }

    // Cloche : monte ou baisse une zone autour de f sans toucher au reste.
    Coefs cloche (double f, double q, double gainDb, double fs) noexcept
    {
        const double a = std::pow (10.0, gainDb / 40.0);
        const double w = pulsation (f, fs), c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        return normaliser ({ 1.0 + alpha * a, -2.0 * c, 1.0 - alpha * a,
                             1.0 + alpha / a, -2.0 * c, 1.0 - alpha / a });
    }

    // Plateau : monte ou baisse tout ce qui est en dessous (grave) ou au-dessus (aigu) de f.
    Coefs plateau (double f, double gainDb, double fs, bool grave) noexcept
    {
        const double a = std::pow (10.0, gainDb / 40.0);
        const double w = pulsation (f, fs), c = std::cos (w);
        const double alpha = std::sin (w) * 0.5 * std::sqrt (2.0);
        const double r = 2.0 * std::sqrt (a) * alpha;
        const double s = grave ? 1.0 : -1.0;      // les deux plateaux sont symétriques

        return normaliser ({ a * ((a + 1.0) - s * (a - 1.0) * c + r),
                             s * 2.0 * a * ((a - 1.0) - s * (a + 1.0) * c),
                             a * ((a + 1.0) - s * (a - 1.0) * c - r),
                             (a + 1.0) + s * (a - 1.0) * c + r,
                             -s * 2.0 * ((a - 1.0) + s * (a + 1.0) * c),
                             (a + 1.0) + s * (a - 1.0) * c - r });
    }

    double versDb (double puissance) noexcept { return 10.0 * std::log10 (puissance + 1.0e-30); }

    // Puissance moyenne entre deux fréquences, à partir des sommes cumulées
    // du spectre : cumul[k] = somme des cases 0 à k - 1. La moyenne d'une plage
    // se réduit alors à une soustraction, ce qui est assez léger pour le fil audio.
    double moyennePlage (const double* cumul, int nbCases, double fs, double f1, double f2) noexcept
    {
        const double largeurCase = fs / (double) ((nbCases - 1) * 2);
        const int k1 = std::clamp ((int) std::lround (f1 / largeurCase), 1, nbCases - 1);
        const int k2 = std::clamp ((int) std::lround (f2 / largeurCase), k1, nbCases - 1);
        return (cumul[k2 + 1] - cumul[k1]) / (double) (k2 - k1 + 1);
    }
}

const Role& role (int index) noexcept
{
    return tableRoles[(size_t) std::clamp (index, 0, nbRoles - 1)];
}

const Source& source (int index) noexcept
{
    return tableSources[(size_t) std::clamp (index, 0, nbSources - 1)];
}

const Son& son (int index) noexcept
{
    return tableSons[(size_t) std::clamp (index, 0, nbSons - 1)];
}

juce::StringArray nomsRoles()
{
    juce::StringArray noms;
    for (const auto& r : tableRoles)
        noms.add (fr (r.nom));
    return noms;
}

juce::StringArray nomsSources()
{
    juce::StringArray noms;
    for (const auto& s : tableSources)
        noms.add (fr (s.nom));
    return noms;
}

juce::StringArray nomsSons()
{
    juce::StringArray noms;
    for (const auto& s : tableSons)
        noms.add (fr (s.nom));
    return noms;
}

Correction doser (const Correction& c, float quantite) noexcept
{
    const float q = std::clamp (quantite, 0.0f, 1.0f);
    Correction d = c;

    // Les gains en dB se dosent en proportion. Le coupe-bas, lui, glisse de
    // 10 Hz (inaudible) vers sa fréquence finale, de façon régulière pour l'oreille.
    d.coupeBasHz = 10.0f * std::pow (std::max (c.coupeBasHz, 10.0f) / 10.0f, q);

    for (auto& g : d.gains)   g *= q;
    for (auto& g : d.resGain) g *= q;

    // Compensation de volume : on calcule de combien la correction (déjà dosée)
    // change l'énergie de la piste, en pondérant chaque bande par la part
    // d'énergie qu'elle contient, puis on rend exactement cette différence.
    // Chaque bande est évaluée en trois points (un quart d'octave de part et
    // d'autre du centre) : une bande couvre une octave, pas une seule fréquence.
    d.compensationDb = 0.0f;
    double somme = 0.0;

    for (float p : d.poids)
        somme += (double) p;

    if (q > 0.0f && somme > 0.0)
    {
        constexpr double fsCalcul = 48000.0;
        const Banc banc = construireFiltres (d, fsCalcul);
        const double quartOctave = std::pow (2.0, 0.25);
        double puissance = 0.0;

        for (int i = 0; i < nbBandes; ++i)
        {
            const double f = frequencesBandes[(size_t) i];
            const double moyenne = (std::pow (10.0, reponseDb (banc, f / quartOctave, fsCalcul) / 10.0)
                                  + std::pow (10.0, reponseDb (banc, f, fsCalcul) / 10.0)
                                  + std::pow (10.0, reponseDb (banc, f * quartOctave, fsCalcul) / 10.0)) / 3.0;
            puissance += (double) d.poids[(size_t) i] * moyenne;
        }

        // Plafonné à +/-6 dB : une mesure aberrante ne doit pas déclencher un gros gain.
        d.compensationDb = (float) std::clamp (-10.0 * std::log10 (puissance / somme + 1.0e-12), -6.0, 6.0);
    }

    return d;
}

Banc construireFiltres (const Correction& c, double fs) noexcept
{
    Banc banc;   // par défaut, chaque filtre laisse passer le son tel quel

    // Coupe-bas 12 dB par octave, Q = 0,707 : la pente la plus raide sans
    // bosse à la coupure.
    banc[0] = passeHaut (c.coupeBasHz, 0.7071, fs);

    // La compensation de volume est un simple gain : on le range dans le premier
    // filtre. La réponse affichée (reponseDb) le contient donc aussi.
    const double gainLineaire = std::pow (10.0, (double) c.compensationDb / 20.0);
    banc[0].b0 *= gainLineaire;
    banc[0].b1 *= gainLineaire;
    banc[0].b2 *= gainLineaire;

    // Les deux bandes extrêmes sont des plateaux, les autres des cloches.
    for (int i = 0; i < nbBandes; ++i)
    {
        const double g = c.gains[(size_t) i], f = frequencesBandes[(size_t) i];

        if (i == 0)                 banc[1]               = plateau (f, g, fs, true);
        else if (i == nbBandes - 1) banc[(size_t) (1 + i)] = plateau (f, g, fs, false);
        else                        banc[(size_t) (1 + i)] = cloche (f, qBande, g, fs);
    }

    for (int i = 0; i < std::min (c.nbRes, nbResMax); ++i)
        banc[(size_t) (1 + nbBandes + i)] = cloche (c.resHz[(size_t) i], qResonance, c.resGain[(size_t) i], fs);

    return banc;
}

double reponseDb (const Banc& banc, double f, double fs) noexcept
{
    const double w = 2.0 * pi * f / fs;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
    double db = 0.0;

    for (const auto& c : banc)
    {
        const auto h = (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
        db += 20.0 * std::log10 (std::abs (h) + 1.0e-12);
    }

    return db;
}

double frequenceGrille (int index) noexcept
{
    return 200.0 * std::pow (2.0, (double) index / 24.0);
}

void marquerBosses (const double* cumul, int nbCases, double fs, Compteurs& compteurs) noexcept
{
    // Même critère que dans analyser() : une fenêtre étroite (1/12 d'octave)
    // qui dépasse son voisinage (une octave) de plus de seuilResonanceDb.
    const double fMax = std::min (5000.0, fs * 0.4);
    const double demiPas = std::pow (2.0, 1.0 / 24.0), demiOctave = std::sqrt (2.0);

    for (int i = 0; i < nbGrille; ++i)
    {
        const double f = frequenceGrille (i);

        if (f > fMax)
            break;

        const double exces = versDb (moyennePlage (cumul, nbCases, fs, f / demiPas, f * demiPas))
                           - versDb (moyennePlage (cumul, nbCases, fs, f / demiOctave, f * demiOctave));

        if (exces > seuilResonanceDb)
            ++compteurs[(size_t) i];
    }
}

Mesure analyser (const std::vector<double>& spectre, double fs, const Compteurs* compteurs, int nbTrames)
{
    Mesure m;
    const int nbCases = (int) spectre.size();

    if (nbCases < 64 || fs <= 0.0)
        return m;

    // Sommes cumulées du spectre : la moyenne sur une plage devient une soustraction.
    std::vector<double> cumul ((size_t) nbCases + 1, 0.0);

    for (int k = 0; k < nbCases; ++k)
        cumul[(size_t) k + 1] = cumul[(size_t) k] + spectre[(size_t) k];

    auto moyenne = [&] (double f1, double f2) { return moyennePlage (cumul.data(), nbCases, fs, f1, f2); };

    // Niveau de chaque bande d'octave : puissance moyenne x largeur de la bande.
    const double demiOctave = std::sqrt (2.0);
    double total = 0.0;

    for (int i = 0; i < nbBandes; ++i)
    {
        const double f1 = frequencesBandes[(size_t) i] / demiOctave;
        const double f2 = frequencesBandes[(size_t) i] * demiOctave;
        const double puissance = moyenne (f1, f2) * (f2 - f1);
        total += puissance;
        m.niveaux[(size_t) i] = (float) versDb (puissance);
    }

    if (! (total > 0.0) || ! std::isfinite (total))
        return m;      // silence ou données abîmées : pas de mesure

    m.valide = true;

    // Résonances : on compare une fenêtre étroite (1/12 d'octave) à son
    // voisinage (une octave). Une bosse nette et étroite ressort du lot ;
    // une pente régulière du spectre, non.
    // Recherche limitée à 200 Hz - 5 kHz : plus bas l'analyse est trop
    // grossière, plus haut une résonance gêne rarement.
    //
    // Persistance : une bosse du spectre moyen n'est retenue que si elle est
    // aussi apparue dans la grande majorité des trames de l'écoute. Sans cela,
    // une note souvent jouée (le « la » d'une mélodie, par exemple) ressort
    // dans la moyenne et serait atténuée à tort.
    struct Candidat { double f, exces; };
    std::vector<Candidat> candidats;

    const double fMax = std::min (5000.0, fs * 0.4);
    const double demiPas = std::pow (2.0, 1.0 / 24.0);
    std::array<double, nbGrille> exces {};
    int nbPoints = 0;

    for (int i = 0; i < nbGrille; ++i)
    {
        const double f = frequenceGrille (i);

        if (f > fMax)
            break;

        exces[(size_t) i] = versDb (moyenne (f / demiPas, f * demiPas))
                          - versDb (moyenne (f / demiOctave, f * demiOctave));
        nbPoints = i + 1;
    }

    for (int i = 1; i < nbPoints - 1; ++i)
    {
        const double e = exces[(size_t) i];

        // Un sommet dépasse ses deux voisins.
        if (! (e > exces[(size_t) i - 1] && e >= exces[(size_t) i + 1] && e > seuilResonanceDb))
            continue;

        if (compteurs != nullptr && nbTrames > 0)
        {
            // On regarde aussi les deux points voisins : le sommet de la moyenne
            // et celui d'une trame isolée peuvent différer d'un cran.
            const int vues = std::max ({ (*compteurs)[(size_t) i - 1], (*compteurs)[(size_t) i], (*compteurs)[(size_t) i + 1] });

            if ((double) vues < seuilPersistance * (double) nbTrames)
                continue;      // une note, pas une résonance
        }

        candidats.push_back ({ frequenceGrille (i), e });
    }

    std::sort (candidats.begin(), candidats.end(),
               [] (const Candidat& a, const Candidat& b) { return a.exces > b.exces; });

    for (const auto& c : candidats)
    {
        if (m.nbRes >= nbResMax)
            break;

        // Deux résonances retenues doivent être séparées d'au moins un tiers d'octave.
        bool tropProche = false;
        for (int i = 0; i < m.nbRes; ++i)
            tropProche = tropProche || std::abs (std::log2 (c.f / (double) m.resHz[(size_t) i])) < 1.0 / 3.0;

        if (! tropProche)
        {
            m.resHz[(size_t) m.nbRes]    = (float) c.f;
            m.resExces[(size_t) m.nbRes] = (float) c.exces;
            ++m.nbRes;
        }
    }

    return m;
}

Correction calculerCorrection (const Role& r, const Source& s, const Son& n, const Mesure& m)
{
    Correction c;

    // Le coupe-bas du rôle protège la place des autres pistes, celui de la source
    // retire les défauts de l'enregistrement : on garde le plus exigeant des deux.
    c.coupeBasHz = std::max (r.coupeBasHz, s.coupeBasHz);

    if (! m.valide)
        return c;

    // 1. Écart entre l'équilibre visé et l'équilibre mesuré, bande par bande.
    //    La cible est celle du rôle, déplacée par la source puis par le son voulu.
    std::array<double, nbBandes> voulu {};
    double moyenne = 0.0, niveauMax = -1000.0;

    for (int i = 0; i < nbBandes; ++i)
    {
        const double cible = (double) r.cible[(size_t) i] + (double) s.decalage[(size_t) i] + (double) n.decalage[(size_t) i];
        voulu[(size_t) i] = cible - (double) m.niveaux[(size_t) i];
        moyenne += voulu[(size_t) i] / nbBandes;
        niveauMax = std::max (niveauMax, (double) m.niveaux[(size_t) i]);
    }

    // Part d'énergie de chaque bande dans la piste écoutée. Elle sert deux fois :
    // ici, pour savoir ce que « ne pas monter » veut dire, et plus bas pour la
    // compensation de volume (voir doser()).
    double total = 0.0;
    std::array<double, nbBandes> energie {};

    for (int i = 0; i < nbBandes; ++i)
    {
        energie[(size_t) i] = std::pow (10.0, (double) m.niveaux[(size_t) i] / 10.0);
        total += energie[(size_t) i];
    }

    const bool poidsConnus = total > 0.0 && std::isfinite (total);

    if (poidsConnus)
        for (int i = 0; i < nbBandes; ++i)
            c.poids[(size_t) i] = (float) (energie[(size_t) i] / total);

    // Bandes qu'on s'interdit de monter :
    //   - une bande presque vide (45 dB sous la plus forte) ne contient que du
    //     bruit : la monter ne ferait entendre que du souffle ;
    //   - une bande située à moins d'une octave au-dessus du coupe-bas est au bord
    //     de la zone utile de l'instrument. Une guitare (coupe-bas à 80 Hz) n'a
    //     presque rien de musical dans la bande de 100 Hz, surtout de la ronflette.
    //     Pour une basse ou un kick (coupe-bas à 30 Hz), aucune bande n'est concernée.
    std::array<bool, nbBandes> sansMontee {};

    for (int i = 0; i < nbBandes; ++i)
    {
        // On retire la moyenne : la correction change le timbre, pas le volume.
        voulu[(size_t) i] = std::clamp (voulu[(size_t) i] - moyenne, coupeMax, monteeMax);

        sansMontee[(size_t) i] = (double) m.niveaux[(size_t) i] < niveauMax - 45.0
                              || frequencesBandes[(size_t) i] < 2.0 * (double) c.coupeBasHz;
    }

    // « Ne pas monter » se juge APRÈS la compensation de volume. Si la correction
    // baisse les bandes où se trouve l'énergie (les médiums d'une guitare, par
    // exemple), la compensation remonte toute la courbe d'autant : une bande
    // laissée « à zéro » se retrouverait montée. On la plafonne donc au gain moyen
    // de la piste, pondéré par l'énergie : après compensation, elle sera à 0 dB au plus.
    // Deux passes : abaisser une bande déplace un peu ce gain moyen.
    for (int passe = 0; passe < 3; ++passe)
    {
        double gainMoyen = 0.0;

        if (poidsConnus)
        {
            double puissance = 0.0;
            for (int i = 0; i < nbBandes; ++i)
                puissance += (double) c.poids[(size_t) i] * std::pow (10.0, voulu[(size_t) i] / 10.0);
            gainMoyen = 10.0 * std::log10 (puissance + 1.0e-12);
        }

        for (int i = 0; i < nbBandes; ++i)
            if (sansMontee[(size_t) i])
                voulu[(size_t) i] = std::clamp (std::min (voulu[(size_t) i], gainMoyen), coupeMax, monteeMax);
    }

    // 2. Réglage des sept filtres. Ils se chevauchent : le gain réglé sur une
    //    bande déborde sur ses voisines. On ajuste donc par petites touches
    //    jusqu'à ce que la courbe réelle passe par les valeurs voulues.
    //    Les plateaux sont évalués un peu à l'écart de leur fréquence charnière,
    //    là où ils ont presque atteint leur gain.
    std::array<double, nbBandes> fEval = frequencesBandes;
    fEval[0] = 70.0;
    fEval[nbBandes - 1] = 9000.0;

    const double fsCalcul = 48000.0;
    Correction essai;      // coupe-bas à 10 Hz : il ne perturbe pas l'ajustement

    for (int tour = 0; tour < 10; ++tour)
    {
        const Banc banc = construireFiltres (essai, fsCalcul);

        for (int i = 0; i < nbBandes; ++i)
        {
            const double erreur = voulu[(size_t) i] - reponseDb (banc, fEval[(size_t) i], fsCalcul);
            essai.gains[(size_t) i] = (float) std::clamp ((double) essai.gains[(size_t) i] + 0.6 * erreur, -9.0, 9.0);
        }
    }

    c.gains = essai.gains;

    // Repère de niveau pour le démasquage : transmis tel quel au fil audio.
    c.refValide = m.refValide;
    c.refDb = m.refDb;

    // 3. Résonances : atténuation prudente, de 2 à 4 dB selon le dépassement.
    //    Même filtrées par la persistance, certaines bosses peuvent n'être que
    //    des notes tenues ; une atténuation légère limite les dégâts dans ce cas.
    //    Il faut que le rôle ET la source s'y prêtent : pas de recherche pour
    //    une basse ou un synthé, dont les bosses sont voulues.
    if (r.resonances && s.resonances)
    {
        c.nbRes = std::min (m.nbRes, nbResMax);

        for (int i = 0; i < c.nbRes; ++i)
        {
            const double exces = (double) m.resExces[(size_t) i];
            c.resHz[(size_t) i]   = m.resHz[(size_t) i];
            c.resGain[(size_t) i] = (float) -std::clamp (2.0 + (exces - seuilResonanceDb) * 0.6, 2.0, 4.0);
        }
    }

    return c;
}

// --- Démasquage dynamique ------------------------------------------------------

namespace
{
    // Passe-bande : ne garde que la zone autour de f. Avec Q = 1,4, il laisse
    // passer environ une octave, la largeur de nos bandes.
    Coefs passeBande (double f, double q, double fs) noexcept
    {
        const double w = pulsation (f, fs), c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        return normaliser ({ alpha, 0.0, -alpha, 1.0 + alpha, -2.0 * c, 1.0 - alpha });
    }
}

BancBandes construireFiltresAnalyse (double fs) noexcept
{
    BancBandes banc;

    for (int i = 0; i < nbBandes; ++i)
        banc[(size_t) i] = passeBande (frequencesBandes[(size_t) i], 1.4, fs);

    return banc;
}

BancBandes construireFiltresDynamiques (const Reductions& reductions, double fs) noexcept
{
    // Mêmes formes que les bandes de la correction fixe : plateaux aux deux
    // bouts, cloches d'environ une octave entre les deux.
    BancBandes banc;

    for (int i = 0; i < nbBandes; ++i)
    {
        const double g = reductions[(size_t) i], f = frequencesBandes[(size_t) i];

        if (i == 0)                 banc[0]          = plateau (f, g, fs, true);
        else if (i == nbBandes - 1) banc[(size_t) i] = plateau (f, g, fs, false);
        else                        banc[(size_t) i] = cloche (f, qBande, g, fs);
    }

    return banc;
}

double reponseDb (const BancBandes& banc, double f, double fs) noexcept
{
    const double w = 2.0 * pi * f / fs;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
    double db = 0.0;

    for (const auto& c : banc)
    {
        const auto h = (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
        db += 20.0 * std::log10 (std::abs (h) + 1.0e-12);
    }

    return db;
}

Equilibre poidsZone (const Role& r) noexcept
{
    Equilibre poids {};

    for (int i = 0; i < nbBandes; ++i)
    {
        // Distance, en octaves, entre le centre de la bande et la zone du rôle.
        const double f = frequencesBandes[(size_t) i];
        double distance = 0.0;

        if (f < (double) r.importantMin)      distance = std::log2 ((double) r.importantMin / f);
        else if (f > (double) r.importantMax) distance = std::log2 (f / (double) r.importantMax);

        poids[(size_t) i] = (float) std::clamp (1.0 - distance / 0.5, 0.0, 1.0);
    }

    return poids;
}

float activite (float niveauDb, float refDb) noexcept
{
    // De 0 (20 dB sous le niveau habituel de la piste : elle ne joue pas ici)
    // à 1 (à moins de 8 dB de son niveau habituel : elle joue franchement).
    return std::clamp ((niveauDb - (refDb - 20.0f)) / 12.0f, 0.0f, 1.0f);
}

float facteurPriorite (int quiDemande, int quiCede) noexcept
{
    // À priorité égale ou inférieure, on ne demande rien : personne ne cède.
    return std::clamp ((float) (quiDemande - quiCede) * 0.25f, 0.0f, 1.0f);
}

float reductionBande (float poidsZoneBande, float activiteBande, int quiDemande, int quiCede, float quantite) noexcept
{
    return -profondeurMaxDb * std::clamp (quantite, 0.0f, 1.0f) * facteurPriorite (quiDemande, quiCede)
         * std::clamp (activiteBande, 0.0f, 1.0f) * std::clamp (poidsZoneBande, 0.0f, 1.0f);
}

} // namespace rogus
