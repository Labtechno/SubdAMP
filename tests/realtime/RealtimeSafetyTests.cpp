// Vérifie qu'aucun module n'alloue ni ne libère de mémoire pendant le traitement audio.

#include "AllocationTracking.h"

#include "subdamp/dsp/AmpModel.h"
#include "subdamp/dsp/FrequencyShifter.h"
#include "subdamp/dsp/NoiseGate.h"
#include "subdamp/dsp/OctaveDivider.h"
#include "subdamp/dsp/RateBridge.h"
#include "subdamp/dsp/SwitchableEffect.h"
#include "subdamp/dsp/ToneStack.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <thread>
#include <vector>

using namespace subdamp::dsp;
using namespace subdamp::test;

namespace
{
    constexpr int blockSize = 64;

    std::vector<float> guitarLikeSignal (int numSamples)
    {
        std::vector<float> signal (static_cast<std::size_t> (numSamples));

        for (int n = 0; n < numSamples; ++n)
            signal[static_cast<std::size_t> (n)] = 0.3f * std::sin (0.01f * static_cast<float> (n))
                                                 + 0.1f * std::sin (0.037f * static_cast<float> (n));

        return signal;
    }

    /** Traite 2 secondes par blocs et renvoie le nombre d'allocations observées. */
    int allocationsWhileProcessing (MonoEffect& effect, double sampleRate)
    {
        auto signal = guitarLikeSignal (static_cast<int> (sampleRate) * 2);

        // Premier bloc hors mesure : certains modules initialisent des tables paresseusement.
        effect.process (std::span<float> { signal.data(), blockSize });

        ScopedAllocationCounter counter;

        for (std::size_t start = blockSize; start + blockSize <= signal.size(); start += blockSize)
            effect.process (std::span<float> { signal.data() + start, blockSize });

        return counter.count();
    }
}

namespace
{
    // Le compilateur a le droit de supprimer un new/delete dont le résultat ne sert à rien
    // (clang le fait) : on fait « fuir » le pointeur vers une variable volatile.
    void* volatile allocationSink = nullptr;
}

TEST_CASE ("Le compteur d'allocations détecte bien une allocation", "[realtime]")
{
    ScopedAllocationCounter counter;
    allocationSink = new std::vector<float> (1024);
    delete static_cast<std::vector<float>*> (allocationSink);

    REQUIRE (counter.count() >= 2); // le vecteur et son tableau, au moins
}

TEST_CASE ("Le compteur ignore les allocations des autres threads", "[realtime]")
{
    ScopedAllocationCounter counter;

    std::thread other ([]
    {
        for (int i = 0; i < 100; ++i)
        {
            allocationSink = new std::vector<float> (1024);
            delete static_cast<std::vector<float>*> (allocationSink);
        }
    });

    other.join();

    // Comptées, ces allocations donneraient au moins 200. Créer le thread lui-même alloue
    // un peu sur le thread courant (1 ou 2 selon la plateforme), d'où la marge.
    REQUIRE (counter.count() < 10);
}

TEST_CASE ("Les modules LIVE n'allouent rien pendant le traitement", "[realtime]")
{
    NoiseGate gate;
    OctaveDivider sub;
    FrequencyShifter shifter;
    ToneStack tone;
    FallbackCabinet cabinet;

    for (MonoEffect* effect : std::initializer_list<MonoEffect*> { &gate, &sub, &shifter, &tone, &cabinet })
    {
        effect->prepare ({ 48000.0, blockSize });
        REQUIRE (allocationsWhileProcessing (*effect, 48000.0) == 0);
    }
}

TEST_CASE ("Un module mis en marche puis arrêté n'alloue rien", "[realtime]")
{
    FrequencyShifter shifter;
    SwitchableEffect slot (shifter);
    slot.prepare ({ 48000.0, blockSize });

    auto signal = guitarLikeSignal (48000);

    ScopedAllocationCounter counter;

    for (std::size_t start = 0, i = 0; start + blockSize <= signal.size(); start += blockSize, ++i)
    {
        slot.setEnabled (i % 50 < 25);
        slot.process (std::span<float> { signal.data() + start, blockSize });
    }

    REQUIRE (counter.count() == 0);
}

TEST_CASE ("Le pont de fréquence d'échantillonnage n'alloue rien", "[realtime]")
{
    RateBridge bridge;
    bridge.prepare (88200.0, 48000.0, blockSize);

    auto signal = guitarLikeSignal (88200);

    ScopedAllocationCounter counter;

    for (std::size_t start = 0; start + blockSize <= signal.size(); start += blockSize)
        bridge.process (std::span<float> { signal.data() + start, blockSize },
                        [] (float* in, float* out, int n) { std::copy (in, in + n, out); });

    REQUIRE (counter.count() == 0);
}

TEST_CASE ("Une capture NAM n'alloue rien pendant le traitement, avec ou sans rééchantillonnage", "[realtime][nam]")
{
    const auto modelFile = GENERATE (as<std::string> {}, "wavenet_a1_standard.nam", "A2.nam", "lstm.nam");
    const auto sampleRate = GENERATE (48000.0, 88200.0);

    AmpModel amp;
    amp.prepare ({ sampleRate, blockSize });
    REQUIRE (amp.load (std::filesystem::path (SUBDAMP_NAM_EXAMPLE_MODELS) / modelFile).succeeded);

    REQUIRE (allocationsWhileProcessing (amp, sampleRate) == 0);
}
