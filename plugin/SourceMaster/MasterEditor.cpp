#include "MasterEditor.h"
#include "Textes.h"

#include <cmath>

namespace
{
    const juce::Colour couleurFond   { 0xff1e2428 };
    const juce::Colour couleurCadre  { 0xff161b1e };
    const juce::Colour couleurTexte  { 0xffa9b4bb };
    const juce::Colour couleurAccent { 0xff4fb3e8 };
    const juce::Colour couleurOk     { 0xff79c36a };
    const juce::Colour couleurAlerte { 0xffe0764f };
}

RogusMasterEditor::RogusMasterEditor (RogusMasterProcessor& m)
    : AudioProcessorEditor (&m), moteur (m)
{
    setSize (560, 480);

    etiquetteCible.setText (rogus::fr ("Cible"), juce::dontSendNotification);
    etiquetteCible.setJustificationType (juce::Justification::centredRight);
    etiquetteCible.setColour (juce::Label::textColourId, couleurTexte);
    addAndMakeVisible (etiquetteCible);

    choixCible.addItemList ({ rogus::fr ("Streaming -14"), rogus::fr ("Fort -11") }, 1);
    addAndMakeVisible (choixCible);
    attacheCible = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        moteur.parametres, IdParamMaster::cible, choixCible);

    lufsTitre.setText (rogus::fr ("LUFS intégré"), juce::dontSendNotification);
    lufsTitre.setJustificationType (juce::Justification::centred);
    lufsTitre.setFont (juce::FontOptions (12.0f));
    lufsTitre.setColour (juce::Label::textColourId, couleurTexte);
    addAndMakeVisible (lufsTitre);

    lufsValeur.setText (rogus::fr ("—"), juce::dontSendNotification);
    lufsValeur.setJustificationType (juce::Justification::centred);
    lufsValeur.setFont (juce::FontOptions (46.0f).withStyle ("Bold"));
    lufsValeur.setColour (juce::Label::textColourId, couleurAccent);
    addAndMakeVisible (lufsValeur);

    cibleValeur.setText (rogus::fr ("Cible : -14,0 LUFS"), juce::dontSendNotification);
    cibleValeur.setJustificationType (juce::Justification::centred);
    cibleValeur.setFont (juce::FontOptions (13.0f));
    cibleValeur.setColour (juce::Label::textColourId, couleurTexte);
    addAndMakeVisible (cibleValeur);

    auto preparerPetit = [this] (juce::Label& titre, juce::Label& valeur, const char* texte)
    {
        titre.setText (rogus::fr (texte), juce::dontSendNotification);
        titre.setJustificationType (juce::Justification::centred);
        titre.setFont (juce::FontOptions (11.0f));
        titre.setColour (juce::Label::textColourId, couleurTexte);
        addAndMakeVisible (titre);

        valeur.setText (rogus::fr ("—"), juce::dontSendNotification);
        valeur.setJustificationType (juce::Justification::centred);
        valeur.setFont (juce::FontOptions (20.0f));
        valeur.setColour (juce::Label::textColourId, couleurTexte);
        addAndMakeVisible (valeur);
    };

    preparerPetit (courtTitre, courtValeur, "Court terme (3 s)");
    preparerPetit (creteTitre, creteValeur, "Crête réelle");
    preparerPetit (margeTitre, margeValeur, "Marge dynamique");
    preparerPetit (gainTitre, gainValeur, "Gain appliqué");
    preparerPetit (reductionTitre, reductionValeur, "Limiteur");

    etat.setJustificationType (juce::Justification::centredLeft);
    etat.setColour (juce::Label::textColourId, couleurTexte);
    etat.setFont (juce::FontOptions (13.0f));
    etat.setMinimumHorizontalScale (0.75f);
    addAndMakeVisible (etat);

    boutonEcoute.onClick = [this]
    {
        if (moteur.ecouteEnCours())
            moteur.arreterEtRegler();
        else
            moteur.demarrerEcoute();
    };
    addAndMakeVisible (boutonEcoute);

    timerCallback();
    startTimerHz (15);
}

RogusMasterEditor::~RogusMasterEditor()
{
    stopTimer();
}

juce::String RogusMasterEditor::ecrireDb (float db, int decimales) const
{
    juce::String texte (db, decimales);
    texte = texte.replaceCharacter ('.', ',');

    if (db > 0.0f)
        texte = "+" + texte;

    return texte;
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
    {
        return rogus::fr ("Dernière mesure : ") + ecrireDb (moteur.lireLufsMesure(), 1)
             + rogus::fr (" LUFS. Changer de cible ne demande pas de réécouter.");
    }

    return rogus::fr ("Clique sur Écouter le morceau, puis joue-le en entier.");
}

