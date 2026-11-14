#include "MesureLoudness.h"

#include <algorithm>
#include <cmath>

namespace
{
    // Paramètres du filtre K-weighting de la norme ITU-R BS.1770-4.
    constexpr double frequencePlateau = 1681.974450955533;
    constexpr double gainPlateauDb   = 3.999843853973347;
    constexpr double qPlateau        = 0.7071752369554196;
    constexpr double frequencePasseHaut = 38.13547087602444;
    constexpr double qPasseHaut      = 0.5003270373238773;

    // Décalage de la norme : un sinus à 1 kHz de niveau connu donne le LUFS.
    constexpr double decalageLufs = -0.691;
}

namespace rogus
{

void MesureLoudness::prepare (double frequenceEch, int nbCanaux, int tailleBlocMax)
{
    fs = frequenceEch > 0.0 ? frequenceEch : 48000.0;
    canaux = juce::jlimit (1, 2, nbCanaux);
    echantillonsSousBloc = juce::jmax (1, (int) std::round (fs * 0.1));

    // 4 heures de morceau au maximum : la mémoire est réservée une fois pour
    // toutes, le fil audio ne doit jamais allouer.
    blocs.clear();
    blocs.reserve ((size_t) (4 * 3600 * 10));

    auto plateau = [this] (double f0, double q, double gainDb)
    {
        const double k  = std::tan (juce::MathConstants<double>::pi * f0 / fs);
        const double vh = std::pow (10.0, gainDb / 20.0);
        const double vb = std::pow (vh, 0.4996667741545416);
        const double a0 = 1.0 + k / q + k * k;

        Biquad b;
        b.b0 = (vh + vb * k / q + k * k) / a0;
        b.b1 = 2.0 * (k * k - vh) / a0;
        b.b2 = (vh - vb * k / q + k * k) / a0;
        b.a1 = 2.0 * (k * k - 1.0) / a0;
        b.a2 = (1.0 - k / q + k * k) / a0;
        return b;
    };

    auto passeHaut = [this] (double f0, double q)
    {
        const double k  = std::tan (juce::MathConstants<double>::pi * f0 / fs);
        const double a0 = 1.0 + k / q + k * k;

        Biquad b;
        b.b0 = 1.0 / a0;
        b.b1 = -2.0 / a0;
        b.b2 = 1.0 / a0;
        b.a1 = 2.0 * (k * k - 1.0) / a0;
        b.a2 = (1.0 - k / q + k * k) / a0;
        return b;
    };

    filtre1 = plateau (frequencePlateau, qPlateau, gainPlateauDb);
    filtre2 = passeHaut (frequencePasseHaut, qPasseHaut);

    surEchantillonnage = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) canaux, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
    surEchantillonnage->initProcessing ((size_t) juce::jmax (1, tailleBlocMax));
    copie.setSize (canaux, juce::jmax (1, tailleBlocMax));

    reset();
}

void MesureLoudness::reset() noexcept
{
    compteurSousBloc = 0;
    sommeCarres[0] = sommeCarres[1] = 0.0;
    etats1[0] = etats1[1] = {};
    etats2[0] = etats2[1] = {};

    sousBlocs.fill (0.0);
    indexSousBloc = 0;
    nbSousBlocs = 0;
    blocs.clear();

    if (surEchantillonnage != nullptr)
        surEchantillonnage->reset();

    integre.store (-100.0f);
    court.store (-100.0f);
    creteDb.store (-100.0f);
    valide.store (false);
}

double MesureLoudness::filtrer (const Biquad& c, Etat& e, double x) noexcept
{
    const double y = c.b0 * x + e.z1;
    e.z1 = c.b1 * x - c.a1 * y + e.z2;
    e.z2 = c.b2 * x - c.a2 * y;
    return y;
}

