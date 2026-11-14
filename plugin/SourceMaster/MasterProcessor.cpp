#include "MasterProcessor.h"
#include "MasterEditor.h"
#include "Textes.h"

namespace
{
    // Le plafond de crête réelle. Il n'est pas réglable : -1 dBTP est le bon
    // compromis pour les encodeurs et les plateformes.
    constexpr float plafondDb = -1.0f;

    constexpr float gainMinDb = -24.0f, gainMaxDb = 24.0f;
}

RogusMasterProcessor::RogusMasterProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  (rogus::fr ("Entrée"), juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Sortie", juce::AudioChannelSet::stereo(), true)),
      parametres (*this, nullptr, "ROGUSMASTER", creerParametres())
{
    paramCible = parametres.getRawParameterValue (IdParamMaster::cible);
    paramGain  = parametres.getRawParameterValue (IdParamMaster::gain);
    paramGainObjet = parametres.getParameter (IdParamMaster::gain);

    cibleVue = lireChoixCible();
    startTimerHz (15);
}

RogusMasterProcessor::~RogusMasterProcessor()
{
    stopTimer();
}

juce::AudioProcessorValueTreeState::ParameterLayout RogusMasterProcessor::creerParametres()
{
    juce::AudioProcessorValueTreeState::ParameterLayout disposition;

    // Deux destinations, pas de champ libre : l'utilisateur choisit un contexte,
    // le plugin s'occupe du reste.
    disposition.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { IdParamMaster::cible, 1 }, rogus::fr ("Cible"),
        juce::StringArray { rogus::fr ("Streaming -14"), rogus::fr ("Fort -11") }, 0));

    // Le gain est calculé par l'écoute, mais reste un paramètre : l'hôte
    // l'enregistre et l'automation peut le piloter.
    disposition.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { IdParamMaster::gain, 1 }, "Gain",
        juce::NormalisableRange<float> (gainMinDb, gainMaxDb, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    return disposition;
}

float RogusMasterProcessor::lireCibleLufs() const noexcept
{
    // Choix 0 = streaming -14, choix 1 = fort -11.
    return lireChoixCible() == 0 ? -14.0f : -11.0f;
}

int RogusMasterProcessor::lireChoixCible() const noexcept
{
    return juce::jlimit (0, 1, juce::roundToInt (paramCible->load()));
}

float RogusMasterProcessor::secondesEcoute() const noexcept
{
    const double fs = frequenceEchVue.load();
    return fs > 0.0 ? (float) ((double) echantillonsEcoute.load() / fs) : 0.0f;
}

void RogusMasterProcessor::demarrerEcoute()
{
    reinitialiserMesure.store (true);
    ecouteActive.store (true);
    echantillonsEcoute.store (0);
}

void RogusMasterProcessor::arreterEtRegler()
{
    if (! ecouteActive.exchange (false))
        return;

    const float lufs = mesure.lufsIntegre();

    if (lufs <= -70.0f)
        return;      // pas assez de son : on ne change rien

    const float gainActuel = paramGain->load();

    lufsMesure = lufs;
    gainPendantMesure = gainActuel;

    // Le gain est relatif : on retire l'écart entre la cible et ce qui a été
    // mesuré. Un second réglage n'est utile que si le limiteur change beaucoup
    // le résultat.
    appliquerGain (gainActuel + (lireCibleLufs() - lufs));
}

void RogusMasterProcessor::appliquerGain (float db) noexcept
{
    if (paramGainObjet == nullptr)
        return;

    const float valeur = juce::jlimit (gainMinDb, gainMaxDb, db);
    paramGainObjet->setValueNotifyingHost (paramGainObjet->convertTo0to1 (valeur));
}

void RogusMasterProcessor::mettreAJour()
{
    timerCallback();
}

