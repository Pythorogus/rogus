#include "PluginEditor.h"

namespace
{
    const juce::Colour couleurFond    { 0xff1e2428 };
    const juce::Colour couleurCadre   { 0xff161b1e };
    const juce::Colour couleurGrille  { 0xff323b41 };
    const juce::Colour couleurTexte   { 0xffa9b4bb };
    const juce::Colour couleurCourbe  { 0xff4fb3e8 };
    const juce::Colour couleurZone    { 0xff7f77dd };
    const juce::Colour couleurReson   { 0xffe0764f };
    const juce::Colour couleurDemasquage { 0xffe6c14d };

    constexpr float fMin = 20.0f, fMax = 20000.0f;     // étendue de l'oreille humaine
    constexpr float dbMax = 12.0f;                     // échelle verticale : -12 à +12 dB

    juce::String ecrireFrequence (float hz)
    {
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, 1).replaceCharacter ('.', ',') + " kHz"
                             : juce::String (juce::roundToInt (hz)) + " Hz";
    }
}

// --- Courbe ------------------------------------------------------------------

void CourbeCorrection::paint (juce::Graphics& g)
{
    const auto zone = getLocalBounds().toFloat();
    g.setColour (couleurCadre);
    g.fillRoundedRectangle (zone, 6.0f);

    const auto trace = zone.reduced (34.0f, 14.0f).withTrimmedBottom (10.0f);

    // Axe horizontal logarithmique : chaque octave occupe la même largeur,
    // comme sur un clavier de piano.
    auto xDe = [&] (float f) { return trace.getX() + trace.getWidth() * std::log (f / fMin) / std::log (fMax / fMin); };
    auto yDe = [&] (float db) { return trace.getCentreY() - trace.getHeight() * 0.5f * juce::jlimit (-dbMax, dbMax, db) / dbMax; };

    const auto& role = rogus::role (moteur.lireRole());

    // Zone où ce rôle doit rester lisible dans le mix.
    g.setColour (couleurZone.withAlpha (0.16f));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xDe (role.importantMin), trace.getY(),
                                                             xDe (role.importantMax), trace.getBottom()));

    g.setFont (juce::FontOptions (11.0f));

    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        g.setColour (couleurGrille);
        g.drawVerticalLine (juce::roundToInt (xDe (f)), trace.getY(), trace.getBottom());
        g.setColour (couleurTexte);
        g.drawText (f >= 1000.0f ? juce::String (juce::roundToInt (f / 1000.0f)) + "k" : juce::String (juce::roundToInt (f)),
                    juce::Rectangle<float> (xDe (f) - 20.0f, trace.getBottom() + 2.0f, 40.0f, 14.0f),
                    juce::Justification::centred);
    }

    for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
    {
        g.setColour (db == 0.0f ? couleurTexte.withAlpha (0.6f) : couleurGrille);
        g.drawHorizontalLine (juce::roundToInt (yDe (db)), trace.getX(), trace.getRight());
        g.setColour (couleurTexte);
        g.drawText ((db > 0.0f ? "+" : "") + juce::String (juce::roundToInt (db)),
                    juce::Rectangle<float> (zone.getX(), yDe (db) - 7.0f, 30.0f, 14.0f),
                    juce::Justification::centredRight);
    }

    // La courbe elle-même, avec la quantité en cours.
    const double fs = moteur.lireFrequenceEch();
    const auto dosee = rogus::doser (moteur.lireCorrection(), moteur.lireQuantiteEffective());
    const auto banc = rogus::construireFiltres (dosee, fs);

    juce::Path chemin;
    const int nbPoints = juce::jmax (2, (int) trace.getWidth());

    for (int i = 0; i < nbPoints; ++i)
    {
        const float f = fMin * std::pow (fMax / fMin, (float) i / (float) (nbPoints - 1));
        const float y = yDe ((float) rogus::reponseDb (banc, juce::jmin ((double) f, fs * 0.49), fs));

        if (i == 0) chemin.startNewSubPath (trace.getX(), y);
        else        chemin.lineTo (trace.getX() + (float) i, y);
    }

    g.saveState();
    g.reduceClipRegion (trace.toNearestInt());
    g.setColour (moteur.lireQuantiteEffective() > 0.0f ? couleurCourbe : couleurTexte.withAlpha (0.5f));
    g.strokePath (chemin, juce::PathStrokeType (2.0f));
    g.restoreState();

    // Démasquage en cours : une seconde courbe montre le total (correction fixe
    // + place cédée aux autres pistes). Elle bouge en direct avec le morceau.
    const auto reductions = moteur.lireReductions();
    float reductionMax = 0.0f;

    for (float r : reductions)
        reductionMax = juce::jmax (reductionMax, -r);

    if (reductionMax > 0.1f)
    {
        const auto dynamiques = rogus::construireFiltresDynamiques (reductions, fs);
        juce::Path total;

        for (int i = 0; i < nbPoints; ++i)
        {
            const double f = juce::jmin ((double) (fMin * std::pow (fMax / fMin, (float) i / (float) (nbPoints - 1))), fs * 0.49);
            const float y = yDe ((float) (rogus::reponseDb (banc, f, fs) + rogus::reponseDb (dynamiques, f, fs)));

            if (i == 0) total.startNewSubPath (trace.getX(), y);
            else        total.lineTo (trace.getX() + (float) i, y);
        }

        g.saveState();
        g.reduceClipRegion (trace.toNearestInt());
        g.setColour (couleurDemasquage);
        g.strokePath (total, juce::PathStrokeType (1.6f));
        g.restoreState();
    }

    // Repères des résonances atténuées.
    g.setColour (couleurReson);

    for (int i = 0; i < dosee.nbRes; ++i)
    {
        const float f = dosee.resHz[(size_t) i];
        const float y = yDe ((float) rogus::reponseDb (banc, f, fs));
        g.fillEllipse (xDe (f) - 4.0f, y - 4.0f, 8.0f, 8.0f);
    }
}

