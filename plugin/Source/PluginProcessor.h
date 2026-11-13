#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Correction.h"

// Identifiants des paramètres. L'hôte s'en sert pour enregistrer les réglages
// et l'automation : une fois un vrai projet FL sauvegardé, on ne les renomme plus.
namespace IdParam
{
    inline const juce::String role             { "role" };
    inline const juce::String source           { "source" };
    inline const juce::String son              { "son" };
    inline const juce::String correctionActive { "correctionActive" };
    inline const juce::String quantite         { "quantite" };
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

    // Termine une analyse en attente et suit les changements de rôle.
    // Appelé par le minuteur ; public pour pouvoir être déclenché par les tests.
    void mettreAJour();

    // Durées d'écoute, en secondes de son réellement joué (les silences ne comptent pas).
    static constexpr double dureeEcoute    = 10.0;
    static constexpr double dureeEcouteMin = 3.0;

    juce::AudioProcessorValueTreeState parametres;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout creerParametres();
    void timerCallback() override { mettreAJour(); }

    void recalculer();                                           // hors fil audio
    void capturer (const juce::AudioBuffer<float>&, int nbCanaux) noexcept;
    void analyserTrame() noexcept;
    void terminerEcoute() noexcept;
    void adopterCorrection (bool attendre) noexcept;

    std::atomic<float>* paramRole = nullptr;
    std::atomic<float>* paramSource = nullptr;
    std::atomic<float>* paramSon = nullptr;
    std::atomic<float>* paramActif = nullptr;
    std::atomic<float>* paramQuantite = nullptr;

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
