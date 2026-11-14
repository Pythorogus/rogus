#include "PluginEditor.h"
#include "Textes.h"

namespace
{
    using namespace rogus::console;

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
    // Pas de fond propre : la courbe est posée sur le « puits » sombre de la fenêtre.
    const auto zone = getLocalBounds().toFloat();
    const auto trace = zone.withTrimmedLeft (34.0f).withTrimmedRight (6.0f)
                           .withTrimmedTop (6.0f).withTrimmedBottom (20.0f);

    // Axe horizontal logarithmique : chaque octave occupe la même largeur,
    // comme sur un clavier de piano.
    auto xDe = [&] (float f) { return trace.getX() + trace.getWidth() * std::log (f / fMin) / std::log (fMax / fMin); };
    auto yDe = [&] (float db) { return trace.getCentreY() - trace.getHeight() * 0.5f * juce::jlimit (-dbMax, dbMax, db) / dbMax; };

    const auto& role = rogus::role (moteur.lireRole());

    // Zone où ce rôle doit rester lisible dans le mix : c'est là que les
    // pistes moins prioritaires lui cèdent la place.
    const auto zoneRole = juce::Rectangle<float>::leftTopRightBottom (xDe (role.importantMin), trace.getY(),
                                                                      xDe (role.importantMax), trace.getBottom());
    g.setColour (violet.withAlpha (0.10f));
    g.fillRect (zoneRole);

    if (zoneRole.getWidth() > 86.0f)
    {
        g.setColour (violet);
        g.setFont (policeChiffres (11.0f));
        g.drawText (rogus::fr ("ZONE DU RÔLE"), zoneRole.withHeight (16.0f).translated (0.0f, 3.0f),
                    juce::Justification::centred);
    }

