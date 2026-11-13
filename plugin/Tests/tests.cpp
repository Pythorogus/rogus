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
    }

    std::cout << std::endl << (nbEchecs == 0 ? "Tous les tests passent." : "Tests en echec : " + juce::String (nbEchecs)) << std::endl;
    return nbEchecs == 0 ? 0 : 1;
}
