#include "plugin/ChainOrder.h"
#include "plugin/StateVersioning.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>

using namespace subdamp;
using namespace subdamp::state;

namespace
{
    juce::ValueTree makeState (std::initializer_list<std::pair<const char*, float>> parameters)
    {
        juce::ValueTree state ("SubdAmpState");

        for (const auto& [id, value] : parameters)
        {
            juce::ValueTree child ("PARAM");
            child.setProperty ("id", id, nullptr);
            child.setProperty ("value", value, nullptr);
            state.appendChild (child, nullptr);
        }

        return state;
    }

    float valueOf (const juce::ValueTree& state, const char* id)
    {
        return state.getChildWithProperty ("id", id).getProperty ("value");
    }
}

TEST_CASE ("Un état sans version est traité comme la version 1, puis migré", "[state]")
{
    auto state = makeState ({ { "inputGain", 3.0f } });

    REQUIRE (migrateToCurrentVersion (state) == MigrationResult::migrated);
    REQUIRE (static_cast<int> (state.getProperty (versionProperty)) == currentVersion);
    REQUIRE (valueOf (state, "inputGain") == 3.0f);

    // Positions par défaut de la version 1 : SUB et WHAMMY avant l'ampli, le reste après,
    // puis les textures (autrefois en parallèle).
    REQUIRE (state.getProperty (chainOrderProperty).toString()
             == "gate sub whammy amp cab harmonizer freqShift shimmer poly freeze granular");
}

TEST_CASE ("Migration 1 → 2 : ordre reconstruit depuis les positions, MIX au même équilibre", "[state]")
{
    auto state = makeState ({
        { "subPosition", 1.0f }, { "whPosition", 1.0f }, { "hmPosition", 0.0f }, { "fsPosition", 0.0f },
        { "subDry", 0.0f }, { "hmDirect", 50.0f },
        { "shLevel", 100.0f }, { "grLevel", 0.0f }, { "frLevel", 80.0f },
        { "shSource", 0.0f }, { "poSource", 1.0f }, { "frSource", 1.0f }, { "grSource", 0.0f },
        { "whMix", 70.0f }
    });
    state.setProperty (versionProperty, 1, nullptr);

    REQUIRE (migrateToCurrentVersion (state) == MigrationResult::migrated);

    REQUIRE (state.getProperty (chainOrderProperty).toString()
             == "gate harmonizer freqShift amp cab sub whammy shimmer poly freeze granular");

    REQUIRE (valueOf (state, "subMix") == 100.0f);                 // pas de direct : octaves seules
    REQUIRE (std::abs (valueOf (state, "hmMix") - 200.0f / 3.0f) < 0.01f); // voix 2 fois plus fortes que le direct
    REQUIRE (valueOf (state, "shMix") == 50.0f);                   // texture au niveau du direct
    REQUIRE (valueOf (state, "grMix") == 0.0f);
    REQUIRE (valueOf (state, "frMix") == 80.0f);                   // le FREEZE s'ajoute au jeu
    REQUIRE (valueOf (state, "poMix") == 50.0f);
    REQUIRE (valueOf (state, "whMix") == 70.0f);                   // même sens qu'avant : conservé

    for (const auto* retired : { "subPosition", "whPosition", "hmPosition", "fsPosition", "subDry", "hmDirect",
                                 "shLevel", "grLevel", "frLevel", "shSource", "poSource", "frSource", "grSource" })
        REQUIRE_FALSE (state.getChildWithProperty ("id", retired).isValid());
}

//==============================================================================
TEST_CASE ("Ordre de chaîne : lecture tolérante (inconnus, doublons, modules manquants)", "[chain]")
{
    const auto order = parseChainOrder ("amp, gate  amp reverb-du-futur shimmer;cab");

    REQUIRE (order[0] == ModuleId::amp);
    REQUIRE (order[1] == ModuleId::gate);
    REQUIRE (order[2] == ModuleId::shimmer);
    REQUIRE (order[3] == ModuleId::cab);

    // Les manquants suivent, dans l'ordre par défaut ; chaque module apparaît une seule fois.
    REQUIRE (order[4] == ModuleId::sub);

    std::array<int, numModules> seen {};

    for (const auto module : order)
        ++seen[indexOf (module)];

    for (const auto count : seen)
        REQUIRE (count == 1);

    REQUIRE (parseChainOrder ("") == defaultChainOrder);
    REQUIRE (parseChainOrder (formatChainOrder (order)) == order);
}

TEST_CASE ("Ordre de chaîne : déplacer un module, empaqueter pour le thread audio", "[chain]")
{
    // Glisser le SHIMMER (dernier) juste après le GATE.
    const auto moved = withModuleMoved (defaultChainOrder, ModuleId::shimmer, 1);
    REQUIRE (moved[0] == ModuleId::gate);
    REQUIRE (moved[1] == ModuleId::shimmer);
    REQUIRE (moved[2] == ModuleId::sub);
    REQUIRE (moved.back() == ModuleId::freeze);

    // Et le remettre à la fin.
    REQUIRE (withModuleMoved (moved, ModuleId::shimmer, numModules - 1) == defaultChainOrder);

    REQUIRE (unpackChainOrder (packChainOrder (moved)) == moved);
}

TEST_CASE ("Un état de la version courante est chargé tel quel", "[state]")
{
    auto state = makeState ({ { "outputGain", -12.0f } });
    state.setProperty (versionProperty, currentVersion, nullptr);

    REQUIRE (migrateToCurrentVersion (state) == MigrationResult::upToDate);
    REQUIRE (valueOf (state, "outputGain") == -12.0f);
}

TEST_CASE ("Un état venant d'une version plus récente est refusé", "[state]")
{
    auto state = makeState ({});
    state.setProperty (versionProperty, currentVersion + 1, nullptr);

    REQUIRE (migrateToCurrentVersion (state) == MigrationResult::fromNewerVersion);
}

TEST_CASE ("Les paramètres absents d'un ancien preset reprennent leur valeur par défaut", "[state]")
{
    auto state = makeState ({ { "inputGain", 6.0f } });

    const std::array<ParameterDefault, 3> defaults {{
        { "inputGain", 0.0f },
        { "outputGain", -3.0f },
        { "bypass", 0.0f }
    }};

    addMissingParameters (state, defaults);

    REQUIRE (state.getNumChildren() == 3);
    REQUIRE (valueOf (state, "inputGain") == 6.0f); // la valeur sauvegardée est conservée
    REQUIRE (valueOf (state, "outputGain") == -3.0f);
    REQUIRE (valueOf (state, "bypass") == 0.0f);
}

TEST_CASE ("addMissingParameters ne duplique pas les paramètres présents", "[state]")
{
    auto state = makeState ({ { "inputGain", 1.0f }, { "outputGain", 2.0f } });
    const std::array<ParameterDefault, 2> defaults {{ { "inputGain", 0.0f }, { "outputGain", 0.0f } }};

    addMissingParameters (state, defaults);
    addMissingParameters (state, defaults);

    REQUIRE (state.getNumChildren() == 2);
}
