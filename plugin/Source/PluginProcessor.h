#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Correction.h"
#include "Orchestre.h"
#include "Piste.h"

// Identifiants des paramètres. L'hôte s'en sert pour enregistrer les réglages
// et l'automation : une fois un vrai projet FL sauvegardé, on ne les renomme plus.
namespace IdParam
{
    inline const juce::String role             { "role" };
    inline const juce::String source           { "source" };
    inline const juce::String son              { "son" };
    inline const juce::String correctionActive { "correctionActive" };
    inline const juce::String quantite         { "quantite" };
    // Étape 3 : démasquage dynamique entre pistes.
    inline const juce::String priorite            { "priorite" };
    inline const juce::String demasquageActif     { "demasquageActif" };
    inline const juce::String demasquageQuantite  { "demasquageQuantite" };
    inline const juce::String reactivite          { "reactivite" };
    // Étape 4 : compression par rôle.
    inline const juce::String compressionActive   { "compressionActive" };
    inline const juce::String compressionQuantite { "compressionQuantite" };
    // v0.5 : creux de volume (la piste entière baisse quand une piste plus prioritaire joue).
    inline const juce::String creuxVolume         { "creuxVolume" };
}

// Le « moteur » du plugin. Étape 2 : EQ fixe par rôle.
//
// Trois fils d'exécution se partagent le travail :
//   - le fil audio (processBlock) filtre le son et, pendant une écoute,
//     accumule le spectre de la piste. Il ne doit jamais attendre ni allouer ;
//   - le fil des messages (minuteur, interface) transforme le spectre en
//     correction, ce qui est trop lourd pour le fil audio ;
//   - l'hôte, qui lit et écrit les réglages du projet.
class RogusProcessor : public juce::AudioProcessor,
                       private juce::Timer
{
public:
    RogusProcessor();
    ~RogusProcessor() override;

    // --- Audio ---
    void prepareToPlay (double frequenceEchantillonnage, int tailleBloc) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& disposition) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // --- Interface ---
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // --- Informations demandées par l'hôte ---
    const juce::String getName() const override { return "Rogus"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    // --- Sauvegarde dans le projet de l'hôte (réglages + mesure apprise) ---
    void getStateInformation (juce::MemoryBlock& destination) override;
    void setStateInformation (const void* donnees, int taille) override;

    // --- Écoute de la piste (appelé par l'interface) ---
    void demarrerEcoute();
    void arreterEcoute();
    bool ecouteEnCours() const noexcept       { return ecouteActive.load(); }
    float progressionEcoute() const noexcept  { return progression.load(); }
    bool ecouteTropCourte() const noexcept    { return tropCourte.load(); }

    // --- Ce que le plugin est en train de faire (lu par l'interface) ---
    rogus::Correction lireCorrection() const;
    rogus::Mesure lireMesure() const;
    int lireRole() const noexcept;
    int lireSource() const noexcept;
    int lireSon() const noexcept;
    float lireQuantiteEffective() const noexcept;      // 0 si la correction est désactivée
    double lireFrequenceEch() const noexcept  { return frequenceEchPartagee.load(); }

    // --- Compression : ce que le compresseur est en train de faire ---
    float lireQuantiteCompression() const noexcept;                              // 0 si désactivée
    float lireReductionCompression() const noexcept  { return reductionCompVue.load(); }   // dB, <= 0, en ce moment
    float lireRattrapage() const noexcept            { return rattrapageVu.load(); }       // dB rendus

    // --- Démasquage : ce que l'orchestration est en train de faire ---
    int lirePriorite() const noexcept;                 // priorité effective, de 1 à 5
    bool lireDemasquageActif() const noexcept;
    rogus::Reductions lireReductions() const noexcept; // réduction appliquée par bande (dB, <= 0)
    int nbPistesConnectees() const noexcept;           // instances de Rogus visibles, celle-ci comprise
    int lireRoleDominant() const noexcept  { return roleDominant.load(); }   // rôle de la piste à qui on cède le plus, -1 si aucune
    bool lireSynchroHote() const noexcept  { return synchroHoteVue.load(); }

    // --- Creux de volume : la piste entière baisse pour une piste plus prioritaire ---
    bool lireCreuxActif() const noexcept;                                    // coché, et démasquage en marche
    float lireCreux() const noexcept      { return creuxVu.load(); }         // dB, <= 0, en ce moment
    int lireRoleCreux() const noexcept    { return roleCreux.load(); }       // rôle de la piste qui le déclenche, -1 si aucune

    // Termine une analyse en attente et suit les changements de rôle.
    // Appelé par le minuteur ; public pour pouvoir être déclenché par les tests.
    void mettreAJour();

    // Durées d'écoute, en secondes de son réellement joué (les silences ne comptent pas).
    static constexpr double dureeEcoute    = 10.0;
    static constexpr double dureeEcouteMin = 3.0;

    juce::AudioProcessorValueTreeState parametres;

    // --- Nom et couleur de la piste, si l'hôte les donne ---
    // Tous les hôtes ne le font pas : sans appel, `piste` reste vide et la
    // fenêtre garde ses valeurs par défaut (pas d'étiquette, accent ambre).
    void updateTrackProperties (const TrackProperties& proprietes) override  { piste.recevoir (proprietes); }
    rogus::Piste piste;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout creerParametres();
    void timerCallback() override { mettreAJour(); }

    void recalculer();                                           // hors fil audio
    void capturer (const juce::AudioBuffer<float>&, int nbCanaux) noexcept;
    void analyserTrame() noexcept;
    void terminerEcoute() noexcept;
    void adopterCorrection (bool attendre) noexcept;

    // Orchestration (fil audio)
    struct PositionBloc { std::int64_t debut = 0; bool hote = false; };
    PositionBloc positionDuBloc (int nbEch) noexcept;
    void analyserBandes (const juce::AudioBuffer<float>&, int nbCanaux, const PositionBloc&) noexcept;
    void finirTick (std::int64_t tick) noexcept;
    // Ce que les pistes plus prioritaires demandent à celle-ci pour un tick.
    struct Demandes
    {
        rogus::Reductions bandes {};      // démasquage : réduction par bande (dB, <= 0)
        int roleBandes = -1;              // rôle de la piste qui demande le plus
        float creux = 0.0f;               // creux de volume (dB, <= 0)
        int roleCreux = -1;
    };

    Demandes lireAutresPistes (std::int64_t tick, bool tickPrecis, float quantite, int maPriorite,
                               bool avecCreux) const noexcept;
    void calculerDemasquage (bool actif, const PositionBloc&, int nbEch) noexcept;

    std::atomic<float>* paramRole = nullptr;
    std::atomic<float>* paramSource = nullptr;
    std::atomic<float>* paramSon = nullptr;
    std::atomic<float>* paramActif = nullptr;
    std::atomic<float>* paramQuantite = nullptr;
    std::atomic<float>* paramPriorite = nullptr;
    std::atomic<float>* paramDemasquage = nullptr;
    std::atomic<float>* paramDemQuantite = nullptr;
    std::atomic<float>* paramReactivite = nullptr;
    std::atomic<float>* paramCompression = nullptr;
    std::atomic<float>* paramCompQuantite = nullptr;
    std::atomic<float>* paramCreux = nullptr;

    // --- Partagé entre fils ---

    // Mesure et correction, côté « hors audio ». Protégées par un verrou
    // ordinaire : le fil audio n'y touche jamais.
    mutable juce::CriticalSection verrouDonnees;
    rogus::Mesure mesure;
    rogus::Correction correction;
    std::atomic<int> configCalculee { -1 };      // rôle, source et son pris en compte par `correction`
    int cleConfig() const noexcept;

    // Passage de la correction au fil audio. Le fil audio n'attend jamais ce
    // verrou : s'il est pris, il réessaie simplement au bloc suivant.
    juce::SpinLock verrouAudio;
    rogus::Correction correctionPourAudio;
    std::atomic<int> versionCorrection { 0 };

    std::atomic<bool> demandeDebut { false }, demandeArret { false };
    std::atomic<bool> ecouteActive { false }, tropCourte { false }, analysePrete { false };
    std::atomic<float> progression { 0.0f };
    std::atomic<double> frequenceEchPartagee { 44100.0 };

    // Orchestration : la place de cette instance dans l'orchestre, et ce que
    // le fil audio laisse voir à l'interface.
    rogus::Annonce* annonce = nullptr;
    std::array<rogus::Equilibre, rogus::nbRoles> zonesRoles {};     // poids de la zone de chaque rôle, par bande
    std::array<std::atomic<float>, rogus::nbBandes> reductionsVues {};
    std::atomic<int> roleDominant { -1 };
    std::atomic<bool> synchroHoteVue { false };
    std::atomic<float> reductionCompVue { 0.0f }, rattrapageVu { 0.0f };
    std::atomic<float> creuxVu { 0.0f };
    std::atomic<int> roleCreux { -1 };

    // --- Réservé au fil audio ---

    double frequenceEch = 44100.0;

    // Filtres : coefficients communs aux canaux, mémoire propre à chaque canal
    // (gauche et droite sont traités à l'identique pour ne pas déplacer l'image stéréo).
    rogus::Correction cible;            // dernière correction reçue (pour 100 %)
    rogus::Correction lissee;           // ce qui est appliqué en ce moment
    rogus::Banc banc;
    std::array<std::array<std::array<double, 2>, rogus::nbFiltres>, 2> memoire {};
    int versionVue = -1;
    bool filtresNeutres = true;

    // Compresseur. Pendant une écoute : histogramme des niveaux de la piste.
    std::array<std::uint32_t, rogus::nbHisto> histoEcoute {};
    double enveloppeEcoute = 0.0, coefEnveloppeEcoute = 0.0;
    int compteurHisto = 0;
    // En marche : enveloppe de crête (linéaire) qui commande le compresseur,
    // réduction en cours (dB, <= 0), quantité lissée et gain rendu.
    double enveloppeComp = 0.0;
    double reductionComp = 0.0;
    float quantiteCompLisse = 0.0f;
    float rattrapageLisse = 0.0f, rattrapageVise = 0.0f;
    float quantiteCompCalculee = -1.0f;
    int versionCompCalculee = -1;

    // Démasquage : mesure de l'énergie par bande (pour l'annoncer aux autres)...
    rogus::BancBandes filtresAnalyse;
    std::array<std::array<double, 2>, rogus::nbBandes> memoireAnalyse {};
    std::array<double, rogus::nbBandes> energieTick {};      // énergie accumulée dans le tick en cours
    int nbEchTick = 0;
    std::int64_t positionAttendue = -1, compteurHorloge = 0;
    bool dernierModeHote = false;
    std::array<double, rogus::nbBandes> sommeRef {};         // pendant une écoute : repère de niveau
    int nbTicksRef = 0;

    // ... et réduction appliquée à cette piste pour laisser la place aux autres.
    rogus::Reductions reductionLissee {};                    // dB, <= 0, valeur en cours
    rogus::Reductions reductionFiltres {};                   // valeur pour laquelle les filtres sont calculés
    std::int64_t dernierTickLu = -1;
    rogus::BancBandes filtresDyn;
    std::array<std::array<std::array<double, 2>, rogus::nbBandes>, 2> memoireDyn {};
    bool dynNeutre = true;

    // Creux de volume. `creuxTick` avance tick par tick, comme le démasquage ;
    // `creuxLisse` le rejoint échantillon par échantillon, pour que le volume
    // glisse au lieu de sauter d'un bloc à l'autre (un saut de gain s'entend).
    float creuxTick = 0.0f;                                  // dB, <= 0
    double creuxLisse = 0.0;                                 // dB, <= 0, valeur appliquée

    // Écoute : tampon circulaire des derniers échantillons, puis FFT.
    juce::dsp::FFT fft { rogus::ordreFft };
    juce::dsp::WindowingFunction<float> fenetre { (size_t) rogus::tailleFft,
                                                  juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> anneau, donneesFft;
    std::vector<double> sommeSpectre;
    std::vector<double> cumulSpectre;        // sommes cumulées de la trame en cours
    rogus::Compteurs compteursBosses {};     // trames où chaque point de la grille a une bosse
    int indexEcriture = 0, depuisDerniereTrame = 0, echantillonsRecus = 0;
    int trames = 0, tramesCible = 1, tramesMin = 1;
    bool ecoute = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RogusProcessor)
};
