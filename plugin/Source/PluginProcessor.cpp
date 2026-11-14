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
    paramPriorite    = parametres.getRawParameterValue (IdParam::priorite);
    paramDemasquage  = parametres.getRawParameterValue (IdParam::demasquageActif);
    paramDemQuantite = parametres.getRawParameterValue (IdParam::demasquageQuantite);
    paramReactivite  = parametres.getRawParameterValue (IdParam::reactivite);
    paramCompression  = parametres.getRawParameterValue (IdParam::compressionActive);
    paramCompQuantite = parametres.getRawParameterValue (IdParam::compressionQuantite);
    paramCreux        = parametres.getRawParameterValue (IdParam::creuxVolume);

    // La zone « à garder lisible » de chaque rôle, traduite une fois en poids par bande.
    for (int i = 0; i < rogus::nbRoles; ++i)
        zonesRoles[(size_t) i] = rogus::poidsZone (rogus::role (i));

    for (auto& r : reductionsVues)
        r.store (0.0f);

    // Cette instance prend sa place dans l'orchestre : les autres pistes la voient.
    annonce = rogus::Orchestre::instance().inscrire();

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
    rogus::Orchestre::instance().retirer (annonce);      // les autres pistes ne la verront plus
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

    // --- Étape 3 : démasquage dynamique. Paramètres ajoutés à la suite des
    //     précédents ; un ancien projet les reçoit à leur valeur par défaut. ---

    // « Auto » = la priorité habituelle du rôle. Sinon de 1 (fond) à 5 (devant).
    disposition.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { IdParam::priorite, 1 }, rogus::fr ("Priorité"),
        juce::StringArray { "Auto", "1", "2", "3", "4", "5" }, 0));

    disposition.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { IdParam::demasquageActif, 1 }, rogus::fr ("Démasquage"), true));

    disposition.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { IdParam::demasquageQuantite, 1 }, rogus::fr ("Quantité du démasquage"),
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    disposition.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { IdParam::reactivite, 1 }, rogus::fr ("Réactivité"),
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    // --- Étape 4 : compression par rôle. Elle n'agit que sur une piste écoutée
    //     avec cette version : un ancien projet sonne donc comme avant.
    //     Elle est à l'ARRÊT par défaut : elle est surtout utile sur la voix et
    //     la basse, et doit rester un choix au cas par cas. Les réglages par
    //     rôle restent dans la table, prêts à servir quand on la coche. ---
    disposition.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { IdParam::compressionActive, 1 }, "Compression", false));

    disposition.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { IdParam::compressionQuantite, 1 }, rogus::fr ("Quantité de compression"),
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    // --- v0.5 : creux de volume. La piste entière baisse pendant qu'une piste
    //     plus prioritaire joue dans sa zone (le « sidechain » classique : la
    //     basse qui se pousse à chaque coup de kick). À l'ARRÊT par défaut : un
    //     ancien projet sonne comme avant, et il ne sert que sur quelques pistes. ---
    disposition.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { IdParam::creuxVolume, 1 }, "Creux de volume", false));

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

float RogusProcessor::lireQuantiteCompression() const noexcept
{
    return paramCompression->load() > 0.5f ? juce::jlimit (0.0f, 1.0f, paramCompQuantite->load() * 0.01f) : 0.0f;
}

int RogusProcessor::lirePriorite() const noexcept
{
    // Choix 0 = « Auto » : la priorité habituelle du rôle. Choix 1 à 5 = la priorité elle-même.
    const int choix = juce::jlimit (0, 5, juce::roundToInt (paramPriorite->load()));
    return choix == 0 ? rogus::role (lireRole()).priorite : choix;
}

bool RogusProcessor::lireDemasquageActif() const noexcept
{
    return paramDemasquage->load() > 0.5f;
}

bool RogusProcessor::lireCreuxActif() const noexcept
{
    // Le creux de volume fait partie du démasquage : couper celui-ci coupe aussi le creux.
    return lireDemasquageActif() && paramCreux->load() > 0.5f;
}

rogus::Reductions RogusProcessor::lireReductions() const noexcept
{
    rogus::Reductions r {};

    for (size_t b = 0; b < (size_t) rogus::nbBandes; ++b)
        r[b] = reductionsVues[b].load();

    return r;
}

