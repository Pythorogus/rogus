#pragma once

#include <atomic>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include "Limiteur.h"
#include "MesureLoudness.h"

// Identifiants des paramètres de Rogus Master. Comme pour le plugin de piste,
// une fois un projet enregistré, on ne les renomme plus.
namespace IdParamMaster
{
    inline const juce::String cible { "cible" };     // 0 = streaming -14, 1 = fort -11
    inline const juce::String gain  { "gain" };      // gain appliqué, en dB
}

// Le masteriseur : il écoute le morceau (analyse), règle le gain pour atteindre
// la cible de volume perçu, puis limite la crête réelle.
//
// Contrairement au plugin de piste, il n'a aucune mémoire partagée et ne voit
// aucune autre instance : il ne traite que le signal de la piste master.
class RogusMasterProcessor : public juce::AudioProcessor,
                             private juce::Timer
{
public:
    RogusMasterProcessor();
    ~RogusMasterProcessor() override;

    // --- Audio ---
    void prepareToPlay (double frequenceEchantillonnage, int tailleBloc) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // --- Interface ---
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // --- Informations demandées par l'hôte ---
    const juce::String getName() const override { return "Rogus Master"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    // --- Sauvegarde dans le projet de l'hôte ---
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- Écoute du morceau et calage du gain ---
    void demarrerEcoute();
    void arreterEtRegler();
    bool ecouteEnCours() const noexcept       { return ecouteActive.load(); }
    float secondesEcoute() const noexcept;
    float progressionEcoute() const noexcept  { return progression.load(); }

    // --- Ce que le plugin montre ---
    float lireLufsIntegre() const noexcept  { return mesure.lufsIntegre(); }
    float lireLufsCourtTerme() const noexcept { return mesure.lufsCourtTerme(); }
    float lireCreteReelle() const noexcept  { return mesure.creteReelleDb(); }
    float lireMargeDynamique() const noexcept { return mesure.margeDynamiqueDb(); }
    float lireReduction() const noexcept    { return reductionVue.load(); }
    float lireGain() const noexcept         { return paramGain->load(); }
    float lireCibleLufs() const noexcept;
    float lireLufsMesure() const noexcept   { return lufsMesure; }   // dernière mesure de l'écoute
    bool mesureDisponible() const noexcept  { return mesure.mesureValide(); }

    // Termine une analyse en attente et suit les changements de cible.
    // Appelé par le minuteur ; public pour les tests.
    void mettreAJour();

    juce::AudioProcessorValueTreeState parametres;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout creerParametres();
    void timerCallback() override;

    int lireChoixCible() const noexcept;
    void appliquerGain (float db) noexcept;

    std::atomic<float>* paramCible = nullptr;
    std::atomic<float>* paramGain = nullptr;
    juce::RangedAudioParameter* paramGainObjet = nullptr;

    rogus::MesureLoudness mesure;
    rogus::Limiteur limiteur;

    std::atomic<float> reductionVue { 0.0f };
    std::atomic<bool> ecouteActive { false };
    std::atomic<juce::int64> echantillonsEcoute { 0 };
    std::atomic<bool> reinitialiserMesure { false };
    std::atomic<double> frequenceEchVue { 48000.0 };
    std::atomic<float> progression { 0.0f };

    // Mesure gardée pour changer de cible sans réécouter. Modifiée seulement
    // sur le fil des messages.
    float lufsMesure = -100.0f;
    float gainPendantMesure = 0.0f;
    int cibleVue = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RogusMasterProcessor)
};
