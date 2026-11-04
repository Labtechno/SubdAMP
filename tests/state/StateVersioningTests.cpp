#include "plugin/StateVersioning.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

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

TEST_CASE ("Un état sans version est traité comme la version 1", "[state]")
{
    auto state = makeState ({ { "inputGain", 3.0f } });

    REQUIRE (migrateToCurrentVersion (state) == MigrationResult::upToDate);
    REQUIRE (valueOf (state, "inputGain") == 3.0f);
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
