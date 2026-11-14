#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "Habillage.h"

// Dessine la courbe d'égalisation réellement appliquée à la piste.
// Elle est calculée avec les mêmes filtres que l'audio.
class CourbeCorrection : public juce::Component
{
public:
    explicit CourbeCorrection (RogusProcessor& m) : moteur (m) {}
    void paint (juce::Graphics&) override;

    juce::Colour accent { rogus::console::ambre };

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
    juce::String texteCompression() const;
    juce::String texteDemasquage() const;
    void suivrePiste (bool forcer);

    RogusProcessor& moteur;

    // L'habillage est déclaré avant les contrôles : il doit leur survivre.
    rogus::HabillageConsole habillage;

    juce::ComboBox choixRole, choixSource, choixSon;
    CourbeCorrection courbe;
    juce::TextButton boutonEcoute;

    // Trois cartes, dans l'ordre du trajet du son : correction fixe,
    // compression, démasquage.
    juce::ToggleButton boutonActif { "Correction fixe" };
    rogus::Potard potardQuantite { "Quantité", 76 };
    juce::Label etat;

    juce::ToggleButton boutonCompression { "Compression" };
    rogus::Potard potardCompQuantite { "Quantité", 76 };
    juce::Label etatCompression;

    juce::ToggleButton boutonDemasquage;
    rogus::Potard potardDemQuantite { "Quantité", 60 };
    rogus::Potard potardReactivite { "Réactivité", 60 };
    juce::ComboBox choixPriorite;
    juce::Label etatDemasquage;

    // Les « attaches » relient un contrôle à un paramètre dans les deux sens :
    // bouger le contrôle change le paramètre, et une automation de l'hôte fait
    // bouger le contrôle. Elles sont déclarées après les contrôles pour être
    // détruites avant eux.
    using AttacheChoix   = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using AttacheBouton  = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using AttacheCurseur = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<AttacheChoix> attacheRole, attacheSource, attacheSon, attachePriorite;
    std::unique_ptr<AttacheBouton> attacheActif, attacheCompression, attacheDemasquage;
    std::unique_ptr<AttacheCurseur> attacheQuantite, attacheCompQuantite, attacheDemQuantite, attacheReactivite;

    // Mise en page (calculée dans resized, dessinée dans paint).
    juce::Rectangle<int> cadreCourbe, ligneCourbe;
    std::array<juce::Rectangle<int>, 3> cartes;

    // Ce que l'hôte dit de la piste, et le résumé affiché au-dessus de la courbe.
    rogus::InfosPiste piste;
    juce::String etatCourt;
    juce::Colour pastille { rogus::console::eteint };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RogusEditor)
};
