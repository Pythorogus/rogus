#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Ce que l'hôte dit de la piste qui porte le plugin : son nom et sa couleur.
// Tous les hôtes ne le disent pas (et un hôte peut ne donner que l'un des deux) :
// sans information, le nom reste vide et `aCouleur` reste faux, et la fenêtre
// garde ses valeurs par défaut (pas d'étiquette, accent ambre).
// Partagé par le plugin de piste et Rogus Master.
namespace rogus
{
    struct InfosPiste
    {
        juce::String nom;
        juce::Colour couleur;
        bool aCouleur = false;

        bool operator== (const InfosPiste& autre) const noexcept
        {
            return nom == autre.nom && couleur == autre.couleur && aCouleur == autre.aCouleur;
        }
    };

    // L'hôte peut prévenir depuis un autre fil que celui de la fenêtre : un
    // verrou ordinaire protège la copie. Le fil audio n'y touche jamais.
    class Piste
    {
    public:
        void recevoir (const juce::AudioProcessor::TrackProperties& proprietes)
        {
            const juce::ScopedLock verrouille (verrou);

            if (proprietes.name.has_value())
                infos.nom = proprietes.name->trim();

            if (proprietes.colour.has_value())
            {
                infos.couleur = *proprietes.colour;
                infos.aCouleur = true;
            }
        }

        InfosPiste lire() const
        {
            const juce::ScopedLock verrouille (verrou);
            return infos;
        }

    private:
        juce::CriticalSection verrou;
        InfosPiste infos;
    };
}
