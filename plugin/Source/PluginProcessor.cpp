#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Une trame d'analyse toutes les 4096 mesures : les trames se recouvrent
    // de moitié, ce qui compense l'atténuation des bords par la fenêtre.
    constexpr int pasAnalyse = rogus::tailleFft / 2;

    // En dessous de -50 dBFS, on considère que personne ne joue : la trame
    // est ignorée, pour que les silences ne faussent pas l'équilibre mesuré.
    constexpr float seuilSilence = 0.00316f;

    // Temps de glissement des filtres quand un réglage change.
    constexpr double tempsLissage = 0.05;

    const juce::Identifier idMesure { "Mesure" };

    // --- Compatibilité avec les projets enregistrés par les versions précédentes ---
    //
    // L'état du plugin est écrit dans le projet de l'hôte. Une nouvelle version doit
    // toujours pouvoir relire ce qu'une ancienne a écrit, sans rien faire perdre.
    // Formats connus :
    //   sans « version », avec coupeBasActif / coupeBasFreq : étape 1
    //   sans « version », avec role (6 choix)                : étape 2
    //   version 3 : trois menus (rôle sur 9 choix, source, son)
    // Quand un format change de façon incompatible, on augmente `versionEtat` et on
    // ajoute ici la conversion depuis la version précédente.
    constexpr int versionEtat = 3;

    juce::ValueTree trouverParametre (const juce::ValueTree& etat, const juce::String& id)
    {
        for (int i = 0; i < etat.getNumChildren(); ++i)
        {
            const auto enfant = etat.getChild (i);

            if (enfant.hasType ("PARAM") && enfant.getProperty ("id").toString() == id)
                return enfant;
        }

        return {};
    }

    void poserParametre (juce::ValueTree etat, const juce::String& id, float valeur)
    {
        auto parametre = trouverParametre (etat, id);

        if (! parametre.isValid())
        {
            parametre = juce::ValueTree ("PARAM");
            parametre.setProperty ("id", id, nullptr);
            etat.appendChild (parametre, nullptr);
        }

        parametre.setProperty ("value", valeur, nullptr);
    }

    void migrerEtat (juce::ValueTree& etat)
    {
        if ((int) etat.getProperty ("version", 0) >= versionEtat)
            return;

        if (const auto ancienRole = trouverParametre (etat, "role"); ancienRole.isValid())
        {
            // Étape 2 : le rôle mélangeait fonction et instrument. Ancienne liste :
            // guitare lead, guitare rythmique, guitare acoustique, voix, basse, kick.
            // Chaque entrée devient un rôle (fonction) et une source (instrument).
            constexpr int nouveauRole[]   = { 0, 1, 1, 7, 3, 4 };   // Lead, Rythmique, Rythmique, Voix, Basse, Kick
            constexpr int nouvelleSource[] = { 0, 0, 1, 2, 3, 4 };  // Guitare él., Guitare él., Guitare ac., Voix, Basse, Batterie

            const int ancien = juce::jlimit (0, 5, juce::roundToInt ((float) ancienRole.getProperty ("value", 0.0f)));
            poserParametre (etat, "role", (float) nouveauRole[ancien]);
            poserParametre (etat, "source", (float) nouvelleSource[ancien]);
            // « quantite » et « correctionActive » gardent le même sens : rien à convertir.
            // « son » n'existait pas : il reste à sa valeur par défaut (Clair).
        }
        else if (const auto ancienActif = trouverParametre (etat, "coupeBasActif"); ancienActif.isValid())
        {
            // Étape 1 : un simple coupe-bas. On garde l'interrupteur (correction active ou non).
            // La fréquence réglée à la main n'a plus d'équivalent : le coupe-bas dépend
            // maintenant du rôle et de la source.
            poserParametre (etat, "correctionActive", (float) ancienActif.getProperty ("value", 1.0f));
        }

        etat.setProperty ("version", versionEtat, nullptr);
    }
}

