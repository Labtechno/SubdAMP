#include "TestSignals.h"

#include "subdamp/dsp/FractionalResampler.h"
#include "subdamp/dsp/RateBridge.h"
#include "subdamp/dsp/RealtimeHandoff.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <random>

using namespace subdamp::dsp;
using namespace subdamp::test;

TEST_CASE ("FractionalResampler : une sinusoïde garde sa fréquence et son amplitude", "[resampler]")
{
    const auto [inRate, outRate] = GENERATE (std::pair { 88200.0, 48000.0 },
                                             std::pair { 48000.0, 96000.0 },
                                             std::pair { 44100.0, 48000.0 });
    FractionalResampler resampler;
    resampler.prepare (inRate, outRate, 512);

    const auto input = sine (1000.0, inRate, static_cast<int> (inRate));
    std::vector<float> output (static_cast<std::size_t> (outRate) + 64);

    int produced = 0;

    for (std::size_t start = 0; start < input.size(); start += 512)
    {
        const auto n = static_cast<int> (std::min<std::size_t> (512, input.size() - start));
        produced += resampler.process (input.data() + start, n, output.data() + produced,
                                       static_cast<int> (output.size()) - produced);
    }

    output.resize (static_cast<std::size_t> (produced));
    const std::span<const float> steady { output.data() + produced / 4, static_cast<std::size_t> (produced / 2) };

    REQUIRE (magnitudeAt (steady, 1000.0, outRate) == Catch::Approx (0.5).margin (0.005));
}

TEST_CASE ("FractionalResampler : en descente, le contenu au-dessus du nouveau Nyquist est filtré", "[resampler]")
{
    FractionalResampler resampler;
    resampler.prepare (96000.0, 48000.0, 512);

    // 30 kHz n'existe pas à 48 kHz : sans filtre, il se replierait à 18 kHz.
    const auto input = sine (30000.0, 96000.0, 96000);
    std::vector<float> output (48064);
    const auto produced = resampler.process (input.data(), static_cast<int> (input.size()), output.data(), static_cast<int> (output.size()));
    output.resize (static_cast<std::size_t> (produced));

    REQUIRE (rms (std::span<const float> { output.data() + 1000, output.size() - 2000 }) < 0.5 * 0.001); // < -60 dB
}

TEST_CASE ("RateBridge : aller-retour 88,2 → 48 → 88,2 kHz sans trou et aligné sur la latence annoncée", "[bridge]")
{
    constexpr double hostRate = 88200.0;
    RateBridge bridge;
    bridge.prepare (hostRate, 48000.0, 512);

    REQUIRE (bridge.latencySamples() > 0);
    REQUIRE (bridge.latencySamples() < static_cast<int> (hostRate * 0.001)); // moins d'1 ms

    const auto input = sine (440.0, hostRate, static_cast<int> (hostRate) * 2);
    auto output = input;

    // Tailles de blocs irrégulières, comme certains hôtes.
    std::mt19937 random (42);
    std::uniform_int_distribution<int> blockSizes (1, 512);

    for (std::size_t start = 0; start < output.size();)
    {
        const auto size = std::min (static_cast<std::size_t> (blockSizes (random)), output.size() - start);
        bridge.process (std::span<float> { output.data() + start, size },
                        [] (float* in, float* out, int n) { std::copy (in, in + n, out); });
        start += size;
    }

    REQUIRE (bridge.underflowCount() == 0);

    const auto latency = static_cast<std::size_t> (bridge.latencySamples());
    double maxError = 0.0;

    for (std::size_t i = 20000; i < output.size(); ++i)
        maxError = std::max (maxError, static_cast<double> (std::abs (output[i] - input[i - latency])));

    REQUIRE (maxError < 0.005); // < -40 dB par rapport au signal, en tout point
}

TEST_CASE ("RealtimeHandoff : le thread audio prend toujours l'objet le plus récent", "[handoff]")
{
    RealtimeHandoff<int> handoff;

    REQUIRE (handoff.acquire() == nullptr);
    REQUIRE (handoff.getGeneration() == 0);

    handoff.publish (std::make_unique<int> (1));
    REQUIRE (*handoff.acquire() == 1);
    REQUIRE (handoff.getGeneration() == 1);

    // Deux dépôts avant le passage du thread audio : seul le dernier compte.
    handoff.publish (std::make_unique<int> (2));
    handoff.publish (std::make_unique<int> (3));
    REQUIRE (*handoff.acquire() == 3);
    REQUIRE (handoff.getGeneration() == 2);

    // Rien de nouveau : l'objet courant reste en place.
    REQUIRE (*handoff.acquire() == 3);
    REQUIRE (handoff.getGeneration() == 2);

    handoff.collect();
    handoff.publish (std::make_unique<int> (4));
    REQUIRE (*handoff.acquire() == 4);
    REQUIRE (handoff.getCurrent() != nullptr);
}
