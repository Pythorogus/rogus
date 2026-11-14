#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Piste.h"

// Habillage « Console » des fenêtres de Rogus et de Rogus Master : fond
// graphite, un seul accent de couleur (celle de la piste dans l'hôte, ambre
// par défaut), potards, interrupteurs et menus dessinés à la main.
// Il ne change rien au son : il ne fait que dessiner.
namespace rogus
{
    namespace console
    {
        const juce::Colour fond          { 0xff121416 };
        const juce::Colour entete        { 0xff16191c };
        const juce::Colour bord          { 0xff2a2f35 };
        const juce::Colour carte         { 0xff1a1d21 };
        const juce::Colour puits         { 0xff0c0e10 };
        const juce::Colour texte         { 0xffeceae5 };
        const juce::Colour texteDoux     { 0xffa3a6aa };
        const juce::Colour texteAxe      { 0xff8f9398 };
        const juce::Colour grille        { 0xff22272c };
        const juce::Colour grilleZero    { 0xff4a5058 };
        const juce::Colour ambre         { 0xfff2a33a };
        const juce::Colour vert          { 0xff6fcf8e };
        const juce::Colour avertissement { 0xfff2c27a };
        const juce::Colour alerte        { 0xffe0764f };
        const juce::Colour violet        { 0xffa9a2f0 };
        const juce::Colour eteint        { 0xff5b636c };

        constexpr int hauteurEntete = 48;

        enum class Graisse { normale, demi, grasse };

        // Police du texte courant et police à chasse fixe pour les chiffres
        // (ils ne « dansent » pas quand la valeur change).
        juce::Font police (float taille, Graisse graisse = Graisse::normale, float espacement = 0.0f);
        juce::Font policeChiffres (float taille, bool grasse = false);

        float largeurTexte (const juce::Font&, const juce::String&);

        // Couleur d'accent tirée de la piste. Sans couleur reçue, ou si elle
        // est grise (la couleur par défaut d'une piste), on garde l'ambre ;
        // une couleur trop sombre est éclaircie pour rester lisible sur le fond.
        juce::Colour accentPour (const InfosPiste&);

        // Bandeau du haut : logo, nom du plugin, étiquette de la piste si l'hôte
        // en donne le nom (rien sinon), numéro de version à droite.
        void dessinerLogo (juce::Graphics&, juce::Rectangle<float> zone, juce::Colour accent);
        void dessinerEntete (juce::Graphics&, int largeur, const juce::String& titre,
                             const juce::String& nomPiste, const juce::String& version, juce::Colour accent);

        void dessinerCarte (juce::Graphics&, juce::Rectangle<int> zone, float rayon = 8.0f);
        void dessinerPuits (juce::Graphics&, juce::Rectangle<int> zone, float rayon = 8.0f);

        // Petit titre en capitales espacées, au-dessus d'une valeur.
        void dessinerTitre (juce::Graphics&, const juce::String& texte, juce::Rectangle<int> zone,
                            juce::Justification = juce::Justification::centredLeft);
    }

    class HabillageConsole : public juce::LookAndFeel_V4
    {
    public:
        HabillageConsole();

        void changerAccent (juce::Colour);
        juce::Colour accent() const noexcept { return couleurAccent; }

        void drawRotarySlider (juce::Graphics&, int x, int y, int largeur, int hauteur, float position,
                               float angleDebut, float angleFin, juce::Slider&) override;

        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool survole, bool enfonce) override;

        void drawComboBox (juce::Graphics&, int largeur, int hauteur, bool enfonce,
                           int, int, int, int, juce::ComboBox&) override;
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        juce::Font getPopupMenuFont() override;

        // Un bouton portant la propriété « plein » est rempli de la couleur
        // d'accent ; sinon il n'en a que le contour. La propriété « enEcoute »
        // remplace le triangle de lecture par un carré d'arrêt.
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                                   bool survole, bool enfonce) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool survole, bool enfonce) override;

    private:
        juce::Colour couleurAccent { console::ambre };
    };

    // Un potard avec sa valeur et son nom en dessous.
    class Potard : public juce::Component
    {
    public:
        Potard (const char* titreUtf8, int diametre);

        void paint (juce::Graphics&) override;
        void resized() override;

        int hauteurVoulue() const noexcept { return diametre + 40; }
        int largeurVoulue() const noexcept { return diametre + 28; }

        juce::Slider curseur;

    private:
        juce::String titre;
        int diametre;
    };
}
