#pragma once

#include <memory>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace rogus
{

// Limiteur de crête réelle avec anticipation.
//
// Le signal est suréchantillonné 4x : on limite donc le vrai pic (celui que
// verrait un encodeur), pas seulement le pic des échantillons. L'anticipation
// (5 ms par défaut) laisse au gain le temps de descendre avant le pic ; une
// attaque courte puis un relâchement plus long évitent le « pompage ».
//
// Le gain d'entrée (réglé par l'écoute du morceau) est appliqué avant le
// limiteur. Il est lissé pour qu'un changement de calage ne fasse pas de clic.
class Limiteur
{
public:
    void prepare (double frequenceEch, int nbCanaux, int tailleBlocMax,
                  double anticipationMs = 5.0);
    void reset() noexcept;

    // Renvoie la réduction moyenne appliquée au bloc, en dB (<= 0).
    void traiter (juce::AudioBuffer<float>& tampon, int nbCanaux,
                  float gainDb, float plafondDb, float& reductionMoyenne) noexcept;

    int latenceEchantillons() const noexcept;

private:
    double fs = 48000.0;
    int canaux = 2;
    int retard = 0;              // en échantillons suréchantillonnés
    int tailleFenetre = 1;

    std::unique_ptr<juce::dsp::Oversampling<float>> surEchantillonnage;

    std::vector<float> retardG, retardD;
    std::vector<float> valeursFenetre;   // valeurs de crête récentes
    std::vector<juce::int64> file;       // file monotone des indices
    juce::int64 tete = 0, queue = 0, compteur = 0;
    int positionEcriture = 0;

    double gainLisse = 1.0;
    double reduction = 1.0;
    double coefAttaque = 0.0, coefRelache = 0.0, coefGain = 0.0;
    bool premierBloc = true;
};

} // namespace rogus
