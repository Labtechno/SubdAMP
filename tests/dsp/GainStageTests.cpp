#include "subdamp/dsp/GainStage.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

using namespace subdamp::dsp;

namespace
{
    GainStage makePreparedStage (float decibels)
    {
        GainStage stage;
        stage.prepare ({ 48000.0, 512 });
        stage.setGainDecibels (decibels);
        stage.reset();
        return stage;
    }
}

TEST_CASE ("GainStage à 0 dB laisse le signal intact", "[gain]")
{
    auto stage = makePreparedStage (0.0f);
    std::vector<float> block { 0.1f, -0.5f, 0.9f, -1.0f };
    const auto original = block;

    stage.process (block);

    REQUIRE (block == original);
}

TEST_CASE ("GainStage applique un gain constant hors rampe", "[gain]")
{
    auto stage = makePreparedStage (-6.0206f);
    std::vector<float> block (64, 1.0f);

    stage.process (block);

    for (const auto s : block)
        REQUIRE (s == Catch::Approx (0.5f).margin (1.0e-4));
}

TEST_CASE ("GainStage passe progressivement d'un gain à l'autre, sans saut", "[gain]")
{
    auto stage = makePreparedStage (0.0f);
    stage.setGainDecibels (-60.0f);

    // 20 ms à 48 kHz = 960 échantillons de rampe
    std::vector<float> block (1200, 1.0f);
    stage.process (block);

    float maxJump = 0.0f;

    for (std::size_t i = 1; i < block.size(); ++i)
        maxJump = std::max (maxJump, block[i - 1] - block[i]);

    REQUIRE (block.front() < 1.0f);
    REQUIRE (block.front() > 0.99f);
    REQUIRE (maxJump < 0.002f);
    REQUIRE (block.back() == Catch::Approx (0.001f).margin (1.0e-6));
}