RogusProcessor::RogusProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  (rogus::fr ("Entrée"), juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Sortie", juce::AudioChannelSet::stereo(), true)),
      parametres (*this, nullptr, "ROGUS", creerParametres())
{
    paramRole     = parametres.getRawParameterValue (IdParam::role);
    paramSource   = parametres.getRawParameterValue (IdParam::source);
    paramSon      = parametres.getRawParameterValue (IdParam::son);
    paramActif    = parametres.getRawParameterValue (IdParam::correctionActive);
    paramQuantite = parametres.getRawParameterValue (IdParam::quantite);

    // Toute la mémoire de l'analyse est réservée ici, une fois pour toutes.
    anneau.assign ((size_t) rogus::tailleFft, 0.0f);
    donneesFft.assign ((size_t) rogus::tailleFft * 2, 0.0f);
    sommeSpectre.assign ((size_t) rogus::tailleFft / 2 + 1, 0.0);
    cumulSpectre.assign ((size_t) rogus::tailleFft / 2 + 2, 0.0);

    recalculer();          // la correction du rôle par défaut est prête dès le premier bloc
    startTimerHz (15);
}

RogusProcessor::~RogusProcessor()
{
    stopTimer();
}

juce::AudioProcessorValueTreeState::ParameterLayout RogusProcessor::creerParametres()
{
    juce::AudioProcessorValueTreeState::ParameterLayout disposition;

    // Le « 1 » est le numéro de version du paramètre : il permet d'en ajouter
    // d'autres plus tard sans perturber les projets déjà enregistrés.
    disposition.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { IdParam::role, 1 }, rogus::fr ("Rôle"), rogus::nomsRoles(), 0));

    disposition.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { IdParam::source, 1 }, "Source", rogus::nomsSources(), 0));

    disposition.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { IdParam::son, 1 }, "Son", rogus::nomsSons(), 0));

    disposition.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { IdParam::correctionActive, 1 }, "Correction fixe", true));

    disposition.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { IdParam::quantite, 1 }, rogus::fr ("Quantité"),
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 60.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    return disposition;
}

// --- Lecture de l'état par l'interface ---------------------------------------

int RogusProcessor::lireRole() const noexcept
{
    return juce::jlimit (0, rogus::nbRoles - 1, juce::roundToInt (paramRole->load()));
}

int RogusProcessor::lireSource() const noexcept
{
    return juce::jlimit (0, rogus::nbSources - 1, juce::roundToInt (paramSource->load()));
}

int RogusProcessor::lireSon() const noexcept
{
    return juce::jlimit (0, rogus::nbSons - 1, juce::roundToInt (paramSon->load()));
}

// Les trois choix réunis en un seul nombre, pour repérer d'un coup d'œil
// si l'un d'eux a changé depuis le dernier calcul.
int RogusProcessor::cleConfig() const noexcept
{
    return lireRole() | (lireSource() << 8) | (lireSon() << 16);
}

float RogusProcessor::lireQuantiteEffective() const noexcept
{
    return paramActif->load() > 0.5f ? juce::jlimit (0.0f, 1.0f, paramQuantite->load() * 0.01f) : 0.0f;
}

rogus::Correction RogusProcessor::lireCorrection() const
{
    const juce::ScopedLock verrou (verrouDonnees);
    return correction;
}

rogus::Mesure RogusProcessor::lireMesure() const
{
    const juce::ScopedLock verrou (verrouDonnees);
    return mesure;
}

// --- Calcul de la correction (hors fil audio) --------------------------------

void RogusProcessor::recalculer()
{
    // On note la configuration AVANT de calculer : si l'utilisateur change un
    // menu pendant le calcul, la différence sera vue et le calcul refait.
    const int cle = cleConfig();
    const auto nouvelle = rogus::calculerCorrection (rogus::role (cle & 0xff),
                                                     rogus::source ((cle >> 8) & 0xff),
                                                     rogus::son ((cle >> 16) & 0xff),
                                                     lireMesure());

    {
        const juce::ScopedLock verrou (verrouDonnees);
        correction = nouvelle;
    }

    {
        const juce::SpinLock::ScopedLockType verrou (verrouAudio);
        correctionPourAudio = nouvelle;
    }

    configCalculee.store (cle);
    versionCorrection.fetch_add (1);      // signale au fil audio qu'il y a du nouveau
}

void RogusProcessor::mettreAJour()
{
    // Une écoute vient de se terminer : le fil audio a rendu la main sur le
    // spectre accumulé, on peut le lire sans risque.
    if (analysePrete.exchange (false))
    {
        const auto nouvelleMesure = rogus::analyser (sommeSpectre, frequenceEchPartagee.load(),
                                                     &compteursBosses, trames);

        if (nouvelleMesure.valide)
        {
            {
                const juce::ScopedLock verrou (verrouDonnees);
                mesure = nouvelleMesure;
            }
            recalculer();
        }

        ecouteActive.store (false);
    }

    // Le rôle, la source ou le son a changé : même mesure, autre cible, donc
    // autre correction. Aucune nouvelle écoute n'est nécessaire.
    if (cleConfig() != configCalculee.load())
        recalculer();
}