int RogusProcessor::nbPistesConnectees() const noexcept
{
    return rogus::Orchestre::instance().nbInstances();
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
        auto nouvelleMesure = rogus::analyser (sommeSpectre, frequenceEchPartagee.load(),
                                               &compteursBosses, trames);

        // Repère de niveau pour le démasquage : niveau moyen de chaque bande
        // pendant que la piste jouait (les ticks de silence n'ont pas été comptés).
        if (nouvelleMesure.valide && nbTicksRef > 0)
        {
            nouvelleMesure.refValide = true;

            for (size_t b = 0; b < (size_t) rogus::nbBandes; ++b)
                nouvelleMesure.refDb[b] = (float) (10.0 * std::log10 (sommeRef[b] / nbTicksRef + 1.0e-20));
        }

        // Histogramme des niveaux pour le compresseur : on passe des comptes aux parts du temps.
        double totalHisto = 0.0;

        for (auto compte : histoEcoute)
            totalHisto += (double) compte;

        if (nouvelleMesure.valide && totalHisto > 0.0)
        {
            nouvelleMesure.histoValide = true;

            for (size_t k = 0; k < (size_t) rogus::nbHisto; ++k)
                nouvelleMesure.histo[k] = (float) ((double) histoEcoute[k] / totalHisto);
        }

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

    // Compresseur : on repart d'un état vierge. L'enveloppe de l'écoute retombe en 100 ms.
    coefEnveloppeEcoute = std::exp (-1.0 / (frequenceEch * 0.1));
    enveloppeEcoute = 0.0;
    enveloppeComp = 0.0;
    reductionComp = 0.0;
    quantiteCompLisse = lireQuantiteCompression();
    quantiteCompCalculee = -1.0f;
    versionCompCalculee = -1;
    rattrapageVise = rogus::rattrapageCompression (cible.compression, cible.histo, lireQuantiteCompression());
    rattrapageLisse = rattrapageVise;

    // Démasquage : on repart d'un état vierge.
    filtresAnalyse = rogus::construireFiltresAnalyse (frequenceEch);
    memoireAnalyse = {};
    energieTick = {};
    nbEchTick = 0;
    positionAttendue = -1;
    compteurHorloge = 0;
    reductionLissee = {};
    reductionFiltres = {};
    dernierTickLu = -1;
    filtresDyn = rogus::construireFiltresDynamiques (reductionLissee, frequenceEch);
    memoireDyn = {};
    dynNeutre = true;
    creuxTick = 0.0f;
    creuxLisse = 0.0;
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

    // --- Orchestration : on mesure ce que joue cette piste (avant toute
    //     correction), on l'annonce aux autres, et on regarde ce qu'elles jouent. ---
    const bool demasquage = lireDemasquageActif();
    const auto position = positionDuBloc (nbEch);

    if (annonce != nullptr)
    {
        annonce->role.store (lireRole());
        annonce->priorite.store (lirePriorite());
        annonce->synchroHote.store (position.hote);
        annonce->participe.store (demasquage);
    }

    synchroHoteVue.store (position.hote);
    analyserBandes (tampon, nbCanaux, position);
    calculerDemasquage (demasquage, position, nbEch);

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

    glisser (lissee.compensationDb, voulue.compensationDb, 0.005f);

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

    // Un filtre du 2e ordre appliqué sur place à un canal.
    auto filtrer = [nbEch] (float* donnees, const rogus::Coefs& c, std::array<double, 2>& etat)
    {
        double z1 = etat[0], z2 = etat[1];

        for (int n = 0; n < nbEch; ++n)
        {
            const double x = donnees[n];
            const double y = c.b0 * x + z1;
            z1 = c.b1 * x - c.a1 * y + z2;
            z2 = c.b2 * x - c.a2 * y;
            donnees[n] = (float) y;
        }

        etat[0] = z1;
        etat[1] = z2;
    };

    // --- Couche 1 : correction fixe ---
    // Désactivée ou quantité à zéro : cette couche laisse passer le son intact.
    if (quantite <= 0.0f && ! change)
    {
        filtresNeutres = true;
    }
    else
    {
        // À la remise en route, la mémoire des filtres contient encore le son
        // d'avant la coupure : on la vide pour éviter un « clic ».
        if (filtresNeutres)
        {
            memoire = {};
            filtresNeutres = false;
        }

        const int nbActifs = 1 + rogus::nbBandes + lissee.nbRes;

        for (int canal = 0; canal < nbCanaux; ++canal)
            for (int f = 0; f < nbActifs; ++f)
                filtrer (tampon.getWritePointer (canal), banc[(size_t) f], memoire[(size_t) canal][(size_t) f]);
    }

    // --- Couche 2 : compression ---
    // Placée après la correction fixe (on compresse le son déjà nettoyé) et avant
    // le démasquage (la place cédée aux autres pistes ne doit pas être « rattrapée »).
    {
        const auto& comp = cible.compression;
        const float quantiteComp = comp.valide ? lireQuantiteCompression() : 0.0f;

        // Le gain à rendre ne dépend que des réglages : on ne le recalcule que s'ils changent.
        if (quantiteComp != quantiteCompCalculee || versionVue != versionCompCalculee)
        {
            rattrapageVise = rogus::rattrapageCompression (comp, cible.histo, quantiteComp);
            quantiteCompCalculee = quantiteComp;
            versionCompCalculee = versionVue;
        }

        rattrapageLisse += (rattrapageVise - rattrapageLisse) * part;

        if (std::abs (rattrapageVise - rattrapageLisse) < 0.005f)
            rattrapageLisse = rattrapageVise;

        // La quantité glisse elle aussi : tourner le curseur pendant la lecture
        // ne doit pas faire sauter le gain d'un bloc à l'autre (un « clic »).
        quantiteCompLisse += (quantiteComp - quantiteCompLisse) * part;

        if (std::abs (quantiteComp - quantiteCompLisse) < 0.001f)
            quantiteCompLisse = quantiteComp;

        // À l'arrêt, une fois la quantité et le gain revenus à zéro : le son passe intact.
        if (quantiteCompLisse <= 0.0f && rattrapageLisse == 0.0f)
        {
            reductionComp = 0.0;
            enveloppeComp = 0.0;
            reductionCompVue.store (0.0f);
            rattrapageVu.store (0.0f);
        }
        else
        {
            // Détecteur : enveloppe de crête, la MÊME que pendant l'écoute
            // (montée immédiate, descente en 100 ms). C'est indispensable : le
            // seuil est posé d'après cet histogramme de niveaux. Si le
            // compresseur lisait une enveloppe plus lisse, il croirait la piste
            // 6 à 8 dB plus faible qu'elle n'est et ne compresserait presque rien.
            // Un échantillon seul ne convient pas non plus : il passe par zéro à
            // chaque période, donc la moitié du temps le niveau paraîtrait nul.
            // L'attaque et le relâchement du rôle s'appliquent ensuite au GAIN
            // (plus bas), pas à l'enveloppe : ils décident à quelle vitesse la
            // réduction s'installe, et laissent ainsi passer l'attaque des notes.
            const double coefEnveloppe = std::exp (-1.0 / (frequenceEch * 0.1));
            const double coefAttaque = std::exp (-1.0 / (frequenceEch * 0.001 * juce::jmax (1.0f, comp.attaqueMs)));
            const double coefRelache = std::exp (-1.0 / (frequenceEch * 0.001 * juce::jmax (1.0f, comp.relachementMs)));
            float* gauche = tampon.getWritePointer (0);
            float* droite = nbCanaux > 1 ? tampon.getWritePointer (1) : nullptr;
            double somme = 0.0;

            // Le seuil du compresseur a été posé d'après le niveau d'ENTRÉE du
            // plugin (l'écoute se fait avant la correction fixe). La compensation
            // de volume de cette correction décale le niveau que voit le
            // compresseur : on la retire pour comparer des niveaux comparables.
            // Sans cela, une piste corrigée serait compressée plus que prévu.
            const float niveauEntreeDb = lissee.compensationDb;

            for (int n = 0; n < nbEch; ++n)
            {
                // Le plus fort des deux canaux commande : gauche et droite reçoivent
                // le même gain, sinon l'image stéréo bougerait.
                const float crete = droite != nullptr ? juce::jmax (std::abs (gauche[n]), std::abs (droite[n]))
                                                      : std::abs (gauche[n]);
                enveloppeComp = juce::jmax ((double) crete, enveloppeComp * coefEnveloppe);

                const float niveau = (float) (20.0 * std::log10 (enveloppeComp + 1.0e-9)) - niveauEntreeDb;
                const double visee = (double) (quantiteCompLisse * rogus::gainCompression (comp, niveau));

                // Attaque quand la réduction doit augmenter, relâchement quand elle revient.
                reductionComp = visee + (reductionComp - visee) * (visee < reductionComp ? coefAttaque : coefRelache);
                somme += reductionComp;

                const float gain = (float) std::pow (10.0, (reductionComp + (double) rattrapageLisse) / 20.0);
                gauche[n] *= gain;

                if (droite != nullptr)
                    droite[n] *= gain;
            }

            reductionCompVue.store ((float) (somme / nbEch));
            rattrapageVu.store (rattrapageLisse);
        }
    }

    // --- Couche 3 : démasquage dynamique ---
    // Les filtres ne sont recalculés que si la réduction a bougé de façon audible.
    float reductionMax = 0.0f, ecartMax = 0.0f;

    for (size_t b = 0; b < (size_t) rogus::nbBandes; ++b)
    {
        reductionMax = juce::jmax (reductionMax, std::abs (reductionLissee[b]));
        ecartMax = juce::jmax (ecartMax, std::abs (reductionLissee[b] - reductionFiltres[b]));
    }

    // Couche à l'arrêt et réduction revenue à zéro : le son passe intact.
    if (! demasquage && reductionMax < 0.01f)
    {
        reductionLissee = {};
        dynNeutre = true;
    }
    else
    {
        if (dynNeutre)
        {
            memoireDyn = {};
            reductionFiltres = reductionLissee;
            filtresDyn = rogus::construireFiltresDynamiques (reductionFiltres, frequenceEch);
            dynNeutre = false;
        }
        else if (ecartMax > 0.02f)
        {
            reductionFiltres = reductionLissee;
            filtresDyn = rogus::construireFiltresDynamiques (reductionFiltres, frequenceEch);
        }

        for (int canal = 0; canal < nbCanaux; ++canal)
            for (int b = 0; b < rogus::nbBandes; ++b)
                filtrer (tampon.getWritePointer (canal), filtresDyn[(size_t) b], memoireDyn[(size_t) canal][(size_t) b]);
    }

    for (size_t b = 0; b < (size_t) rogus::nbBandes; ++b)
        reductionsVues[b].store (dynNeutre ? 0.0f : reductionLissee[b]);

    // --- Creux de volume ---
    // Un simple gain, le même sur tous les canaux, en bout de chaîne : comme
    // pour le démasquage, la place cédée ne doit pas être « rattrapée ».
    // `creuxTick` ne bouge qu'au début d'un bloc ; le gain appliqué le rejoint
    // en 5 ms environ, échantillon par échantillon, pour ne pas faire de marche.
    if (creuxTick != 0.0f || creuxLisse != 0.0)      // sinon rien à céder : le son passe intact
    {
        const double coefCreux = std::exp (-1.0 / (frequenceEch * 0.005));
        float* gauche = tampon.getWritePointer (0);
        float* droite = nbCanaux > 1 ? tampon.getWritePointer (1) : nullptr;

        for (int n = 0; n < nbEch; ++n)
        {
            creuxLisse = (double) creuxTick + (creuxLisse - (double) creuxTick) * coefCreux;

            // Revenu à un millième de dB de zéro : on s'y pose. Ce test se fait
            // à chaque échantillon (et non une fois par bloc) pour que le
            // résultat ne dépende pas de la taille des blocs de l'hôte.
            if (creuxTick == 0.0f && creuxLisse > -0.001)
                creuxLisse = 0.0;

            const float gain = (float) std::pow (10.0, creuxLisse / 20.0);
            gauche[n] *= gain;

            if (droite != nullptr)
                droite[n] *= gain;
        }
    }

    creuxVu.store ((float) creuxLisse);
}

