// Essai sur de vrais fichiers, hors FL Studio : deux instances du moteur de
// Rogus (un kick et une basse) traitent deux fichiers WAV comme le ferait
// l'hôte, et on enregistre ce que sort la basse, avec et sans creux de volume.
// Cela permet de vérifier le plugin sur le vrai son d'un morceau sans passer
// par des exports successifs dans FL.
//
//   RogusEssai.exe <kick.wav> <basse.wav> <dossier de sortie> [priorité de la basse, 0 = Auto]

#include <iostream>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/PluginProcessor.h"

namespace
{
    constexpr int tailleBloc = 512;

    // La « tête de lecture » de l'hôte : elle donne la position dans le morceau.
    struct TeteLecture : juce::AudioPlayHead
    {
        juce::int64 position = 0;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying (true);
            info.setTimeInSamples (position);
            return info;
        }
    };

    bool lire (const juce::File& fichier, juce::AudioBuffer<float>& son, double& fs)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> lecteur (formats.createReaderFor (fichier));

        if (lecteur == nullptr)
            return false;

        fs = lecteur->sampleRate;
        son.setSize (2, (int) lecteur->lengthInSamples);
        lecteur->read (&son, 0, (int) lecteur->lengthInSamples, 0, true, true);
        return true;
    }

    void ecrire (const juce::File& fichier, const juce::AudioBuffer<float>& son, double fs)
    {
        fichier.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> flux = std::make_unique<juce::FileOutputStream> (fichier);
        auto ecrivain = wav.createWriterFor (flux, juce::AudioFormatWriterOptions {}
                                                       .withSampleRate (fs).withNumChannels (2).withBitsPerSample (24));

        if (ecrivain != nullptr)
            ecrivain->writeFromAudioSampleBuffer (son, 0, son.getNumSamples());
    }

    void choisir (RogusProcessor& p, const juce::String& id, float valeur)
    {
        auto* param = p.parametres.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (valeur));
    }

    // Copie un bloc du fichier dans le tampon de travail (le fichier tourne en boucle).
    void remplir (juce::AudioBuffer<float>& tampon, const juce::AudioBuffer<float>& son, juce::int64 debut)
    {
        for (int canal = 0; canal < 2; ++canal)
            for (int n = 0; n < tailleBloc; ++n)
                tampon.setSample (canal, n, son.getSample (canal, (int) ((debut + n) % son.getNumSamples())));
    }

    // L'écoute, comme le bouton « Écouter la piste » : on joue le fichier en
    // boucle jusqu'à ce que le plugin ait entendu ses 10 secondes de son.
    bool ecouter (RogusProcessor& p, const juce::AudioBuffer<float>& son)
    {
        juce::AudioBuffer<float> tampon (2, tailleBloc);
        juce::MidiBuffer midi;
        p.demarrerEcoute();

        for (juce::int64 debut = 0; debut < (juce::int64) son.getNumSamples() * 60 && p.ecouteEnCours(); debut += tailleBloc)
        {
            remplir (tampon, son, debut);
            p.processBlock (tampon, midi);
            p.mettreAJour();
        }

        p.mettreAJour();
        return p.lireMesure().valide && p.lireMesure().refValide;
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI initialisation;

    if (argc < 4)
    {
        std::cout << "Usage : RogusEssai <kick.wav> <basse.wav> <dossier de sortie> [priorite basse]" << std::endl;
        return 1;
    }

    juce::AudioBuffer<float> sonKick, sonBasse;
    double fs = 44100.0, fsBasse = 44100.0;

    if (! lire (juce::File (argv[1]), sonKick, fs) || ! lire (juce::File (argv[2]), sonBasse, fsBasse) || fs != fsBasse)
    {
        std::cout << "Fichiers illisibles, ou frequences d'echantillonnage differentes." << std::endl;
        return 1;
    }

    const juce::File dossier (argv[3]);
    dossier.createDirectory();
    const float prioriteBasse = argc > 4 ? (float) juce::String (argv[4]).getIntValue() : 0.0f;
    const int nbEch = juce::jmin (sonKick.getNumSamples(), sonBasse.getNumSamples()) / tailleBloc * tailleBloc;

    RogusProcessor kick, basse;

    for (auto* p : { &kick, &basse })
    {
        p->setRateAndBufferSizeDetails (fs, tailleBloc);
        p->prepareToPlay (fs, tailleBloc);
        // Les fichiers sortent déjà de FL avec la correction fixe : on ne garde
        // ici que le démasquage et le creux, pour ne mesurer qu'eux.
        choisir (*p, IdParam::correctionActive, 0.0f);
        choisir (*p, IdParam::compressionActive, 0.0f);
    }

    choisir (kick, IdParam::role, 4.0f);       // Kick
    choisir (kick, IdParam::source, 4.0f);     // Batterie
    choisir (basse, IdParam::role, 3.0f);      // Basse
    choisir (basse, IdParam::source, 3.0f);    // Basse
    choisir (basse, IdParam::priorite, prioriteBasse);
    choisir (basse, IdParam::demasquageQuantite, 100.0f);
    choisir (basse, IdParam::reactivite, 100.0f);

    if (! ecouter (kick, sonKick) || ! ecouter (basse, sonBasse))
    {
        std::cout << "L'ecoute n'a pas abouti." << std::endl;
        return 1;
    }

    std::cout << "Ecoutes faites. Priorite kick " << kick.lirePriorite() << ", basse " << basse.lirePriorite() << std::endl;

    // Trois passes : démasquage coupé (référence), démasquage seul, démasquage + creux.
    struct Passe { const char* nom; float demasquage; float creux; };

    for (const Passe passe : { Passe { "basse_reference", 0.0f, 0.0f },
                               Passe { "basse_demasquage_seul", 1.0f, 0.0f },
                               Passe { "basse_avec_creux", 1.0f, 1.0f } })
    {
        choisir (basse, IdParam::demasquageActif, passe.demasquage);
        choisir (basse, IdParam::creuxVolume, passe.creux);

        for (auto* p : { &kick, &basse })
            p->prepareToPlay (fs, tailleBloc);

        TeteLecture tete;
        kick.setPlayHead (&tete);
        basse.setPlayHead (&tete);

        juce::AudioBuffer<float> tamponKick (2, tailleBloc), tamponBasse (2, tailleBloc);
        juce::AudioBuffer<float> sortie (2, nbEch), melange (2, nbEch);
        juce::MidiBuffer midi;
        float creuxMin = 0.0f;

        for (juce::int64 debut = 0; debut < nbEch; debut += tailleBloc)
        {
            tete.position = debut;
            remplir (tamponKick, sonKick, debut);
            remplir (tamponBasse, sonBasse, debut);
            kick.processBlock (tamponKick, midi);
            basse.processBlock (tamponBasse, midi);
            creuxMin = juce::jmin (creuxMin, basse.lireCreux());

            for (int canal = 0; canal < 2; ++canal)
            {
                sortie.copyFrom (canal, (int) debut, tamponBasse, canal, 0, tailleBloc);
                melange.copyFrom (canal, (int) debut, tamponBasse, canal, 0, tailleBloc);
                melange.addFrom (canal, (int) debut, tamponKick, canal, 0, tailleBloc);
            }
        }

        kick.setPlayHead (nullptr);
        basse.setPlayHead (nullptr);

        ecrire (dossier.getChildFile (juce::String (passe.nom) + ".wav"), sortie, fs);
        ecrire (dossier.getChildFile (juce::String (passe.nom) + "_et_batterie.wav"), melange, fs);
        std::cout << passe.nom << " : creux le plus profond " << juce::String (creuxMin, 2) << " dB" << std::endl;
    }

    return 0;
}
