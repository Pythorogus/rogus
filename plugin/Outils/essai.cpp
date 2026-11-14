// Essai sur de vrais fichiers, hors FL Studio : deux instances du moteur de
// Rogus (un kick et une basse) traitent deux fichiers WAV comme le ferait
// l'hôte, et on enregistre ce que sort la basse, avec et sans creux de volume.
// Cela permet de vérifier le plugin sur le vrai son d'un morceau sans passer
// par des exports successifs dans FL.
//
//   RogusEssai.exe <kick.wav> <basse.wav> <dossier de sortie> [priorité de la basse, 0 = Auto]

#include <algorithm>
#include <iostream>
#include <limits>
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

// --- Mode « morceau » ---------------------------------------------------------
// Toutes les pistes d'un dossier passent dans le plugin complet, chacune dans
// sa propre instance, comme dans un projet FL. Le rôle et la source de chaque
// piste sont devinés d'après le nom du fichier.
//
//   RogusEssai.exe --morceau <dossier des pistes> <dossier de sortie> [quantité du démasquage, 50 par défaut]

namespace
{
    struct Piste
    {
        juce::String nom;
        juce::AudioBuffer<float> son;
        std::unique_ptr<RogusProcessor> moteur;
        int role = 0, source = 0;
        bool ecoutee = false;
    };

    // Numéros des menus : rôles 0 Lead, 1 Rythmique, 2 Arpège, 3 Basse, 4 Kick,
    // 5 Snare, 6 Hits, 7 Voix, 8 Nappe ; sources 0 guitare électrique,
    // 1 guitare acoustique, 2 voix, 3 basse, 4 batterie, 5 synthé.
    bool deviner (const juce::String& nom, int& role, int& source)
    {
        struct Regle { const char* debut; int role, source; };

        for (const Regle r : { Regle { "kick", 4, 4 }, Regle { "snare", 5, 4 }, Regle { "hit", 6, 4 },
                               Regle { "bass", 3, 3 }, Regle { "guitare_ryth", 1, 0 }, Regle { "ryth", 1, 0 },
                               Regle { "lead", 0, 0 }, Regle { "synth", 0, 5 }, Regle { "arp", 2, 5 },
                               Regle { "voix", 7, 2 }, Regle { "nappe", 8, 5 }, Regle { "pad", 8, 5 } })
        {
            if (nom.startsWithIgnoreCase (r.debut))
            {
                role = r.role;
                source = r.source;
                return true;
            }
        }

        return false;
    }

    double energie (const juce::AudioBuffer<float>& son, int nbEch)
    {
        double somme = 0.0;

        for (int canal = 0; canal < 2; ++canal)
            for (int n = 0; n < nbEch; ++n)
                somme += (double) son.getSample (canal, n) * (double) son.getSample (canal, n);

        return somme;
    }

    float enDb (double rapport)  { return (float) (10.0 * std::log10 (rapport + 1.0e-30)); }

    juce::String db (float valeur, int decimales = 1)
    {
        return (valeur >= 0.0f ? "+" : "") + juce::String (valeur, decimales);
    }

    // Ce qu'une passe a produit pour une piste.
    struct Resultat
    {
        juce::AudioBuffer<float> sortie;
        rogus::Reductions cedeMax {};      // plus forte réduction du démasquage, par bande
        int roleDominant = -1;
        float compMax = 0.0f;              // plus forte baisse du compresseur
        bool fini = true;
    };

