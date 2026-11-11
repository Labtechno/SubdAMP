// Vérifie qu'aucun module n'alloue ni ne libère de mémoire pendant le traitement audio.

#include "AllocationTracking.h"

#include "subdamp/dsp/AmpModel.h"
#include "subdamp/dsp/FrequencyShifter.h"
#include "subdamp/dsp/GranularCloud.h"
#include "subdamp/dsp/Harmonizer.h"
#include "subdamp/dsp/NoiseGate.h"
#include "subdamp/dsp/OctaveDivider.h"
#include "subdamp/dsp/PolyShifter.h"
#include "subdamp/dsp/RateBridge.h"
#include "subdamp/dsp/ShimmerReverb.h"
#include "subdamp/dsp/SpectralFreeze.h"
#include "subdamp/dsp/StereoTexture.h"
#include "subdamp/dsp/ChainSlot.h"
#include "subdamp/dsp/ToneStack.h"
#include "subdamp/dsp/Whammy.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
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
                                                 + 0.1f * std::sin (0.03f * static_cast<float> (n)); // 3e harmonique : signal périodique

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

TEST_CASE ("WHAMMY et HARMONIZER n'allouent rien, quel que soit le réglage", "[realtime]")
{
    const auto sampleRate = GENERATE (44100.0, 48000.0, 96000.0);

    Whammy whammy;
    whammy.prepare ({ sampleRate, blockSize });
    whammy.setMode (Whammy::Mode::attackToInterval);
    whammy.setInterval (-24.0f);
    whammy.setMix (1.0f);
    REQUIRE (allocationsWhileProcessing (whammy, sampleRate) == 0);

    whammy.setMode (Whammy::Mode::fixed);
    whammy.setInterval (24.0f);
    REQUIRE (allocationsWhileProcessing (whammy, sampleRate) == 0);

    Harmonizer harmonizer;
    harmonizer.prepare ({ sampleRate, blockSize });
    harmonizer.setVoiceInterval (0, 14);  // +2 octaves : le plus de grains simultanés
    harmonizer.setVoiceInterval (1, -14);
    harmonizer.setVoiceLevel (0, 1.0f);
    harmonizer.setVoiceLevel (1, 1.0f);
    harmonizer.setFormantSemitones (-12.0f); // grains les plus longs
    REQUIRE (allocationsWhileProcessing (harmonizer, sampleRate) == 0);
    REQUIRE (harmonizer.isTracking());
}

TEST_CASE ("Les textures n'allouent rien, y compris en marche / arrêt et changements de réglage", "[realtime]")
{
    const auto sampleRate = GENERATE (48000.0, 96000.0);

    ShimmerReverb shimmer;
    PolyShifter poly;
    SpectralFreeze freeze;
    GranularCloud granular;

    shimmer.setShimmer (1.0f);
    poly.setInterval (0, 12.0f);
    poly.setInterval (1, -7.0f);
    poly.setVoiceLevel (0, 1.0f);
    poly.setFormantSemitones (-5.0f);
    granular.setSpread (1.0f);
    granular.setReverseProbability (0.5f);

    // En série, comme dans le plugin : chaque texture traite la sortie stéréo de la précédente.
    std::array<ChainSlot, 4> slots { ChainSlot { shimmer }, ChainSlot { poly }, ChainSlot { freeze }, ChainSlot { granular } };

    for (auto& slot : slots)
    {
        slot.prepare ({ sampleRate, blockSize });
        slot.setMix (0.6f);
        slot.setEnabled (true);
    }

    auto signal = guitarLikeSignal (static_cast<int> (sampleRate) * 3);
    std::vector<float> left (blockSize), right (blockSize);

    const auto runChain = [&] (std::size_t start)
    {
        std::copy_n (signal.data() + start, blockSize, left.begin());
        std::copy_n (signal.data() + start, blockSize, right.begin());

        for (auto& slot : slots)
            slot.process (left, right);
    };

    // Premier passage hors mesure (réveil des textures).
    runChain (0);

    ScopedAllocationCounter counter;

    for (std::size_t start = blockSize, i = 0; start + blockSize <= signal.size(); start += blockSize, ++i)
    {
        // Réglages qui bougent pendant le jeu : gel / dégel, voix 2 allumée puis coupée,
        // changement de mode du granulaire, texture arrêtée puis relancée.
        freeze.setHold ((i / 200) % 2 == 1);
        poly.setVoiceLevel (1, (i / 300) % 2 == 1 ? 1.0f : 0.0f);
        poly.setInterval (0, (i / 250) % 2 == 1 ? 12.0f : 7.0f);
        granular.setMode ((i / 400) % 2 == 1 ? GranularCloud::Mode::stutter : GranularCloud::Mode::cloud);
        shimmer.setDecaySeconds ((i / 350) % 2 == 1 ? 12.0f : 3.0f);
        shimmer.setPitchMode ((i / 180) % 2 == 1 ? ShimmerReverb::PitchMode::dual : ShimmerReverb::PitchMode::single);
        shimmer.setInterval ((i / 220) % 2 == 1 ? 19.0f : 12.0f);
        shimmer.setSize (static_cast<float> (i % 97) / 96.0f);
        shimmer.setPreDelayMs (static_cast<float> ((i * 7) % 501));
        shimmer.setDiffusion (static_cast<float> (i % 53) / 52.0f);
        shimmer.setModulationRate (0.05f + static_cast<float> (i % 31) * 0.16f);
        shimmer.setModulationDepth (static_cast<float> (i % 41) / 40.0f);
        shimmer.setLowCutHz (20.0f + static_cast<float> ((i * 13) % 981));
        shimmer.setToneHz (500.0f + static_cast<float> ((i * 37) % 15501));
        shimmer.setWidth (static_cast<float> (i % 23) / 22.0f);
        shimmer.setShimmer ((i / 400) % 3 == 2 ? 0.0f : 1.0f); // boucle arrêtée puis relancée
        slots[3].setEnabled ((i / 500) % 3 != 2);
        slots[0].setMix ((i / 150) % 2 == 1 ? 1.0f : 0.3f);

        runChain (start);
    }

    REQUIRE (counter.count() == 0);
}

TEST_CASE ("Un module mis en marche puis arrêté, MIX et retard du sec qui bougent : aucune allocation", "[realtime]")
{
    FrequencyShifter shifter;
    ChainSlot slot (shifter);
    slot.prepare ({ 48000.0, blockSize });

    auto signal = guitarLikeSignal (48000);
    auto right = signal;

    ScopedAllocationCounter counter;

    for (std::size_t start = 0, i = 0; start + blockSize <= signal.size(); start += blockSize, ++i)
    {
        slot.setEnabled (i % 50 < 25);
        slot.setMix (i % 30 < 15 ? 1.0f : 0.4f);
        slot.setDryDelaySamples (static_cast<int> (i % 7) * 10);
        slot.process (std::span<float> { signal.data() + start, blockSize }, std::span<float> { right.data() + start, blockSize });
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