// --- Orchestration (fil audio) -----------------------------------------------

RogusProcessor::PositionBloc RogusProcessor::positionDuBloc (int nbEch) noexcept
{
    // On numérote le temps d'après la position de lecture de l'hôte : deux
    // instances qui traitent le même moment du morceau s'accordent ainsi sur
    // le tick, à l'écoute comme à l'export. Si l'hôte ne la donne pas (ou si la
    // lecture est arrêtée), on se rabat sur un simple compteur.
    PositionBloc p;

    if (auto* tete = getPlayHead())
    {
        const auto info = tete->getPosition();

        if (info.hasValue() && info->getIsPlaying())
        {
            const auto temps = info->getTimeInSamples();

            if (temps.hasValue() && *temps >= 0)
            {
                p.debut = *temps;
                p.hote = true;
            }
        }
    }

    if (! p.hote)
        p.debut = compteurHorloge;

    compteurHorloge += nbEch;
    return p;
}

void RogusProcessor::analyserBandes (const juce::AudioBuffer<float>& tampon, int nbCanaux, const PositionBloc& pos) noexcept
{
    // Un saut de position (déplacement dans le morceau, boucle, démarrage de
    // la lecture) : le tick en cours est incomplet, on repart d'un tick vide.
    if (pos.hote != dernierModeHote || (pos.hote && positionAttendue >= 0 && pos.debut != positionAttendue))
    {
        energieTick = {};
        nbEchTick = 0;
    }

    dernierModeHote = pos.hote;

    const int nbEch = tampon.getNumSamples();
    const float* gauche = tampon.getReadPointer (0);
    const float* droite = tampon.getReadPointer (nbCanaux > 1 ? 1 : 0);
    std::int64_t position = pos.debut;

    for (int n = 0; n < nbEch; ++n)
    {
        const double x = 0.5 * ((double) gauche[n] + (double) droite[n]);

        // Sept passe-bande : l'énergie de la piste dans chaque bande.
        for (size_t b = 0; b < (size_t) rogus::nbBandes; ++b)
        {
            const auto& c = filtresAnalyse[b];
            auto& etat = memoireAnalyse[b];
            const double y = c.b0 * x + etat[0];
            etat[0] = c.b1 * x - c.a1 * y + etat[1];
            etat[1] = c.b2 * x - c.a2 * y;
            energieTick[b] += y * y;
        }

        ++nbEchTick;
        ++position;

        // Fin d'un tick : sa position est un multiple de 1024.
        if ((position & (rogus::tailleTick - 1)) == 0)
        {
            finirTick (position / rogus::tailleTick - 1);
            energieTick = {};
            nbEchTick = 0;
        }
    }

    positionAttendue = pos.hote ? position : -1;
}

