#include "subdamp/dsp/LinearSmoother.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using subdamp::dsp::LinearSmoother;

TEST_CASE ("LinearSmoother atteint exactement la cible à la fin de la rampe", "[smoother]")
{
    LinearSmoother s;
    s.prepare (1000.0, 0.01); // rampe de 10 échantillons
    s.setCurrentAndTarget (0.0f);
    s.setTarget (1.0f);

    for (int i = 0; i < 9; ++i)
    {
        const auto v = s.getNext();
        REQUIRE (v > 0.0f);
        REQUIRE (v < 1.0f);
    }

    REQUIRE (s.getNext() == 1.0f);
    REQUIRE_FALSE (s.isSmoothing());
    REQUIRE (s.getNext() == 1.0f);
}

TEST_CASE ("LinearSmoother est monotone et ne dépasse jamais la cible", "[smoother]")
{
    LinearSmoother s;
    s.prepare (48000.0, 0.02);
    s.setCurrentAndTarget (0.25f);
    s.setTarget (0.0f);

    float previous = s.getCurrent();

    while (s.isSmoothing())
    {
        const auto v = s.getNext();
        REQUIRE (v <= previous);
        REQUIRE (v >= 0.0f);
        previous = v;
    }

    REQUIRE (s.getCurrent() == 0.0f);
}

TEST_CASE ("LinearSmoother repart de la valeur courante si la cible change en cours de rampe", "[smoother]")
{
    LinearSmoother s;
    s.prepare (1000.0, 0.01);
    s.setCurrentAndTarget (0.0f);
    s.setTarget (1.0f);

    for (int i = 0; i < 5; ++i)
        s.getNext();

    const auto midRamp = s.getCurrent();
    REQUIRE (midRamp == Catch::Approx (0.5f));

    s.setTarget (0.0f);
    REQUIRE (s.getNext() < midRamp); // pas de saut vers l'ancienne cible

    while (s.isSmoothing())
        s.getNext();

    REQUIRE (s.getCurrent() == 0.0f);
}

TEST_CASE ("LinearSmoother::skip est équivalent à des appels successifs à getNext", "[smoother]")
{
    LinearSmoother a, b;

    for (auto* s : { &a, &b })
    {
        s->prepare (1000.0, 0.1);
        s->setCurrentAndTarget (1.0f);
        s->setTarget (2.0f);
    }

    a.skip (37);

    for (int i = 0; i < 37; ++i)
        b.getNext();

    REQUIRE (a.getCurrent() == Catch::Approx (b.getCurrent()).margin (1.0e-5));

    a.skip (1000);
    REQUIRE (a.getCurrent() == 2.0f);
    REQUIRE_FALSE (a.isSmoothing());
}

TEST_CASE ("LinearSmoother::prepare annule la rampe en cours et fixe la cible", "[smoother]")
{
    LinearSmoother s;
    s.prepare (1000.0, 0.01);
    s.setCurrentAndTarget (0.0f);
    s.setTarget (1.0f);
    s.getNext();

    s.prepare (48000.0, 0.02);

    REQUIRE_FALSE (s.isSmoothing());
    REQUIRE (s.getCurrent() == 1.0f);
}
