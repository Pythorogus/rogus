#include "Habillage.h"
#include "Textes.h"
#include "Polices.h"

#include <map>

namespace rogus
{
namespace console
{
    // Les polices (Inter pour le texte, JetBrains Mono pour les chiffres) sont
    // embarquées dans le plugin : la fenêtre a le même aspect sur tous les
    // postes. Si l'une d'elles ne se charge pas, on se rabat sur une police du
    // système, la fenêtre reste lisible.
    static juce::Font creerPolice (const void* donnees, int taille, const char* nomRepli,
                                   const char* styleRepli, float hauteur, float espacement)
    {
        // Une police chargée est gardée pour toute la durée du programme.
        static std::map<const void*, juce::Typeface::Ptr> chargees;
        auto trouvee = chargees.find (donnees);

        if (trouvee == chargees.end())
            trouvee = chargees.emplace (donnees, juce::Typeface::createSystemTypefaceFor (donnees, (size_t) taille)).first;

        const auto options = trouvee->second != nullptr ? juce::FontOptions (trouvee->second).withHeight (hauteur)
                                                        : juce::FontOptions (nomRepli, styleRepli, hauteur);
        return juce::Font (options.withKerningFactor (espacement));
    }

    juce::Font police (float taille, Graisse graisse, float espacement)
    {
        switch (graisse)
        {
            case Graisse::grasse: return creerPolice (Polices::InterBold_ttf, Polices::InterBold_ttfSize, "Segoe UI", "Bold", taille, espacement);
            case Graisse::demi:   return creerPolice (Polices::InterSemiBold_ttf, Polices::InterSemiBold_ttfSize, "Segoe UI", "Semibold", taille, espacement);
            case Graisse::normale: break;
        }

        return creerPolice (Polices::InterRegular_ttf, Polices::InterRegular_ttfSize, "Segoe UI", "Regular", taille, espacement);
    }

    juce::Font policeChiffres (float taille, bool grasse)
    {
        return grasse ? creerPolice (Polices::JetBrainsMonoBold_ttf, Polices::JetBrainsMonoBold_ttfSize, "Consolas", "Bold", taille, 0.0f)
                      : creerPolice (Polices::JetBrainsMonoRegular_ttf, Polices::JetBrainsMonoRegular_ttfSize, "Consolas", "Regular", taille, 0.0f);
    }

    float largeurTexte (const juce::Font& f, const juce::String& t)
    {
        return juce::GlyphArrangement::getStringWidth (f, t);
    }

    juce::Colour accentPour (const InfosPiste& piste)
    {
        if (! piste.aCouleur || piste.couleur.getAlpha() == 0 || piste.couleur.getSaturation() < 0.15f)
            return ambre;

        auto couleur = piste.couleur.withAlpha (1.0f);

        // Sur un fond presque noir, une couleur sombre (bleu nuit, bordeaux)
        // serait illisible : on la tire vers le blanc jusqu'à ce qu'elle ressorte.
        for (int i = 0; i < 8 && couleur.getPerceivedBrightness() < 0.5f; ++i)
            couleur = couleur.interpolatedWith (juce::Colours::white, 0.15f);

        return couleur;
    }

