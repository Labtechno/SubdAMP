#include "TestSignals.h"

#include "subdamp/dsp/Biquad.h"
#include "subdamp/dsp/Decibels.h"
#include "subdamp/dsp/ToneStack.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace subdamp::dsp;
using namespace subdamp::test;

namespace
{
    constexpr double sr = 48000.0;

    double gainOf (const BiquadCoefficients& coefficients, double frequency)
    {
        Biquad filter;
        filter.setCoefficients (coefficients);

        auto signal = sine (frequency, sr, 48000, 1.0f);

        for (auto& s : signal)
            s = filter.processSample (s);

        // On ignore le régime transitoire du début.
        const std::span<const float> steady { signal.data() + 24000, 24000 };
        return magnitudeAt (steady, frequency, sr);
    }
}

TEST_CASE ("Passe-bas : gain unité dans la bande, -3 dB à la coupure, forte atténuation au-delà", "[biquad]")
{
    const auto lp = BiquadCoefficients::lowPass (sr, 1000.0, butterworthQ);

    REQUIRE (gainOf (lp, 50.0) == Catch::Approx (1.0).margin (0.01));
    REQUIRE (gainToDecibels (static_cast<float> (gainOf (lp, 1000.0))) == Catch::Approx (-3.0).margin (0.2));
    REQUIRE (gainToDecibels (static_cast<float> (gainOf (lp, 10000.0))) < -35.0f);
}

TEST_CASE ("Passe-haut : coupe le grave, laisse passer l'aigu", "[biquad]")
{
    const auto hp = BiquadCoefficients::highPass (sr, 100.0, butterworthQ);

    REQUIRE (gainOf (hp, 5000.0) == Catch::Approx (1.0).margin (0.01));
    REQUIRE (gainToDecibels (static_cast<float> (gainOf (hp, 20.0))) < -25.0f);
}

TEST_CASE ("Cloche et plateaux : le gain demandé est atteint", "[biquad]")
{
    REQUIRE (gainToDecibels (static_cast<float> (gainOf (BiquadCoefficients::peak (sr, 750.0, 0.8, 6.0), 750.0))) == Catch::Approx (6.0).margin (0.1));
    REQUIRE (gainToDecibels (static_cast<float> (gainOf (BiquadCoefficients::lowShelf (sr, 120.0, -9.0), 20.0))) == Catch::Approx (-9.0).margin (0.3));
    REQUIRE (gainToDecibels (static_cast<float> (gainOf (BiquadCoefficients::highShelf (sr, 3000.0, 9.0), 15000.0))) == Catch::Approx (9.0).margin (0.3));
}

TEST_CASE ("ToneStack à plat ne modifie pas le signal", "[tonestack]")
{
    ToneStack tone;
    tone.prepare ({ sr, 256 });
    tone.setGains (0.0f, 0.0f, 0.0f);

    auto signal = sine (440.0, sr, 4096);
    const auto original = signal;
    processInBlocks (tone, signal, 256);

    for (std::size_t i = 0; i < signal.size(); ++i)
        REQUIRE (signal[i] == Catch::Approx (original[i]).margin (1.0e-4)); // -80 dB : arrondis des coefficients
}

TEST_CASE ("Le cabinet de secours coupe l'extrême grave et l'aigu", "[cabinet]")
{
    FallbackCabinet cab;
    cab.prepare ({ sr, 256 });

    const auto measure = [&] (double frequency)
    {
        cab.reset();
        auto signal = sine (frequency, sr, 48000, 1.0f);
        processInBlocks (cab, signal, 256);
        return magnitudeAt (std::span<const float> { signal.data() + 24000, 24000 }, frequency, sr);
    };

    const auto mid = measure (1000.0);
    REQUIRE (measure (30.0) < mid * 0.3);
    REQUIRE (measure (12000.0) < mid * 0.05);
}

TEST_CASE ("Biquad : la courbe calculée (écrans de l'interface) correspond au filtre", "[filter]")
{
    constexpr double rate = 48000.0;

    // Cloche de +6 dB à 750 Hz : +6 dB au centre, quasi rien loin de lui.
    const auto peak = BiquadCoefficients::peak (rate, 750.0, 0.8, 6.0);
    REQUIRE (peak.magnitudeDecibels (750.0, rate) == Catch::Approx (6.0).margin (0.01));
    REQUIRE (std::abs (peak.magnitudeDecibels (20.0, rate)) < 0.1);

    // Passe-bas de Butterworth : -3 dB à la coupure ; et la mesure sur un vrai signal concorde.
    const auto lowPass = BiquadCoefficients::lowPass (rate, 2000.0, butterworthQ);
    REQUIRE (lowPass.magnitudeDecibels (2000.0, rate) == Catch::Approx (-3.01).margin (0.05));

    Biquad filter;
    filter.setCoefficients (lowPass);
    auto signal = subdamp::test::sine (5000.0, rate, 48000, 0.5f);

    for (auto& s : signal)
        s = filter.processSample (s);

    const auto measured = 20.0 * std::log10 (subdamp::test::rms (std::span<const float> { signal }.subspan (24000)) / (0.5 / std::sqrt (2.0)));
    REQUIRE (measured == Catch::Approx (lowPass.magnitudeDecibels (5000.0, rate)).margin (0.1));
}