    g.setFont (policeChiffres (12.0f));

    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        g.setColour (grille);
        g.drawVerticalLine (juce::roundToInt (xDe (f)), trace.getY(), trace.getBottom());
        g.setColour (texteAxe);
        g.drawText (f >= 1000.0f ? juce::String (juce::roundToInt (f / 1000.0f)) + "k" : juce::String (juce::roundToInt (f)),
                    juce::Rectangle<float> (xDe (f) - 20.0f, trace.getBottom() + 4.0f, 40.0f, 14.0f),
                    juce::Justification::centred);
    }

    for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
    {
        g.setColour (db == 0.0f ? grilleZero : grille);
        g.drawHorizontalLine (juce::roundToInt (yDe (db)), trace.getX(), trace.getRight());
        g.setColour (texteAxe);
        g.drawText ((db > 0.0f ? "+" : "") + juce::String (juce::roundToInt (db)),
                    juce::Rectangle<float> (zone.getX(), yDe (db) - 7.0f, 28.0f, 14.0f),
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

    const bool active = moteur.lireQuantiteEffective() > 0.0f;
    const auto couleurCourbe = active ? accent : texteDoux.withAlpha (0.6f);

    g.saveState();
    g.reduceClipRegion (trace.toNearestInt());

    // Surface entre la courbe et le zéro : on voit d'un coup d'œil ce qui est
    // monté (au-dessus) et ce qui est baissé (en dessous).
    juce::Path surface (chemin);
    surface.lineTo (trace.getX() + (float) (nbPoints - 1), yDe (0.0f));
    surface.lineTo (trace.getX(), yDe (0.0f));
    surface.closeSubPath();
    g.setColour (couleurCourbe.withAlpha (0.12f));
    g.fillPath (surface);

    g.setColour (couleurCourbe);
    g.strokePath (chemin, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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
        g.setColour (violet);
        g.strokePath (total, juce::PathStrokeType (1.6f));
        g.restoreState();
    }

    // Repères des résonances atténuées.
    g.setColour (alerte);

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
    setLookAndFeel (&habillage);

    // Trois grands menus : le rôle (fonction dans le mix), la source (ce qui
    // produit le son) et le son (son caractère). Le titre est dessiné dans le menu.
    auto preparerMenu = [this] (const char* titre, juce::ComboBox& menu, const juce::StringArray& choix)
    {
        // Les choix doivent exister avant l'attache, qui sélectionne le bon.
        menu.setTitle (rogus::fr (titre));
        menu.addItemList (choix, 1);
        addAndMakeVisible (menu);
    };

    preparerMenu ("Rôle",   choixRole,   rogus::nomsRoles());
    preparerMenu ("Source", choixSource, rogus::nomsSources());
    preparerMenu ("Son",    choixSon,    rogus::nomsSons());

    addAndMakeVisible (courbe);

    boutonEcoute.onClick = [this]
    {
        if (moteur.ecouteEnCours()) moteur.arreterEcoute();
        else                        moteur.demarrerEcoute();
    };
    addAndMakeVisible (boutonEcoute);

    auto preparerEtat = [this] (juce::Label& etiquette)
    {
        etiquette.setJustificationType (juce::Justification::topLeft);
        etiquette.setColour (juce::Label::textColourId, texteDoux);
        etiquette.setFont (police (13.0f));
        etiquette.setMinimumHorizontalScale (0.85f);
        etiquette.setBorderSize ({ 0, 0, 0, 0 });
        addAndMakeVisible (etiquette);
    };

    // --- Correction fixe ---
    addAndMakeVisible (boutonActif);
    addAndMakeVisible (potardQuantite);
    preparerEtat (etat);

    // --- Compression : resserre l'écart entre passages forts et faibles ---
    addAndMakeVisible (boutonCompression);
    addAndMakeVisible (potardCompQuantite);
    preparerEtat (etatCompression);

    // --- Démasquage dynamique : la piste cède de la place aux pistes plus prioritaires ---
    boutonDemasquage.setButtonText (rogus::fr ("Démasquage"));
    addAndMakeVisible (boutonDemasquage);
    addAndMakeVisible (potardDemQuantite);
    addAndMakeVisible (potardReactivite);
    preparerEtat (etatDemasquage);

    choixPriorite.setTitle (rogus::fr ("Priorité"));
    choixPriorite.setTooltip (rogus::fr ("Priorité de la piste : Auto prend celle du rôle."));
    choixPriorite.addItemList ({ "Auto", "1", "2", "3", "4", "5" }, 1);
    addAndMakeVisible (choixPriorite);

    // Creux de volume : au lieu de quelques fréquences, c'est la piste entière
    // qui baisse quand une piste plus prioritaire joue (la basse sous le kick).
    boutonCreux.setTooltip (rogus::fr ("La piste entière baisse pendant qu'une piste plus prioritaire joue, puis remonte."));
    addAndMakeVisible (boutonCreux);

    attacheCreux = std::make_unique<AttacheBouton> (moteur.parametres, IdParam::creuxVolume, boutonCreux);
    attacheRole        = std::make_unique<AttacheChoix>   (moteur.parametres, IdParam::role, choixRole);
    attacheSource       = std::make_unique<AttacheChoix>   (moteur.parametres, IdParam::source, choixSource);
    attacheSon          = std::make_unique<AttacheChoix>   (moteur.parametres, IdParam::son, choixSon);
    attachePriorite     = std::make_unique<AttacheChoix>   (moteur.parametres, IdParam::priorite, choixPriorite);
    attacheActif        = std::make_unique<AttacheBouton>  (moteur.parametres, IdParam::correctionActive, boutonActif);
    attacheCompression  = std::make_unique<AttacheBouton>  (moteur.parametres, IdParam::compressionActive, boutonCompression);
    attacheDemasquage   = std::make_unique<AttacheBouton>  (moteur.parametres, IdParam::demasquageActif, boutonDemasquage);
    attacheQuantite     = std::make_unique<AttacheCurseur> (moteur.parametres, IdParam::quantite, potardQuantite.curseur);
    attacheCompQuantite = std::make_unique<AttacheCurseur> (moteur.parametres, IdParam::compressionQuantite, potardCompQuantite.curseur);
    attacheDemQuantite  = std::make_unique<AttacheCurseur> (moteur.parametres, IdParam::demasquageQuantite, potardDemQuantite.curseur);
    attacheReactivite   = std::make_unique<AttacheCurseur> (moteur.parametres, IdParam::reactivite, potardReactivite.curseur);

    suivrePiste (true);
    setSize (700, 768);
    timerCallback();
    startTimerHz (15);     // rafraîchit la courbe et les textes d'état
}

RogusEditor::~RogusEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

// Nom et couleur de la piste, si l'hôte les donne. Sinon : pas d'étiquette,
// accent ambre.
void RogusEditor::suivrePiste (bool forcer)
{
    const auto infos = moteur.piste.lire();

    if (! forcer && infos == piste)
        return;

    piste = infos;
    const auto accent = accentPour (piste);
    habillage.changerAccent (accent);
    courbe.accent = accent;
    repaint();
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

    juce::String phrase = rogus::fr ("Équilibre corrigé d'après l'écoute.");

    if (correction.nbRes > 0)
    {
        juce::StringArray liste;
        for (int i = 0; i < correction.nbRes; ++i)
            liste.add (ecrireFrequence (correction.resHz[(size_t) i]));

        phrase += rogus::fr (" Résonances atténuées : ") + liste.joinIntoString (", ") + ".";
    }

    return phrase;
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

    juce::String phrase = rogus::fr ("Priorité ") + juce::String (moteur.lirePriorite()) + ", " + pistes + ".";

    if (! moteur.lireMesure().refValide)
        return phrase + rogus::fr (" Fais une écoute pour que les autres pistes lui cèdent la place.");

    // La bande où cette piste cède le plus en ce moment, et à qui.
    const auto reductions = moteur.lireReductions();
    size_t bande = 0;

    for (size_t b = 1; b < reductions.size(); ++b)
        if (reductions[b] < reductions[bande])
            bande = b;

    const int role = moteur.lireRoleDominant();
    const bool cedeBande = reductions[bande] < -0.2f && role >= 0;

    if (cedeBande)
        phrase += rogus::fr (" Cède ") + juce::String (-reductions[bande], 1).replaceCharacter ('.', ',') + " dB vers "
               + ecrireFrequence ((float) rogus::frequencesBandes[bande]) + rogus::fr (" à : ")
               + rogus::fr (rogus::role (role).nom) + ".";

    // Le creux de volume en cours : de combien la piste entière baisse, et pour qui.
    const float creux = moteur.lireCreux();
    const int roleCreux = moteur.lireRoleCreux();
    const bool creuse = moteur.lireCreuxActif() && creux < -0.2f;

    if (creuse)
        phrase += rogus::fr (" Volume baissé de ") + juce::String (-creux, 1).replaceCharacter ('.', ',') + " dB"
               + (roleCreux >= 0 ? rogus::fr (" pour : ") + rogus::fr (rogus::role (roleCreux).nom) : juce::String()) + ".";

    if (! cedeBande && ! creuse)
        phrase += rogus::fr (" Ne cède rien en ce moment.");

    return phrase;
}

void RogusEditor::timerCallback()
{
    suivrePiste (false);

    const bool enEcoute = moteur.ecouteEnCours();
    const bool mesureValide = moteur.lireMesure().valide;
    const bool correctionActive = moteur.lireQuantiteEffective() > 0.0f;
    const bool compressionActive = moteur.lireQuantiteCompression() > 0.0f;
    const bool compressionPossible = moteur.lireCorrection().compression.valide;
    const bool demasquageActif = moteur.lireDemasquageActif();

    etat.setText (texteEtat(), juce::dontSendNotification);
    etatCompression.setText (texteCompression(), juce::dontSendNotification);
    etatDemasquage.setText (texteDemasquage(), juce::dontSendNotification);

    // Un texte passe en couleur d'avertissement quand il demande quelque chose
    // à l'utilisateur (faire une écoute).
    etat.setColour (juce::Label::textColourId, moteur.ecouteTropCourte() && ! enEcoute ? avertissement : texteDoux);
    etatCompression.setColour (juce::Label::textColourId,
                               compressionActive && ! compressionPossible ? avertissement : texteDoux);
    etatDemasquage.setColour (juce::Label::textColourId,
                              demasquageActif && ! moteur.lireMesure().refValide ? avertissement : texteDoux);

    // Un potard qui n'agit pas en ce moment est estompé.
    potardQuantite.setAlpha (correctionActive ? 1.0f : 0.45f);
    potardCompQuantite.setAlpha (compressionActive && compressionPossible ? 1.0f : 0.45f);
    potardDemQuantite.setAlpha (demasquageActif ? 1.0f : 0.45f);
    potardReactivite.setAlpha (demasquageActif ? 1.0f : 0.45f);
    boutonCreux.setAlpha (demasquageActif ? 1.0f : 0.45f);

    boutonEcoute.setButtonText (enEcoute ? rogus::fr ("Arrêter l'écoute") : rogus::fr ("Écouter la piste"));

    if ((bool) boutonEcoute.getProperties().getWithDefault ("enEcoute", false) != enEcoute)
    {
        boutonEcoute.getProperties().set ("enEcoute", enEcoute);
        boutonEcoute.repaint();
    }

    // Résumé en une ligne au-dessus de la courbe, avec une pastille de couleur.
    juce::String resume;
    juce::Colour couleur = eteint;

    if (enEcoute)
    {
        resume = rogus::fr ("Écoute : ") + juce::String (juce::roundToInt (moteur.progressionEcoute() * 100.0f)) + " %";
        couleur = habillage.accent();
    }
    else if (moteur.ecouteTropCourte())
    {
        resume = rogus::fr ("Écoute trop courte");
        couleur = avertissement;
    }
    else if (! correctionActive)
    {
        resume = rogus::fr ("Correction à l'arrêt");
    }
    else if (mesureValide)
    {
        resume = rogus::fr ("Corrigé d'après l'écoute");
        couleur = vert;
    }
    else
    {
        resume = rogus::fr ("Pas encore d'écoute");
    }

    if (resume != etatCourt || couleur != pastille)
    {
        etatCourt = resume;
        pastille = couleur;
        repaint (ligneCourbe);
    }

    courbe.repaint();
}

void RogusEditor::paint (juce::Graphics& g)
{
    g.fillAll (fond);

    // Le numéro de version reste visible en haut à droite : il permet de
    // vérifier que l'hôte a bien chargé la dernière version installée.
    dessinerEntete (g, getWidth(), "ROGUS", piste.nom, "v" ROGUS_VERSION, habillage.accent());

    // Le cadre de la courbe, avec sa ligne de titre.
    dessinerPuits (g, cadreCourbe, 10.0f);

    auto ligne = ligneCourbe;
    dessinerTitre (g, rogus::fr ("Équilibre"), ligne.removeFromLeft (82));
    g.setColour (pastille);
    g.fillEllipse ((float) ligne.getX(), (float) ligne.getCentreY() - 3.5f, 7.0f, 7.0f);
    g.setColour (texte);
    g.setFont (police (13.0f));
    g.drawText (etatCourt, ligne.withTrimmedLeft (14), juce::Justification::centredLeft, true);

    // Les trois cartes du bas, et la petite mention à droite de leur titre.
    for (const auto& carteZone : cartes)
        dessinerCarte (g, carteZone, 10.0f);

    g.setColour (texteDoux);
    g.setFont (police (12.0f));
    g.drawText ("EQ",  cartes[0].reduced (14).removeFromTop (24), juce::Justification::centredRight);
    g.drawText ("Dyn", cartes[1].reduced (14).removeFromTop (24), juce::Justification::centredRight);
}

void RogusEditor::resized()
{
    auto zone = getLocalBounds();
    zone.removeFromTop (hauteurEntete);
    zone = zone.reduced (16, 0);
    zone.removeFromTop (14);
    zone.removeFromBottom (16);

    // Trois grands menus de même largeur.
    auto menus = zone.removeFromTop (48);
    const int largeurMenu = (menus.getWidth() - 2 * 10) / 3;
    choixRole.setBounds (menus.removeFromLeft (largeurMenu));
    menus.removeFromLeft (10);
    choixSource.setBounds (menus.removeFromLeft (largeurMenu));
    menus.removeFromLeft (10);
    choixSon.setBounds (menus);
    zone.removeFromTop (12);

    // En bas : les trois cartes, dans l'ordre du trajet du son.
    auto bas = zone.removeFromBottom (300);
    const int largeurCarte = (bas.getWidth() - 2 * 10) / 3;

    for (size_t i = 0; i < cartes.size(); ++i)
    {
        cartes[i] = i + 1 < cartes.size() ? bas.removeFromLeft (largeurCarte) : bas;
        bas.removeFromLeft (10);
    }

    zone.removeFromBottom (12);

    // Au milieu : la courbe, sous une ligne qui porte le résumé et le bouton d'écoute.
    cadreCourbe = zone;
    auto dedans = cadreCourbe.reduced (12, 10);
    ligneCourbe = dedans.removeFromTop (30);
    boutonEcoute.setBounds (ligneCourbe.removeFromRight (168));
    ligneCourbe.removeFromRight (10);
    dedans.removeFromTop (6);
    courbe.setBounds (dedans);

    // Dans une carte : l'interrupteur et son titre, le ou les potards, puis le texte d'état.
    auto placerCarte = [] (juce::Rectangle<int> carteZone, juce::Component& interrupteur, int placeADroite,
                           std::initializer_list<rogus::Potard*> potards, juce::Label& texteEtatCarte,
                           juce::Component* optionDessous = nullptr)
    {
        auto interieur = carteZone.reduced (14);
        interrupteur.setBounds (interieur.removeFromTop (24).withTrimmedRight (placeADroite));
        texteEtatCarte.setBounds (interieur.removeFromBottom (68));

        // Une option de plus (un second interrupteur), entre les potards et le texte.
        if (optionDessous != nullptr)
        {
            interieur.removeFromBottom (8);
            optionDessous->setBounds (interieur.removeFromBottom (24));
        }

        int largeurTotale = 0, hauteur = 0;

        for (auto* p : potards)
        {
            largeurTotale += p->largeurVoulue();
            hauteur = juce::jmax (hauteur, p->hauteurVoulue());
        }

        int x = interieur.getCentreX() - largeurTotale / 2;
        const int y = interieur.getCentreY() - hauteur / 2;

        for (auto* p : potards)
        {
            p->setBounds (x, y, p->largeurVoulue(), p->hauteurVoulue());
            x += p->largeurVoulue();
        }
    };

    placerCarte (cartes[0], boutonActif, 30, { &potardQuantite }, etat);
    placerCarte (cartes[1], boutonCompression, 30, { &potardCompQuantite }, etatCompression);
    placerCarte (cartes[2], boutonDemasquage, 60, { &potardDemQuantite, &potardReactivite }, etatDemasquage, &boutonCreux);

    // Le menu de priorité tient à droite du titre de la carte Démasquage.
    choixPriorite.setBounds (cartes[2].reduced (14).removeFromTop (24).removeFromRight (58));
}