    // Le logo : deux ondes qui se chevauchent sans s'écraser, comme deux pistes
    // démasquées. Celle de devant prend la couleur d'accent.
    void dessinerLogo (juce::Graphics& g, juce::Rectangle<float> zone, juce::Colour accent)
    {
        // Tracé repris de la maquette, dessinée dans un carré de 48 unités.
        const auto versZone = juce::AffineTransform::scale (zone.getWidth() / 48.0f, zone.getHeight() / 48.0f)
                                  .translated (zone.getX(), zone.getY());
        const juce::PathStrokeType trait (4.5f * zone.getWidth() / 48.0f,
                                          juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        juce::Path derriere;
        derriere.startNewSubPath (3.0f, 34.0f);
        derriere.cubicTo (11.0f, 34.0f, 11.0f, 12.0f, 19.0f, 12.0f);
        derriere.cubicTo (27.0f, 12.0f, 27.0f, 34.0f, 35.0f, 34.0f);
        g.setColour (texte.withAlpha (0.55f));
        g.strokePath (derriere, trait, versZone);

        juce::Path devant;
        devant.startNewSubPath (13.0f, 38.0f);
        devant.cubicTo (21.0f, 38.0f, 21.0f, 20.0f, 29.0f, 20.0f);
        devant.cubicTo (37.0f, 20.0f, 37.0f, 38.0f, 45.0f, 38.0f);
        g.setColour (accent);
        g.strokePath (devant, trait, versZone);
    }

    void dessinerEntete (juce::Graphics& g, int largeur, const juce::String& titre,
                         const juce::String& nomPiste, const juce::String& version, juce::Colour accent)
    {
        const juce::Rectangle<int> bandeau (0, 0, largeur, hauteurEntete);
        g.setColour (entete);
        g.fillRect (bandeau);
        g.setColour (bord);
        g.fillRect (0, hauteurEntete - 1, largeur, 1);

        const auto policeTitre = police (16.0f, Graisse::grasse, 0.16f);
        const int largeurTitre = juce::roundToInt (largeurTexte (policeTitre, titre)) + 4;
        g.setColour (texte);
        g.setFont (policeTitre);
        const int xTitre = 18 + 28 + 10;
        dessinerLogo (g, { 18.0f, (float) (hauteurEntete - 28) * 0.5f, 28.0f, 28.0f }, accent);
        g.setColour (texte);
        g.drawText (titre, xTitre, 0, largeurTitre, hauteurEntete - 1, juce::Justification::centredLeft);

        const auto policeVersion = policeChiffres (12.0f);
        g.setColour (texteDoux);
        g.setFont (policeVersion);
        g.drawText (version, largeur - 18 - 80, 0, 80, hauteurEntete - 1, juce::Justification::centredRight);

        // Étiquette de la piste : seulement si l'hôte a donné un nom.
        if (nomPiste.isNotEmpty())
        {
            const int gauche = xTitre + largeurTitre + 10;
            const int place = largeur - 18 - 80 - gauche - 10;
            const int voulu = juce::roundToInt (largeurTexte (policeVersion, nomPiste)) + 16;
            const juce::Rectangle<int> cadre (gauche, (hauteurEntete - 22) / 2, juce::jmin (voulu, place), 22);

            if (cadre.getWidth() > 30)
            {
                g.setColour (bord);
                g.drawRoundedRectangle (cadre.toFloat().reduced (0.5f), 4.0f, 1.0f);
                g.setColour (texteDoux);
                g.drawText (nomPiste, cadre.reduced (7, 0), juce::Justification::centredLeft, true);
            }
        }
    }

    void dessinerCarte (juce::Graphics& g, juce::Rectangle<int> zone, float rayon)
    {
        g.setColour (carte);
        g.fillRoundedRectangle (zone.toFloat(), rayon);
        g.setColour (bord);
        g.drawRoundedRectangle (zone.toFloat().reduced (0.5f), rayon, 1.0f);
    }

    void dessinerPuits (juce::Graphics& g, juce::Rectangle<int> zone, float rayon)
    {
        g.setColour (puits);
        g.fillRoundedRectangle (zone.toFloat(), rayon);
        g.setColour (bord);
        g.drawRoundedRectangle (zone.toFloat().reduced (0.5f), rayon, 1.0f);
    }

    // Mise en capitales qui n'oublie pas les lettres accentuées du français
    // (la conversion du système les laisse en minuscules).
    static juce::String enCapitales (const juce::String& t)
    {
        const auto minuscules = fr ("àâäçéèêëîïôöùûü");
        const auto capitales  = fr ("ÀÂÄÇÉÈÊËÎÏÔÖÙÛÜ");
        auto resultat = t.toUpperCase();

        for (int i = 0; i < minuscules.length(); ++i)
            resultat = resultat.replaceCharacter (minuscules[i], capitales[i]);

        return resultat;
    }

    void dessinerTitre (juce::Graphics& g, const juce::String& t, juce::Rectangle<int> zone,
                        juce::Justification justification)
    {
        g.setColour (texteDoux);
        g.setFont (police (11.0f, Graisse::normale, 0.1f));
        g.drawText (enCapitales (t), zone, justification, true);
    }
}

// --- Habillage des contrôles -------------------------------------------------

HabillageConsole::HabillageConsole()
{
    using namespace console;

    setColour (juce::ComboBox::textColourId, texte);
    setColour (juce::Label::textColourId, texte);
    setColour (juce::PopupMenu::backgroundColourId, carte);
    setColour (juce::PopupMenu::textColourId, texte);
    setColour (juce::PopupMenu::highlightedTextColourId, texte);
    setColour (juce::ResizableWindow::backgroundColourId, fond);
    changerAccent (ambre);
}

void HabillageConsole::changerAccent (juce::Colour couleur)
{
    couleurAccent = couleur;
    setColour (juce::PopupMenu::highlightedBackgroundColourId, couleur.withAlpha (0.28f));
}

void HabillageConsole::drawRotarySlider (juce::Graphics& g, int x, int y, int largeur, int hauteur,
                                         float position, float angleDebut, float angleFin, juce::Slider& curseur)
{
    using namespace console;

    const auto zone = juce::Rectangle<int> (x, y, largeur, hauteur).toFloat();
    const float rayon = juce::jmin (zone.getWidth(), zone.getHeight()) * 0.5f - 3.0f;
    const auto centre = zone.getCentre();
    const float angle = angleDebut + position * (angleFin - angleDebut);
    const juce::PathStrokeType trait (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    // L'arc gris montre toute la course, l'arc coloré la valeur réglée.
    juce::Path course;
    course.addCentredArc (centre.x, centre.y, rayon, rayon, 0.0f, angleDebut, angleFin, true);
    g.setColour (juce::Colour (0xff2e3339));
    g.strokePath (course, trait);

    if (position > 0.001f)
    {
        juce::Path valeur;
        valeur.addCentredArc (centre.x, centre.y, rayon, rayon, 0.0f, angleDebut, angle, true);
        g.setColour (couleurAccent);
        g.strokePath (valeur, trait);
    }

    const float corps = rayon * 0.78f;
    g.setColour (juce::Colour (curseur.isMouseOverOrDragging() ? 0xff282d33 : 0xff22262b));
    g.fillEllipse (centre.x - corps, centre.y - corps, corps * 2.0f, corps * 2.0f);
    g.setColour (juce::Colour (0xff363c43));
    g.drawEllipse (centre.x - corps, centre.y - corps, corps * 2.0f, corps * 2.0f, 1.0f);

    // Le repère blanc : il pointe vers la valeur.
    const juce::Point<float> direction (std::sin (angle), -std::cos (angle));
    g.setColour (juce::Colour (0xfff4f1ea));
    g.drawLine (juce::Line<float> (centre + direction * (corps * 0.42f), centre + direction * (corps * 0.82f)), 2.6f);
}

void HabillageConsole::drawToggleButton (juce::Graphics& g, juce::ToggleButton& bouton, bool survole, bool)
{
    using namespace console;

    const auto zone = bouton.getLocalBounds();
    const juce::Rectangle<float> rail (0.0f, ((float) zone.getHeight() - 20.0f) * 0.5f, 34.0f, 20.0f);

    g.setColour (puits);
    g.fillRoundedRectangle (rail, 10.0f);
    g.setColour (survole ? grilleZero : bord);
    g.drawRoundedRectangle (rail.reduced (0.5f), 10.0f, 1.0f);

    const bool allume = bouton.getToggleState();
    const float xPastille = allume ? rail.getRight() - 17.0f : rail.getX() + 3.0f;
    g.setColour (allume ? couleurAccent : eteint);
    g.fillEllipse (xPastille, rail.getY() + 3.0f, 14.0f, 14.0f);

    g.setColour (texte);
    g.setFont (police (14.5f, Graisse::demi));
    g.drawText (bouton.getButtonText(), zone.withTrimmedLeft (43), juce::Justification::centredLeft, true);
}

void HabillageConsole::drawComboBox (juce::Graphics& g, int largeur, int hauteur, bool,
                                     int, int, int, int, juce::ComboBox& menu)
{
    using namespace console;

    const juce::Rectangle<int> zone (0, 0, largeur, hauteur);
    const bool grand = hauteur >= 40;
    const float rayon = grand ? 7.0f : 5.0f;

    g.setColour (carte);
    g.fillRoundedRectangle (zone.toFloat(), rayon);
    g.setColour (menu.isMouseOver (true) || menu.isPopupActive() ? grilleZero : bord);
    g.drawRoundedRectangle (zone.toFloat().reduced (0.5f), rayon, 1.0f);

    // Un grand menu porte son titre au-dessus de la valeur choisie.
    if (grand)
        dessinerTitre (g, menu.getTitle(), { 12, 7, largeur - 40, 13 });

    // Chevron : il signale qu'une liste s'ouvre.
    const float cx = (float) largeur - (grand ? 18.0f : 12.0f), cy = (float) hauteur * 0.5f;
    juce::Path chevron;
    chevron.startNewSubPath (cx - 4.0f, cy - 2.0f);
    chevron.lineTo (cx, cy + 2.0f);
    chevron.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (texteDoux);
    g.strokePath (chevron, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void HabillageConsole::positionComboBoxText (juce::ComboBox& menu, juce::Label& etiquette)
{
    if (menu.getHeight() >= 40)
        etiquette.setBounds (8, 19, menu.getWidth() - 36, menu.getHeight() - 23);
    else
        etiquette.setBounds (4, 1, menu.getWidth() - 22, menu.getHeight() - 2);

    etiquette.setFont (getComboBoxFont (menu));
}

juce::Font HabillageConsole::getComboBoxFont (juce::ComboBox& menu)
{
    return console::police (menu.getHeight() >= 40 ? 16.0f : 13.5f, console::Graisse::demi);
}

juce::Font HabillageConsole::getPopupMenuFont()
{
    return console::police (15.0f);
}

void HabillageConsole::drawButtonBackground (juce::Graphics& g, juce::Button& bouton, const juce::Colour&,
                                             bool survole, bool enfonce)
{
    const auto zone = bouton.getLocalBounds().toFloat();
    const bool plein = (bool) bouton.getProperties().getWithDefault ("plein", false);

    if (plein)
    {
        g.setColour (enfonce ? couleurAccent.darker (0.15f) : survole ? couleurAccent.brighter (0.15f) : couleurAccent);
        g.fillRoundedRectangle (zone, 8.0f);
    }
    else
    {
        g.setColour (couleurAccent.withAlpha (enfonce ? 0.22f : survole ? 0.12f : 0.0f));
        g.fillRoundedRectangle (zone, 7.0f);
        g.setColour (couleurAccent);
        g.drawRoundedRectangle (zone.reduced (0.5f), 7.0f, 1.0f);
    }
}

void HabillageConsole::drawButtonText (juce::Graphics& g, juce::TextButton& bouton, bool, bool)
{
    using namespace console;

    const bool plein = (bool) bouton.getProperties().getWithDefault ("plein", false);
    const bool enEcoute = (bool) bouton.getProperties().getWithDefault ("enEcoute", false);
    const auto policeBouton = police (plein ? 16.0f : 14.0f, plein ? Graisse::grasse : Graisse::demi);
    const auto texteBouton = bouton.getButtonText();

    // Icône et texte centrés ensemble dans le bouton.
    const float icone = plein ? 12.0f : 10.0f, ecart = 8.0f;
    const float largeur = icone + ecart + largeurTexte (policeBouton, texteBouton);
    const auto zone = bouton.getLocalBounds().toFloat();
    const float x = zone.getCentreX() - largeur * 0.5f, cy = zone.getCentreY();

    g.setColour (plein ? juce::Colour (0xff1a1408) : couleurAccent);

    if (enEcoute)
    {
        g.fillRect (x, cy - icone * 0.5f, icone, icone);            // carré : arrêter
    }
    else
    {
        juce::Path triangle;                                        // triangle : lancer
        triangle.addTriangle (x, cy - icone * 0.6f, x, cy + icone * 0.6f, x + icone, cy);
        g.fillPath (triangle);
    }

    g.setFont (policeBouton);
    g.drawText (texteBouton, juce::Rectangle<float> (x + icone + ecart, zone.getY(), largeur, zone.getHeight()),
                juce::Justification::centredLeft, false);
}

// --- Potard ------------------------------------------------------------------

Potard::Potard (const char* titreUtf8, int d) : titre (fr (titreUtf8)), diametre (d)
{
    curseur.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    curseur.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);

    // Course de trois quarts de tour, ouverte vers le bas, comme sur une console.
    curseur.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                 juce::MathConstants<float>::pi * 2.75f, true);
    curseur.onValueChange = [this] { repaint(); };
    addAndMakeVisible (curseur);
}

void Potard::resized()
{
    curseur.setBounds ((getWidth() - diametre) / 2, 0, diametre, diametre);
}

void Potard::paint (juce::Graphics& g)
{
    using namespace console;

    g.setColour (texte);
    g.setFont (policeChiffres (15.0f));
    g.drawText (juce::String (juce::roundToInt (curseur.getValue())) + " %",
                0, diametre + 5, getWidth(), 18, juce::Justification::centred);

    dessinerTitre (g, titre, { 0, diametre + 24, getWidth(), 14 }, juce::Justification::centred);
}
}
