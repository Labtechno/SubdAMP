// Mesure la charge CPU des captures NAM d'exemple, en pourcentage d'un cœur, comme dans un hôte
// réel (blocs de 64 échantillons). Usage : SubdAmpBenchmark [fichier.nam ...]

#include "subdamp/dsp/AmpModel.h"
#include "subdamp/dsp/Harmonizer.h"
#include "subdamp/dsp/Whammy.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace subdamp::dsp;

namespace
{
    double measureLoadPercent (const std::filesystem::path& file, double sampleRate, int blockSize)
    {
        AmpModel amp;
        amp.prepare ({ sampleRate, blockSize });

        if (const auto result = amp.load (file); ! result.succeeded)
        {
            std::printf ("  échec du chargement : %s\n", result.error.c_str());
            return -1.0;
        }

        constexpr double seconds = 10.0;
        const auto numBlocks = static_cast<int> (sampleRate * seconds / blockSize);
        std::vector<float> block (static_cast<std::size_t> (blockSize));
        double phase = 0.0;

        const auto start = std::chrono::steady_clock::now();

        for (int b = 0; b < numBlocks; ++b)
        {
            for (auto& sample : block)
            {
                sample = 0.3f * static_cast<float> (std::sin (phase));
                phase += 2.0 * 3.14159265358979 * 110.0 / sampleRate;
            }

            amp.process (block);
        }

        const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
        return 100.0 * elapsed.count() / seconds;
    }
}

/** Charge CPU d'un module quelconque, sur 10 s de signal à 110 Hz riche en harmoniques. */
double measureEffectLoadPercent (MonoEffect& effect, double sampleRate, int blockSize)
{
    effect.prepare ({ sampleRate, blockSize });

    constexpr double seconds = 10.0;
    const auto numBlocks = static_cast<int> (sampleRate * seconds / blockSize);
    std::vector<float> block (static_cast<std::size_t> (blockSize));
    double phase = 0.0;

    const auto start = std::chrono::steady_clock::now();

    for (int b = 0; b < numBlocks; ++b)
    {
        for (auto& sample : block)
        {
            sample = 0.3f * static_cast<float> (std::sin (phase) + 0.5 * std::sin (2.0 * phase) + 0.25 * std::sin (3.0 * phase));
            phase += 2.0 * 3.14159265358979 * 110.0 / sampleRate;
        }

        effect.process (block);
    }

    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
    return 100.0 * elapsed.count() / seconds;
}

int main (int argc, char** argv)
{
    std::vector<std::filesystem::path> files;

    for (int i = 1; i < argc; ++i)
        files.emplace_back (argv[i]);

    if (files.empty())
        for (const auto* name : { "wavenet_a1_standard.nam", "A2.nam", "lstm.nam" })
            files.push_back (std::filesystem::path (SUBDAMP_NAM_EXAMPLE_MODELS) / name);

    std::printf ("Charge CPU (%% d'un cœur), blocs de 64 échantillons :\n");

    for (const auto& file : files)
    {
        std::printf ("%s\n", file.filename().string().c_str());

        for (const auto sampleRate : { 48000.0, 88200.0 })
            std::printf ("  %6.1f kHz : %5.1f %%\n", sampleRate / 1000.0, measureLoadPercent (file, sampleRate, 64));
    }

    std::printf ("\nModules de pitch (pire cas) :\n");

    for (const auto sampleRate : { 48000.0, 96000.0 })
    {
        Whammy whammy;
        whammy.setInterval (24.0f);
        whammy.setMix (1.0f);
        std::printf ("  WHAMMY +2 oct, %4.0f kHz : %5.1f %%\n", sampleRate / 1000.0, measureEffectLoadPercent (whammy, sampleRate, 64));

        Harmonizer harmonizer;
        harmonizer.setVoiceInterval (0, 14);
        harmonizer.setVoiceInterval (1, 7);
        harmonizer.setVoiceLevel (0, 1.0f);
        harmonizer.setVoiceLevel (1, 1.0f);
        harmonizer.setFormantSemitones (-12.0f);
        std::printf ("  HARMONIZER 2 voix, %4.0f kHz : %5.1f %%\n", sampleRate / 1000.0, measureEffectLoadPercent (harmonizer, sampleRate, 64));
    }

    return 0;
}
