#include "MasterEditor.h"
#include "Textes.h"

#include <cmath>

namespace
{
    using namespace rogus::console;

    // Échelle de la barre de niveau : de -40 à 0 LUFS.
    constexpr float lufsMin = -40.0f, lufsMax = 0.0f;
}

RogusMasterEditor::RogusMasterEditor (RogusMasterProcessor& m)
    : AudioProcessorEditor (&m), moteur (m)
{
    setLookAndFeel (&habillage);

    choixCible.setTitle (rogus::fr ("Cible"));
    choixCible.addItemList ({ rogus::fr ("Streaming −14"), rogus::fr ("Fort −11") }, 1);
    addAndMakeVisible (choixCible);
    attacheCible = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        moteur.parametres, IdParamMaster::cible, choixCible);

    boutonEcoute.getProperties().set ("plein", true);
    boutonEcoute.onClick = [this]
    {
        if (moteur.ecouteEnCours())
            moteur.arreterEtRegler();
        else
            moteur.demarrerEcoute();
    };
    addAndMakeVisible (boutonEcoute);

    suivrePiste (true);
    setSize (620, 576);
    timerCallback();
    startTimerHz (15);
}

RogusMasterEditor::~RogusMasterEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

// Nom et couleur de la piste, si l'hôte les donne. Sinon : pas d'étiquette,
// accent ambre.
void RogusMasterEditor::suivrePiste (bool forcer)
{
    const auto infos = moteur.piste.lire();

    if (! forcer && infos == piste)
        return;

    piste = infos;
    habillage.changerAccent (accentPour (piste));
}

juce::String RogusMasterEditor::ecrireDb (float db, int decimales) const
{
    // Virgule décimale et vrai signe moins, plus lisible qu'un trait d'union.
    juce::String texteDb (std::abs (db), decimales);
    texteDb = texteDb.replaceCharacter ('.', ',');

    const bool nul = texteDb.containsOnly ("0,");

    if (db < 0.0f && ! nul)
        texteDb = rogus::fr ("−") + texteDb;
    else if (db > 0.0f && ! nul)
        texteDb = "+" + texteDb;

    return texteDb;
}

juce::String RogusMasterEditor::texteEtat() const
{
    if (moteur.ecouteEnCours())
    {
        const int total = juce::jmax (0, (int) moteur.secondesEcoute());
        const juce::String temps = juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);

        return rogus::fr ("Écoute : ") + temps
             + rogus::fr (" — joue le morceau en entier, puis Arrêter et régler.");
    }

    if (moteur.mesureDisponible())
    {
        return rogus::fr ("Mesure : ") + ecrireDb (moteur.lireLufsIntegre(), 1)
             + rogus::fr (" LUFS. Gain : ") + ecrireDb (moteur.lireGain(), 1)
             + rogus::fr (" dB. Refais une écoute si le mix change.");
    }

    if (moteur.lireLufsMesure() > -70.0f)
        return rogus::fr ("Changer de cible ne demande pas de réécouter.");

    return rogus::fr ("Clique sur Écouter le morceau, puis joue-le en entier.");
}

void RogusMasterEditor::timerCallback()
{
    suivrePiste (false);

    const bool enEcoute = moteur.ecouteEnCours();
    boutonEcoute.setButtonText (rogus::fr (enEcoute ? "Arrêter et régler" : "Écouter le morceau"));
    boutonEcoute.getProperties().set ("enEcoute", enEcoute);

    // Les compteurs bougent en continu pendant la lecture : on redessine tout.
    repaint();
}

void RogusMasterEditor::dessinerCompteur (juce::Graphics& g, juce::Rectangle<int> zone, const char* titre,
                                          const juce::String& valeur, const juce::String& unite,
                                          juce::Colour couleur) const
{
    dessinerCarte (g, zone);
    auto dedans = zone.reduced (13, 9);
    dessinerTitre (g, rogus::fr (titre), dedans.removeFromTop (14));

    const auto policeValeur = policeChiffres (21.0f);
    g.setColour (couleur);
    g.setFont (policeValeur);
    g.drawText (valeur, dedans, juce::Justification::bottomLeft);

    if (unite.isNotEmpty())
    {
        g.setColour (texteDoux);
        g.setFont (policeChiffres (13.0f));
        g.drawText (unite, dedans.withTrimmedLeft (juce::roundToInt (largeurTexte (policeValeur, valeur)) + 5)
                                 .withTrimmedBottom (2),
                    juce::Justification::bottomLeft);
    }
}