    // Joue tout le morceau une fois : chaque bloc passe dans toutes les pistes,
    // dans l'ordre demandé, avec la même position de lecture.
    std::vector<Resultat> jouerMorceau (std::vector<Piste>& pistes, int nbEch, double fs, int bloc, bool ordreInverse)
    {
        TeteLecture tete;
        std::vector<Resultat> resultats (pistes.size());

        for (size_t i = 0; i < pistes.size(); ++i)
        {
            pistes[i].moteur->setRateAndBufferSizeDetails (fs, bloc);
            pistes[i].moteur->prepareToPlay (fs, bloc);
            pistes[i].moteur->setPlayHead (&tete);
            resultats[i].sortie.setSize (2, nbEch);
        }

        juce::AudioBuffer<float> tampon (2, bloc);
        juce::MidiBuffer midi;

        for (int debut = 0; debut + bloc <= nbEch; debut += bloc)
        {
            tete.position = debut;

            for (size_t k = 0; k < pistes.size(); ++k)
            {
                const size_t i = ordreInverse ? pistes.size() - 1 - k : k;
                auto& moteur = *pistes[i].moteur;
                auto& r = resultats[i];

                for (int canal = 0; canal < 2; ++canal)
                    tampon.copyFrom (canal, 0, pistes[i].son, canal, debut, bloc);

                moteur.processBlock (tampon, midi);

                for (int canal = 0; canal < 2; ++canal)
                {
                    r.sortie.copyFrom (canal, debut, tampon, canal, 0, bloc);

                    for (int n = 0; n < bloc; ++n)
                        r.fini = r.fini && std::isfinite (tampon.getSample (canal, n));
                }

                const auto reductions = moteur.lireReductions();

                for (size_t b = 0; b < (size_t) rogus::nbBandes; ++b)
                {
                    if (reductions[b] < r.cedeMax[b])
                    {
                        const bool laPlusForte = std::all_of (r.cedeMax.begin(), r.cedeMax.end(),
                                                              [&] (float v) { return reductions[b] < v; });
                        r.cedeMax[b] = reductions[b];

                        if (laPlusForte && moteur.lireRoleDominant() >= 0)
                            r.roleDominant = moteur.lireRoleDominant();
                    }
                }

                r.compMax = juce::jmin (r.compMax, moteur.lireReductionCompression());
            }
        }

        for (auto& p : pistes)
            p.moteur->setPlayHead (nullptr);

        return resultats;
    }

    juce::AudioBuffer<float> melanger (const std::vector<Resultat>& resultats, int nbEch)
    {
        juce::AudioBuffer<float> mix (2, nbEch);
        mix.clear();

        for (const auto& r : resultats)
            for (int canal = 0; canal < 2; ++canal)
                mix.addFrom (canal, 0, r.sortie, canal, 0, nbEch);

        return mix;
    }

    float ecartMax (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int nbEch)
    {
        float ecart = 0.0f;

        for (int canal = 0; canal < 2; ++canal)
            for (int n = 0; n < nbEch; ++n)
                ecart = juce::jmax (ecart, std::abs (a.getSample (canal, n) - b.getSample (canal, n)));

        return ecart;
    }