void RogusMasterEditor::timerCallback()
{
    boutonEcoute.setButtonText (rogus::fr (moteur.ecouteEnCours() ? "Arrêter et régler"
                                                                 : "Écouter le morceau"));

    const bool mesure = moteur.mesureDisponible();
    const float lufs = mesure ? moteur.lireLufsIntegre() : moteur.lireLufsMesure();
    const bool affichable = lufs > -70.0f;

    lufsValeur.setText (affichable ? ecrireDb (lufs, 1) : rogus::fr ("—"),
                        juce::dontSendNotification);
    cibleValeur.setText (rogus::fr ("Cible : ") + ecrireDb (moteur.lireCibleLufs(), 1) + " LUFS",
                         juce::dontSendNotification);

    if (affichable)
    {
        const float ecart = lufs - moteur.lireCibleLufs();
        const auto couleur = ! mesure ? couleurTexte
                           : (std::abs (ecart) < 0.5f ? couleurOk
                           : (ecart > 0.0f ? couleurAlerte : couleurAccent));
        lufsValeur.setColour (juce::Label::textColourId, couleur);
    }
    else
    {
        lufsValeur.setColour (juce::Label::textColourId, couleurTexte.withAlpha (0.5f));
    }

    const float court = moteur.lireLufsCourtTerme();
    const float crete = moteur.lireCreteReelle();
    const float marge = moteur.lireMargeDynamique();

    courtValeur.setText (court > -70.0f ? ecrireDb (court, 1) : rogus::fr ("—"),
                         juce::dontSendNotification);
    creteValeur.setText (crete > -100.0f ? ecrireDb (crete, 1) + " dBTP" : rogus::fr ("—"),
                         juce::dontSendNotification);
    margeValeur.setText (marge > -70.0f ? ecrireDb (marge, 1) + " dB" : rogus::fr ("—"),
                         juce::dontSendNotification);
    gainValeur.setText (ecrireDb (moteur.lireGain(), 1) + " dB", juce::dontSendNotification);
    reductionValeur.setText (ecrireDb (moteur.lireReduction(), 1) + " dB", juce::dontSendNotification);

    creteValeur.setColour (juce::Label::textColourId,
                           crete > -1.0f ? couleurAlerte : couleurTexte);

    etat.setText (texteEtat(), juce::dontSendNotification);
}

void RogusMasterEditor::paint (juce::Graphics& g)
{
    g.fillAll (couleurFond);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (20.0f));
    g.drawText ("Rogus Master", getLocalBounds().reduced (16, 0).removeFromTop (40),
                juce::Justification::centredLeft);

    g.setColour (couleurCadre);
    g.fillRoundedRectangle (cadreLufs.toFloat(), 8.0f);
}

void RogusMasterEditor::resized()
{
    auto zone = getLocalBounds().reduced (16);
    zone.removeFromTop (34);                    // titre

    auto ligneCible = zone.removeFromTop (30);
    etiquetteCible.setBounds (ligneCible.removeFromLeft (60));
    choixCible.setBounds (ligneCible.removeFromRight (220).reduced (0, 3));

    zone.removeFromTop (8);

    cadreLufs = zone.removeFromTop (122);
    auto dedans = cadreLufs.reduced (8);
    lufsTitre.setBounds (dedans.removeFromTop (20));
    cibleValeur.setBounds (dedans.removeFromBottom (20));
    lufsValeur.setBounds (dedans);

    zone.removeFromTop (8);

    auto colonnes = zone.removeFromTop (58);
    const int largeur = colonnes.getWidth() / 3;

    auto placerColonne = [] (juce::Rectangle<int> colonne,
                             juce::Label& titre, juce::Label& valeur)
    {
        titre.setBounds (colonne.removeFromTop (16));
        valeur.setBounds (colonne.removeFromTop (28));
    };

    auto c1 = colonnes.removeFromLeft (largeur);
    auto c2 = colonnes.removeFromLeft (largeur);
    placerColonne (c1, courtTitre, courtValeur);
    placerColonne (c2, creteTitre, creteValeur);
    placerColonne (colonnes, margeTitre, margeValeur);

    zone.removeFromTop (4);

    auto ligneGain = zone.removeFromTop (50);
    auto g1 = ligneGain.removeFromLeft (ligneGain.getWidth() / 2);
    gainTitre.setBounds (g1.removeFromTop (16));
    gainValeur.setBounds (g1.removeFromTop (28));
    reductionTitre.setBounds (ligneGain.removeFromTop (16));
    reductionValeur.setBounds (ligneGain.removeFromTop (28));

    zone.removeFromTop (6);

    boutonEcoute.setBounds (zone.removeFromBottom (38).reduced (60, 0));
    zone.removeFromBottom (8);
    etat.setBounds (zone.removeFromBottom (58));
}
