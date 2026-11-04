#include "subdamp/dsp/Decibels.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace subdamp::dsp;

TEST_CASE ("Conversion dB vers gain", "[decibels]")
{
    REQUIRE (decibelsToGain (0.0f) == 1.0f);
    REQUIRE (decibelsToGain (-6.0206f) == Catch::Approx (0.5f).margin (1.0e-4));
    REQUIRE (decibelsToGain (20.0f) == Catch::Approx (10.0f));
    REQUIRE (decibelsToGain (minusInfinityDb) == 0.0f);
    REQUIRE (decibelsToGain (-200.0f) == 0.0f);
}

TEST_CASE ("Conversion gain vers dB", "[decibels]")
{
    REQUIRE (gainToDecibels (1.0f) == 0.0f);
    REQUIRE (gainToDecibels (0.5f) == Catch::Approx (-6.0206f).margin (1.0e-3));
    REQUIRE (gainToDecibels (0.0f) == minusInfinityDb);
    REQUIRE (gainToDecibels (-1.0f) == minusInfinityDb);
    REQUIRE (gainToDecibels (1.0e-12f) == minusInfinityDb);
}