    int essayerMorceau (const juce::File& dossierPistes, const juce::File& dossierSortie, float quantiteDemasquage)
    {
        std::vector<Piste> pistes;
        double fs = 0.0;
        int nbEch = std::numeric_limits<int>::max();

        for (const auto& fichier : dossierPistes.findChildFiles (juce::File::findFiles, false, "*.wav"))
        {
            Piste p;
            p.nom = fichier.getFileNameWithoutExtension();
            double fsPiste = 0.0;

            if (! deviner (p.nom, p.role, p.source))
            {
                std::cout << "  (piste ignoree, role inconnu : " << p.nom << ")" << std::endl;
                continue;
            }

            if (! lire (fichier, p.son, fsPiste) || (fs != 0.0 && fsPiste != fs))
            {
                std::cout << "Fichier illisible : " << p.nom << std::endl;
                return 1;
            }

            fs = fsPiste;
            nbEch = juce::jmin (nbEch, p.son.getNumSamples());
            pistes.push_back (std::move (p));
        }

        if (pistes.empty())
        {
            std::cout << "Aucune piste reconnue dans ce dossier." << std::endl;
            return 1;
        }

        nbEch = nbEch / 1024 * 1024;      // un nombre entier de blocs, quelle que soit leur taille
        dossierSortie.createDirectory();

        // --- Écoute de chaque piste, réglages d'usine ---
        std::cout << "ECOUTE (reglages d'usine)" << std::endl;

        for (auto& p : pistes)
        {
            p.moteur = std::make_unique<RogusProcessor>();
            p.moteur->setRateAndBufferSizeDetails (fs, tailleBloc);
            p.moteur->prepareToPlay (fs, tailleBloc);
            choisir (*p.moteur, IdParam::role, (float) p.role);
            choisir (*p.moteur, IdParam::source, (float) p.source);
            p.ecoutee = ecouter (*p.moteur, p.son);

            if (p.moteur->ecouteEnCours())
            {
                // Le plugin attend encore du son : la piste est trop faible pour lui.
                juce::AudioBuffer<float> tampon (2, tailleBloc);
                juce::MidiBuffer midi;
                p.moteur->arreterEcoute();
                tampon.clear();
                p.moteur->processBlock (tampon, midi);
                p.moteur->mettreAJour();
            }

            const auto m = p.moteur->lireMesure();
            const auto c = p.moteur->lireCorrection();
            const auto dosee = rogus::doser (c, 0.6f);
            juce::String ligne = "  " + p.nom.paddedRight (' ', 20) + rogus::role (p.role).nom + " / " + rogus::source (p.source).nom;

            if (! m.valide)
            {
                std::cout << ligne << " : PAS D'ECOUTE (son trop faible ou trop court)" << std::endl;
                continue;
            }

            ligne << " : coupe-bas " << juce::roundToInt (c.coupeBasHz) << " Hz, bandes a 60 % [";

            for (float g : dosee.gains)
                ligne << " " << db (g);

            ligne << " ] dB, compensation " << db (dosee.compensationDb) << " dB";

            for (int i = 0; i < c.nbRes; ++i)
                ligne << ", resonance " << juce::roundToInt (c.resHz[(size_t) i]) << " Hz (" << db (dosee.resGain[(size_t) i]) << " dB)";

            ligne << (m.refValide ? "" : ", SANS repere de demasquage") << (m.histoValide ? "" : ", SANS histogramme");
            std::cout << ligne << std::endl;
        }

        auto regler = [&] (bool correction, bool demasquage)
        {
            for (auto& p : pistes)
            {
                choisir (*p.moteur, IdParam::correctionActive, correction ? 1.0f : 0.0f);
                choisir (*p.moteur, IdParam::demasquageActif, demasquage ? 1.0f : 0.0f);
            }
        };

        std::vector<double> energieEntree;

        for (auto& p : pistes)
            energieEntree.push_back (energie (p.son, nbEch));

        auto decrire = [&] (const char* titre, const std::vector<Resultat>& resultats, bool detail)
        {
            std::cout << titre << std::endl;

            for (size_t i = 0; i < pistes.size(); ++i)
            {
                const auto& r = resultats[i];
                juce::String ligne = "  " + pistes[i].nom.paddedRight (' ', 20) + "volume "
                                   + db (enDb (energie (r.sortie, nbEch) / energieEntree[i]), 2) + " dB, crete "
                                   + juce::String (juce::Decibels::gainToDecibels (r.sortie.getMagnitude (0, nbEch)), 1) + " dBFS"
                                   + (r.fini ? "" : "  VALEUR INVALIDE");

                if (detail)
                {
                    size_t bande = 0;

                    for (size_t b = 1; b < r.cedeMax.size(); ++b)
                        if (r.cedeMax[b] < r.cedeMax[bande])
                            bande = b;

                    if (r.cedeMax[bande] < -0.05f)
                        ligne << ", cede jusqu'a " << juce::String (-r.cedeMax[bande], 1) << " dB vers "
                              << juce::roundToInt (rogus::frequencesBandes[bande]) << " Hz"
                              << (r.roleDominant >= 0 ? " a : " + juce::String (rogus::role (r.roleDominant).nom) : juce::String());
                    else
                        ligne << ", ne cede rien";

                    if (r.compMax < -0.05f)
                        ligne << ", compresse jusqu'a " << juce::String (-r.compMax, 1) << " dB";
                }

                std::cout << ligne << std::endl;
            }

            juce::AudioBuffer<float> entree (2, nbEch);
            entree.clear();

            for (auto& p : pistes)
                for (int canal = 0; canal < 2; ++canal)
                    entree.addFrom (canal, 0, p.son, canal, 0, nbEch);

            const auto mix = melanger (resultats, nbEch);
            std::cout << "  => mix complet : volume " << db (enDb (energie (mix, nbEch) / energie (entree, nbEch)), 2)
                      << " dB, crete " << juce::String (juce::Decibels::gainToDecibels (mix.getMagnitude (0, nbEch)), 1) << " dBFS" << std::endl;
            return mix;
        };

        // --- 1. Tout coupé : le son doit traverser le plugin intact ---
        regler (false, false);
        const auto coupe = jouerMorceau (pistes, nbEch, fs, tailleBloc, false);
        float ecartCoupe = 0.0f;

        for (size_t i = 0; i < pistes.size(); ++i)
            ecartCoupe = juce::jmax (ecartCoupe, ecartMax (coupe[i].sortie, pistes[i].son, nbEch));

        std::cout << "1. TOUT COUPE : ecart max entre sortie et entree = " << ecartCoupe
                  << (ecartCoupe == 0.0f ? "  (identique)" : "  (DIFFERENT)") << std::endl;
        ecrire (dossierSortie.getChildFile ("mix_sans_rogus.wav"), melanger (coupe, nbEch), fs);

        // --- 2. Correction fixe seule ---
        regler (true, false);
        const auto correctionSeule = jouerMorceau (pistes, nbEch, fs, tailleBloc, false);
        decrire ("2. CORRECTION FIXE SEULE (60 %)", correctionSeule, false);

        // --- 3. Correction fixe + démasquage (50 % = réglage d'usine) ---
        regler (true, true);

        for (auto& p : pistes)
            choisir (*p.moteur, IdParam::demasquageQuantite, quantiteDemasquage);

        const auto usine = jouerMorceau (pistes, nbEch, fs, tailleBloc, false);
        std::cout << "(demasquage a " << quantiteDemasquage << " %)" << std::endl;
        ecrire (dossierSortie.getChildFile ("mix_avec_rogus.wav"),
                decrire ("3. CORRECTION 60 % + DEMASQUAGE", usine, true), fs);

        // Chaque piste à part, pour mesurer ensuite ce que le démasquage lui retire.
        for (size_t i = 0; i < pistes.size(); ++i)
        {
            ecrire (dossierSortie.getChildFile ("correction_" + pistes[i].nom + ".wav"), correctionSeule[i].sortie, fs);
            ecrire (dossierSortie.getChildFile ("demasquage_" + pistes[i].nom + ".wav"), usine[i].sortie, fs);
        }

        // --- 4. Même résultat quels que soient les blocs et l'ordre des pistes ---
        const auto petits = jouerMorceau (pistes, nbEch, fs, 256, false);
        const auto inverse = jouerMorceau (pistes, nbEch, fs, tailleBloc, true);
        float ecartBlocs = 0.0f, ecartOrdre = 0.0f;

        for (size_t i = 0; i < pistes.size(); ++i)
        {
            ecartBlocs = juce::jmax (ecartBlocs, ecartMax (petits[i].sortie, usine[i].sortie, nbEch));
            ecartOrdre = juce::jmax (ecartOrdre, ecartMax (inverse[i].sortie, usine[i].sortie, nbEch));
        }

        std::cout << "4. ECOUTE = EXPORT : blocs de 256 au lieu de 512, ecart max " << ecartBlocs
                  << " ; ordre des pistes inverse, ecart max " << ecartOrdre << std::endl;

        // --- 5. Compression cochée partout (50 %), en plus ---
        for (auto& p : pistes)
            choisir (*p.moteur, IdParam::compressionActive, 1.0f);

        decrire ("5. AVEC LA COMPRESSION COCHEE PARTOUT (50 %)", jouerMorceau (pistes, nbEch, fs, tailleBloc, false), true);

        for (auto& p : pistes)
            choisir (*p.moteur, IdParam::compressionActive, 0.0f);

        // --- 6. Creux de volume coché partout, en plus des réglages d'usine ---
        for (auto& p : pistes)
            choisir (*p.moteur, IdParam::creuxVolume, 1.0f);

        decrire ("6. AVEC LE CREUX DE VOLUME COCHE PARTOUT (demasquage 50 %)", jouerMorceau (pistes, nbEch, fs, tailleBloc, false), false);
        return 0;
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI initialisation;

    if (argc >= 4 && juce::String (argv[1]) == "--morceau")
        return essayerMorceau (juce::File (juce::String::fromUTF8 (argv[2])), juce::File (juce::String::fromUTF8 (argv[3])),
                               argc > 4 ? juce::String (argv[4]).getFloatValue() : 50.0f);

    if (argc < 4)
    {
        std::cout << "Usage : RogusEssai <kick.wav> <basse.wav> <dossier de sortie> [priorite basse]" << std::endl
                  << "        RogusEssai --morceau <dossier des pistes> <dossier de sortie>" << std::endl;
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
