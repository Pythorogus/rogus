#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "MasterProcessor.h"

// Fenêtre de Rogus Master : le choix de la cible, les compteurs, et le bouton
// qui lance l'écoute du morceau puis applique le gain.
class RogusMasterEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit RogusMasterEditor (RogusMasterProcessor&);
    ~RogusMasterEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    juce::String texteEtat() const;
    juce::String ecrireDb (float db, int decimales) const;

    RogusMasterProcessor& moteur;

    juce::Label etiquetteCible, etat;
    juce::ComboBox choixCible;
    juce::Label lufsTitre, lufsValeur, cibleValeur;
    juce::Label courtTitre, courtValeur, creteTitre, creteValeur, margeTitre, margeValeur;
    juce::Label gainTitre, gainValeur, reductionTitre, reductionValeur;
    juce::TextButton boutonEcoute;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attacheCible;

    juce::Rectangle<int> cadreLufs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RogusMasterEditor)
};