void RogusProcessor::finirTick (std::int64_t tick) noexcept
{
    std::array<double, rogus::nbBandes> puissance {};
    double total = 0.0;

    for (size_t b = 0; b < (size_t) rogus::nbBandes; ++b)
    {
        puissance[b] = energieTick[b] / juce::jmax (1, nbEchTick);
        total += puissance[b];
    }

    // Pendant une écoute : on retient le niveau moyen de chaque bande, en
    // ignorant les silences (même seuil que pour le spectre : -50 dBFS).
    if (ecoute && total > (double) seuilSilence * seuilSilence)
    {
        for (size_t b = 0; b < (size_t) rogus::nbBandes; ++b)
            sommeRef[b] += puissance[b];

        ++nbTicksRef;
    }

    if (annonce == nullptr)
        return;

    // Ce qu'on annonce aux autres : pour chaque bande, la piste joue-t-elle,
    // par rapport à son propre niveau habituel ? Sans écoute, on ne sait pas
    // ce qu'est son niveau habituel : elle n'annonce rien et ne demande donc
    // aucune place.
    rogus::Activites activites {};

    if (cible.refValide)
        for (size_t b = 0; b < (size_t) rogus::nbBandes; ++b)
            activites[b] = rogus::activite ((float) (10.0 * std::log10 (puissance[b] + 1.0e-20)), cible.refDb[b]);

    annonce->publier (tick, activites, juce::Time::getMillisecondCounter());
}

