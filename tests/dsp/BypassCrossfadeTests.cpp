#include "subdamp/dsp/BypassCrossfade.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace subdamp::dsp;

namespace
{
    BypassCrossfade makePrepared (bool bypassed)
    {
        BypassCrossfade fade;
        fade.prepare ({ 1000.0, 64 }, 0.01); // fondu de 10 échantillons
        fade.setBypassed (bypassed);
        fade.reset();
        return fade;
    }
}

TEST_CASE ("Bypass actif : le signal traité passe tel quel", "[bypass]")
{
    auto fade = makePrepared (false);
    const std::vector<float> dry (16, 1.0f);
    std::vector<float> processed (16, 0.25f);

    fade.process (dry, processed);

    REQUIRE_FALSE (fade.isFullyBypassed());

    for (const auto s : processed)
        REQUIRE (s == 0.25f);
}

TEST_CASE ("Bypass engagé : le signal sec remplace le signal traité", "[bypass]")
{
    auto fade = makePrepared (true);
    const std::vector<float> dry (16, 1.0f);
    std::vector<float> processed (16, 0.25f);

    fade.process (dry, processed);

    REQUIRE (fade.isFullyBypassed());

    for (const auto s : processed)
        REQUIRE (s == 1.0f);
}

TEST_CASE ("Engager le bypass produit un fondu progressif puis le signal sec", "[bypass]")
{
    auto fade = makePrepared (false);
    fade.setBypassed (true);

    const std::vector<float> dry (20, 1.0f);
    std::vector<float> processed (20, 0.0f);

    REQUIRE_FALSE (fade.isFullyBypassed());
    fade.process (dry, processed);

    for (std::size_t i = 1; i < 10; ++i)
        REQUIRE (processed[i] > processed[i - 1]);

    REQUIRE (processed[0] == Catch::Approx (0.1f));
    REQUIRE (processed[9] == 1.0f);
    REQUIRE (processed[19] == 1.0f);
    REQUIRE (fade.isFullyBypassed());
}