// --- Fenêtre -----------------------------------------------------------------

RogusEditor::RogusEditor (RogusProcessor& m)
    : AudioProcessorEditor (&m), moteur (m), courbe (m)
{
    // Un petit titre au-dessus de chaque menu : le rôle (fonction), la source
    // (ce qui produit le son) et le son (son caractère).
    auto preparerMenu = [this] (juce::Label& etiquette, const char* titre,
                                juce::ComboBox& menu, const juce::StringArray& choix)
    {
        etiquette.setText (rogus::fr (titre), juce::dontSendNotification);
        etiquette.setFont (juce::FontOptions (12.0f));
        etiquette.setColour (juce::Label::textColourId, couleurTexte);
        etiquette.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (etiquette);

        // Les choix doivent exister avant l'attache, qui sélectionne le bon.
        menu.addItemList (choix, 1);
        addAndMakeVisible (menu);
    };

    preparerMenu (etiquetteRole,   "Rôle",   choixRole,   rogus::nomsRoles());
    preparerMenu (etiquetteSource, "Source", choixSource, rogus::nomsSources());
    preparerMenu (etiquetteSon,    "Son",    choixSon,    rogus::nomsSons());

    addAndMakeVisible (courbe);

    etat.setJustificationType (juce::Justification::centredLeft);
    etat.setColour (juce::Label::textColourId, couleurTexte);
    etat.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (etat);

    addAndMakeVisible (boutonActif);

    etiquetteQuantite.setText (rogus::fr ("Quantité"), juce::dontSendNotification);
    etiquetteQuantite.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (etiquetteQuantite);

    curseurQuantite.setSliderStyle (juce::Slider::LinearHorizontal);
    curseurQuantite.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 22);
    curseurQuantite.setTextValueSuffix (" %");
    addAndMakeVisible (curseurQuantite);

    boutonEcoute.onClick = [this]
    {
        if (moteur.ecouteEnCours()) moteur.arreterEcoute();
        else                        moteur.demarrerEcoute();
    };
    addAndMakeVisible (boutonEcoute);

    attacheRole     = std::make_unique<AttacheChoix>   (moteur.parametres, IdParam::role, choixRole);
    attacheSource   = std::make_unique<AttacheChoix>   (moteur.parametres, IdParam::source, choixSource);
    attacheSon      = std::make_unique<AttacheChoix>   (moteur.parametres, IdParam::son, choixSon);
    attacheActif    = std::make_unique<AttacheBouton>  (moteur.parametres, IdParam::correctionActive, boutonActif);
    attacheQuantite = std::make_unique<AttacheCurseur> (moteur.parametres, IdParam::quantite, curseurQuantite);

    // --- Démasquage dynamique : la piste cède de la place aux pistes plus prioritaires ---
    etatDemasquage.setJustificationType (juce::Justification::centredLeft);
    etatDemasquage.setColour (juce::Label::textColourId, couleurTexte);
    etatDemasquage.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (etatDemasquage);

    boutonDemasquage.setButtonText (rogus::fr ("Démasquage"));
    addAndMakeVisible (boutonDemasquage);

    auto preparerCurseur = [this] (juce::Label& etiquette, const char* titre, juce::Slider& curseur)
    {
        etiquette.setText (rogus::fr (titre), juce::dontSendNotification);
        etiquette.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (etiquette);

        curseur.setSliderStyle (juce::Slider::LinearHorizontal);
        curseur.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 22);
        curseur.setTextValueSuffix (" %");
        addAndMakeVisible (curseur);
    };

    preparerCurseur (etiquetteDemQuantite, "Quantité", curseurDemQuantite);
    preparerCurseur (etiquetteReactivite, "Réactivité", curseurReactivite);

    // --- Compression : resserre l'écart entre passages forts et faibles ---
    etatCompression.setJustificationType (juce::Justification::centredLeft);
    etatCompression.setColour (juce::Label::textColourId, couleurTexte);
    etatCompression.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (etatCompression);
    addAndMakeVisible (boutonCompression);
    preparerCurseur (etiquetteCompQuantite, "Quantité", curseurCompQuantite);

    attacheCompression  = std::make_unique<AttacheBouton>  (moteur.parametres, IdParam::compressionActive, boutonCompression);
    attacheCompQuantite = std::make_unique<AttacheCurseur> (moteur.parametres, IdParam::compressionQuantite, curseurCompQuantite);

    etiquettePriorite.setText (rogus::fr ("Priorité"), juce::dontSendNotification);
    etiquettePriorite.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (etiquettePriorite);
    choixPriorite.addItemList ({ "Auto", "1", "2", "3", "4", "5" }, 1);
    addAndMakeVisible (choixPriorite);

    attachePriorite    = std::make_unique<AttacheChoix>   (moteur.parametres, IdParam::priorite, choixPriorite);
    attacheDemasquage  = std::make_unique<AttacheBouton>  (moteur.parametres, IdParam::demasquageActif, boutonDemasquage);
    attacheDemQuantite = std::make_unique<AttacheCurseur> (moteur.parametres, IdParam::demasquageQuantite, curseurDemQuantite);
    attacheReactivite  = std::make_unique<AttacheCurseur> (moteur.parametres, IdParam::reactivite, curseurReactivite);

    setSize (640, 650);
    timerCallback();
    startTimerHz (15);     // rafraîchit la courbe et le texte d'état
}