void RogusMasterProcessor::timerCallback()
{
    const int choix = lireChoixCible();

    if (choix == cibleVue)
        return;

    // La cible a changé : la mesure précédente suffit pour recalculer le gain.
    // On ne le fait pas pendant une écoute, qui est en train de mesurer.
    if (! ecouteActive.load() && lufsMesure > -70.0f)
        appliquerGain (gainPendantMesure + (lireCibleLufs() - lufsMesure));

    cibleVue = choix;
}

void RogusMasterProcessor::prepareToPlay (double frequenceEchantillonnage, int tailleBloc)
{
    frequenceEchVue.store (frequenceEchantillonnage);

    const int canaux = juce::jmax (1, getTotalNumInputChannels());
    const int tailleMax = juce::jmax (tailleBloc, 4096);

    mesure.prepare (frequenceEchantillonnage, canaux, tailleMax);
    limiteur.prepare (frequenceEchantillonnage, canaux, tailleMax);

    setLatencySamples (limiteur.latenceEchantillons());

    reinitialiserMesure.store (true);
    ecouteActive.store (false);
    echantillonsEcoute.store (0);
    reductionVue.store (0.0f);
}

bool RogusMasterProcessor::isBusesLayoutSupported (const BusesLayout& disposition) const
{
    const auto sortie = disposition.getMainOutputChannelSet();

    if (sortie != juce::AudioChannelSet::mono() && sortie != juce::AudioChannelSet::stereo())
        return false;

    return disposition.getMainInputChannelSet() == sortie;
}

void RogusMasterProcessor::processBlock (juce::AudioBuffer<float>& tampon, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals sansDenormaux;

    const int n = tampon.getNumSamples();
    const int canaux = juce::jmin (2, getTotalNumInputChannels(), tampon.getNumChannels());

    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        tampon.clear (c, 0, n);

    if (n <= 0 || canaux <= 0)
        return;

    if (reinitialiserMesure.exchange (false))
    {
        mesure.reset();
        echantillonsEcoute.store (0);
    }

    const float gain = paramGain->load();
    float reduction = 0.0f;

    limiteur.traiter (tampon, canaux, gain, plafondDb, reduction);
    mesure.traiter (tampon, canaux);

    reductionVue.store (reduction);

    if (ecouteActive.load())
        echantillonsEcoute.fetch_add (n);
}

void RogusMasterProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    auto etat = parametres.copyState();

    // La mesure qui a servi à caler le gain est gardée avec le projet : changer
    // de cible après coup ne demande pas de réécouter le morceau.
    juce::ValueTree noeud ("MesureMaster");
    noeud.setProperty ("lufs", lufsMesure, nullptr);
    noeud.setProperty ("gainMesure", gainPendantMesure, nullptr);
    etat.appendChild (noeud, nullptr);

    if (auto xml = etat.createXml())
        copyXmlToBinary (*xml, destination);
}

void RogusMasterProcessor::setStateInformation (const void* donnees, int taille)
{
    auto xml = getXmlFromBinary (donnees, taille);

    if (xml == nullptr || ! xml->hasTagName (parametres.state.getType()))
        return;

    auto etat = juce::ValueTree::fromXml (*xml);
    const auto noeud = etat.getChildWithName ("MesureMaster");

    if (noeud.isValid())
    {
        lufsMesure = (float) noeud.getProperty ("lufs", -100.0f);
        gainPendantMesure = (float) noeud.getProperty ("gainMesure", 0.0f);
        etat.removeChild (noeud, nullptr);
    }
    else
    {
        lufsMesure = -100.0f;
        gainPendantMesure = 0.0f;
    }

    parametres.replaceState (etat);

    // On ne recalcule pas le gain au chargement : il a été enregistré tel quel.
    cibleVue = lireChoixCible();
}

juce::AudioProcessorEditor* RogusMasterProcessor::createEditor()
{
    return new RogusMasterEditor (*this);
}

// Point d'entrée appelé par l'hôte pour créer une instance du plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RogusMasterProcessor();
}
