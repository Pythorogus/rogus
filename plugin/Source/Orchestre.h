#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include "Correction.h"

// L'orchestre : le lieu où les instances du plugin se voient.
//
// Chaque instance (une par piste) y publie, en continu, dans quelles bandes
// de fréquences sa piste est en train de jouer. Les autres instances lisent
// ces annonces pour décider si elles doivent céder un peu de place.
//
// Toutes les instances chargées par le même hôte vivent dans le même programme
// et partagent donc cette mémoire, sans aucun câblage dans l'hôte. (Si un hôte
// isolait chaque plugin dans un programme séparé, les instances ne se verraient
// plus : le plugin affiche le nombre de pistes connectées pour qu'on le sache.)
//
// Tout se fait avec des « atomiques » : des cases mémoire que plusieurs fils
// peuvent lire et écrire en même temps sans verrou. Le fil audio ne doit
// jamais attendre.
namespace rogus
{

// Le temps est découpé en « ticks » de 1024 échantillons (environ 23 ms à
// 44,1 kHz), numérotés d'après la position de lecture de l'hôte. Deux
// instances qui traitent le même moment du morceau parlent donc du même tick :
// c'est ce qui rend le résultat identique à l'écoute et à l'export.
constexpr int tailleTick = 1024;          // doit rester une puissance de 2
constexpr int nbTicks = 256;              // mémoire : 256 ticks, environ 6 secondes
constexpr int nbInstancesMax = 64;

using Activites = std::array<float, nbBandes>;      // 0 (silence) à 1 (joue franchement)

// Ce qu'une instance publie pour les autres.
struct Annonce
{
    Annonce() noexcept { reinitialiser(); }
    void reinitialiser() noexcept;

    // Écrit l'activité de la piste pendant un tick (appelé par le fil audio de
    // l'instance). `maintenantMs` est l'heure de l'horloge, en millisecondes.
    void publier (std::int64_t tick, const Activites&, std::uint32_t maintenantMs) noexcept;

    // Lit l'activité à un tick précis. Renvoie false s'il n'est pas (ou plus) disponible.
    bool lire (std::int64_t tick, Activites&) const noexcept;

    // Lit le tick le plus récent (quand l'hôte ne donne pas la position de lecture).
    // Refusé s'il date de plus d'un quart de seconde : une piste arrêtée ne doit
    // pas continuer à faire baisser les autres avec sa dernière annonce.
    bool lireDernier (Activites&, std::uint32_t maintenantMs) const noexcept;

    std::atomic<bool> utilisee { false };        // cette place est prise par une instance
    std::atomic<bool> participe { false };       // démasquage activé sur cette piste
    std::atomic<bool> synchroHote { false };     // ticks numérotés d'après la position de l'hôte
    std::atomic<int> role { 0 };
    std::atomic<int> priorite { 3 };

private:
    std::atomic<std::int64_t> dernierTick { -1 };
    std::atomic<std::uint32_t> heureDernierTick { 0 };
    std::array<std::atomic<std::int64_t>, nbTicks> numeroTick;
    std::array<std::array<std::atomic<float>, nbBandes>, nbTicks> activites;
};

class Orchestre
{
public:
    // L'unique orchestre du programme, créé au premier appel.
    static Orchestre& instance() noexcept;

    // Réserve une place pour une instance. nullptr si les 64 places sont prises.
    Annonce* inscrire() noexcept;
    void retirer (Annonce*) noexcept;

    int nbInstances() const noexcept;

    Annonce& place (int index) noexcept             { return places[(size_t) index]; }
    const Annonce& place (int index) const noexcept { return places[(size_t) index]; }

private:
    Orchestre() = default;
    std::array<Annonce, nbInstancesMax> places;
};

} // namespace rogus
