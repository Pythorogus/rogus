#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

// Dessine la courbe d'égalisation réellement appliquée à la piste.
// Elle est calculée avec les mêmes filtres que l'audio.
class CourbeCorrection : public juce::Component
{
public:
    explicit CourbeCorrection (RogusProcessor& m) : moteur (m) {}
    void paint (juce::Graphics&) override;

private:
    RogusProcessor& moteur;
};

// La fenêtre du plugin. Elle ne touche jamais l'audio directement : elle
// modifie les paramètres et demande les écoutes, le moteur fait le reste.
class RogusEditor : public juce::AudioProcessorEditor,
                    private juce::Timer
{
public:
    explicit RogusEditor (RogusProcessor&);
    ~RogusEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    juce::String texteEtat() const;

    RogusProcessor& moteur;

    juce::Label etiquetteRole, etiquetteSource, etiquetteSon, etiquetteQuantite, etat;
    juce::ComboBox choixRole, choixSource, choixSon;
    CourbeCorrection courbe;
    juce::ToggleButton boutonActif { "Correction fixe" };
    juce::Slider curseurQuantite;
    juce::TextButton boutonEcoute;

    // Les « attaches » relient un contrôle à un paramètre dans les deux sens :
    // bouger le contrôle change le paramètre, et une automation de l'hôte fait
    // bouger le contrôle. Elles sont déclarées après les contrôles pour être
    // détruites avant eux.
    using AttacheChoix   = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using AttacheBouton  = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using AttacheCurseur = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<AttacheChoix> attacheRole, attacheSource, attacheSon;
    std::unique_ptr<AttacheBouton> attacheActif;
    std::unique_ptr<AttacheCurseur> attacheQuantite;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RogusEditor)
};