RogusProcessor::Demandes RogusProcessor::lireAutresPistes (std::int64_t tick, bool tickPrecis, float quantite,
                                                           int maPriorite, bool avecCreux) const noexcept
{
    Demandes d;
    float plusForte = 0.0f;
    const auto maintenant = juce::Time::getMillisecondCounter();
    auto& orchestre = rogus::Orchestre::instance();

    for (int i = 0; i < rogus::nbInstancesMax; ++i)
    {
        const auto& autre = orchestre.place (i);

        if (&autre == annonce || ! autre.utilisee.load() || ! autre.participe.load())
            continue;

        // Seule une piste strictement plus prioritaire peut demander de la place.
        const int saPriorite = autre.priorite.load();

        if (saPriorite <= maPriorite)
            continue;

        // Même tick pour tout le monde quand l'hôte donne la position ; sinon,
        // la dernière annonce de l'autre piste.
        rogus::Activites activites {};
        const bool lu = (tickPrecis && autre.synchroHote.load()) ? autre.lire (tick, activites)
                                                                 : autre.lireDernier (activites, maintenant);
        if (! lu)
            continue;

        const auto& zone = zonesRoles[(size_t) juce::jlimit (0, rogus::nbRoles - 1, autre.role.load())];

        for (size_t b = 0; b < (size_t) rogus::nbBandes; ++b)
        {
            const float r = rogus::reductionBande (zone[b], activites[b], saPriorite, maPriorite, quantite);

            // Plusieurs pistes peuvent demander la même bande : on ne cumule pas,
            // on garde la demande la plus forte.
            d.bandes[b] = juce::jmin (d.bandes[b], r);

            if (r < plusForte)
            {
                plusForte = r;
                d.roleBandes = autre.role.load();
            }
        }

        // Creux de volume : même règle (la demande la plus forte, pas la somme),
        // mais c'est la piste entière qui baisse, d'après ce que l'autre joue
        // dans sa propre zone.
        if (avecCreux)
        {
            const float c = rogus::creuxVolume (rogus::activiteDansZone (zone, activites), saPriorite, maPriorite, quantite);

            if (c < d.creux)
            {
                d.creux = c;
                d.roleCreux = autre.role.load();
            }
        }
    }

    return d;
}

