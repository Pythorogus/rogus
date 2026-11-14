#include "Limiteur.h"

#include <algorithm>
#include <cmath>

namespace rogus
{

void Limiteur::prepare (double frequenceEch, int nbCanaux, int tailleBlocMax,
                        double anticipationMs)
{
    fs = frequenceEch > 0.0 ? frequenceEch : 48000.0;
    canaux = juce::jlimit (1, 2, nbCanaux);

    const double fs4 = fs * 4.0;
    // Lissage à un pôle : à chaque échantillon, la valeur parcourt la fraction
    // « coef » du chemin qui la sépare de sa cible. Pour une constante de temps
    // T, cette fraction vaut 1 - exp(-1 / (fs * T)) : elle est petite (un pas
    // fin à chaque échantillon). Avec exp(...) seul, proche de 1, la valeur
    // rejoindrait sa cible en un échantillon et rien ne serait lissé.
    coefAttaque = 1.0 - std::exp (-1.0 / (fs4 * 0.001));       // 1 ms
    coefRelache = 1.0 - std::exp (-1.0 / (fs4 * 0.150));       // 150 ms
    coefGain    = 1.0 - std::exp (-1.0 / (fs4 * 0.050));       // 50 ms

    retard = juce::jmax (1, (int) std::round (fs4 * anticipationMs / 1000.0));
    tailleFenetre = retard + 1;

    retardG.assign ((size_t) retard, 0.0f);
    retardD.assign ((size_t) retard, 0.0f);
    valeursFenetre.assign ((size_t) tailleFenetre, 0.0f);
    file.assign ((size_t) tailleFenetre + 2, 0);

    surEchantillonnage = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) canaux, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
    surEchantillonnage->initProcessing ((size_t) juce::jmax (1, tailleBlocMax));

    reset();
}

void Limiteur::reset() noexcept
{
    std::fill (retardG.begin(), retardG.end(), 0.0f);
    std::fill (retardD.begin(), retardD.end(), 0.0f);
    std::fill (valeursFenetre.begin(), valeursFenetre.end(), 0.0f);

    tete = queue = compteur = 0;
    positionEcriture = 0;
    gainLisse = 1.0;
    reduction = 1.0;
    premierBloc = true;

    if (surEchantillonnage != nullptr)
        surEchantillonnage->reset();
}

int Limiteur::latenceEchantillons() const noexcept
{
    const int latenceSur = surEchantillonnage != nullptr
                             ? (int) surEchantillonnage->getLatencyInSamples() : 0;

    return latenceSur + retard / 4;
}

void Limiteur::traiter (juce::AudioBuffer<float>& tampon, int nbCanaux,
                        float gainDb, float plafondDb, float& reductionMoyenne) noexcept
{
    reductionMoyenne = 0.0f;

    const int n = tampon.getNumSamples();

    if (surEchantillonnage == nullptr || n <= 0 || nbCanaux < canaux
        || tampon.getNumChannels() < canaux)
        return;

    juce::dsp::AudioBlock<float> bloc (tampon.getArrayOfWritePointers(),
                                       (size_t) canaux, 0, (size_t) n);
    auto sur = surEchantillonnage->processSamplesUp (bloc);

    const int n4 = (int) sur.getNumSamples();

    if (n4 <= 0)
        return;

    const float gainCible = (float) std::pow (10.0, (double) gainDb / 20.0);
    const float plafond  = (float) std::pow (10.0, (double) plafondDb / 20.0);

    if (premierBloc)
    {
        gainLisse = gainCible;
        premierBloc = false;
    }

    double sommeReduction = 0.0;
    const juce::int64 tailleFile = (juce::int64) file.size();

    for (int i = 0; i < n4; ++i)
    {
        gainLisse += ((double) gainCible - gainLisse) * coefGain;

        // Le gain d'entrée est appliqué avant le retard : la mémoire du
        // limiteur voit donc déjà le niveau définitif.
        float crete = 0.0f;
        float xG = 0.0f, xD = 0.0f;

        xG = sur.getSample (0, (size_t) i) * (float) gainLisse;
        crete = std::abs (xG);

        if (canaux > 1)
        {
            xD = sur.getSample (1, (size_t) i) * (float) gainLisse;
            crete = juce::jmax (crete, std::abs (xD));
        }

        // --- Maximum glissant sur la fenêtre d'anticipation -----------------
        valeursFenetre[(size_t) (compteur % tailleFenetre)] = crete;

        while (queue > tete)
        {
            const juce::int64 index = file[(size_t) ((queue - 1) % tailleFile)];
            const float valeur = valeursFenetre[(size_t) (index % tailleFenetre)];

            if (valeur <= crete)
                --queue;
            else
                break;
        }

        file[(size_t) (queue % tailleFile)] = compteur;
        ++queue;

        while (file[(size_t) (tete % tailleFile)] < compteur - retard)
            ++tete;

        const float maxFenetre = valeursFenetre[
            (size_t) (file[(size_t) (tete % tailleFile)] % tailleFenetre)];

        const float cible = juce::jmin (1.0f, plafond / juce::jmax (maxFenetre, 1.0e-12f));

        reduction += ((double) cible - reduction)
                   * (cible < reduction ? coefAttaque : coefRelache);
        sommeReduction += reduction;

        // --- Retard de l'audio et application du gain du limiteur -----------
        const int lecture = positionEcriture;
        const float delaiG = retardG[(size_t) lecture];
        const float delaiD = canaux > 1 ? retardD[(size_t) lecture] : 0.0f;

        retardG[(size_t) lecture] = xG;
        if (canaux > 1)
            retardD[(size_t) lecture] = xD;

        positionEcriture = (positionEcriture + 1) % retard;

        sur.setSample (0, (size_t) i, delaiG * (float) reduction);

        if (canaux > 1)
            sur.setSample (1, (size_t) i, delaiD * (float) reduction);

        ++compteur;
    }

    surEchantillonnage->processSamplesDown (bloc);

    reductionMoyenne = (float) (20.0 * std::log10 (sommeReduction / (double) n4 + 1.0e-20));
}

} // namespace rogus