void RogusProcessor::demarrerEcoute()
{
    tropCourte.store (false);
    progression.store (0.0f);
    demandeArret.store (false);
    ecouteActive.store (true);
    demandeDebut.store (true);
}

void RogusProcessor::arreterEcoute()
{
    demandeArret.store (true);
}

// --- Audio -------------------------------------------------------------------

void RogusProcessor::prepareToPlay (double frequenceEchantillonnage, int)
{
    frequenceEch = frequenceEchantillonnage;
    frequenceEchPartagee.store (frequenceEchantillonnage);

    tramesCible = juce::jmax (1, (int) std::ceil (dureeEcoute * frequenceEch / pasAnalyse));
    tramesMin   = juce::jmax (1, (int) std::ceil (dureeEcouteMin * frequenceEch / pasAnalyse));

    // Une écoute en cours ne survit pas à un changement de fréquence d'échantillonnage.
    ecoute = false;
    ecouteActive.store (false);
    demandeDebut.store (false);
    demandeArret.store (false);

    // Les filtres démarrent directement sur leur réglage, sans glissement.
    adopterCorrection (true);
    lissee = rogus::doser (cible, lireQuantiteEffective());
    banc = rogus::construireFiltres (lissee, frequenceEch);
    memoire = {};
    filtresNeutres = lireQuantiteEffective() <= 0.0f;
}

bool RogusProcessor::isBusesLayoutSupported (const BusesLayout& disposition) const
{
    // Piste mono ou stéréo, avec autant de canaux en sortie qu'en entrée.
    const auto sortie = disposition.getMainOutputChannelSet();

    if (sortie != juce::AudioChannelSet::mono() && sortie != juce::AudioChannelSet::stereo())
        return false;

    return disposition.getMainInputChannelSet() == sortie;
}

void RogusProcessor::adopterCorrection (bool attendre) noexcept
{
    const int version = versionCorrection.load();

    if (version == versionVue)
        return;

    if (attendre)
    {
        const juce::SpinLock::ScopedLockType verrou (verrouAudio);
        cible = correctionPourAudio;
        versionVue = version;
    }
    else
    {
        const juce::SpinLock::ScopedTryLockType verrou (verrouAudio);

        if (verrou.isLocked())
        {
            cible = correctionPourAudio;
            versionVue = version;
        }
    }
}