void RogusProcessor::calculerDemasquage (bool actif, const PositionBloc& pos, int nbEch) noexcept
{
    // Réactivité : à 0 % la réduction s'installe et repart lentement (discret),
    // à 100 % elle suit de près ce que jouent les autres (plus efficace, plus audible).
    const double r = juce::jlimit (0.0, 1.0, (double) paramReactivite->load() * 0.01);
    const double tempsAttaque  = 0.200 + (0.015 - 0.200) * r;
    const double tempsRelache  = 0.800 + (0.080 - 0.800) * r;

    // Le creux de volume doit tomber sur le coup de l'autre piste : il s'installe
    // tout de suite (10 ms, soit presque d'un tick à l'autre). Son retour suit
    // la réactivité : de 250 ms (le volume revient en douceur) à 60 ms (il
    // « pompe » franchement, au rythme des coups).
    const double tempsAttaqueCreux = 0.010;
    const double tempsRelacheCreux = 0.250 + (0.060 - 0.250) * r;

    auto avancer = [&] (const Demandes& d, double duree)
    {
        for (size_t b = 0; b < (size_t) rogus::nbBandes; ++b)
        {
            // Baisser davantage = attaque (rapide) ; revenir = relâchement (plus lent).
            const double temps = d.bandes[b] < reductionLissee[b] ? tempsAttaque : tempsRelache;
            reductionLissee[b] += (d.bandes[b] - reductionLissee[b]) * (float) (1.0 - std::exp (-duree / temps));
        }

        const double tempsCreux = d.creux < creuxTick ? tempsAttaqueCreux : tempsRelacheCreux;
        creuxTick += (d.creux - creuxTick) * (float) (1.0 - std::exp (-duree / tempsCreux));

        // Revenu à moins d'un centième de dB : on s'arrête là, sinon la valeur
        // s'approcherait de zéro sans jamais l'atteindre.
        if (d.creux == 0.0f && creuxTick > -0.01f)
            creuxTick = 0.0f;
    };

    if (! actif)
    {
        // Couche à l'arrêt : la réduction revient doucement à zéro.
        avancer ({}, (double) nbEch / frequenceEch);
        dernierTickLu = -1;
        roleDominant.store (-1);
        roleCreux.store (-1);
        return;
    }

    const float quantite = juce::jlimit (0.0f, 1.0f, paramDemQuantite->load() * 0.01f);
    const int maPriorite = lirePriorite();
    const bool avecCreux = paramCreux->load() > 0.5f;

    if (pos.hote)
    {
        // On lit le dernier tick forcément terminé chez toutes les instances :
        // celui qui précède le début de ce bloc. Peu importe alors dans quel
        // ordre l'hôte traite les pistes : le résultat est le même.
        const std::int64_t tickLisible = pos.debut / rogus::tailleTick - 1;

        // Saut dans le morceau : on repart de zéro plutôt que de rattraper.
        if (tickLisible < dernierTickLu || tickLisible - dernierTickLu > 64)
        {
            dernierTickLu = tickLisible - 1;
            reductionLissee = {};
            creuxTick = 0.0f;
        }

        // On avance tick par tick (et non bloc par bloc) : la réduction ne
        // dépend ainsi pas de la taille des blocs choisie par l'hôte.
        for (std::int64_t t = dernierTickLu + 1; t <= tickLisible; ++t)
        {
            const auto demandes = lireAutresPistes (t, true, quantite, maPriorite, avecCreux);
            avancer (demandes, (double) rogus::tailleTick / frequenceEch);
            dernierTickLu = t;
            roleDominant.store (demandes.roleBandes);
            roleCreux.store (demandes.roleCreux);
        }
    }
    else
    {
        const auto demandes = lireAutresPistes (-1, false, quantite, maPriorite, avecCreux);
        avancer (demandes, (double) nbEch / frequenceEch);
        dernierTickLu = -1;
        roleDominant.store (demandes.roleBandes);
        roleCreux.store (demandes.roleCreux);
    }
}

