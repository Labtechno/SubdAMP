// Mesure la charge CPU des captures NAM d'exemple, en pourcentage d'un cœur, comme dans un hôte
// réel (blocs de 64 échantillons). Usage : SubdAmpBenchmark [fichier.nam ...]

#include "subdamp/dsp/AmpModel.h"

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

    return 0;
}
