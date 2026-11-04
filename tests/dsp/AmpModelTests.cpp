#include "TestSignals.h"

#include "subdamp/dsp/AmpModel.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace subdamp::dsp;
using namespace subdamp::test;

namespace
{
    const std::filesystem::path models { SUBDAMP_NAM_EXAMPLE_MODELS };
    constexpr int blockSize = 128;

    /** Charge une capture d'exemple ; en cas d'échec, le message d'erreur apparaît dans le rapport. */
    bool loadExample (AmpModel& amp, const char* fileName)
    {
        const auto result = amp.load (models / fileName);
        INFO ("Erreur de chargement : " << result.error);
        CHECK (result.succeeded);
        return result.succeeded;
    }

    bool allFinite (const std::vector<float>& signal)
    {
        return std::all_of (signal.begin(), signal.end(), [] (float s) { return std::isfinite (s); });
    }
}

TEST_CASE ("AmpModel sans capture : le signal passe tel quel", "[amp]")
{
    AmpModel amp;
    amp.prepare ({ 48000.0, blockSize });

    auto signal = sine (220.0, 48000.0, 4096);
    const auto original = signal;
    processInBlocks (amp, signal, blockSize);

    REQUIRE (signal == original);
    REQUIRE (amp.getLatencySamples() == 0);
}

TEST_CASE ("AmpModel : une capture WaveNet traite le signal à 48 kHz, sans latence", "[amp]")
{
    AmpModel amp;
    amp.prepare ({ 48000.0, blockSize });

    REQUIRE (loadExample (amp, "wavenet_a1_standard.nam"));

    auto signal = sine (220.0, 48000.0, 48000, 0.2f);
    const auto original = signal;
    processInBlocks (amp, signal, blockSize);

    REQUIRE (allFinite (signal));
    REQUIRE (signal != original);
    REQUIRE (rms (signal) > 0.001);
    REQUIRE (amp.getLatencySamples() == 0);
}

TEST_CASE ("AmpModel : à 88,2 kHz, la capture tourne à sa fréquence d'origine via le pont", "[amp]")
{
    AmpModel amp;
    amp.prepare ({ 88200.0, blockSize });
    REQUIRE (loadExample (amp, "wavenet_a1_standard.nam"));

    auto signal = sine (220.0, 88200.0, 88200, 0.2f);
    processInBlocks (amp, signal, blockSize);

    REQUIRE (allFinite (signal));
    REQUIRE (rms (signal) > 0.001);
    REQUIRE (amp.getLatencySamples() > 0);
    REQUIRE (amp.getLatencySamples() < 88); // moins d'1 ms
}

TEST_CASE ("AmpModel : une capture préparée avant prepareToPlay est mise à la bonne fréquence", "[amp]")
{
    AmpModel amp;
    REQUIRE (loadExample (amp, "wavenet.nam")); // état restauré avant que l'audio démarre

    amp.prepare ({ 96000.0, blockSize });

    auto signal = sine (220.0, 96000.0, 9600, 0.2f);
    processInBlocks (amp, signal, blockSize);

    REQUIRE (allFinite (signal));
    REQUIRE (amp.getLatencySamples() > 0);
}

TEST_CASE ("AmpModel : un fichier invalide est refusé sans toucher à la capture en place", "[amp]")
{
    AmpModel amp;
    amp.prepare ({ 48000.0, blockSize });
    REQUIRE (loadExample (amp, "wavenet.nam"));

    const auto result = amp.load (models / "ce_fichier_n_existe_pas.nam");
    REQUIRE_FALSE (result.succeeded);
    REQUIRE_FALSE (result.error.empty());

    auto signal = sine (220.0, 48000.0, 4800, 0.2f);
    const auto original = signal;
    processInBlocks (amp, signal, blockSize);

    REQUIRE (signal != original); // la capture précédente est toujours active
}

TEST_CASE ("AmpModel : retirer la capture rend le module transparent (après un fondu)", "[amp]")
{
    AmpModel amp;
    amp.prepare ({ 48000.0, blockSize });
    REQUIRE (loadExample (amp, "wavenet.nam"));

    auto warmup = sine (220.0, 48000.0, 4800, 0.2f);
    processInBlocks (amp, warmup, blockSize);

    amp.unload();
    amp.collectGarbage();

    auto signal = sine (220.0, 48000.0, 9600, 0.2f);
    const auto original = signal;
    processInBlocks (amp, signal, blockSize);

    // Après le fondu d'entrée de 30 ms, plus aucune différence.
    for (std::size_t i = 2000; i < signal.size(); ++i)
        REQUIRE (signal[i] == original[i]);
}