RogusEditor::~RogusEditor()
{
    stopTimer();
}

juce::String RogusEditor::texteEtat() const
{
    if (moteur.ecouteEnCours())
        return rogus::fr ("Écoute : ") + juce::String (juce::roundToInt (moteur.progressionEcoute() * 100.0f))
             + rogus::fr (" %. Joue normalement, avec des notes variées.");

    if (moteur.ecouteTropCourte())
        return rogus::fr ("Écoute trop courte : il faut au moins 3 secondes de son.");

    if (moteur.lireQuantiteEffective() <= 0.0f)
        return rogus::fr ("Correction à l'arrêt : le son n'est pas modifié.");

    const auto correction = moteur.lireCorrection();

    if (! moteur.lireMesure().valide)
        return rogus::fr ("Pas encore d'écoute : seul le coupe-bas du rôle (")
             + ecrireFrequence (correction.coupeBasHz) + rogus::fr (") est appliqué.");

    juce::String texte = rogus::fr ("Équilibre corrigé d'après l'écoute.");

    if (correction.nbRes > 0)
    {
        juce::StringArray liste;
        for (int i = 0; i < correction.nbRes; ++i)
            liste.add (ecrireFrequence (correction.resHz[(size_t) i]));

        texte += rogus::fr (" Résonances atténuées : ") + liste.joinIntoString (", ") + ".";
    }

    return texte;
}

