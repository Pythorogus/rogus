#include "Orchestre.h"

namespace rogus
{

void Annonce::reinitialiser() noexcept
{
    participe.store (false);
    synchroHote.store (false);
    role.store (0);
    priorite.store (3);
    dernierTick.store (-1);

    for (auto& n : numeroTick)
        n.store (-1);

    for (auto& tick : activites)
        for (auto& a : tick)
            a.store (0.0f);
}

void Annonce::publier (std::int64_t tick, const Activites& valeurs, std::uint32_t maintenantMs) noexcept
{
    if (tick < 0)
        return;

    const auto index = (size_t) (tick & (nbTicks - 1));

    // On marque d'abord la case comme « en cours d'écriture » : un lecteur qui
    // passerait à ce moment ne prendra pas un mélange d'ancien et de nouveau.
    numeroTick[index].store (-1);

    for (size_t b = 0; b < (size_t) nbBandes; ++b)
        activites[index][b].store (valeurs[b], std::memory_order_relaxed);

    numeroTick[index].store (tick);
    heureDernierTick.store (maintenantMs);
    dernierTick.store (tick);
}

bool Annonce::lire (std::int64_t tick, Activites& valeurs) const noexcept
{
    if (tick < 0)
        return false;

    const auto index = (size_t) (tick & (nbTicks - 1));

    if (numeroTick[index].load() != tick)
        return false;

    for (size_t b = 0; b < (size_t) nbBandes; ++b)
        valeurs[b] = activites[index][b].load (std::memory_order_relaxed);

    // Si l'auteur a réécrit la case pendant notre lecture, on n'en tient pas compte.
    return numeroTick[index].load() == tick;
}

bool Annonce::lireDernier (Activites& valeurs, std::uint32_t maintenantMs) const noexcept
{
    // Soustraction sur des entiers non signés : elle reste juste même quand
    // le compteur de millisecondes repasse par zéro.
    if (maintenantMs - heureDernierTick.load() > 250u)
        return false;

    return lire (dernierTick.load(), valeurs);
}

Orchestre& Orchestre::instance() noexcept
{
    static Orchestre unique;
    return unique;
}

Annonce* Orchestre::inscrire() noexcept
{
    for (auto& p : places)
    {
        bool libre = false;

        if (p.utilisee.compare_exchange_strong (libre, true))
        {
            p.reinitialiser();
            return &p;
        }
    }

    return nullptr;
}

void Orchestre::retirer (Annonce* annonce) noexcept
{
    if (annonce == nullptr)
        return;

    annonce->participe.store (false);
    annonce->utilisee.store (false);
}

int Orchestre::nbInstances() const noexcept
{
    int total = 0;

    for (const auto& p : places)
        if (p.utilisee.load())
            ++total;

    return total;
}

} // namespace rogus
