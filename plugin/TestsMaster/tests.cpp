// Tests de Rogus Master, hors FL Studio : mesure LUFS, crête réelle, limiteur,
// calage du gain. Ils ne disent rien de la qualité sonore.
//
//   .\plugin\tester.ps1   (lance aussi ces tests)

#include <cmath>
#include <iostream>

#include "../SourceMaster/Limiteur.h"
#include "../SourceMaster/MasterProcessor.h"
#include "../SourceMaster/MesureLoudness.h"

namespace
{
    int nbEchecs = 0;

    void verifier (bool condition, const juce::String& quoi)
    {
        std::cout << (condition ? "  ok     " : "  ECHEC  ") << quoi << std::endl;
        if (! condition)
            ++nbEchecs;
    }

    constexpr double fs = 48000.0;
    constexpr int tailleBloc = 512;

    // Dessine la fenêtre hors écran. Si la variable d'environnement
    // ROGUS_CAPTURE donne un dossier, l'image y est enregistrée : cela permet
    // de vérifier la mise en page sans ouvrir FL Studio.
    bool dessinerFenetre (juce::AudioProcessor& p, const juce::String& nomFichier)
    {
        std::unique_ptr<juce::AudioProcessorEditor> fenetre (p.createEditor());

        if (fenetre == nullptr || fenetre->getWidth() <= 0 || fenetre->getHeight() <= 0)
            return false;

        const auto image = fenetre->createComponentSnapshot (fenetre->getLocalBounds());
        const auto dossier = juce::SystemStats::getEnvironmentVariable ("ROGUS_CAPTURE", {});

        if (dossier.isNotEmpty())
        {
            const auto fichier = juce::File (dossier).getChildFile (nomFichier);
            fichier.deleteFile();
            juce::FileOutputStream flux (fichier);

            const bool ecrit = flux.openedOk() && juce::PNGImageFormat().writeImageToStream (juce::SoftwareImageType().convert (image), flux);
            std::cout << "         capture " << (ecrit ? "enregistree : " : "impossible : ")
                      << fichier.getFullPathName() << std::endl;
        }

        return image.isValid();
    }

    // Remplit un bloc de sinus stéréo, en gardant la phase d'un bloc à l'autre.
    struct Sinus
    {
        double phase = 0.0, increment = 0.0;
        float amplitude = 0.0f;

        Sinus (double frequence, float amp, double phaseDepart = 0.0)
            : phase (phaseDepart),
              increment (2.0 * juce::MathConstants<double>::pi * frequence / fs),
              amplitude (amp) {}

        void remplir (juce::AudioBuffer<float>& tampon)
        {
            for (int n = 0; n < tampon.getNumSamples(); ++n)
            {
                const float v = amplitude * (float) std::sin (phase);
                tampon.setSample (0, n, v);
                tampon.setSample (1, n, v);
                phase += increment;

                if (phase > juce::MathConstants<double>::twoPi)
                    phase -= juce::MathConstants<double>::twoPi;
            }
        }
    };