// Le grand cadre : le volume perçu du morceau, la cible, et une barre qui
// montre le chemin qui reste entre les deux.
void RogusMasterEditor::dessinerLufs (juce::Graphics& g, juce::Rectangle<int> zone) const
{
    dessinerPuits (g, zone, 10.0f);
    auto dedans = zone.reduced (18, 14);

    const bool mesure = moteur.mesureDisponible();
    const float lufs = mesure ? moteur.lireLufsIntegre() : moteur.lireLufsMesure();
    const float cible = moteur.lireCibleLufs();
    const bool affichable = lufs > -70.0f;
    const float ecart = lufs - cible;
    const auto accent = habillage.accent();

    // Vert quand la cible est atteinte, alerte quand le morceau est trop fort.
    const auto couleurLufs = ! affichable ? texteDoux.withAlpha (0.5f)
                           : ! mesure ? texte
                           : std::abs (ecart) < 0.5f ? vert
                           : ecart > 0.0f ? alerte : texte;

    auto haut = dedans.removeFromTop (96);
    auto droite = haut.removeFromRight (150);

    dessinerTitre (g, rogus::fr ("LUFS intégré"), haut.removeFromTop (14));
    g.setColour (couleurLufs);
    g.setFont (policeChiffres (78.0f, true));
    g.drawText (affichable ? ecrireDb (lufs, 1) : rogus::fr ("—"), haut, juce::Justification::bottomLeft);

    droite.removeFromTop (30);
    g.setColour (texteDoux);
    g.setFont (police (13.0f));
    g.drawText ("Cible", droite.removeFromTop (16), juce::Justification::centredRight);
    g.setColour (accent);
    g.setFont (policeChiffres (23.0f));
    g.drawText (ecrireDb (cible, 1), droite.removeFromTop (28), juce::Justification::centredRight);

    if (affichable)
    {
        g.setColour (texteDoux);
        g.setFont (police (13.0f));
        g.drawText (rogus::fr ("écart ") + ecrireDb (std::abs (ecart), 1).removeCharacters ("+") + " LU",
                    droite.removeFromTop (16), juce::Justification::centredRight);
    }

    dedans.removeFromTop (10);

    // La barre : en gris le niveau mesuré, en couleur ce qu'il reste à gagner,
    // et un trait à la cible.
    const auto barre = dedans.removeFromTop (12).toFloat();
    auto xDe = [&] (float l) { return barre.getX() + barre.getWidth() * juce::jlimit (0.0f, 1.0f, (l - lufsMin) / (lufsMax - lufsMin)); };

    g.setColour (juce::Colour (0xff1e2226));
    g.fillRoundedRectangle (barre, 3.0f);

    if (affichable)
    {
        g.saveState();
        juce::Path arrondi;
        arrondi.addRoundedRectangle (barre, 3.0f);
        g.reduceClipRegion (arrondi);
        g.setColour (eteint);
        g.fillRect (barre.withRight (xDe (lufs)));

        if (lufs < cible)
        {
            g.setColour (accent.withAlpha (0.22f));
            g.fillRect (barre.withLeft (xDe (lufs)).withRight (xDe (cible)));
        }

        g.restoreState();
    }

    g.setColour (accent);
    g.fillRect (xDe (cible) - 1.5f, barre.getY() - 2.0f, 3.0f, barre.getHeight() + 4.0f);

    // Graduations sous la barre. Celle de la cible remplace sa voisine si elles se touchent.
    dedans.removeFromTop (5);
    const auto ligneEchelle = dedans.removeFromTop (14).toFloat();
    g.setFont (policeChiffres (11.0f));

    auto graduation = [&] (float l, juce::Colour couleur)
    {
        const float x = xDe (l);
        const auto justification = l <= lufsMin ? juce::Justification::centredLeft
                                 : l >= lufsMax ? juce::Justification::centredRight
                                                : juce::Justification::centred;
        const float gauche = l <= lufsMin ? x : l >= lufsMax ? x - 40.0f : x - 20.0f;
        g.setColour (couleur);
        g.drawText (ecrireDb (l, 0), juce::Rectangle<float> (gauche, ligneEchelle.getY(), 40.0f, ligneEchelle.getHeight()),
                    justification);
    };

    for (float l : { -40.0f, -30.0f, -20.0f, -10.0f, 0.0f })
        if (std::abs (l - cible) > 2.5f)
            graduation (l, texteAxe);

    graduation (cible, accent);
}