void RogusProcessor::processBlock (juce::AudioBuffer<float>& tampon, juce::MidiBuffer&)
{
    // Les nombres « dénormalisés » (infiniment petits, quand le son s'éteint)
    // ralentissent énormément le processeur : on les force à zéro.
    juce::ScopedNoDenormals sansDenormaux;

    const int nbEch = tampon.getNumSamples();
    const int nbCanaux = juce::jmin (2, getTotalNumInputChannels(), tampon.getNumChannels());

    for (int canal = getTotalNumInputChannels(); canal < getTotalNumOutputChannels(); ++canal)
        tampon.clear (canal, 0, nbEch);

    if (nbEch == 0 || nbCanaux == 0)
        return;

    // L'écoute analyse le son d'entrée, avant toute correction.
    capturer (tampon, nbCanaux);

    adopterCorrection (false);

    // --- Glissement des réglages vers leur cible ---
    // Un saut brutal des coefficients d'un filtre s'entend comme un « clic » :
    // chaque valeur se rapproche de sa cible un peu à chaque bloc.
    const float quantite = lireQuantiteEffective();
    const auto voulue = rogus::doser (cible, quantite);
    const float part = 1.0f - (float) std::exp (-(double) nbEch / (frequenceEch * tempsLissage));
    bool change = false;

    auto glisser = [&] (float& valeur, float visee, float precision)
    {
        float nouvelle = valeur + (visee - valeur) * part;

        if (std::abs (visee - nouvelle) < precision)
            nouvelle = visee;

        if (nouvelle != valeur)
        {
            valeur = nouvelle;
            change = true;
        }
    };

    // La fréquence du coupe-bas glisse sur une échelle logarithmique, comme l'oreille.
    float logCoupeBas = std::log (lissee.coupeBasHz);
    glisser (logCoupeBas, std::log (voulue.coupeBasHz), 0.002f);
    lissee.coupeBasHz = (logCoupeBas == std::log (voulue.coupeBasHz)) ? voulue.coupeBasHz
                                                                      : std::exp (logCoupeBas);

    for (size_t i = 0; i < (size_t) rogus::nbBandes; ++i)
        glisser (lissee.gains[i], voulue.gains[i], 0.005f);

    // Les résonances changent de fréquence d'un coup (rare : fin d'une écoute),
    // mais leur gain glisse comme le reste.
    if (lissee.nbRes != voulue.nbRes || lissee.resHz != voulue.resHz)
    {
        lissee.nbRes = voulue.nbRes;
        lissee.resHz = voulue.resHz;
        change = true;
    }

    for (size_t i = 0; i < (size_t) rogus::nbResMax; ++i)
        glisser (lissee.resGain[i], voulue.resGain[i], 0.005f);

    if (change)
        banc = rogus::construireFiltres (lissee, frequenceEch);

    // --- Correction désactivée ou quantité à zéro : le son passe intact ---
    if (quantite <= 0.0f && ! change)
    {
        filtresNeutres = true;
        return;
    }

    // À la remise en route, la mémoire des filtres contient encore le son
    // d'avant la coupure : on la vide pour éviter un « clic ».
    if (filtresNeutres)
    {
        memoire = {};
        filtresNeutres = false;
    }

    // --- Filtrage ---
    const int nbActifs = 1 + rogus::nbBandes + lissee.nbRes;

    for (int canal = 0; canal < nbCanaux; ++canal)
    {
        float* donnees = tampon.getWritePointer (canal);

        for (int f = 0; f < nbActifs; ++f)
        {
            const auto& c = banc[(size_t) f];
            double z1 = memoire[(size_t) canal][(size_t) f][0];
            double z2 = memoire[(size_t) canal][(size_t) f][1];

            for (int n = 0; n < nbEch; ++n)
            {
                const double x = donnees[n];
                const double y = c.b0 * x + z1;
                z1 = c.b1 * x - c.a1 * y + z2;
                z2 = c.b2 * x - c.a2 * y;
                donnees[n] = (float) y;
            }

            memoire[(size_t) canal][(size_t) f][0] = z1;
            memoire[(size_t) canal][(size_t) f][1] = z2;
        }
    }
}

// --- Écoute (fil audio) ------------------------------------------------------

void RogusProcessor::capturer (const juce::AudioBuffer<float>& tampon, int nbCanaux) noexcept
{
    if (demandeDebut.exchange (false))
    {
        std::fill (sommeSpectre.begin(), sommeSpectre.end(), 0.0);
        compteursBosses = {};
        std::fill (anneau.begin(), anneau.end(), 0.0f);
        indexEcriture = depuisDerniereTrame = echantillonsRecus = trames = 0;
        analysePrete.store (false);
        ecoute = true;
    }

    if (demandeArret.exchange (false) && ecoute)
        terminerEcoute();

    if (! ecoute)
        return;

    const int nbEch = tampon.getNumSamples();
    const float* gauche = tampon.getReadPointer (0);
    const float* droite = tampon.getReadPointer (nbCanaux > 1 ? 1 : 0);

    for (int n = 0; n < nbEch && ecoute; ++n)
    {
        // L'analyse se fait sur la somme mono : c'est l'équilibre global qui compte.
        anneau[(size_t) indexEcriture] = 0.5f * (gauche[n] + droite[n]);
        indexEcriture = (indexEcriture + 1) & (rogus::tailleFft - 1);
        echantillonsRecus = juce::jmin (echantillonsRecus + 1, rogus::tailleFft);

        if (++depuisDerniereTrame >= pasAnalyse && echantillonsRecus >= rogus::tailleFft)
        {
            depuisDerniereTrame = 0;
            analyserTrame();
        }
    }
}