void MesureLoudness::traiter (const juce::AudioBuffer<float>& tampon, int nbCanaux) noexcept
{
    const int n = tampon.getNumSamples();

    if (n <= 0 || nbCanaux < canaux || tampon.getNumChannels() < canaux)
        return;

    const int canauxUtilises = canaux;

    // --- Crête réelle : suréchantillonnage 4x du signal de sortie ------------
    if (surEchantillonnage != nullptr)
    {
        float maxi = 0.0f;
        const int tailleMax = copie.getNumSamples();

        for (int debut = 0; debut < n; debut += tailleMax)
        {
            const int m = juce::jmin (tailleMax, n - debut);

            for (int ch = 0; ch < canauxUtilises; ++ch)
                copie.copyFrom (ch, 0, tampon, ch, debut, m);

            juce::dsp::AudioBlock<float> bloc (copie.getArrayOfWritePointers(),
                                               (size_t) canauxUtilises, 0, (size_t) m);
            auto sur = surEchantillonnage->processSamplesUp (bloc);

            for (int ch = 0; ch < (int) sur.getNumChannels(); ++ch)
                for (int i = 0; i < (int) sur.getNumSamples(); ++i)
                    maxi = juce::jmax (maxi, std::abs (sur.getSample (ch, i)));

            // Il faut rendre le bloc au suréchantillonneur pour qu'il reste dans
            // un état cohérent ; on écrit dans la copie, pas dans le tampon réel.
            surEchantillonnage->processSamplesDown (bloc);
        }

        const float db = 20.0f * std::log10 (maxi + 1.0e-9f);

        if (db > creteDb.load())
            creteDb.store (db);
    }

    // --- Volume perçu : filtre K puis énergie par sous-blocs de 100 ms --------
    for (int i = 0; i < n; ++i)
    {
        for (int ch = 0; ch < canauxUtilises; ++ch)
        {
            double y = filtrer (filtre1, etats1[ch], (double) tampon.getSample (ch, i));
            y = filtrer (filtre2, etats2[ch], y);
            sommeCarres[ch] += y * y;
        }

        if (++compteurSousBloc >= echantillonsSousBloc)
            ajouterSousBloc();
    }
}

void MesureLoudness::ajouterSousBloc() noexcept
{
    double ms = 0.0;

    for (int ch = 0; ch < canaux; ++ch)
        ms += sommeCarres[ch] / (double) echantillonsSousBloc;

    sommeCarres[0] = sommeCarres[1] = 0.0;
    compteurSousBloc = 0;

    sousBlocs[(size_t) indexSousBloc] = ms;
    indexSousBloc = (indexSousBloc + 1) % nbSousBlocsCourt;
    nbSousBlocs = juce::jmin (nbSousBlocs + 1, nbSousBlocsCourt);

    // Un bloc de 400 ms (fenêtre de la norme) = les 4 derniers sous-blocs.
    if (nbSousBlocs >= 4)
    {
        double somme = 0.0;

        for (int k = 0; k < 4; ++k)
        {
            const int index = (indexSousBloc - 1 - k + nbSousBlocsCourt) % nbSousBlocsCourt;
            somme += sousBlocs[(size_t) index];
        }

        if (blocs.size() < blocs.capacity())
            blocs.push_back (somme / 4.0);
    }

    recalculer();
}

void MesureLoudness::recalculer() noexcept
{
    // --- LUFS intégré, avec les deux portes de la norme ----------------------
    double lufs = -100.0;

    if (! blocs.empty())
    {
        double sommeAbsolue = 0.0;
        int nbAbsolu = 0;

        for (double ms : blocs)
        {
            const double l = decalageLufs + 10.0 * std::log10 (ms + 1.0e-20);

            if (l > -70.0)
            {
                sommeAbsolue += ms;
                ++nbAbsolu;
            }
        }

        if (nbAbsolu > 0)
        {
            const double lufsAbsolu = decalageLufs
                                    + 10.0 * std::log10 (sommeAbsolue / (double) nbAbsolu);
            double somme = 0.0;
            int nb = 0;

            for (double ms : blocs)
            {
                const double l = decalageLufs + 10.0 * std::log10 (ms + 1.0e-20);

                if (l > -70.0 && l > lufsAbsolu - 10.0)
                {
                    somme += ms;
                    ++nb;
                }
            }

            if (nb > 0)
                lufs = decalageLufs + 10.0 * std::log10 (somme / (double) nb);
        }
    }

    integre.store ((float) lufs);
    valide.store (lufs > -70.0);

    // --- LUFS court terme : les 3 dernières secondes -------------------------
    if (nbSousBlocs >= nbSousBlocsCourt)
    {
        double somme = 0.0;

        for (double ms : sousBlocs)
            somme += ms;

        court.store ((float) (decalageLufs
                              + 10.0 * std::log10 (somme / (double) nbSousBlocsCourt + 1.0e-20)));
    }
    else
    {
        court.store (-100.0f);
    }
}

float MesureLoudness::margeDynamiqueDb() const noexcept
{
    const float i = integre.load();
    const float c = creteDb.load();

    return (i > -70.0f && c > -100.0f) ? c - i : -100.0f;
}

} // namespace rogus