juce::String RogusEditor::texteCompression() const
{
    auto ecrireDb = [] (float db) { return juce::String (db, 1).replaceCharacter ('.', ','); };

    if (moteur.lireQuantiteCompression() <= 0.0f)
        return rogus::fr ("Compression à l'arrêt.");

    const auto comp = moteur.lireCorrection().compression;

    if (! comp.valide)
        return rogus::fr ("Fais une écoute pour activer la compression (elle doit connaître la dynamique de la piste).");

    // Ce que fait le compresseur en ce moment : de combien il baisse, et ce qu'il rend.
    return rogus::fr ("Seuil ") + ecrireDb (comp.seuilDb) + " dB, ratio " + juce::String (juce::roundToInt (comp.ratio))
         + rogus::fr (", baisse de ") + ecrireDb (-moteur.lireReductionCompression())
         + rogus::fr (" dB, volume rendu : +") + ecrireDb (moteur.lireRattrapage()) + " dB.";
}

juce::String RogusEditor::texteDemasquage() const
{
    // Combien d'autres pistes portent Rogus et sont visibles d'ici ? Si ce nombre
    // reste à zéro alors qu'il y en a, les instances ne partagent pas leur mémoire.
    const int autres = moteur.nbPistesConnectees() - 1;
    const juce::String pistes = autres <= 0 ? rogus::fr ("aucune autre piste Rogus visible")
                              : autres == 1 ? rogus::fr ("1 autre piste Rogus visible")
                                            : juce::String (autres) + rogus::fr (" autres pistes Rogus visibles");

    if (! moteur.lireDemasquageActif())
        return rogus::fr ("Démasquage à l'arrêt (") + pistes + ").";

    juce::String texte = rogus::fr ("Priorité ") + juce::String (moteur.lirePriorite()) + ", " + pistes + ".";

    if (! moteur.lireMesure().refValide)
        return texte + rogus::fr (" Fais une écoute pour que les autres pistes lui cèdent la place.");

    // La bande où cette piste cède le plus en ce moment, et à qui.
    const auto reductions = moteur.lireReductions();
    size_t bande = 0;

    for (size_t b = 1; b < reductions.size(); ++b)
        if (reductions[b] < reductions[bande])
            bande = b;

    const int role = moteur.lireRoleDominant();

    if (reductions[bande] < -0.2f && role >= 0)
        texte += rogus::fr (" Cède ") + juce::String (-reductions[bande], 1).replaceCharacter ('.', ',') + " dB vers "
               + ecrireFrequence ((float) rogus::frequencesBandes[bande]) + rogus::fr (" à : ")
               + rogus::fr (rogus::role (role).nom) + ".";
    else
        texte += rogus::fr (" Ne cède rien en ce moment.");

    return texte;
}

void RogusEditor::timerCallback()
{
    etat.setText (texteEtat(), juce::dontSendNotification);
    etatDemasquage.setText (texteDemasquage(), juce::dontSendNotification);
    etatCompression.setText (texteCompression(), juce::dontSendNotification);
    boutonEcoute.setButtonText (moteur.ecouteEnCours() ? rogus::fr ("Arrêter l'écoute")
                                                       : rogus::fr ("Écouter la piste"));
    courbe.repaint();
}