void RogusProcessor::analyserTrame() noexcept
{
    // Remise dans l'ordre des 8192 derniers échantillons.
    const int taille = rogus::tailleFft;
    double energie = 0.0;

    for (int n = 0; n < taille; ++n)
    {
        const float v = anneau[(size_t) ((indexEcriture + n) & (taille - 1))];
        donneesFft[(size_t) n] = v;
        energie += (double) v * v;
    }

    if (std::sqrt (energie / taille) < seuilSilence)
        return;      // silence : cette trame ne compte pas

    // La fenêtre adoucit les bords de la trame ; sans elle, la coupure nette
    // créerait de fausses fréquences dans le spectre.
    std::fill (donneesFft.begin() + taille, donneesFft.end(), 0.0f);
    fenetre.multiplyWithWindowingTable (donneesFft.data(), (size_t) taille);
    fft.performFrequencyOnlyForwardTransform (donneesFft.data(), true);

    // Cumul de la puissance de cette trame (pour chercher les bosses sans
    // allocation), en même temps que l'ajout au spectre moyen de l'écoute.
    cumulSpectre[0] = 0.0;

    for (size_t k = 0; k < sommeSpectre.size(); ++k)
    {
        const double puissance = (double) donneesFft[k] * donneesFft[k];
        sommeSpectre[k] += puissance;
        cumulSpectre[k + 1] = cumulSpectre[k] + puissance;
    }

    rogus::marquerBosses (cumulSpectre.data(), (int) sommeSpectre.size(), frequenceEch, compteursBosses);

    ++trames;
    progression.store (juce::jmin (1.0f, (float) trames / (float) tramesCible));

    if (trames >= tramesCible)
        terminerEcoute();
}

void RogusProcessor::terminerEcoute() noexcept
{
    ecoute = false;

    if (trames >= tramesMin)
    {
        // Le fil des messages prend le relais ; il éteindra le voyant d'écoute.
        analysePrete.store (true);
    }
    else
    {
        tropCourte.store (true);
        ecouteActive.store (false);
    }
}

// --- Interface et sauvegarde -------------------------------------------------

juce::AudioProcessorEditor* RogusProcessor::createEditor()
{
    return new RogusEditor (*this);
}

void RogusProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    // Réglages + mesure apprise, rangés en XML dans le projet de l'hôte (.flp).
    // On enregistre la mesure plutôt que la correction : la correction se
    // recalcule, et profitera ainsi des améliorations futures du calcul.
    auto etat = parametres.copyState();
    etat.setProperty ("version", versionEtat, nullptr);     // permet de reconnaître l'ancien format plus tard
    etat.removeChild (etat.getChildWithName (idMesure), nullptr);

    const auto m = lireMesure();

    if (m.valide)
    {
        juce::ValueTree noeud (idMesure);
        noeud.setProperty ("nbRes", m.nbRes, nullptr);

        for (int i = 0; i < rogus::nbBandes; ++i)
            noeud.setProperty ("niveau" + juce::String (i), m.niveaux[(size_t) i], nullptr);

        for (int i = 0; i < rogus::nbResMax; ++i)
        {
            noeud.setProperty ("resHz" + juce::String (i), m.resHz[(size_t) i], nullptr);
            noeud.setProperty ("resExces" + juce::String (i), m.resExces[(size_t) i], nullptr);
        }

        etat.appendChild (noeud, nullptr);
    }

    if (auto xml = etat.createXml())
        copyXmlToBinary (*xml, destination);
}

void RogusProcessor::setStateInformation (const void* donnees, int taille)
{
    auto xml = getXmlFromBinary (donnees, taille);

    if (xml == nullptr || ! xml->hasTagName (parametres.state.getType()))
        return;

    auto etat = juce::ValueTree::fromXml (*xml);
    migrerEtat (etat);      // projet enregistré par une version précédente : on le convertit
    const auto noeud = etat.getChildWithName (idMesure);
    rogus::Mesure m;

    if (noeud.isValid())
    {
        m.valide = true;
        m.nbRes = juce::jlimit (0, rogus::nbResMax, (int) noeud.getProperty ("nbRes", 0));

        for (int i = 0; i < rogus::nbBandes; ++i)
            m.niveaux[(size_t) i] = (float) noeud.getProperty ("niveau" + juce::String (i), 0.0f);

        for (int i = 0; i < rogus::nbResMax; ++i)
        {
            m.resHz[(size_t) i]    = (float) noeud.getProperty ("resHz" + juce::String (i), 1000.0f);
            m.resExces[(size_t) i] = (float) noeud.getProperty ("resExces" + juce::String (i), 0.0f);
        }
    }

    // La mesure vit dans le plugin, pas dans l'arbre des paramètres.
    etat.removeChild (noeud, nullptr);
    parametres.replaceState (etat);

    {
        const juce::ScopedLock verrou (verrouDonnees);
        mesure = m;
    }

    recalculer();
}

// Point d'entrée appelé par l'hôte pour créer une instance du plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RogusProcessor();
}
