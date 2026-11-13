// Tests hors FL Studio : ils font tourner le moteur du plugin sur des signaux
// de synthèse et vérifient des propriétés mesurables. Ils ne disent rien de
// la qualité sonore : cela, seule l'écoute peut le dire.
//
//   .\plugin\tester.ps1

#include <cmath>
#include <iostream>
#include "../Source/PluginProcessor.h"

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

    // Bruit rose : même énergie dans chaque octave, donc niveau égal dans
    // toutes nos bandes. C'est un signal de test « plat » pour l'oreille.
    struct BruitRose
    {
        juce::Random hasard { 1234 };
        float b0 = 0, b1 = 0, b2 = 0;

        float suivant()
        {
            const float blanc = hasard.nextFloat() * 2.0f - 1.0f;
            b0 = 0.99765f * b0 + blanc * 0.0990460f;
            b1 = 0.96300f * b1 + blanc * 0.2965164f;
            b2 = 0.57000f * b2 + blanc * 1.0526913f;
            return (b0 + b1 + b2 + blanc * 0.1848f) * 0.08f;
        }
    };

    void choisir (RogusProcessor& p, const juce::String& id, float valeur)
    {
        auto* param = p.parametres.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (valeur));
    }

    // Fait passer `secondes` de bruit rose (plus un sifflement facultatif) dans le plugin.
    // Renvoie le plus grand écart entre la sortie et l'entrée.
    float jouer (RogusProcessor& p, double secondes, float sifflementHz = 0.0f, bool* fini = nullptr)
    {
        BruitRose bruit;
        juce::AudioBuffer<float> tampon (2, tailleBloc), entree (2, tailleBloc);
        juce::MidiBuffer midi;
        float ecartMax = 0.0f;
        bool toutFini = true;
        juce::int64 position = 0;

        for (int bloc = 0; bloc < (int) (secondes * fs / tailleBloc); ++bloc)
        {
            for (int n = 0; n < tailleBloc; ++n, ++position)
            {
                float v = bruit.suivant();
                if (sifflementHz > 0.0f)
                    v += 0.12f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * sifflementHz * (double) position / fs);

                tampon.setSample (0, n, v);
                tampon.setSample (1, n, v);
            }

            entree.makeCopyOf (tampon);
            p.processBlock (tampon, midi);

            for (int n = 0; n < tailleBloc; ++n)
            {
                toutFini = toutFini && std::isfinite (tampon.getSample (0, n)) && std::isfinite (tampon.getSample (1, n));
                ecartMax = juce::jmax (ecartMax, std::abs (tampon.getSample (0, n) - entree.getSample (0, n)));
            }
        }

        if (fini != nullptr)
            *fini = toutFini;

        return ecartMax;
    }

    // Une petite mélodie : quatre notes qui tournent, une demi-seconde chacune,
    // sur du bruit rose. Chaque note n'est présente que 25 % du temps : ce sont
    // des notes jouées, pas des résonances. Avec `sifflementHz` > 0, on ajoute
    // en plus un sifflement permanent, lui bien présent en continu.
    void jouerNotes (RogusProcessor& p, double secondes, float sifflementHz = 0.0f)
    {
        constexpr float notes[] = { 261.6f, 329.6f, 392.0f, 523.3f };
        constexpr double pi2 = 2.0 * juce::MathConstants<double>::pi;
        const int tailleNote = (int) (0.5 * fs), fondu = (int) (0.01 * fs);

        BruitRose bruit;
        juce::AudioBuffer<float> tampon (2, tailleBloc);
        juce::MidiBuffer midi;
        double phaseNote = 0.0, phaseSifflement = 0.0;
        juce::int64 position = 0;

        for (int bloc = 0; bloc < (int) (secondes * fs / tailleBloc); ++bloc)
        {
            for (int n = 0; n < tailleBloc; ++n, ++position)
            {
                const int dansNote = (int) (position % tailleNote);
                const float note = notes[(position / tailleNote) % 4];

                // Fondu de 10 ms aux deux bouts : sans lui, le changement brutal
                // de note créerait un claquement qui salirait tout le spectre.
                const float enveloppe = juce::jmin (1.0f, (float) dansNote / (float) fondu,
                                                    (float) (tailleNote - dansNote) / (float) fondu);

                phaseNote += pi2 * note / fs;
                float v = bruit.suivant() + 0.2f * enveloppe * (float) std::sin (phaseNote);

                if (sifflementHz > 0.0f)
                {
                    phaseSifflement += pi2 * sifflementHz / fs;
                    v += 0.12f * (float) std::sin (phaseSifflement);
                }

                tampon.setSample (0, n, v);
                tampon.setSample (1, n, v);
            }

            p.processBlock (tampon, midi);
        }
    }

    // Joue du bruit filtré en bandes d'une octave (une par centre demandé) : l'énergie
    // s'étale sur toute la bande, comme dans un vrai instrument. Des sinus purs
    // seraient un cas bien plus dur que la musique. Renvoie la variation de niveau
    // entre l'entrée et la sortie du plugin, en dB, mesurée sur les deux dernières
    // secondes (le temps que les filtres se soient installés).
    float jouerBandes (RogusProcessor& p, double secondes, const std::vector<float>& centres, float amplitude = 0.8f)
    {
        juce::Random hasard (42);
        std::vector<juce::dsp::IIR::Filter<float>> filtres (centres.size());

        for (size_t i = 0; i < centres.size(); ++i)
            filtres[i].coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (fs, centres[i], 1.4f);

        juce::AudioBuffer<float> tampon (2, tailleBloc);
        juce::MidiBuffer midi;
        const int nbBlocs = (int) (secondes * fs / tailleBloc);
        const int debutMesure = juce::jmax (0, nbBlocs - (int) (2.0 * fs / tailleBloc));
        double energieEntree = 0.0, energieSortie = 0.0;
        juce::int64 position = 0;

        for (int bloc = 0; bloc < nbBlocs; ++bloc)
        {
            for (int n = 0; n < tailleBloc; ++n, ++position)
            {
                double v = 0.0;
                for (auto& filtre : filtres)
                    v += (double) filtre.processSample (amplitude * (hasard.nextFloat() * 2.0f - 1.0f));

                if (bloc >= debutMesure)
                    energieEntree += v * v;

                tampon.setSample (0, n, (float) v);
                tampon.setSample (1, n, (float) v);
            }

            p.processBlock (tampon, midi);

            if (bloc >= debutMesure)
                for (int n = 0; n < tailleBloc; ++n)
                    energieSortie += (double) tampon.getSample (0, n) * (double) tampon.getSample (0, n);
        }

        return energieEntree > 0.0 ? (float) (10.0 * std::log10 (energieSortie / energieEntree + 1.0e-30)) : 0.0f;
    }

    // Une fausse « tête de lecture » : elle joue le rôle de l'hôte, qui indique
    // au plugin où on en est dans le morceau.
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

    // Du bruit limité à quelques bandes d'octave, reproductible (même graine = même bruit).
    struct SourceBruit
    {
        SourceBruit (int graine, const std::vector<float>& centres) : hasard (graine), filtres (centres.size())
        {
            for (size_t i = 0; i < centres.size(); ++i)
                filtres[i].coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (fs, centres[i], 1.4f);
        }

        float suivant()
        {
            float v = 0.0f;
            for (auto& f : filtres)
                v += f.processSample (0.8f * (hasard.nextFloat() * 2.0f - 1.0f));
            return v;
        }

        juce::Random hasard;
        std::vector<juce::dsp::IIR::Filter<float>> filtres;
    };

    // Deux pistes dans le même « hôte » : une voix qui chante par moments (de 2 à
    // 4 s et de 6 à 8 s) et un lead qui joue en continu dans la même zone.
    // On traite les deux pistes bloc par bloc, dans l'ordre demandé, et on
    // mesure de combien le lead baisse quand la voix chante.
    struct Duo
    {
        std::vector<float> sortieLead;      // tout ce que le lead a produit
        float pendantVoix = 0.0f;           // variation de niveau du lead quand la voix chante (dB)
        float sansVoix = 0.0f;              // ... et quand elle se tait
        float ecartVoix = 0.0f;             // plus grand écart entre l'entrée et la sortie de la voix
        int pistesVues = 0;
    };

    Duo jouerDuo (const juce::MemoryBlock& etatVoix, const juce::MemoryBlock& etatLead,
                  bool voixDabord, int bloc, float choixPrioriteLead, bool avecTete = true)
    {
        RogusProcessor voix, lead;
        voix.setStateInformation (etatVoix.getData(), (int) etatVoix.getSize());
        lead.setStateInformation (etatLead.getData(), (int) etatLead.getSize());
        choisir (lead, IdParam::priorite, choixPrioriteLead);

        for (auto* p : { &voix, &lead })
        {
            choisir (*p, IdParam::demasquageQuantite, 100.0f);
            choisir (*p, IdParam::reactivite, 100.0f);
            p->setRateAndBufferSizeDetails (fs, bloc);
            p->prepareToPlay (fs, bloc);
        }

        TeteLecture tete;
        if (avecTete)
        {
            voix.setPlayHead (&tete);
            lead.setPlayHead (&tete);
        }

        SourceBruit bruitVoix (7, { 1600.0f, 3200.0f }), bruitLead (11, { 1600.0f });
        juce::AudioBuffer<float> tamponVoix (2, bloc), tamponLead (2, bloc);
        std::vector<float> entreeLead ((size_t) bloc);
        juce::MidiBuffer midi;
        Duo d;
        double eOn = 0, sOn = 0, eOff = 0, sOff = 0;

        for (juce::int64 debut = 0; debut < (juce::int64) (9.0 * fs); debut += bloc)
        {
            tete.position = debut;

            for (int n = 0; n < bloc; ++n)
            {
                const double t = (double) (debut + n) / fs;
                const bool chante = (t >= 2.0 && t < 4.0) || (t >= 6.0 && t < 8.0);
                const float v = bruitVoix.suivant() * (chante ? 1.0f : 0.0f);
                const float l = bruitLead.suivant();
                tamponVoix.setSample (0, n, v);
                tamponVoix.setSample (1, n, v);
                tamponLead.setSample (0, n, l);
                tamponLead.setSample (1, n, l);
                entreeLead[(size_t) n] = l;

                // La voix ne doit jamais être modifiée : on garde son entrée pour comparer.
                tamponVoix.setSample (1, n, v);
            }

            juce::AudioBuffer<float> entreeVoix;
            entreeVoix.makeCopyOf (tamponVoix);

            if (voixDabord) { voix.processBlock (tamponVoix, midi); lead.processBlock (tamponLead, midi); }
            else            { lead.processBlock (tamponLead, midi); voix.processBlock (tamponVoix, midi); }

            for (int n = 0; n < bloc; ++n)
            {
                const double t = (double) (debut + n) / fs;
                const double e = entreeLead[(size_t) n], s = tamponLead.getSample (0, n);
                d.sortieLead.push_back ((float) s);
                d.ecartVoix = juce::jmax (d.ecartVoix, std::abs (tamponVoix.getSample (0, n) - entreeVoix.getSample (0, n)));

                if (t >= 2.6 && t < 3.9) { eOn += e * e;  sOn += s * s; }
                if (t >= 5.0 && t < 5.9) { eOff += e * e; sOff += s * s; }
            }
        }

        d.pendantVoix = (float) (10.0 * std::log10 (sOn / eOn + 1.0e-30));
        d.sansVoix    = (float) (10.0 * std::log10 (sOff / eOff + 1.0e-30));
        d.pistesVues  = lead.nbPistesConnectees();
        return d;
    }

    // Donne accès aux fonctions statiques de lecture et d'écriture de l'état,
    // pour fabriquer de faux projets enregistrés par d'anciennes versions.
    struct Outils : RogusProcessor
    {
        using juce::AudioProcessor::copyXmlToBinary;
        using juce::AudioProcessor::getXmlFromBinary;
    };

    void fixerParametre (juce::ValueTree& etat, const juce::String& id, float valeur)
    {
        for (int i = 0; i < etat.getNumChildren(); ++i)
        {
            auto enfant = etat.getChild (i);

            if (enfant.getProperty ("id").toString() == id)
                enfant.setProperty ("value", valeur, nullptr);
        }
    }

    // Reproduit l'état qu'écrivait l'étape 2 : sans numéro de version, sans les
    // paramètres « source » et « son », avec l'ancien rôle sur 6 choix.
    juce::MemoryBlock etatEtape2 (const juce::MemoryBlock& actuel, int ancienRole, float quantite, bool actif)
    {
        auto xml = Outils::getXmlFromBinary (actuel.getData(), (int) actuel.getSize());
        auto etat = juce::ValueTree::fromXml (*xml);
        etat.removeProperty ("version", nullptr);

        for (int i = etat.getNumChildren() - 1; i >= 0; --i)
        {
            const auto id = etat.getChild (i).getProperty ("id").toString();

            if (id == IdParam::source || id == IdParam::son)
                etat.removeChild (i, nullptr);
        }

        fixerParametre (etat, IdParam::role, (float) ancienRole);
        fixerParametre (etat, IdParam::quantite, quantite);
        fixerParametre (etat, IdParam::correctionActive, actif ? 1.0f : 0.0f);

        juce::MemoryBlock sortie;
        Outils::copyXmlToBinary (*etat.createXml(), sortie);
        return sortie;
    }

    void preparer (RogusProcessor& p)
    {
        p.setRateAndBufferSizeDetails (fs, tailleBloc);
        p.prepareToPlay (fs, tailleBloc);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juce_;

    std::cout << "Filtres" << std::endl;
    {
        rogus::Correction c;
        c.coupeBasHz = 100.0f;
        auto banc = rogus::construireFiltres (c, fs);
        verifier (std::abs (rogus::reponseDb (banc, 100.0, fs) + 3.0) < 0.2, "coupe-bas : -3 dB a sa frequence");
        verifier (rogus::reponseDb (banc, 25.0, fs) < -22.0, "coupe-bas : 12 dB par octave en dessous");
        verifier (std::abs (rogus::reponseDb (banc, 2000.0, fs)) < 0.1, "coupe-bas : sans effet dans le medium");

        rogus::Correction d;
        d.gains[3] = 6.0f;      // cloche a 800 Hz
        d.gains[0] = -4.0f;     // plateau grave
        d.gains[6] = 3.0f;      // plateau aigu
        banc = rogus::construireFiltres (d, fs);
        verifier (std::abs (rogus::reponseDb (banc, 800.0, fs) - 6.0) < 0.4, "cloche : gain demande au centre");
        verifier (std::abs (rogus::reponseDb (banc, 30.0, fs) + 4.0) < 0.4, "plateau grave : gain demande en bas");
        verifier (std::abs (rogus::reponseDb (banc, 18000.0, fs) - 3.0) < 0.5, "plateau aigu : gain demande en haut");

        const auto neutre = rogus::construireFiltres (rogus::doser (d, 0.0f), fs);
        bool plat = true;
        for (double f = 30.0; f < 20000.0; f *= 1.3)
            plat = plat && std::abs (rogus::reponseDb (neutre, f, fs)) < 0.1;
        // Tolerance de 0,1 dB : le coupe-bas replie a 10 Hz laisse encore -0,05 dB a 30 Hz.
        verifier (plat, "quantite 0 % : courbe plate a 0,1 dB pres");
    }

    std::cout << "Calcul de la correction" << std::endl;
    {
        // Une piste dont l'equilibre est deja celui du role ne doit pas etre touchee.
        const auto& lead = rogus::role (0);
        rogus::Mesure m;
        m.valide = true;
        for (int i = 0; i < rogus::nbBandes; ++i)
            m.niveaux[(size_t) i] = lead.cible[(size_t) i] - 20.0f;

        auto c = rogus::calculerCorrection (lead, rogus::source (0), rogus::son (0), m);
        bool nul = true;
        for (float g : c.gains)
            nul = nul && std::abs (g) < 0.05f;
        verifier (nul, "piste deja equilibree : aucun gain");

        // Trop de bas-medium (+5 dB a 200 et 400 Hz) : la courbe doit creuser la.
        m.niveaux[1] += 5.0f;
        m.niveaux[2] += 5.0f;
        c = rogus::calculerCorrection (lead, rogus::source (0), rogus::son (0), m);
        const auto banc = rogus::construireFiltres (c, fs);
        const double creux = rogus::reponseDb (banc, 280.0, fs), ailleurs = rogus::reponseDb (banc, 2500.0, fs);
        verifier (creux < ailleurs - 3.5 && creux < ailleurs - 0.0, "exces de bas-medium : creuse d'environ 5 dB (obtenu "
                                                                     + juce::String (ailleurs - creux, 1) + ")");

        // Les limites tiennent meme avec une mesure absurde.
        m.niveaux = { 40.0f, -60.0f, 30.0f, -80.0f, 10.0f, -90.0f, 50.0f };
        c = rogus::calculerCorrection (lead, rogus::source (0), rogus::son (0), m);
        const auto extreme = rogus::construireFiltres (c, fs);
        bool borne = true;
        for (double f = 30.0; f < 20000.0; f *= 1.1)
        {
            const double db = rogus::reponseDb (extreme, f, fs) - rogus::reponseDb (rogus::construireFiltres ({ lead.coupeBasHz }, fs), f, fs);
            borne = borne && db < 6.0 && db > -9.0 && std::isfinite (db);
        }
        verifier (borne, "mesure absurde : correction bornee");

        verifier (rogus::calculerCorrection (rogus::role (3), rogus::source (3), rogus::son (0), {}).coupeBasHz == 30.0f,
                  "sans ecoute : seulement le coupe-bas (basse : 30 Hz)");

        // Le coupe-bas final est le plus haut de celui du role et de celui de la source.
        verifier (rogus::calculerCorrection (rogus::role (0), rogus::source (5), rogus::son (0), {}).coupeBasHz == 100.0f,
                  "lead + synthe : coupe-bas du role (100 Hz) car plus haut que celui de la source (25 Hz)");
        verifier (rogus::calculerCorrection (rogus::role (3), rogus::source (0), rogus::son (0), {}).coupeBasHz == 70.0f,
                  "basse + guitare electrique : coupe-bas de la source (70 Hz) car plus haut que celui du role (30 Hz)");

        // On ne monte jamais le grave sous la zone utile de l'instrument : pour une
        // guitare (coupe-bas a 80 Hz), la bande de 100 Hz ne contient presque que
        // de la ronflette. Cas vu chez Pierre : +5,5 dB vers 60 Hz sur une guitare claire.
        {
            const auto& rythmique = rogus::role (1);
            rogus::Mesure maigre;
            maigre.valide = true;
            for (int i = 0; i < rogus::nbBandes; ++i)
                maigre.niveaux[(size_t) i] = rythmique.cible[(size_t) i] - 20.0f;
            maigre.niveaux[0] -= 15.0f;       // tres peu de grave par rapport a la cible

            // On regarde la courbe telle que Pierre la voit (compensation de volume
            // comprise), sans le coupe-bas qui masquerait une montee du grave.
            auto bandesSeules = [] (rogus::Correction c)
            {
                c.coupeBasHz = 10.0f;
                return rogus::construireFiltres (rogus::doser (c, 1.0f), fs);
            };

            const auto guitare = rogus::calculerCorrection (rythmique, rogus::source (0), rogus::son (0), maigre);
            const double grave = rogus::reponseDb (bandesSeules (guitare), 60.0, fs);
            verifier (grave <= 0.3,
                      "guitare pauvre en grave : le grave n'est pas monte (" + juce::String (grave, 1) + " dB a 60 Hz)");

            // Le cas de la capture : mediums trop forts ET grave faible. La compensation
            // de volume remonte toute la courbe ; le grave ne doit pas en profiter.
            rogus::Mesure mediumFort = maigre;
            for (int i = 1; i <= 4; ++i)
                mediumFort.niveaux[(size_t) i] += 8.0f;
            const auto corrigee = rogus::calculerCorrection (rythmique, rogus::source (0), rogus::son (0), mediumFort);
            const double graveFort = rogus::reponseDb (bandesSeules (corrigee), 60.0, fs);
            verifier (graveFort <= 0.3 && rogus::doser (corrigee, 1.0f).compensationDb > 1.0f,
                      "guitare aux mediums forts : le grave reste a 0 dB malgre la compensation de volume ("
                      + juce::String (graveFort, 1) + " dB a 60 Hz, compensation "
                      + juce::String (rogus::doser (corrigee, 1.0f).compensationDb, 1) + " dB)");

            // Pour une basse, le grave EST la zone utile : la meme mesure doit pouvoir le remonter.
            const auto& basse = rogus::role (3);
            for (int i = 0; i < rogus::nbBandes; ++i)
                maigre.niveaux[(size_t) i] = basse.cible[(size_t) i] - 20.0f;
            maigre.niveaux[0] -= 15.0f;
            verifier (rogus::calculerCorrection (basse, rogus::source (3), rogus::son (0), maigre).gains[0] > 2.0f,
                      "basse pauvre en grave : le grave peut etre remonte");
        }

        // La source et le son deplacent la cible : meme mesure, autres gains.
        rogus::Mesure plate;
        plate.valide = true;
        for (int i = 0; i < rogus::nbBandes; ++i)
            plate.niveaux[(size_t) i] = -20.0f;

        const auto clair   = rogus::calculerCorrection (lead, rogus::source (0), rogus::son (0), plate);
        const auto sombre  = rogus::calculerCorrection (lead, rogus::source (0), rogus::son (2), plate);
        const auto brillant = rogus::calculerCorrection (lead, rogus::source (0), rogus::son (3), plate);
        verifier (sombre.gains[6] < clair.gains[6] - 0.5f && brillant.gains[6] > clair.gains[6] + 0.5f,
                  "son : sombre baisse et brillant monte la bande des aigus (" + juce::String (sombre.gains[6], 1) + " / "
                  + juce::String (clair.gains[6], 1) + " / " + juce::String (brillant.gains[6], 1) + " dB)");

        const auto acoustique = rogus::calculerCorrection (lead, rogus::source (1), rogus::son (0), plate);
        verifier (acoustique.gains != clair.gains, "source : guitare acoustique donne une autre correction que l'electrique");

        // Une resonance mesuree n'est corrigee que si le role ET la source s'y pretent.
        plate.nbRes = 1;
        plate.resHz[0] = 1000.0f;
        plate.resExces[0] = 9.0f;
        verifier (rogus::calculerCorrection (lead, rogus::source (0), rogus::son (0), plate).nbRes == 1, "lead + guitare : resonance corrigee");
        verifier (rogus::calculerCorrection (lead, rogus::source (5), rogus::son (0), plate).nbRes == 0, "lead + synthe : resonance laissee");
        verifier (rogus::calculerCorrection (rogus::role (8), rogus::source (0), rogus::son (0), plate).nbRes == 0, "nappe : resonance laissee");
    }

    std::cout << "Moteur : neutralite" << std::endl;
    {
        RogusProcessor p;
        choisir (p, IdParam::correctionActive, 0.0f);
        preparer (p);
        bool fini = false;
        verifier (jouer (p, 1.0, 0.0f, &fini) == 0.0f && fini, "correction desactivee : sortie identique a l'entree");

        choisir (p, IdParam::correctionActive, 1.0f);
        choisir (p, IdParam::quantite, 0.0f);
        jouer (p, 0.5);
        verifier (jouer (p, 0.5) == 0.0f, "quantite 0 % : sortie identique a l'entree");

        choisir (p, IdParam::quantite, 100.0f);
        verifier (jouer (p, 0.5, 0.0f, &fini) > 0.0f && fini, "quantite 100 % : le son est modifie, sans valeur invalide");
    }

    std::cout << "Moteur : ecoute" << std::endl;
    {
        RogusProcessor p;
        preparer (p);

        // Ecoute interrompue trop tot.
        p.demarrerEcoute();
        jouer (p, 1.0);
        p.arreterEcoute();
        jouer (p, 0.1);
        p.mettreAJour();
        verifier (p.ecouteTropCourte() && ! p.ecouteEnCours() && ! p.lireMesure().valide, "ecoute de 1 s : refusee");

        // Ecoute complete d'un bruit rose avec un sifflement a 1 kHz.
        p.demarrerEcoute();
        bool fini = false;
        jouer (p, 6.0, 1000.0f, &fini);
        verifier (p.ecouteEnCours() && p.progressionEcoute() > 0.4f && p.progressionEcoute() < 0.7f, "ecoute en cours : progression coherente");
        jouer (p, 6.0, 1000.0f, &fini);
        p.mettreAJour();

        const auto m = p.lireMesure();
        verifier (m.valide && ! p.ecouteEnCours() && fini, "ecoute de 10 s : terminee toute seule");

        float etendue = 0.0f;
        for (int i = 1; i < rogus::nbBandes; ++i)
            if (i != 3)      // la bande de 800 Hz contient le sifflement
                etendue = juce::jmax (etendue, std::abs (m.niveaux[(size_t) i] - m.niveaux[1]));
        verifier (etendue < 2.0f, "bruit rose : niveaux egaux entre bandes (ecart max " + juce::String (etendue, 1) + " dB)");

        verifier (m.nbRes == 1 && std::abs (m.resHz[0] - 1000.0f) < 40.0f,
                  "sifflement a 1 kHz : une resonance trouvee (" + juce::String (m.nbRes) + ", "
                  + juce::String (m.resHz[0], 0) + " Hz)");

        const auto c = p.lireCorrection();
        verifier (c.nbRes == 1 && c.resGain[0] <= -2.0f && c.resGain[0] >= -4.0f, "resonance : attenuee de 2 a 4 dB");

        // Des notes jouees ne sont pas des resonances.
        RogusProcessor n;
        preparer (n);
        n.demarrerEcoute();
        jouerNotes (n, 12.0);
        n.mettreAJour();
        const auto mn = n.lireMesure();
        verifier (mn.valide && ! n.ecouteEnCours() && mn.nbRes == 0,
                  "melodie de 4 notes : aucune resonance retenue (" + juce::String (mn.nbRes) + ")");

        // Une vraie resonance, melangee a la melodie, doit etre la seule retenue.
        RogusProcessor r;
        preparer (r);
        r.demarrerEcoute();
        jouerNotes (r, 12.0, 1000.0f);
        r.mettreAJour();
        const auto mr = r.lireMesure();
        verifier (mr.valide && mr.nbRes == 1 && std::abs (mr.resHz[0] - 1000.0f) < 40.0f,
                  "melodie + sifflement permanent : seul le sifflement est retenu (" + juce::String (mr.nbRes) + ")");

        // Changer de role recalcule la correction sans reecoute ; la basse ne touche pas aux resonances.
        choisir (p, IdParam::role, 3.0f);       // Basse
        choisir (p, IdParam::source, 3.0f);     // Basse
        p.mettreAJour();
        const auto basse = p.lireCorrection();
        verifier (basse.coupeBasHz == 30.0f && basse.nbRes == 0 && basse.gains != c.gains, "changement de role : correction recalculee");

        // Sauvegarde puis rechargement dans une autre instance.
        juce::MemoryBlock bloc;
        p.getStateInformation (bloc);
        RogusProcessor q;
        q.setStateInformation (bloc.getData(), (int) bloc.getSize());
        const auto relue = q.lireCorrection();
        verifier (q.lireRole() == 3 && q.lireSource() == 3 && q.lireMesure().valide && relue.gains == basse.gains && relue.coupeBasHz == basse.coupeBasHz,
                  "sauvegarde : role, mesure et correction retrouves");

        // Le silence ne compte pas dans une ecoute.
        RogusProcessor s;
        preparer (s);
        s.demarrerEcoute();
        juce::AudioBuffer<float> vide (2, tailleBloc);
        juce::MidiBuffer midi;
        for (int i = 0; i < 400; ++i)
        {
            vide.clear();
            s.processBlock (vide, midi);
        }
        verifier (s.ecouteEnCours() && s.progressionEcoute() == 0.0f, "silence : l'ecoute attend du son");
    }

    std::cout << "Moteur : volume conserve" << std::endl;
    {
        // La correction change la forme du spectre, pas le volume de la piste.
        // On ecoute du bruit en bandes d'octave concentre dans quelques bandes
        // (un cas plus dur que la musique, ou l'energie est plus etalee), puis
        // on compare le niveau de sortie a celui d'entree. Source Synthe : sans
        // recherche de resonances, qui ajouterait des encoches etroites.
        struct Cas { const char* nom; int role; std::vector<float> centres; };
        const Cas cas[] = {
            { "lead, bandes 200 Hz + 3,2 kHz",              0, { 200.0f, 3200.0f } },
            { "rythmique, bandes 100 + 400 + 1600 Hz",      1, { 100.0f, 400.0f, 1600.0f } },
            { "voix, bandes 400 Hz + 1,6 kHz + 6,4 kHz",    7, { 400.0f, 1600.0f, 6400.0f } },
        };

        for (const auto& c : cas)
        {
            RogusProcessor p;
            preparer (p);
            choisir (p, IdParam::role, (float) c.role);
            choisir (p, IdParam::source, 5.0f);        // Synthe
            choisir (p, IdParam::quantite, 100.0f);
            choisir (p, IdParam::compressionActive, 0.0f);     // on isole la correction fixe

            p.demarrerEcoute();
            jouerBandes (p, 11.0, c.centres);
            p.mettreAJour();
            const float variation = jouerBandes (p, 4.0, c.centres);
            const auto dosee = rogus::doser (p.lireCorrection(), 1.0f);

            verifier (p.lireMesure().valide && std::abs (variation) < 0.5f,
                      juce::String ("volume conserve a 0,5 dB pres : ") + c.nom + " (variation " + juce::String (variation, 2)
                      + " dB, compensation " + juce::String (dosee.compensationDb, 2) + " dB)");
        }
    }

    std::cout << "Compression" << std::endl;
    {
        rogus::Compression comp;
        comp.valide = true;
        comp.seuilDb = -20.0f;
        comp.ratio = 4.0f;
        verifier (rogus::gainCompression (comp, -40.0f) == 0.0f && std::abs (rogus::gainCompression (comp, -8.0f) + 9.0f) < 0.01f,
                  "gain : rien sous le seuil, 3/4 du depassement retire au-dessus (ratio 4)");
        verifier (std::abs (rogus::gainCompression (comp, -17.0f) + 2.25f) < 0.01f && std::abs (rogus::gainCompression (comp, -23.0f)) < 0.01f
                  && rogus::gainCompression (comp, -20.0f) < 0.0f && rogus::gainCompression (comp, -20.0f) > -1.0f,
                  "gain : le genou raccorde en douceur les deux zones");
        verifier (rogus::gainCompression ({}, 0.0f) == 0.0f, "gain : un compresseur sans mesure ne fait rien");

        // Une piste qui passe autant de temps a chaque niveau entre -30 et -10 dBFS.
        rogus::Mesure m;
        m.valide = m.histoValide = true;
        const int debut = rogus::caseDuNiveau (-30.0f), fin = rogus::caseDuNiveau (-10.5f);
        for (int k = debut; k <= fin; ++k)
            m.histo[(size_t) k] = 1.0f / (float) (fin - debut + 1);

        const auto voix = rogus::calculerCompression (rogus::role (7).compression, m);
        verifier (voix.valide && std::abs (voix.seuilDb - (-12.0f - 8.0f)) < 1.6f && voix.ratio == 3.0f,
                  "seuil : pose 8 dB sous les passages forts de la voix (" + juce::String (voix.seuilDb, 1) + " dB)");

        const float moitie = rogus::rattrapageCompression (voix, m.histo, 0.5f), plein = rogus::rattrapageCompression (voix, m.histo, 1.0f);
        verifier (rogus::rattrapageCompression (voix, m.histo, 0.0f) == 0.0f && moitie > 0.3f && plein > moitie && plein < 6.0f,
                  "volume rendu : nul a 0 %, croissant avec la quantite (" + juce::String (moitie, 1) + " puis "
                  + juce::String (plein, 1) + " dB)");

        m.histoValide = false;
        verifier (! rogus::calculerCompression (rogus::role (7).compression, m).valide, "ecoute sans histogramme : pas de compression");

        // Dans le moteur : une piste qui alterne passages forts et faibles (12 dB d'ecart).
        struct Niveaux { double fortE = 0, fortS = 0, faibleE = 0, faibleS = 0; float ecartMax = 0; bool fini = true; };

        auto jouerAlterne = [] (RogusProcessor& p, double secondes)
        {
            SourceBruit bruit (3, { 400.0f, 1600.0f });
            juce::AudioBuffer<float> tampon (2, tailleBloc);
            juce::MidiBuffer midi;
            std::vector<float> entree ((size_t) tailleBloc);
            Niveaux n;
            juce::int64 position = 0;

            for (int bloc = 0; bloc < (int) (secondes * fs / tailleBloc); ++bloc)
            {
                for (int i = 0; i < tailleBloc; ++i)
                {
                    const double t = (double) (position + i) / fs;
                    const bool fort = std::fmod (t, 1.0) < 0.5;
                    const float v = bruit.suivant() * (fort ? 0.5f : 0.125f);
                    entree[(size_t) i] = v;
                    tampon.setSample (0, i, v);
                    tampon.setSample (1, i, v);
                }

                p.processBlock (tampon, midi);

                for (int i = 0; i < tailleBloc; ++i, ++position)
                {
                    const double t = (double) position / fs, dansDemi = std::fmod (t, 0.5);
                    const double e = entree[(size_t) i], s = tampon.getSample (0, i);
                    n.fini = n.fini && std::isfinite (s);
                    n.ecartMax = juce::jmax (n.ecartMax, (float) std::abs (s - e));

                    // On laisse au compresseur le temps de reagir : debut de chaque demi-seconde ecarte.
                    if (t < 1.0 || dansDemi < 0.2)
                        continue;

                    if (std::fmod (t, 1.0) < 0.5) { n.fortE += e * e;   n.fortS += s * s; }
                    else                          { n.faibleE += e * e; n.faibleS += s * s; }
                }
            }

            return n;
        };

        auto dB = [] (double a, double b) { return (float) (10.0 * std::log10 (a / b + 1.0e-30)); };

        RogusProcessor p;
        preparer (p);
        choisir (p, IdParam::role, 7.0f);                    // Voix
        choisir (p, IdParam::source, 5.0f);
        choisir (p, IdParam::correctionActive, 0.0f);        // on isole la compression
        choisir (p, IdParam::demasquageActif, 0.0f);
        choisir (p, IdParam::compressionQuantite, 100.0f);
        choisir (p, IdParam::compressionActive, 1.0f);       // à l'arrêt par défaut : on la coche
        p.demarrerEcoute();
        jouerAlterne (p, 11.0);
        p.mettreAJour();
        verifier (p.lireMesure().histoValide && p.lireCorrection().compression.valide, "ecoute : la dynamique de la piste est mesuree");

        const auto n = jouerAlterne (p, 6.0);
        const float ecartEntree = dB (n.fortE, n.faibleE), ecartSortie = dB (n.fortS, n.faibleS);
        const float energie = dB (n.fortS + n.faibleS, n.fortE + n.faibleE);
        verifier (n.fini && ecartSortie < ecartEntree - 2.0f && ecartSortie > 4.0f,
                  "compression a 100 % : l'ecart fort/faible passe de " + juce::String (ecartEntree, 1) + " a "
                  + juce::String (ecartSortie, 1) + " dB");
        verifier (std::abs (energie) < 1.5f, "compression a 100 % : volume global garde a 1,5 dB pres (variation "
                                              + juce::String (energie, 2) + " dB)");

        choisir (p, IdParam::compressionQuantite, 0.0f);
        jouerAlterne (p, 2.0);
        verifier (jouerAlterne (p, 2.0).ecartMax == 0.0f, "quantite 0 % : sortie identique a l'entree");

        choisir (p, IdParam::compressionQuantite, 100.0f);
        choisir (p, IdParam::compressionActive, 0.0f);
        jouerAlterne (p, 2.0);
        verifier (jouerAlterne (p, 2.0).ecartMax == 0.0f, "compression decochee : sortie identique a l'entree");

        // Sans écoute, le compresseur n'a pas d'histogramme : il ne doit rien
        // faire, même coché et à 100 %. Un projet neuf ou ouvert avec cette
        // version garde donc son son tant que la piste n'a pas été écoutée.
        {
            RogusProcessor neuf;
            preparer (neuf);
            choisir (neuf, IdParam::correctionActive, 0.0f);
            choisir (neuf, IdParam::demasquageActif, 0.0f);
            choisir (neuf, IdParam::compressionQuantite, 100.0f);
            choisir (neuf, IdParam::compressionActive, 1.0f);   // coché, mais sans écoute
            jouerAlterne (neuf, 2.0);
            verifier (jouerAlterne (neuf, 2.0).ecartMax == 0.0f,
                      "sans ecoute : la compression ne touche pas au son, meme a 100 %");
        }
    }

    std::cout << "Demasquage : briques de calcul" << std::endl;
    {
        const auto zoneVoix = rogus::poidsZone (rogus::role (7));       // voix : 1 a 5 kHz
        verifier (zoneVoix[4] == 1.0f && zoneVoix[5] == 1.0f && zoneVoix[2] == 0.0f && zoneVoix[3] > 0.2f && zoneVoix[3] < 0.5f,
                  "zone de la voix : pleine a 1,6 et 3,2 kHz, partielle a 800 Hz, nulle a 400 Hz");
        verifier (rogus::poidsZone (rogus::role (4))[0] == 1.0f && rogus::poidsZone (rogus::role (4))[1] == 0.0f,
                  "zone du kick : la bande de 100 Hz seulement");

        verifier (rogus::facteurPriorite (5, 4) == 0.25f && rogus::facteurPriorite (5, 1) == 1.0f
                  && rogus::facteurPriorite (4, 4) == 0.0f && rogus::facteurPriorite (3, 5) == 0.0f,
                  "priorite : seule une piste strictement plus prioritaire demande de la place");

        verifier (rogus::activite (-40.0f, -20.0f) == 0.0f && rogus::activite (-28.0f, -20.0f) == 1.0f
                  && std::abs (rogus::activite (-34.0f, -20.0f) - 0.5f) < 0.01f,
                  "activite : 0 a 20 dB sous le niveau habituel, 1 a moins de 8 dB");

        verifier (rogus::reductionBande (1.0f, 1.0f, 5, 1, 1.0f) == -6.0f && rogus::reductionBande (1.0f, 0.0f, 5, 1, 1.0f) == 0.0f
                  && rogus::reductionBande (0.0f, 1.0f, 5, 1, 1.0f) == 0.0f && rogus::reductionBande (1.0f, 1.0f, 5, 1, 0.0f) == 0.0f,
                  "reduction : -6 dB au maximum, nulle sans activite, hors zone ou a quantite 0");

        verifier (rogus::role (4).priorite > rogus::role (3).priorite && rogus::role (7).priorite > rogus::role (0).priorite
                  && rogus::role (8).priorite == 1,
                  "priorites par defaut : kick devant basse, voix devant lead, nappe au fond");

        rogus::Reductions creux {};
        creux[4] = -4.0f;
        const auto filtres = rogus::construireFiltresDynamiques (creux, fs);
        verifier (std::abs (rogus::reponseDb (filtres, 1600.0, fs) + 4.0) < 0.2 && std::abs (rogus::reponseDb (filtres, 200.0, fs)) < 0.3,
                  "filtres dynamiques : -4 dB a 1,6 kHz, presque rien a 200 Hz");
    }

    std::cout << "Orchestre" << std::endl;
    {
        auto& orchestre = rogus::Orchestre::instance();
        const int avant = orchestre.nbInstances();
        {
            RogusProcessor a, b;
            verifier (orchestre.nbInstances() == avant + 2 && a.nbPistesConnectees() == avant + 2,
                      "deux instances du plugin se voient dans le meme programme");
        }
        verifier (orchestre.nbInstances() == avant, "une instance supprimee quitte l'orchestre");

        auto* annonce = orchestre.inscrire();
        rogus::Activites ecrites {}, lues {};
        ecrites[2] = 0.7f;
        annonce->publier (1000, ecrites, 5000u);
        verifier (annonce->lire (1000, lues) && lues[2] == 0.7f && ! annonce->lire (999, lues) && ! annonce->lire (1000 + rogus::nbTicks, lues),
                  "annonce : on relit le tick publie, et seulement lui");
        verifier (annonce->lireDernier (lues, 5100u) && ! annonce->lireDernier (lues, 5400u),
                  "annonce : la derniere valeur n'est plus prise en compte apres un quart de seconde");
        orchestre.retirer (annonce);
    }

    std::cout << "Demasquage : deux pistes" << std::endl;
    {
        // Chaque piste ecoute d'abord son propre son, comme dans FL Studio.
        auto preparerPiste = [] (int role, int graine, const std::vector<float>& centres)
        {
            RogusProcessor p;
            preparer (p);
            choisir (p, IdParam::role, (float) role);
            choisir (p, IdParam::source, 5.0f);              // Synthe
            choisir (p, IdParam::correctionActive, 0.0f);    // on isole le demasquage
            choisir (p, IdParam::compressionActive, 0.0f);
            p.demarrerEcoute();

            SourceBruit bruit (graine, centres);
            juce::AudioBuffer<float> tampon (2, tailleBloc);
            juce::MidiBuffer midi;

            for (int bloc = 0; bloc < (int) (11.0 * fs / tailleBloc); ++bloc)
            {
                for (int n = 0; n < tailleBloc; ++n)
                {
                    const float v = bruit.suivant();
                    tampon.setSample (0, n, v);
                    tampon.setSample (1, n, v);
                }
                p.processBlock (tampon, midi);
            }

            p.mettreAJour();
            juce::MemoryBlock etat;
            p.getStateInformation (etat);
            return std::pair<juce::MemoryBlock, bool> (etat, p.lireMesure().refValide);
        };

        const auto [etatVoix, refVoix] = preparerPiste (7, 7, { 1600.0f, 3200.0f });
        const auto [etatLead, refLead] = preparerPiste (0, 11, { 1600.0f });
        verifier (refVoix && refLead, "ecoute : le repere de niveau du demasquage est enregistre");

        // Priorite du lead reglee sur 1 (choix n. 1) : ecart maximal avec la voix (5).
        const auto fort = jouerDuo (etatVoix, etatLead, true, 512, 1.0f);
        verifier (fort.pistesVues >= 2, "les deux pistes se voient (" + juce::String (fort.pistesVues) + ")");
        verifier (fort.pendantVoix < -3.0f && fort.sansVoix > -0.3f,
                  "lead priorite 1 : baisse quand la voix chante (" + juce::String (fort.pendantVoix, 1)
                  + " dB), revient quand elle se tait (" + juce::String (fort.sansVoix, 1) + " dB)");
        verifier (fort.ecartVoix == 0.0f, "la voix, plus prioritaire, n'est jamais modifiee");

        // Priorite automatique : lead = 4, voix = 5, un seul cran d'ecart.
        const auto leger = jouerDuo (etatVoix, etatLead, true, 512, 0.0f);
        verifier (leger.pendantVoix < -0.5f && leger.pendantVoix > fort.pendantVoix + 1.5f,
                  "lead priorite auto (4) : baisse plus legere (" + juce::String (leger.pendantVoix, 1) + " dB)");

        // A priorite egale, personne ne cede.
        const auto egal = jouerDuo (etatVoix, etatLead, true, 512, 5.0f);
        verifier (std::abs (egal.pendantVoix) < 0.05f, "lead priorite 5, comme la voix : aucune baisse ("
                                                        + juce::String (egal.pendantVoix, 2) + " dB)");

        // L'ordre dans lequel l'hote traite les pistes ne doit rien changer.
        const auto inverse = jouerDuo (etatVoix, etatLead, false, 512, 1.0f);
        verifier (inverse.sortieLead == fort.sortieLead, "ordre de traitement inverse : sortie identique a l'echantillon pres");

        // La taille des blocs non plus (l'export utilise souvent d'autres blocs que l'ecoute).
        const auto petitsBlocs = jouerDuo (etatVoix, etatLead, true, 256, 1.0f);
        verifier (petitsBlocs.sortieLead == fort.sortieLead, "blocs de 256 au lieu de 512 : sortie identique a l'echantillon pres");

        const auto blocsImpairs = jouerDuo (etatVoix, etatLead, true, 480, 1.0f);
        verifier (std::abs (blocsImpairs.pendantVoix - fort.pendantVoix) < 0.1f,
                  "blocs de 480 (non alignes sur les ticks) : meme baisse a 0,1 dB pres ("
                  + juce::String (blocsImpairs.pendantVoix, 2) + " contre " + juce::String (fort.pendantVoix, 2) + " dB)");

        // Sans position donnee par l'hote : le demasquage marche quand meme.
        const auto sansTete = jouerDuo (etatVoix, etatLead, true, 512, 1.0f, false);
        verifier (sansTete.pendantVoix < -3.0f && sansTete.sansVoix > -0.3f,
                  "sans position de l'hote : meme comportement (" + juce::String (sansTete.pendantVoix, 1) + " / "
                  + juce::String (sansTete.sansVoix, 1) + " dB)");

        // Une piste sans repere de niveau (ecoute d'avant l'etape 3) ne demande rien.
        auto xml = Outils::getXmlFromBinary (etatVoix.getData(), (int) etatVoix.getSize());
        auto arbre = juce::ValueTree::fromXml (*xml);
        auto mesureVoix = arbre.getChildWithName ("Mesure");
        for (int i = 0; i < rogus::nbBandes; ++i)
            mesureVoix.removeProperty ("ref" + juce::String (i), nullptr);
        juce::MemoryBlock etatVoixAncien;
        Outils::copyXmlToBinary (*arbre.createXml(), etatVoixAncien);

        const auto sansRepere = jouerDuo (etatVoixAncien, etatLead, true, 512, 1.0f);
        verifier (std::abs (sansRepere.pendantVoix) < 0.05f, "voix ecoutee avant l'etape 3 : le lead ne baisse pas ("
                                                              + juce::String (sansRepere.pendantVoix, 2) + " dB)");
    }

    std::cout << "Anciens projets" << std::endl;
    {
        // Un état réel (avec une mesure) sert de base aux faux anciens états.
        RogusProcessor base;
        preparer (base);
        base.demarrerEcoute();
        jouer (base, 11.0);
        base.mettreAJour();

        juce::MemoryBlock actuel;
        base.getStateInformation (actuel);
        verifier (base.lireMesure().valide, "base de test : une ecoute est bien enregistree");
        verifier (Outils::getXmlFromBinary (actuel.getData(), (int) actuel.getSize())->getIntAttribute ("version") == 3,
                  "etat ecrit : numero de version present");

        // Étape 2 : l'ancien rôle (6 choix) devient un rôle + une source.
        const int attenduRole[]   = { 0, 1, 1, 7, 3, 4 };
        const int attenduSource[] = { 0, 0, 1, 2, 3, 4 };
        bool toutBon = true;

        for (int ancien = 0; ancien < 6; ++ancien)
        {
            const auto vieux = etatEtape2 (actuel, ancien, 75.0f, false);
            RogusProcessor q;
            q.setStateInformation (vieux.getData(), (int) vieux.getSize());

            const bool bon = q.lireRole() == attenduRole[ancien] && q.lireSource() == attenduSource[ancien] && q.lireSon() == 0
                          && std::abs (q.parametres.getRawParameterValue (IdParam::quantite)->load() - 75.0f) < 0.01f
                          && q.parametres.getRawParameterValue (IdParam::correctionActive)->load() < 0.5f
                          && q.lireMesure().valide;

            if (! bon)
                std::cout << "    ancien role " << ancien << " : obtenu role " << q.lireRole() << ", source " << q.lireSource() << std::endl;

            toutBon = toutBon && bon;
        }

        verifier (toutBon, "projet de l'etape 2 : role converti en role + source, quantite, interrupteur et mesure gardes");

        // Étape 1 : seul l'interrupteur du coupe-bas a un équivalent.
        juce::ValueTree etape1 ("ROGUS");
        auto ajouter = [&] (const char* id, float valeur)
        {
            juce::ValueTree parametre ("PARAM");
            parametre.setProperty ("id", id, nullptr);
            parametre.setProperty ("value", valeur, nullptr);
            etape1.appendChild (parametre, nullptr);
        };
        ajouter ("coupeBasActif", 0.0f);
        ajouter ("coupeBasFreq", 64.0f);

        juce::MemoryBlock blocEtape1;
        Outils::copyXmlToBinary (*etape1.createXml(), blocEtape1);
        RogusProcessor r1;
        r1.setStateInformation (blocEtape1.getData(), (int) blocEtape1.getSize());
        verifier (r1.parametres.getRawParameterValue (IdParam::correctionActive)->load() < 0.5f && r1.lireRole() == 0,
                  "projet de l'etape 1 : interrupteur conserve, le reste a sa valeur par defaut");

        // Un état déjà converti ne doit pas être converti une seconde fois.
        RogusProcessor deux;
        deux.setStateInformation (actuel.getData(), (int) actuel.getSize());
        juce::MemoryBlock reecrit;
        deux.getStateInformation (reecrit);
        RogusProcessor trois;
        trois.setStateInformation (reecrit.getData(), (int) reecrit.getSize());
        verifier (trois.lireRole() == base.lireRole() && trois.lireSource() == base.lireSource() && trois.lireSon() == base.lireSon(),
                  "etat recent relu deux fois : role, source et son inchanges");

        // Projet enregistré juste avant l'étape 3 : même version d'état, mais sans
        // les quatre paramètres du démasquage ni le repère de niveau de l'écoute.
        {
            auto xml = Outils::getXmlFromBinary (actuel.getData(), (int) actuel.getSize());
            auto etat = juce::ValueTree::fromXml (*xml);

            for (int i = etat.getNumChildren() - 1; i >= 0; --i)
            {
                const auto id = etat.getChild (i).getProperty ("id").toString();

                if (id == IdParam::priorite || id == IdParam::demasquageActif
                    || id == IdParam::demasquageQuantite || id == IdParam::reactivite
                    || id == IdParam::compressionActive || id == IdParam::compressionQuantite)
                    etat.removeChild (i, nullptr);
            }

            auto noeudMesure = etat.getChildWithName ("Mesure");
            for (int i = 0; i < rogus::nbBandes; ++i)
                noeudMesure.removeProperty ("ref" + juce::String (i), nullptr);
            for (int i = 0; i < rogus::nbHisto; ++i)
                noeudMesure.removeProperty ("histo" + juce::String (i), nullptr);

            fixerParametre (etat, IdParam::role, 7.0f);          // Voix
            fixerParametre (etat, IdParam::quantite, 35.0f);

            juce::MemoryBlock avantEtape3;
            Outils::copyXmlToBinary (*etat.createXml(), avantEtape3);
            RogusProcessor q;
            q.setStateInformation (avantEtape3.getData(), (int) avantEtape3.getSize());

            verifier (q.lireRole() == 7 && q.lireMesure().valide && ! q.lireMesure().refValide
                      && std::abs (q.parametres.getRawParameterValue (IdParam::quantite)->load() - 35.0f) < 0.01f,
                      "projet d'avant l'etape 3 : role, quantite et ecoute gardes (repere de niveau absent)");
            verifier (q.lirePriorite() == 5 && q.lireDemasquageActif()
                      && std::abs (q.parametres.getRawParameterValue (IdParam::demasquageQuantite)->load() - 50.0f) < 0.01f,
                      "projet d'avant l'etape 3 : nouveaux reglages a leur valeur par defaut (priorite du role)");
            verifier (! q.lireMesure().histoValide && ! q.lireCorrection().compression.valide
                      && q.parametres.getRawParameterValue (IdParam::compressionActive)->load() < 0.5f
                      && std::abs (q.parametres.getRawParameterValue (IdParam::compressionQuantite)->load() - 50.0f) < 0.01f,
                      "projet d'avant l'etape 4 : compression a l'arret par defaut, inactive sans reecoute");
        }

        // L'histogramme des niveaux survit lui aussi à un enregistrement.
        {
            RogusProcessor reluHisto;
            reluHisto.setStateInformation (actuel.getData(), (int) actuel.getSize());
            verifier (base.lireMesure().histoValide && reluHisto.lireMesure().histo == base.lireMesure().histo
                      && reluHisto.lireCorrection().compression.valide,
                      "sauvegarde : histogramme des niveaux et compresseur retrouves");
        }

        // Le repère de niveau survit à un enregistrement.
        RogusProcessor relu;
        relu.setStateInformation (actuel.getData(), (int) actuel.getSize());
        verifier (base.lireMesure().refValide && relu.lireMesure().refValide && relu.lireMesure().refDb == base.lireMesure().refDb,
                  "sauvegarde : repere de niveau du demasquage retrouve");
    }

    std::cout << std::endl << (nbEchecs == 0 ? "Tous les tests passent." : "Tests en echec : " + juce::String (nbEchecs)) << std::endl;
    return nbEchecs == 0 ? 0 : 1;
}