// --- Écoute (fil audio) ------------------------------------------------------

void RogusProcessor::capturer (const juce::AudioBuffer<float>& tampon, int nbCanaux) noexcept
{
    if (demandeDebut.exchange (false))
    {
        std::fill (sommeSpectre.begin(), sommeSpectre.end(), 0.0);
        compteursBosses = {};
        sommeRef = {};
        nbTicksRef = 0;
        histoEcoute = {};
        enveloppeEcoute = 0.0;
        compteurHisto = 0;
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

        // Pour le compresseur : l'enveloppe suit les crêtes du son (montée
        // immédiate, descente en 100 ms), et on note 1 fois sur 32 à quel niveau
        // elle se trouve. Sous -55 dBFS, c'est du silence : on ne le compte pas.
        const double crete = juce::jmax (std::abs (gauche[n]), std::abs (droite[n]));
        enveloppeEcoute = juce::jmax (crete, enveloppeEcoute * coefEnveloppeEcoute);

        if (++compteurHisto >= 32)
        {
            compteurHisto = 0;
            const float niveau = (float) (20.0 * std::log10 (enveloppeEcoute + 1.0e-9));

            if (niveau > -55.0f)
                ++histoEcoute[(size_t) rogus::caseDuNiveau (niveau)];
        }

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

        // Repère de niveau du démasquage (étape 3). Champs ajoutés sans changer
        // les anciens : une version précédente du plugin les ignore simplement.
        if (m.refValide)
            for (int i = 0; i < rogus::nbBandes; ++i)
                noeud.setProperty ("ref" + juce::String (i), m.refDb[(size_t) i], nullptr);

        // Histogramme des niveaux pour le compresseur (étape 4), facultatif lui aussi.
        if (m.histoValide)
            for (int i = 0; i < rogus::nbHisto; ++i)
                noeud.setProperty ("histo" + juce::String (i), m.histo[(size_t) i], nullptr);

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

        // Une écoute faite avant l'étape 3 n'a pas de repère de niveau : la
        // correction fixe marche comme avant, mais la piste ne demandera de
        // place aux autres qu'après une nouvelle écoute.
        m.refValide = noeud.hasProperty ("ref0");

        if (m.refValide)
            for (int i = 0; i < rogus::nbBandes; ++i)
                m.refDb[(size_t) i] = (float) noeud.getProperty ("ref" + juce::String (i), -100.0f);

        // Une écoute faite avant l'étape 4 n'a pas d'histogramme : la piste
        // n'est pas compressée tant qu'elle n'a pas été réécoutée.
        m.histoValide = noeud.hasProperty ("histo0");

        if (m.histoValide)
            for (int i = 0; i < rogus::nbHisto; ++i)
                m.histo[(size_t) i] = (float) noeud.getProperty ("histo" + juce::String (i), 0.0f);
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