void RogusEditor::paint (juce::Graphics& g)
{
    g.fillAll (couleurFond);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (20.0f));
    g.drawText ("Rogus", getLocalBounds().reduced (16, 0).removeFromTop (48), juce::Justification::centredLeft);

    // Numéro de version, discret, dans la marge du bas à droite : il permet de
    // vérifier que l'hôte a bien chargé la dernière version installée.
    g.setColour (couleurTexte.withAlpha (0.45f));
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("v" ROGUS_VERSION, getLocalBounds().removeFromBottom (16).reduced (8, 0),
                juce::Justification::centredRight);
}

void RogusEditor::resized()
{
    auto zone = getLocalBounds().reduced (16);

    zone.removeFromTop (30);                        // place du titre « Rogus »

    // Trois colonnes égales : un titre, puis le menu en dessous.
    auto menus = zone.removeFromTop (46);
    const int largeur = (menus.getWidth() - 2 * 12) / 3;

    auto placer = [&] (juce::Label& etiquette, juce::ComboBox& menu)
    {
        auto colonne = menus.removeFromLeft (largeur);
        menus.removeFromLeft (12);
        etiquette.setBounds (colonne.removeFromTop (16));
        menu.setBounds (colonne.reduced (0, 1));
    };

    placer (etiquetteRole, choixRole);
    placer (etiquetteSource, choixSource);
    placer (etiquetteSon, choixSon);
    zone.removeFromTop (10);

    // Tout en bas : les deux lignes du démasquage. Mêmes colonnes que la ligne
    // de la correction fixe, pour que les curseurs s'alignent.
    auto ligneReactivite = zone.removeFromBottom (34);
    ligneReactivite.removeFromLeft (140);
    ligneReactivite.removeFromRight (150 + 12);
    etiquetteReactivite.setBounds (ligneReactivite.removeFromLeft (70));
    curseurReactivite.setBounds (ligneReactivite);

    auto ligneDemasquage = zone.removeFromBottom (34);
    boutonDemasquage.setBounds (ligneDemasquage.removeFromLeft (140));
    choixPriorite.setBounds (ligneDemasquage.removeFromRight (84).reduced (0, 4));
    etiquettePriorite.setBounds (ligneDemasquage.removeFromRight (66));
    ligneDemasquage.removeFromRight (12);
    etiquetteDemQuantite.setBounds (ligneDemasquage.removeFromLeft (70));
    curseurDemQuantite.setBounds (ligneDemasquage);

    zone.removeFromBottom (4);
    etatDemasquage.setBounds (zone.removeFromBottom (24));
    zone.removeFromBottom (8);

    // Au milieu : la compression (elle vient après la correction fixe dans le trajet du son).
    auto ligneCompression = zone.removeFromBottom (34);
    boutonCompression.setBounds (ligneCompression.removeFromLeft (140));
    ligneCompression.removeFromRight (150 + 12);
    etiquetteCompQuantite.setBounds (ligneCompression.removeFromLeft (70));
    curseurCompQuantite.setBounds (ligneCompression);

    zone.removeFromBottom (4);
    etatCompression.setBounds (zone.removeFromBottom (24));
    zone.removeFromBottom (8);

    // Au-dessus : la ligne de la correction fixe et son texte d'état.
    auto bas = zone.removeFromBottom (34);
    boutonActif.setBounds (bas.removeFromLeft (140));
    boutonEcoute.setBounds (bas.removeFromRight (150).reduced (0, 2));
    bas.removeFromRight (12);
    etiquetteQuantite.setBounds (bas.removeFromLeft (70));
    curseurQuantite.setBounds (bas);

    zone.removeFromBottom (6);
    etat.setBounds (zone.removeFromBottom (24));
    zone.removeFromBottom (6);
    courbe.setBounds (zone);
}