    void choisir (RogusMasterProcessor& p, const juce::String& id, float valeur)
    {
        auto* param = p.parametres.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (valeur));
    }

    void preparer (RogusMasterProcessor& p)
    {
        p.setRateAndBufferSizeDetails (fs, tailleBloc);
        p.prepareToPlay (fs, tailleBloc);
    }

    void jouerSinus (RogusMasterProcessor& p, double secondes, float amplitude, double frequence)
    {
        Sinus sinus (frequence, amplitude);
        juce::AudioBuffer<float> tampon (2, tailleBloc);
        juce::MidiBuffer midi;

        for (int b = 0; b < (int) (secondes * fs / tailleBloc); ++b)
        {
            sinus.remplir (tampon);
            p.processBlock (tampon, midi);
        }
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juce_;

    std::cout << "Mesure LUFS" << std::endl;
    {
        rogus::MesureLoudness m;
        m.prepare (fs, 2, tailleBloc);

        juce::AudioBuffer<float> tampon (2, tailleBloc);
        Sinus sinus (1000.0, 0.1f);

        for (int b = 0; b < (int) (5.0 * fs / tailleBloc); ++b)
        {
            sinus.remplir (tampon);
            m.traiter (tampon, 2);
        }

        verifier (std::abs (m.lufsIntegre() + 20.0f) < 0.4f,
                  "sinus 1 kHz a -20 dBFS : -20,0 LUFS attendu (obtenu "
                  + juce::String (m.lufsIntegre(), 1) + ")");
        verifier (m.mesureValide(), "une mesure est disponible apres 5 s");

        // Silence : la porte absolue doit tout ignorer.
        m.reset();
        tampon.clear();
        for (int b = 0; b < (int) (2.0 * fs / tailleBloc); ++b)
            m.traiter (tampon, 2);

        verifier (! m.mesureValide() && m.lufsIntegre() < -70.0f,
                  "silence : aucune mesure, la porte absolue fonctionne");

        // Un passage fort entoure de silence doit donner la loudness du passage.
        m.reset();
        Sinus fort (1000.0, 0.1f);
        for (int b = 0; b < (int) (2.0 * fs / tailleBloc); ++b)
        {
            fort.remplir (tampon);
            m.traiter (tampon, 2);
        }
        tampon.clear();
        for (int b = 0; b < (int) (3.0 * fs / tailleBloc); ++b)
            m.traiter (tampon, 2);
        for (int b = 0; b < (int) (2.0 * fs / tailleBloc); ++b)
        {
            fort.remplir (tampon);
            m.traiter (tampon, 2);
        }

        verifier (std::abs (m.lufsIntegre() + 20.0f) < 0.6f,
                  "gating : le silence entre deux passages ne fausse pas la mesure ("
                  + juce::String (m.lufsIntegre(), 1) + " LUFS)");
    }

    std::cout << "Crete reelle" << std::endl;
    {
        rogus::MesureLoudness m;
        m.prepare (fs, 2, tailleBloc);
        m.reset();

        juce::AudioBuffer<float> tampon (2, tailleBloc);
        // Sinus a fs/4, dephase de 45 degres : les echantillons sont a -3 dBFS,
        // mais le vrai pic du signal continu est a 0 dBFS.
        Sinus sinus (fs / 4.0, 1.0f, juce::MathConstants<double>::pi / 4.0);

        for (int b = 0; b < (int) (1.0 * fs / tailleBloc); ++b)
        {
            sinus.remplir (tampon);
            m.traiter (tampon, 2);
        }

        verifier (std::abs (m.creteReelleDb()) < 0.3f,
                  "crete inter-echantillon : 0 dBTP attendu (obtenu "
                  + juce::String (m.creteReelleDb(), 2) + " dBTP)");
    }

    std::cout << "Limiteur" << std::endl;
    {
        rogus::Limiteur limiteur;
        rogus::MesureLoudness mesure;
        limiteur.prepare (fs, 2, tailleBloc);
        mesure.prepare (fs, 2, tailleBloc);

        verifier (limiteur.latenceEchantillons() > 0, "latence annoncee a l'hote");

        juce::AudioBuffer<float> tampon (2, tailleBloc);
        Sinus sinus (1000.0, 1.0f);      // 0 dBFS : au-dessus du plafond
        float reduction = 0.0f;

        for (int b = 0; b < (int) (2.0 * fs / tailleBloc); ++b)
        {
            sinus.remplir (tampon);
            limiteur.traiter (tampon, 2, 0.0f, -1.0f, reduction);
            mesure.traiter (tampon, 2);
        }

        verifier (mesure.creteReelleDb() <= -0.7f,
                  "plafond : la crete reelle reste sous -1 dBTP (obtenu "
                  + juce::String (mesure.creteReelleDb(), 2) + " dBTP)");
        verifier (reduction < -0.2f, "reduction du limiteur visible ("
                  + juce::String (reduction, 1) + " dB)");

        // Niveau bas : le limiteur ne doit pas changer le son.
        limiteur.reset();
        mesure.reset();
        Sinus faible (1000.0, 0.03f);
        double energieEntree = 0.0, energieSortie = 0.0;

        for (int b = 0; b < (int) (2.0 * fs / tailleBloc); ++b)
        {
            faible.remplir (tampon);
            for (int n = 0; n < tailleBloc; ++n)
                energieEntree += (double) tampon.getSample (0, n) * tampon.getSample (0, n);

            limiteur.traiter (tampon, 2, 0.0f, -1.0f, reduction);

            for (int n = 0; n < tailleBloc; ++n)
                energieSortie += (double) tampon.getSample (0, n) * tampon.getSample (0, n);
        }

        const double rapport = energieEntree > 0.0 ? energieSortie / energieEntree : 0.0;
        verifier (std::abs (rapport - 1.0) < 0.02,
                  "sous le plafond : le limiteur est transparent (rapport "
                  + juce::String (rapport, 3) + ")");
    }

    std::cout << "Lissages du limiteur" << std::endl;
    {
        // Ces tests échouent si les lissages redeviennent instantanés (c'était
        // le défaut de la première version : coefficient exp(...) au lieu de
        // 1 - exp(...)).
        juce::AudioBuffer<float> tampon (2, tailleBloc);
        float reduction = 0.0f;

        auto creteBloc = [&tampon]
        {
            return tampon.getMagnitude (0, 0, tailleBloc);
        };

        // --- Relâchement : après un passage fort, le gain revient en douceur --
        {
            rogus::Limiteur limiteur;
            limiteur.prepare (fs, 2, tailleBloc);

            Sinus fort (1000.0, 1.0f);
            for (int b = 0; b < (int) (0.5 * fs / tailleBloc); ++b)
            {
                fort.remplir (tampon);
                limiteur.traiter (tampon, 2, 11.0f, -1.0f, reduction);
            }

            const float reductionFort = reduction;      // environ -12 dB

            // Passage faible : 3e bloc = 21 à 32 ms après la fin du passage fort.
            Sinus faible (1000.0, 0.01f);
            float reductionPeuApres = 0.0f;

            for (int b = 0; b < (int) (1.5 * fs / tailleBloc); ++b)
            {
                faible.remplir (tampon);
                limiteur.traiter (tampon, 2, 11.0f, -1.0f, reduction);

                if (b == 2)
                    reductionPeuApres = reduction;
            }

            verifier (reductionFort < -10.0f,
                      "passage fort : reduction d'environ 12 dB ("
                      + juce::String (reductionFort, 1) + " dB)");
            verifier (reductionPeuApres < -4.0f,
                      "relachement : 30 ms apres, le gain n'est pas encore revenu ("
                      + juce::String (reductionPeuApres, 1) + " dB)");
            verifier (reduction > -0.1f,
                      "relachement : 1,5 s apres, le gain est revenu ("
                      + juce::String (reduction, 2) + " dB)");
        }

        // --- Gain de calage : un changement ne saute pas d'un coup ------------
        {
            rogus::Limiteur limiteur;
            limiteur.prepare (fs, 2, tailleBloc);

            Sinus sinus (1000.0, 0.03f);
            for (int b = 0; b < 20; ++b)
            {
                sinus.remplir (tampon);
                limiteur.traiter (tampon, 2, 0.0f, -1.0f, reduction);
            }

            // +12 dB : le premier bloc (11 ms) doit être loin du niveau final.
            sinus.remplir (tampon);
            limiteur.traiter (tampon, 2, 12.0f, -1.0f, reduction);
            const float creteDebut = creteBloc();

            for (int b = 0; b < (int) (1.0 * fs / tailleBloc); ++b)
            {
                sinus.remplir (tampon);
                limiteur.traiter (tampon, 2, 12.0f, -1.0f, reduction);
            }

            const float creteFin = creteBloc();
            const float attendu = 0.03f * (float) std::pow (10.0, 12.0 / 20.0);

            verifier (creteDebut < 0.6f * creteFin,
                      "gain : un changement de +12 dB est lisse (premier bloc a "
                      + juce::String (juce::roundToInt (100.0f * creteDebut / creteFin)) + " % du niveau final)");
            verifier (std::abs (creteFin / attendu - 1.0f) < 0.03f,
                      "gain : le niveau final est atteint apres 1 s");
        }

        // --- Attaque : une entrée brutale très forte ne perce pas le plafond --
        {
            rogus::Limiteur limiteur;
            rogus::MesureLoudness mesure;
            limiteur.prepare (fs, 2, tailleBloc);
            mesure.prepare (fs, 2, tailleBloc);

            // Salves de 100 ms à 0 dBFS poussées de +12 dB, séparées par du
            // silence : le limiteur doit descendre de 13 dB en 5 ms à chaque fois.
            // Réglage réel du plugin : visée -1,5 dBTP pour sortir sous -1.
            Sinus salve (1000.0, 1.0f);

            for (int b = 0; b < (int) (2.0 * fs / tailleBloc); ++b)
            {
                if ((b / 9) % 2 == 0)
                    salve.remplir (tampon);
                else
                    tampon.clear();

                limiteur.traiter (tampon, 2, 12.0f, -1.5f, reduction);
                mesure.traiter (tampon, 2);
            }

            verifier (mesure.creteReelleDb() <= -1.0f,
                      "attaque : des salves a +12 dB restent sous -1 dBTP (obtenu "
                      + juce::String (mesure.creteReelleDb(), 2) + " dBTP)");
        }
    }

    std::cout << "Calage du gain" << std::endl;
    {
        RogusMasterProcessor p;
        preparer (p);

        choisir (p, IdParamMaster::cible, 0.0f);      // -14 LUFS
        p.demarrerEcoute();
        jouerSinus (p, 8.0, 0.1f, 1000.0);
        p.arreterEtRegler();

        const float gain = p.parametres.getRawParameterValue (IdParamMaster::gain)->load();
        verifier (std::abs (gain - 6.0f) < 0.7f,
                  "ecoute : gain calcule pour -14 LUFS (obtenu "
                  + juce::String (gain, 1) + " dB, attendu environ +6,0)");

        // Changer de cible doit recalculer le gain sans réécouter.
        choisir (p, IdParamMaster::cible, 1.0f);      // -11 LUFS
        p.mettreAJour();

        const float gainFort = p.parametres.getRawParameterValue (IdParamMaster::gain)->load();
        verifier (std::abs (gainFort - 9.0f) < 0.7f,
                  "changement de cible : +3 dB sans reecoute (obtenu "
                  + juce::String (gainFort, 1) + " dB)");

        // La mesure et le gain survivent à un enregistrement.
        juce::MemoryBlock etat;
        p.getStateInformation (etat);

        RogusMasterProcessor relu;
        relu.setStateInformation (etat.getData(), (int) etat.getSize());

        verifier (relu.lireCibleLufs() == -11.0f
                  && std::abs (relu.lireGain() - gainFort) < 0.01f
                  && relu.lireLufsMesure() < -19.0f,
                  "sauvegarde : cible, gain et mesure retrouves");
    }

    std::cout << "Plafond du processeur" << std::endl;
    {
        RogusMasterProcessor p;
        preparer (p);
        choisir (p, IdParamMaster::cible, 0.0f);

        // Sinus a 0 dBFS : le limiteur interne vise 0,5 dB sous -1 dBTP pour
        // que la reconstruction ne repasse pas au-dessus du plafond.
        jouerSinus (p, 2.0, 1.0f, 1000.0);

        verifier (p.lireCreteReelle() <= -1.0f,
                  "processeur : la crete reelle de sortie reste sous -1 dBTP (obtenu "
                  + juce::String (p.lireCreteReelle(), 2) + " dBTP)");
    }

    std::cout << "Fenetre, nom et couleur de piste" << std::endl;
    {
        // Un hôte qui ne dit rien de la piste : pas de nom, pas de couleur,
        // et la fenêtre se dessine quand même (accent ambre).
        RogusMasterProcessor p;
        verifier (p.piste.lire().nom.isEmpty() && ! p.piste.lire().aCouleur, "hote muet : pas d'etiquette, pas de couleur");
        verifier (dessinerFenetre (p, "master-defaut.png"), "hote muet : la fenetre se dessine");

        juce::AudioProcessor::TrackProperties proprietes;
        proprietes.name = juce::String ("Master");
        proprietes.colour = juce::Colour (0xff6fcf8e);
        p.updateTrackProperties (proprietes);
        verifier (p.piste.lire().nom == "Master" && p.piste.lire().aCouleur, "nom et couleur de piste recus");
        verifier (dessinerFenetre (p, "master-piste.png"), "piste nommee et coloree : la fenetre se dessine");
    }

    std::cout << (nbEchecs == 0 ? "\nTous les tests passent." : "\nTests en echec : "
                                                                   + juce::String (nbEchecs))
              << std::endl;

    return nbEchecs == 0 ? 0 : 1;
}