void RogusMasterEditor::paint (juce::Graphics& g)
{
    g.fillAll (fond);

    // Le numéro de version reste visible en haut à droite : il permet de
    // vérifier que l'hôte a bien chargé la dernière version installée.
    dessinerEntete (g, getWidth(), "ROGUS MASTER", piste.nom, "v" ROGUS_VERSION, habillage.accent());

    // À droite du menu : la dernière mesure faite par une écoute.
    auto derniere = zoneDerniere.reduced (6, 5);
    dessinerTitre (g, rogus::fr ("Dernière mesure"), derniere.removeFromTop (14));
    const float lufsMesure = moteur.lireLufsMesure();
    g.setColour (texte);
    g.setFont (policeChiffres (17.0f));
    g.drawText (lufsMesure > -70.0f ? ecrireDb (lufsMesure, 1) + " LUFS" : rogus::fr ("—"),
                derniere, juce::Justification::bottomLeft);

    dessinerLufs (g, cadreLufs);

    const float court = moteur.lireLufsCourtTerme();
    const float crete = moteur.lireCreteReelle();
    const float marge = moteur.lireMargeDynamique();
    const juce::String vide = rogus::fr ("—");

    dessinerCompteur (g, compteurs[0], "Court terme 3 s",
                      court > -70.0f ? ecrireDb (court, 1) : vide, court > -70.0f ? "LUFS" : "", texte);

    // La crête réelle doit rester sous le plafond de -1 dBTP : au-dessus, alerte.
    dessinerCompteur (g, compteurs[1], "Crête réelle",
                      crete > -100.0f ? ecrireDb (crete, 1) : vide, crete > -100.0f ? "dBTP" : "",
                      crete > -1.0f ? alerte : texte);
    dessinerCompteur (g, compteurs[2], "Marge dynamique",
                      marge > -70.0f ? ecrireDb (marge, 1).removeCharacters ("+") : vide, marge > -70.0f ? "dB" : "", texte);
    dessinerCompteur (g, compteurs[3], "Gain appliqué", ecrireDb (moteur.lireGain(), 1), "dB", habillage.accent());
    dessinerCompteur (g, compteurs[4], "Limiteur", ecrireDb (moteur.lireReduction(), 1), "dB", texte);

    g.setColour (texteDoux);
    g.setFont (police (13.0f));
    g.drawFittedText (texteEtat(), zoneEtat, juce::Justification::centredLeft, 2, 0.85f);
}

void RogusMasterEditor::resized()
{
    auto zone = getLocalBounds();
    zone.removeFromTop (hauteurEntete);
    zone = zone.reduced (16, 0);
    zone.removeFromTop (14);
    zone.removeFromBottom (16);

    auto ligneCible = zone.removeFromTop (48);
    choixCible.setBounds (ligneCible.removeFromLeft ((ligneCible.getWidth() - 12) / 2));
    ligneCible.removeFromLeft (12);
    zoneDerniere = ligneCible;
    zone.removeFromTop (12);

    cadreLufs = zone.removeFromTop (166);
    zone.removeFromTop (12);

    // Trois compteurs de mesure, puis deux compteurs d'action (gain, limiteur).
    auto ligne = zone.removeFromTop (62);
    const int tiers = (ligne.getWidth() - 2 * 10) / 3;
    compteurs[0] = ligne.removeFromLeft (tiers);
    ligne.removeFromLeft (10);
    compteurs[1] = ligne.removeFromLeft (tiers);
    ligne.removeFromLeft (10);
    compteurs[2] = ligne;
    zone.removeFromTop (10);

    ligne = zone.removeFromTop (62);
    compteurs[3] = ligne.removeFromLeft ((ligne.getWidth() - 10) / 2);
    ligne.removeFromLeft (10);
    compteurs[4] = ligne;

    boutonEcoute.setBounds (zone.removeFromBottom (48));
    zone.removeFromBottom (8);
    zoneEtat = zone.removeFromBottom (36);
}
