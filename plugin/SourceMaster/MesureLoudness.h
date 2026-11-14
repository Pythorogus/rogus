#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace rogus
{

// Mesure du volume perçu (ITU-R BS.1770-4) et de la crête réelle, sur le signal
// qui sort du plugin (donc ce que l'auditeur entend).
//
// - LUFS intégré : moyenne « gated » sur tout ce qui a été joué depuis reset().
//   C'est la valeur qui sert de cible de mastering. Les silences et les passages
//   très faibles sont ignorés (portes à -70 LUFS puis -10 LU sous la moyenne).
// - LUFS court terme : les 3 dernières secondes, pour voir les déséquilibres
//   entre couplet et refrain.
// - Crête réelle : le vrai pic du signal reconstruit, mesuré par
//   suréchantillonnage 4x. C'est ce que voit un encodeur MP3/AAC, et c'est
//   souvent plus haut que la crête « échantillon » affichée par l'hôte.
//
// Le calcul tourne sur le fil audio, sans allocation (la mémoire est réservée
// dans prepare). Les résultats sont publiés par des atomiques pour la fenêtre.
class MesureLoudness
{
public:
    void prepare (double frequenceEch, int nbCanaux, int tailleBlocMax);
    void reset() noexcept;

    // Analyse un bloc déjà traité (sortie du plugin).
    void traiter (const juce::AudioBuffer<float>& tampon, int nbCanaux) noexcept;

    float lufsIntegre() const noexcept    { return integre.load(); }
    float lufsCourtTerme() const noexcept { return court.load(); }
    float creteReelleDb() const noexcept  { return creteDb.load(); }
    float margeDynamiqueDb() const noexcept;
    bool mesureValide() const noexcept    { return valide.load(); }

private:
    struct Biquad { double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0; };
    struct Etat   { double z1 = 0.0, z2 = 0.0; };

    static double filtrer (const Biquad&, Etat&, double x) noexcept;
    void ajouterSousBloc() noexcept;
    void recalculer() noexcept;

    double fs = 48000.0;
    int canaux = 2;
    int echantillonsSousBloc = 4800;
    int compteurSousBloc = 0;
    double sommeCarres[2] { 0.0, 0.0 };

    // Filtres K-weighting (pré-filtre en plateau + passe-haut RLB).
    Biquad filtre1, filtre2;
    Etat etats1[2], etats2[2];

    static constexpr int nbSousBlocsCourt = 30;     // 30 x 100 ms = 3 s
    std::array<double, nbSousBlocsCourt> sousBlocs {};
    int indexSousBloc = 0, nbSousBlocs = 0;

    // Un bloc de 400 ms = moyenne de 4 sous-blocs de 100 ms (recouvrement 75 %).
    std::vector<double> blocs;

    std::unique_ptr<juce::dsp::Oversampling<float>> surEchantillonnage;
    juce::AudioBuffer<float> copie;

    std::atomic<float> integre { -100.0f }, court { -100.0f }, creteDb { -100.0f };
    std::atomic<bool> valide { false };
};

} // namespace rogus
