#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "MasterProcessor.h"
#include "Habillage.h"

// Fenêtre de Rogus Master : le choix de la cible, les compteurs, et le bouton
// qui lance l'écoute du morceau puis applique le gain. Les compteurs sont
// dessinés directement (pas de contrôles) : ils ne font qu'afficher.
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
    void dessinerCompteur (juce::Graphics&, juce::Rectangle<int> zone, const char* titre,
                           const juce::String& valeur, const juce::String& unite, juce::Colour couleur) const;
    void dessinerLufs (juce::Graphics&, juce::Rectangle<int> zone) const;
    void suivrePiste (bool forcer);

    RogusMasterProcessor& moteur;

    // L'habillage est déclaré avant les contrôles : il doit leur survivre.
    rogus::HabillageConsole habillage;

    juce::ComboBox choixCible;
    juce::TextButton boutonEcoute;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attacheCible;

    // Mise en page (calculée dans resized, dessinée dans paint).
    juce::Rectangle<int> zoneDerniere, cadreLufs, zoneEtat;
    std::array<juce::Rectangle<int>, 5> compteurs;

    rogus::InfosPiste piste;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RogusMasterEditor)
};
