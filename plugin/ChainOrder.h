#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

/**
    Ordre des modules dans la chaîne en série. C++ pur (sans JUCE) : partagé par le processeur,
    l'interface et la migration de l'état sauvegardé.
*/
namespace subdamp
{

enum class ModuleId : std::uint8_t
{
    gate,
    sub,
    whammy,
    harmonizer,
    amp,
    cab,
    freqShift,
    shimmer,
    poly,
    freeze,
    granular
};

inline constexpr std::size_t numModules = 11;

/**
    Nom de chaque module dans l'état sauvegardé (propriété « chainOrder »). Stockés en
    toutes lettres plutôt qu'en numéros : un état reste lisible et ne dépend pas de l'ordre de
    l'enum. Ne JAMAIS les renommer.
*/
inline constexpr std::array<std::string_view, numModules> moduleKeys {
    "gate", "sub", "whammy", "harmonizer", "amp", "cab", "freqShift", "shimmer", "poly", "freeze", "granular"
};

using ChainOrder = std::array<ModuleId, numModules>;

/** Ordre d'un nouveau réglage : pitch avant l'ampli, harmonies et textures après, réverbe en dernier. */
inline constexpr ChainOrder defaultChainOrder {
    ModuleId::gate, ModuleId::sub, ModuleId::whammy, ModuleId::amp, ModuleId::cab, ModuleId::harmonizer,
    ModuleId::freqShift, ModuleId::poly, ModuleId::granular, ModuleId::freeze, ModuleId::shimmer
};

inline constexpr std::size_t indexOf (ModuleId module) noexcept { return static_cast<std::size_t> (module); }

/**
    Lit un ordre sauvegardé (« gate sub amp… »), en tolérant tout : noms inconnus et doublons
    ignorés, modules absents (état incomplet, module ajouté dans une version future) placés à
    la fin dans l'ordre par défaut. Le résultat contient toujours chaque module une fois.
*/
inline ChainOrder parseChainOrder (std::string_view text)
{
    ChainOrder order {};
    std::array<bool, numModules> placed {};
    std::size_t count = 0;

    const auto place = [&] (ModuleId module)
    {
        if (! placed[indexOf (module)])
        {
            placed[indexOf (module)] = true;
            order[count++] = module;
        }
    };

    while (! text.empty())
    {
        const auto start = text.find_first_not_of (" ,;");

        if (start == std::string_view::npos)
            break;

        text.remove_prefix (start);
        const auto length = std::min (text.find_first_of (" ,;"), text.size());
        const auto key = text.substr (0, length);
        text.remove_prefix (length);

        for (std::size_t i = 0; i < numModules; ++i)
            if (moduleKeys[i] == key)
                place (static_cast<ModuleId> (i));
    }

    for (const auto module : defaultChainOrder)
        place (module);

    return order;
}

inline std::string formatChainOrder (const ChainOrder& order)
{
    std::string text;

    for (const auto module : order)
    {
        if (! text.empty())
            text += ' ';

        text += moduleKeys[indexOf (module)];
    }

    return text;
}

/** 4 bits par module : l'ordre entier tient dans un atomic de 64 bits, lisible sans verrou. */
inline constexpr std::uint64_t packChainOrder (const ChainOrder& order) noexcept
{
    std::uint64_t packed = 0;

    for (std::size_t i = 0; i < numModules; ++i)
        packed |= static_cast<std::uint64_t> (order[i]) << (4 * i);

    return packed;
}

inline constexpr ChainOrder unpackChainOrder (std::uint64_t packed) noexcept
{
    ChainOrder order {};

    for (std::size_t i = 0; i < numModules; ++i)
        order[i] = static_cast<ModuleId> ((packed >> (4 * i)) & 0xf);

    return order;
}

/** Déplace un module à une nouvelle place (0 = début de chaîne), les autres gardant leur ordre. */
inline ChainOrder withModuleMoved (ChainOrder order, ModuleId module, std::size_t newIndex) noexcept
{
    const auto from = static_cast<std::size_t> (std::find (order.begin(), order.end(), module) - order.begin());
    const auto to = std::min (newIndex, numModules - 1);

    if (from >= numModules || from == to)
        return order;

    if (from < to)
        std::rotate (order.begin() + static_cast<std::ptrdiff_t> (from), order.begin() + static_cast<std::ptrdiff_t> (from + 1),
                     order.begin() + static_cast<std::ptrdiff_t> (to + 1));
    else
        std::rotate (order.begin() + static_cast<std::ptrdiff_t> (to), order.begin() + static_cast<std::ptrdiff_t> (from),
                     order.begin() + static_cast<std::ptrdiff_t> (from + 1));

    return order;
}

} // namespace subdamp
