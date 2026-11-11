#include "TestSignals.h"

#include "subdamp/dsp/ChainSlot.h"
#include "subdamp/dsp/Fft.h"
#include "subdamp/dsp/GranularCloud.h"
#include "subdamp/dsp/PolyShifter.h"
#include "subdamp/dsp/ShimmerReverb.h"
#include "subdamp/dsp/SpectralFreeze.h"
#include "subdamp/dsp/StereoTexture.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <complex>
#include <functional>
#include <numbers>
#include <random>

using namespace subdamp::dsp;
using namespace subdamp::test;

namespace
{
    constexpr double sr = 48000.0;
    constexpr int blockSize = 64;

    struct Stereo
    {
        std::vector<float> left, right;
    };

    /** Fait passer un signal mono dans une texture, par blocs. */
    Stereo render (StereoTexture& texture, const std::vector<float>& input)
    {
        Stereo out { std::vector<float> (input.size()), std::vector<float> (input.size()) };

        for (std::size_t start = 0; start < input.size(); start += blockSize)
        {
            const auto n = std::min<std::size_t> (blockSize, input.size() - start);
            texture.process (std::span<const float> { input.data() + start, n },
                             std::span<float> { out.left.data() + start, n },
                             std::span<float> { out.right.data() + start, n });
        }

        return out;
    }

    std::span<const float> slice (const std::vector<float>& signal, double fromSeconds, double toSeconds)
    {
        const auto from = static_cast<std::size_t> (fromSeconds * sr);
        const auto to = std::min (signal.size(), static_cast<std::size_t> (toSeconds * sr));
        return { signal.data() + from, to - from };
    }

    std::vector<float> noise (std::size_t length, float amplitude, unsigned seed = 3)
    {
        std::mt19937 random (seed);
        std::uniform_real_distribution<float> distribution (-amplitude, amplitude);
        std::vector<float> signal (length);

        for (auto& s : signal)
            s = distribution (random);

        return signal;
    }

    double correlation (std::span<const float> a, std::span<const float> b)
    {
        double ab = 0.0, aa = 0.0, bb = 0.0;

        for (std::size_t i = 0; i < a.size(); ++i)
        {
            ab += static_cast<double> (a[i]) * b[i];
            aa += static_cast<double> (a[i]) * a[i];
            bb += static_cast<double> (b[i]) * b[i];
        }

        return ab / std::sqrt (aa * bb + 1.0e-30);
    }

    double peakAbs (const std::vector<float>& signal)
    {
        double peak = 0.0;

        for (const auto s : signal)
            peak = std::max (peak, static_cast<double> (std::abs (s)));

        return peak;
    }

    double db (double ratio) { return 20.0 * std::log10 (ratio); }
}

//==============================================================================
TEST_CASE ("FFT : aller-retour exact et raie à la bonne case", "[fft]")
{
    Fft fft;
    fft.prepare (1024);

    std::vector<std::complex<float>> data (1024);
    const auto original = noise (1024, 1.0f);

    for (std::size_t i = 0; i < data.size(); ++i)
        data[i] = { original[i], 0.0f };

    fft.forward (data.data());
    fft.inverse (data.data());

    for (std::size_t i = 0; i < data.size(); ++i)
        REQUIRE (data[i].real() == Catch::Approx (original[i]).margin (1.0e-4));

    for (std::size_t i = 0; i < data.size(); ++i)
        data[i] = { static_cast<float> (std::cos (2.0 * std::numbers::pi * 37.0 * static_cast<double> (i) / 1024.0)), 0.0f };

    fft.forward (data.data());

    std::size_t peak = 1;

    for (std::size_t k = 1; k < 512; ++k)
        if (std::abs (data[k]) > std::abs (data[peak]))
            peak = k;

    REQUIRE (peak == 37);
    REQUIRE (std::abs (data[37]) == Catch::Approx (512.0f).margin (0.1f));
}

//==============================================================================
TEST_CASE ("SHIMMER : la queue décroît au rythme réglé", "[shimmer]")
{
    ShimmerReverb reverb;
    reverb.prepare ({ sr, blockSize });
    reverb.setDecaySeconds (2.0f);
    reverb.setShimmer (0.0f);
    reverb.reset();

    std::vector<float> input (static_cast<std::size_t> (sr * 2.0), 0.0f);
    const auto burst = noise (static_cast<std::size_t> (sr * 0.05), 0.5f);
    std::copy (burst.begin(), burst.end(), input.begin());

    const auto out = render (reverb, input);

    // T60 de 2 s : environ -30 dB par seconde.
    const auto drop = db (rms (slice (out.left, 0.3, 0.4)) / rms (slice (out.left, 1.3, 1.4)));
    INFO ("Décroissance mesurée : " << drop << " dB en 1 s");
    REQUIRE (drop > 22.0);
    REQUIRE (drop < 38.0);
}

TEST_CASE ("SHIMMER : la boucle ajoute l'octave au-dessus dans la queue", "[shimmer]")
{
    const auto tailAt = [] (float shimmerAmount, double frequency)
    {
        ShimmerReverb reverb;
        reverb.prepare ({ sr, blockSize });
        reverb.setDecaySeconds (6.0f);
        reverb.setInterval (12.0f);
        reverb.setShimmer (shimmerAmount);
        reverb.reset();

        auto input = sine (220.0, sr, static_cast<int> (sr * 3.0), 0.3f);
        std::fill (input.begin() + static_cast<std::ptrdiff_t> (sr), input.end(), 0.0f); // 1 s de note, puis silence

        const auto out = render (reverb, input);
        return magnitudeAt (slice (out.left, 1.5, 2.5), frequency, sr);
    };

    const auto plain = tailAt (0.0f, 440.0);
    const auto shimmering = tailAt (0.8f, 440.0);

    INFO ("Énergie à 440 Hz : " << plain << " sans shimmer, " << shimmering << " avec");
    REQUIRE (shimmering > 10.0 * plain);
}

TEST_CASE ("SHIMMER : au maximum, la queue finit toujours par s'éteindre", "[shimmer]")
{
    // Régression : dans la première version, le continu et l'extrême grave tournaient dans la
    // boucle sans jamais monter : la queue grossissait jusqu'à la saturation et ne s'arrêtait plus.
    for (const auto interval : { 12.0f, 19.0f, 24.0f, 7.0f, -12.0f })
    {
        ShimmerReverb reverb;
        reverb.prepare ({ sr, blockSize });
        reverb.setDecaySeconds (2.0f);
        reverb.setShimmer (1.0f);
        reverb.setToneHz (16000.0f);
        reverb.setInterval (interval);
        reverb.reset();

        auto input = pluckedString (290, sr, static_cast<int> (sr * 14.0), 0.5f); // ≈ 165 Hz
        std::fill (input.begin() + static_cast<std::ptrdiff_t> (sr), input.end(), 0.0f);

        const auto out = render (reverb, input);
        const auto playing = rms (slice (out.left, 0.2, 1.0));
        const auto tail = rms (slice (out.left, 13.0, 14.0));

        INFO ("Intervalle " << interval << " : " << db (tail / playing) << " dB après 12 s de silence");
        REQUIRE (tail < playing * 1.0e-3); // -60 dB
        REQUIRE (peakAbs (out.left) < 1.0);
    }
}

TEST_CASE ("SHIMMER : ni continu ni grave accumulé, même réglé au plus long", "[shimmer]")
{
    ShimmerReverb reverb;
    reverb.prepare ({ sr, blockSize });
    reverb.setDecaySeconds (30.0f);
    reverb.setShimmer (1.0f);
    reverb.setToneHz (16000.0f);
    reverb.reset();

    // Bruit soutenu, avec un décalage continu (offset d'une carte son) : le pire cas.
    auto input = noise (static_cast<std::size_t> (sr * 10.0), 0.5f);

    for (auto& s : input)
        s += 0.1f;

    const auto out = render (reverb, input);

    REQUIRE (std::all_of (out.left.begin(), out.left.end(), [] (float s) { return std::isfinite (s); }));

    const auto mean = [] (std::span<const float> signal)
    {
        double sum = 0.0;

        for (const auto s : signal)
            sum += s;

        return sum / static_cast<double> (signal.size());
    };

    const auto early = rms (slice (out.left, 2.0, 4.0));
    const auto late = rms (slice (out.left, 8.0, 10.0));
    INFO ("Niveau : " << db (early) << " dB à 2-4 s, " << db (late) << " dB à 8-10 s");

    REQUIRE (late < 1.5 * early);                                 // pas d'emballement
    REQUIRE (std::abs (mean (slice (out.left, 5.0, 10.0))) < 0.005); // pas de continu
    REQUIRE (std::abs (mean (slice (out.right, 5.0, 10.0))) < 0.005);
    REQUIRE (peakAbs (out.left) < 1.5);
    REQUIRE (peakAbs (out.right) < 1.5);
}

TEST_CASE ("SHIMMER : la queue reste harmonique (aucun repliement)", "[shimmer]")
{
    // Une note à 1 kHz : toute la queue doit tomber sur 1 kHz × rapport^k. Un repliement ou des
    // artefacts de transposition mettraient de l'énergie ailleurs, et la boucle les accumulerait.
    for (const auto interval : { 12.0f, 7.0f, 24.0f })
    {
        ShimmerReverb reverb;
        reverb.prepare ({ sr, blockSize });
        reverb.setDecaySeconds (4.0f);
        reverb.setShimmer (1.0f);
        reverb.setToneHz (16000.0f);
        reverb.setInterval (interval);
        reverb.reset();

        auto input = sine (1000.0, sr, static_cast<int> (sr * 3.0), 0.3f);
        std::fill (input.begin() + static_cast<std::ptrdiff_t> (sr * 0.5), input.end(), 0.0f);
        const auto out = render (reverb, input);

        constexpr int size = 65536;
        Fft fft;
        fft.prepare (size);
        std::vector<std::complex<float>> spectrum (size);
        const auto tail = slice (out.left, 1.5, 3.0);

        for (std::size_t i = 0; i < spectrum.size(); ++i)
        {
            const auto window = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * static_cast<double> (i) / size);
            spectrum[i] = { i < tail.size() ? static_cast<float> (window * tail[i]) : 0.0f, 0.0f };
        }

        fft.forward (spectrum.data());

        const auto ratio = std::pow (2.0, interval / 12.0);
        double harmonic = 0.0, total = 0.0;

        for (int bin = 1; bin < size / 2; ++bin)
        {
            const auto frequency = bin * sr / size;

            if (frequency < 100.0)
                continue;

            const auto power = static_cast<double> (std::norm (spectrum[static_cast<std::size_t> (bin)]));
            total += power;

            for (int k = -6; k < 10; ++k)
            {
                const auto target = 1000.0 * std::pow (ratio, k);

                if (std::abs (frequency - target) < std::max (40.0, 0.02 * target))
                {
                    harmonic += power;
                    break;
                }
            }
        }

        INFO ("Intervalle " << interval << " : " << 100.0 * harmonic / total << " % de l'énergie sur les harmoniques attendues");
        REQUIRE (harmonic > 0.97 * total);
    }
}

TEST_CASE ("SHIMMER : sortie large (gauche et droite décorrélées)", "[shimmer]")
{
    ShimmerReverb reverb;
    reverb.prepare ({ sr, blockSize });
    reverb.setShimmer (0.0f);
    reverb.reset();

    const auto out = render (reverb, noise (static_cast<std::size_t> (sr * 2.0), 0.3f));
    REQUIRE (std::abs (correlation (slice (out.left, 0.5, 2.0), slice (out.right, 0.5, 2.0))) < 0.5);
}

namespace
{
    /** Puissance par case d'une FFT de Hann de 65536 points (0,73 Hz par case à 48 kHz). */
    std::vector<double> powerSpectrum (std::span<const float> signal)
    {
        constexpr int size = 65536;
        Fft fft;
        fft.prepare (size);
        std::vector<std::complex<float>> spectrum (size);

        for (std::size_t i = 0; i < spectrum.size(); ++i)
        {
            const auto window = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * static_cast<double> (i) / size);
            spectrum[i] = { i < signal.size() ? static_cast<float> (window * signal[i]) : 0.0f, 0.0f };
        }

        fft.forward (spectrum.data());

        std::vector<double> power (size / 2);

        for (std::size_t bin = 0; bin < power.size(); ++bin)
            power[bin] = static_cast<double> (std::norm (spectrum[bin]));

        return power;
    }

    /** Part de la puissance à moins de `halfWidth` Hz de `frequency`. */
    double fractionNear (const std::vector<double>& power, double frequency, double halfWidth)
    {
        double near = 0.0, total = 0.0;

        for (std::size_t bin = 1; bin < power.size(); ++bin)
        {
            const auto f = static_cast<double> (bin) * sr / 65536.0;
            total += power[bin];

            if (std::abs (f - frequency) <= halfWidth)
                near += power[bin];
        }

        return near / total;
    }

    /** Réglages extrêmes du SHIMMER, appliqués par-dessus la boucle au maximum. */
    struct ShimmerExtreme
    {
        const char* name;
        std::function<void (ShimmerReverb&)> apply;
    };

    std::vector<ShimmerExtreme> shimmerExtremes()
    {
        return {
            { "défaut", [] (ShimmerReverb&) {} },
            { "taille max", [] (ShimmerReverb& r) { r.setSize (1.0f); } },
            { "taille min", [] (ShimmerReverb& r) { r.setSize (0.0f); } },
            { "diffusion max", [] (ShimmerReverb& r) { r.setDiffusion (1.0f); } },
            { "diffusion nulle", [] (ShimmerReverb& r) { r.setDiffusion (0.0f); } },
            { "modulation max", [] (ShimmerReverb& r) { r.setModulationDepth (1.0f); r.setModulationRate (5.0f); } },
            { "coupe-bas min", [] (ShimmerReverb& r) { r.setLowCutHz (20.0f); } },
            { "pré-délai max", [] (ShimmerReverb& r) { r.setPreDelayMs (500.0f); } },
            { "mono", [] (ShimmerReverb& r) { r.setWidth (0.0f); } },
            { "double", [] (ShimmerReverb& r) { r.setPitchMode (ShimmerReverb::PitchMode::dual); } },
            { "tout au maximum", [] (ShimmerReverb& r)
              {
                  r.setPitchMode (ShimmerReverb::PitchMode::dual);
                  r.setSize (1.0f);
                  r.setDiffusion (1.0f);
                  r.setModulationDepth (1.0f);
                  r.setModulationRate (5.0f);
                  r.setLowCutHz (20.0f);
                  r.setPreDelayMs (500.0f);
              } },
        };
    }
}

TEST_CASE ("SHIMMER : chaque réglage poussé à l'extrême, la queue s'éteint toujours", "[shimmer]")
{
    for (const auto& extreme : shimmerExtremes())
    {
        for (const auto interval : { 12.0f, -12.0f, 7.0f })
        {
            ShimmerReverb reverb;
            reverb.prepare ({ sr, blockSize });
            reverb.setDecaySeconds (2.0f);
            reverb.setShimmer (1.0f);
            reverb.setToneHz (16000.0f);
            reverb.setInterval (interval);
            extreme.apply (reverb);
            reverb.reset();

            auto input = pluckedString (290, sr, static_cast<int> (sr * 14.0), 0.5f); // ≈ 165 Hz
            std::fill (input.begin() + static_cast<std::ptrdiff_t> (sr), input.end(), 0.0f);

            const auto out = render (reverb, input);
            const auto playing = rms (slice (out.left, 0.6, 1.5));
            const auto tail = std::max (rms (slice (out.left, 13.0, 14.0)), rms (slice (out.right, 13.0, 14.0)));

            INFO (extreme.name << ", intervalle " << interval << " : " << db (tail / playing) << " dB après 12 s de silence");
            REQUIRE (std::all_of (out.left.begin(), out.left.end(), [] (float s) { return std::isfinite (s); }));
            REQUIRE (tail < playing * 1.0e-3); // -60 dB
            REQUIRE (peakAbs (out.left) < 1.0);
            REQUIRE (peakAbs (out.right) < 1.0);
        }
    }
}

TEST_CASE ("SHIMMER : bruit continu au plus long, aucun réglage extrême ne s'emballe", "[shimmer]")
{
    for (const auto& extreme : shimmerExtremes())
    {
        ShimmerReverb reverb;
        reverb.prepare ({ sr, blockSize });
        reverb.setDecaySeconds (30.0f);
        reverb.setShimmer (1.0f);
        reverb.setToneHz (16000.0f);
        extreme.apply (reverb);
        reverb.reset();

        auto input = noise (static_cast<std::size_t> (sr * 10.0), 0.5f);

        for (auto& s : input)
            s += 0.1f; // offset continu d'une carte son : le pire cas

        const auto out = render (reverb, input);
        const auto early = rms (slice (out.left, 2.0, 4.0));
        const auto late = rms (slice (out.left, 8.0, 10.0));

        INFO (extreme.name << " : " << db (early) << " dB à 2-4 s, " << db (late) << " dB à 8-10 s");
        REQUIRE (late < 1.5 * early);
        REQUIRE (peakAbs (out.left) < 1.5);
        REQUIRE (peakAbs (out.right) < 1.5);
    }
}

TEST_CASE ("SHIMMER : la durée réglée ne dépend pas de la taille", "[shimmer]")
{
    for (const auto sizeAmount : { 0.0f, 0.5f, 1.0f })
    {
        ShimmerReverb reverb;
        reverb.prepare ({ sr, blockSize });
        reverb.setDecaySeconds (2.0f);
        reverb.setShimmer (0.0f);
        reverb.setSize (sizeAmount);
        reverb.reset();

        std::vector<float> input (static_cast<std::size_t> (sr * 2.0), 0.0f);
        const auto burst = noise (static_cast<std::size_t> (sr * 0.05), 0.5f);
        std::copy (burst.begin(), burst.end(), input.begin());

        const auto out = render (reverb, input);
        const auto drop = db (rms (slice (out.left, 0.4, 0.5)) / rms (slice (out.left, 1.4, 1.5)));

        INFO ("Taille " << sizeAmount << " : " << drop << " dB en 1 s (T60 de 2 s : 30 dB attendus)");
        REQUIRE (drop > 22.0);
        REQUIRE (drop < 38.0);
    }
}

TEST_CASE ("SHIMMER : le pré-délai retarde l'arrivée de la réverbe, au même niveau", "[shimmer]")
{
    const auto firstArrival = [] (float preDelayMs, double& level)
    {
        ShimmerReverb reverb;
        reverb.prepare ({ sr, blockSize });
        reverb.setShimmer (0.0f);
        reverb.setPreDelayMs (preDelayMs);
        reverb.reset();

        std::vector<float> input (static_cast<std::size_t> (sr * 1.5), 0.0f);
        input[0] = 1.0f;
        const auto out = render (reverb, input);
        level = rms (slice (out.left, 0.0, 1.5));

        for (std::size_t i = 0; i < out.left.size(); ++i)
            if (std::abs (out.left[i]) > 1.0e-4f)
                return static_cast<double> (i) * 1000.0 / sr;

        return -1.0;
    };

    double levelWithout = 0.0, levelWith = 0.0;
    const auto without = firstArrival (0.0f, levelWithout);
    const auto with = firstArrival (200.0f, levelWith);

    INFO ("Première arrivée : " << without << " ms sans pré-délai, " << with << " ms avec 200 ms");
    REQUIRE (with - without == Catch::Approx (200.0).margin (0.1));
    REQUIRE (levelWith == Catch::Approx (levelWithout).epsilon (0.02));
}

TEST_CASE ("SHIMMER : changer le pré-délai en jouant passe par un fondu, sans saut", "[shimmer]")
{
    // Une sinusoïde grave et une réverbe très diffuse : la sortie est lisse. Un changement de
    // lecture brutal y laisserait une marche ; le fondu enchaîné ne doit pas en laisser.
    const auto largestStep = [] (bool moving)
    {
        ShimmerReverb reverb;
        reverb.prepare ({ sr, blockSize });
        reverb.setShimmer (0.0f);
        reverb.setToneHz (2000.0f);
        reverb.reset();

        const auto input = sine (110.0, sr, static_cast<int> (sr * 3.0), 0.5f);
        Stereo out { std::vector<float> (input.size()), std::vector<float> (input.size()) };

        for (std::size_t start = 0, block = 0; start < input.size(); start += blockSize, ++block)
        {
            if (moving)
                reverb.setPreDelayMs ((block / 40) % 2 == 1 ? 330.0f : 20.0f);

            reverb.process (std::span<const float> { input.data() + start, blockSize },
                            std::span<float> { out.left.data() + start, blockSize },
                            std::span<float> { out.right.data() + start, blockSize });
        }

        float step = 0.0f;

        for (std::size_t i = static_cast<std::size_t> (sr) + 1; i < out.left.size(); ++i)
            step = std::max (step, std::abs (out.left[i] - out.left[i - 1]));

        return static_cast<double> (step);
    };

    const auto steady = largestStep (false);
    const auto moving = largestStep (true);
    INFO ("Plus grand écart entre échantillons : " << steady << " fixe, " << moving << " en bougeant");
    REQUIRE (moving < 1.5 * steady);
}

TEST_CASE ("SHIMMER : le mode double ajoute la transposition vers le bas", "[shimmer]")
{
    const auto tailAt = [] (ShimmerReverb::PitchMode mode, double frequency)
    {
        ShimmerReverb reverb;
        reverb.prepare ({ sr, blockSize });
        reverb.setDecaySeconds (6.0f);
        reverb.setInterval (12.0f);
        reverb.setShimmer (0.8f);
        reverb.setPitchMode (mode);
        reverb.reset();

        auto input = sine (440.0, sr, static_cast<int> (sr * 3.0), 0.3f);
        std::fill (input.begin() + static_cast<std::ptrdiff_t> (sr), input.end(), 0.0f);

        const auto out = render (reverb, input);
        return magnitudeAt (slice (out.left, 1.5, 2.5), frequency, sr);
    };

    const auto singleDown = tailAt (ShimmerReverb::PitchMode::single, 220.0);
    const auto dualDown = tailAt (ShimmerReverb::PitchMode::dual, 220.0);
    const auto dualUp = tailAt (ShimmerReverb::PitchMode::dual, 880.0);
    const auto dualSource = tailAt (ShimmerReverb::PitchMode::dual, 440.0);

    INFO ("220 Hz : " << singleDown << " en simple, " << dualDown << " en double ; 880 Hz en double : " << dualUp);
    REQUIRE (dualDown > 10.0 * singleDown);
    REQUIRE (dualUp > 0.1 * dualSource); // la montée reste là
}

TEST_CASE ("SHIMMER : la largeur resserre la stéréo jusqu'au mono", "[shimmer]")
{
    ShimmerReverb reverb;
    reverb.prepare ({ sr, blockSize });
    reverb.setShimmer (0.0f);
    reverb.setWidth (0.0f);
    reverb.reset();

    const auto out = render (reverb, noise (static_cast<std::size_t> (sr * 1.0), 0.3f));

    for (std::size_t i = 0; i < out.left.size(); ++i)
        REQUIRE (out.left[i] == Catch::Approx (out.right[i]).margin (1.0e-6));

    REQUIRE (rms (slice (out.left, 0.3, 1.0)) > 0.01);
}

TEST_CASE ("SHIMMER : le coupe-bas retire le grave de la nappe", "[shimmer]")
{
    const auto lowAt = [] (float lowCutHz)
    {
        ShimmerReverb reverb;
        reverb.prepare ({ sr, blockSize });
        reverb.setShimmer (0.0f);
        reverb.setLowCutHz (lowCutHz);
        reverb.reset();

        const auto out = render (reverb, sine (100.0, sr, static_cast<int> (sr * 2.0), 0.3f));
        return magnitudeAt (slice (out.left, 1.0, 2.0), 100.0, sr);
    };

    const auto open = lowAt (20.0f);
    const auto cut = lowAt (1000.0f);
    INFO ("100 Hz : " << db (open) << " dB coupe-bas à 20 Hz, " << db (cut) << " dB à 1 kHz");
    REQUIRE (cut < open * 0.05); // -26 dB (deux filtres d'ordre 2 à une décade)
}

TEST_CASE ("SHIMMER : la modulation élargit la raie d'une note tenue", "[shimmer]")
{
    // Sans modulation, la réverbe est linéaire et invariante : une sinusoïde en sort pure. La
    // modulation fait légèrement varier la hauteur de la queue (effet chorus).
    const auto purity = [] (float depth, float rate)
    {
        ShimmerReverb reverb;
        reverb.prepare ({ sr, blockSize });
        reverb.setShimmer (0.0f);
        reverb.setDecaySeconds (4.0f);
        reverb.setModulationDepth (depth);
        reverb.setModulationRate (rate);
        reverb.reset();

        const auto out = render (reverb, sine (1000.0, sr, static_cast<int> (sr * 3.0), 0.3f));
        return fractionNear (powerSpectrum (slice (out.left, 1.5, 3.0)), 1000.0, 3.0);
    };

    const auto still = purity (0.0f, 0.4f);
    const auto moving = purity (1.0f, 5.0f);
    INFO ("Part de l'énergie à ±3 Hz de 1 kHz : " << still << " sans modulation, " << moving << " au maximum");
    REQUIRE (still > 0.95);
    REQUIRE (moving < 0.8);
}

TEST_CASE ("SHIMMER : sans diffusion, l'attaque garde des échos nets", "[shimmer]")
{
    // Facteur de crête des 80 premières ms de la réponse à une impulsion : pics isolés sans
    // diffusion, matière fondue avec.
    const auto crest = [] (float diffusionAmount)
    {
        ShimmerReverb reverb;
        reverb.prepare ({ sr, blockSize });
        reverb.setShimmer (0.0f);
        reverb.setDiffusion (diffusionAmount);
        reverb.reset();

        std::vector<float> input (static_cast<std::size_t> (sr * 0.2), 0.0f);
        input[0] = 1.0f;
        const auto out = render (reverb, input);
        const auto early = slice (out.left, 0.0, 0.08);
        double peak = 0.0;

        for (const auto s : early)
            peak = std::max (peak, static_cast<double> (std::abs (s)));

        return peak / rms (early);
    };

    const auto sharp = crest (0.0f);
    const auto smooth = crest (1.0f);
    INFO ("Facteur de crête : " << sharp << " sans diffusion, " << smooth << " au maximum");
    REQUIRE (sharp > 1.5 * smooth);
}

TEST_CASE ("SHIMMER : réglages par défaut identiques au son d'origine, à la fréquence près", "[shimmer]")
{
    // Les réglages ajoutés ont des valeurs par défaut qui redonnent le son d'avant : un preset
    // ancien ne change pas. Même réglage explicite ou implicite : même sortie.
    const auto renderWith = [] (bool explicitDefaults)
    {
        ShimmerReverb reverb;
        reverb.prepare ({ sr, blockSize });
        reverb.setShimmer (0.5f);

        if (explicitDefaults)
        {
            reverb.setSize (0.5f);
            reverb.setDiffusion (0.8f);
            reverb.setModulationDepth (0.3f);
            reverb.setModulationRate (0.4f);
            reverb.setLowCutHz (50.0f);
            reverb.setPreDelayMs (0.0f);
            reverb.setWidth (1.0f);
            reverb.setPitchMode (ShimmerReverb::PitchMode::single);
        }

        reverb.reset();
        return render (reverb, pluckedString (290, sr, static_cast<int> (sr * 1.0), 0.5f));
    };

    const auto implicit = renderWith (false);
    const auto explicitly = renderWith (true);

    // Seuls les arrondis de la conversion des réglages diffèrent : écart sous -60 dB.
    std::vector<float> difference (implicit.left.size());

    for (std::size_t i = 0; i < difference.size(); ++i)
        difference[i] = explicitly.left[i] - implicit.left[i];

    const auto ratio = rms (difference) / rms (implicit.left);
    INFO ("Écart : " << db (ratio) << " dB");
    REQUIRE (ratio < 1.0e-3);
}

//==============================================================================
TEST_CASE ("FREEZE : silence tant que rien n'est figé", "[freeze]")
{
    SpectralFreeze freeze;
    freeze.prepare ({ sr, blockSize });

    const auto out = render (freeze, sine (220.0, sr, 24000));
    REQUIRE (peakAbs (out.left) == 0.0);
    REQUIRE_FALSE (freeze.isFrozen());
}

TEST_CASE ("FREEZE : la note figée continue, au même niveau, après l'arrêt du jeu", "[freeze]")
{
    SpectralFreeze freeze;
    freeze.prepare ({ sr, blockSize });
    freeze.setAttackMilliseconds (50.0f);

    // 0,5 s de note, on fige, puis 2 s de silence en entrée.
    const auto note = sine (220.0, sr, static_cast<int> (sr * 0.5), 0.3f);
    render (freeze, note);

    freeze.setHold (true);
    const auto out = render (freeze, std::vector<float> (static_cast<std::size_t> (sr * 2.0), 0.0f));

    REQUIRE (freeze.isFrozen());

    const auto frozen = slice (out.left, 1.0, 2.0);
    const auto levelDb = db (rms (frozen) / rms (std::span<const float> { note }));
    INFO ("Niveau de la nappe : " << levelDb << " dB par rapport à la note");
    REQUIRE (std::abs (levelDb) < 6.0);

    // La hauteur figée est celle de la note.
    REQUIRE (magnitudeAt (frozen, 220.0, sr) > 5.0 * magnitudeAt (frozen, 330.0, sr));

    // Nappe large : gauche et droite tirées au hasard séparément.
    REQUIRE (std::abs (correlation (frozen, slice (out.right, 1.0, 2.0))) < 0.6);
}

TEST_CASE ("FREEZE : libérer éteint la nappe en fondu", "[freeze]")
{
    SpectralFreeze freeze;
    freeze.prepare ({ sr, blockSize });
    freeze.setAttackMilliseconds (10.0f);
    freeze.setReleaseMilliseconds (200.0f);

    render (freeze, sine (330.0, sr, 24000, 0.3f));
    freeze.setHold (true);
    render (freeze, std::vector<float> (24000, 0.0f));

    freeze.setHold (false);
    const auto out = render (freeze, std::vector<float> (static_cast<std::size_t> (sr), 0.0f));

    REQUIRE (rms (slice (out.left, 0.0, 0.05)) > 0.01);   // encore audible au début du fondu
    REQUIRE (rms (slice (out.left, 0.6, 1.0)) < 1.0e-6);  // éteinte ensuite
}

//==============================================================================
TEST_CASE ("POLY : transpose les accords d'une octave, avec une latence de texture", "[poly]")
{
    PolyShifter poly;
    poly.prepare ({ sr, blockSize });
    poly.setInterval (0, 12.0f);
    poly.setVoiceLevel (0, 1.0f);
    poly.setVoiceLevel (1, 0.0f);
    poly.setWidth (0.0f);
    poly.reset();

    // Accord : La 220 + Do# 277 + Mi 330
    auto chord = sine (220.0, sr, static_cast<int> (sr * 2.0), 0.15f);
    const auto third = sine (277.18, sr, static_cast<int> (sr * 2.0), 0.15f);
    const auto fifth = sine (329.63, sr, static_cast<int> (sr * 2.0), 0.15f);

    for (std::size_t i = 0; i < chord.size(); ++i)
        chord[i] += third[i] + fifth[i];

    const auto out = render (poly, chord);
    const auto steady = slice (out.left, 1.0, 2.0);

    for (const auto f : { 440.0, 554.37, 659.26 })
    {
        INFO ("Composante transposée à " << f << " Hz");
        REQUIRE (magnitudeAt (steady, f, sr) > 4.0 * magnitudeAt (steady, f / 2.0, sr));
    }

    REQUIRE (poly.latencySamples() > static_cast<int> (0.05 * sr));
    REQUIRE (poly.latencySamples() < static_cast<int> (0.3 * sr));
}

TEST_CASE ("POLY : les deux voix sont placées de part et d'autre", "[poly]")
{
    PolyShifter poly;
    poly.prepare ({ sr, blockSize });
    poly.setInterval (0, 7.0f);
    poly.setVoiceLevel (0, 1.0f);
    poly.setVoiceLevel (1, 0.0f);
    poly.setWidth (1.0f);
    poly.reset();

    const auto out = render (poly, sine (220.0, sr, static_cast<int> (sr * 1.5), 0.3f));

    // Voix 1 seule, largeur maximale : tout à gauche.
    REQUIRE (rms (slice (out.left, 0.8, 1.5)) > 10.0 * rms (slice (out.right, 0.8, 1.5)) + 1.0e-6);
}

//==============================================================================
TEST_CASE ("GRANULAR nuage : les grains sont transposés", "[granular]")
{
    GranularCloud cloud;
    cloud.prepare ({ sr, blockSize });
    cloud.setMode (GranularCloud::Mode::cloud);
    cloud.setPitchSemitones (12.0f);
    cloud.setSpread (0.0f);
    cloud.setReverseProbability (0.0f);
    cloud.setGrainMilliseconds (100.0f);
    cloud.setDensity (20.0f);
    cloud.reset();

    const auto out = render (cloud, sine (220.0, sr, static_cast<int> (sr * 2.0), 0.3f));
    const auto steady = slice (out.left, 1.0, 2.0);

    REQUIRE (magnitudeAt (steady, 440.0, sr) > 0.05);
    REQUIRE (magnitudeAt (steady, 440.0, sr) > 10.0 * magnitudeAt (steady, 220.0, sr));
    REQUIRE (cloud.grainsStarted() == Catch::Approx (40).margin (2));
}

TEST_CASE ("GRANULAR stutter : découpes au tempo, tranche répétée N fois", "[granular]")
{
    GranularCloud stutter;
    stutter.prepare ({ sr, blockSize });
    stutter.setMode (GranularCloud::Mode::stutter);
    stutter.setTempo (120.0);
    stutter.setDivisionBeats (0.5); // croche à 120 BPM = 250 ms
    stutter.setRepeats (4);
    stutter.setSpread (0.0f);
    stutter.reset();

    render (stutter, sine (220.0, sr, static_cast<int> (sr * 4.0), 0.3f));

    REQUIRE (stutter.grainsStarted() == Catch::Approx (16).margin (1));
    REQUIRE (stutter.slicesCaptured() == Catch::Approx (4).margin (1));
}

TEST_CASE ("GRANULAR stutter : calé sur la grille du DAW en lecture", "[granular]")
{
    GranularCloud stutter;
    stutter.prepare ({ sr, blockSize });
    stutter.setMode (GranularCloud::Mode::stutter);
    stutter.setTempo (120.0);
    stutter.setDivisionBeats (1.0);
    stutter.reset();

    // Le DAW est au temps 0,9 : la prochaine noire tombe 0,1 temps plus tard (50 ms).
    stutter.setTransportPosition (0.9);
    std::vector<float> input (4800, 0.1f);
    Stereo out { std::vector<float> (4800), std::vector<float> (4800) };
    stutter.process (input, out.left, out.right);

    // Au premier bloc, la grille se cale (déclenchement immédiat), puis la noire suivante tombe à 50 ms.
    REQUIRE (stutter.lastTriggerSample() == Catch::Approx (2400).margin (1));
}

//==============================================================================
TEST_CASE ("ChainSlot (texture) : arrêtée, la texture laisse finir sa queue puis ne coûte plus rien", "[slot]")
{
    ShimmerReverb reverb;
    ChainSlot slot (reverb);
    slot.prepare ({ sr, blockSize });
    reverb.setDecaySeconds (0.5f);
    reverb.setShimmer (0.0f);
    slot.setMix (1.0f);

    std::vector<float> left (blockSize), right (blockSize);
    const auto block = sine (220.0, sr, blockSize, 0.3f);
    const auto play = [&]
    {
        std::copy (block.begin(), block.end(), left.begin());
        std::copy (block.begin(), block.end(), right.begin());
        slot.process (left, right);
    };

    // Arrêtée dès le départ : le signal passe intact.
    play();
    REQUIRE (slot.isIdle());
    REQUIRE (left == block);

    // En marche, MIX 100 % : seule la réverbe sort.
    slot.setEnabled (true);

    for (int i = 0; i < 200; ++i)
        play();

    REQUIRE (peakAbs (left) > 0.01);

    // Arrêt : le jeu repasse en direct et la queue continue un moment par-dessus...
    slot.setEnabled (false);

    for (int i = 0; i < 20; ++i)
        play();

    std::vector<float> tailLeft (blockSize);
    std::transform (left.begin(), left.end(), block.begin(), tailLeft.begin(), std::minus<float>());
    REQUIRE (peakAbs (tailLeft) > 0.0);
    REQUIRE_FALSE (slot.isIdle());

    // ...puis, une fois éteinte, la texture n'est plus calculée et le signal est intact.
    for (int i = 0; i < 2000; ++i)
        play();

    REQUIRE (slot.isIdle());
    REQUIRE (left == block);
}

TEST_CASE ("ChainSlot (texture) : sortie = sec × (1 − MIX) + texture × MIX, en stéréo", "[slot]")
{
    // Texture de test : renvoie +1 à gauche, -1 à droite.
    struct Constant final : StereoTexture
    {
        void prepare (const ProcessSpec&) override {}
        void reset() noexcept override {}
        void process (std::span<const float>, std::span<float> l, std::span<float> r) noexcept override
        {
            std::fill (l.begin(), l.end(), 1.0f);
            std::fill (r.begin(), r.end(), -1.0f);
        }
    };

    Constant constant;
    ChainSlot slot (constant);
    slot.prepare ({ sr, blockSize });
    slot.setMix (0.25f);
    slot.setEnabled (true);
    slot.reset();

    std::vector<float> left (blockSize, 0.4f), right (blockSize, 0.4f);

    for (int i = 0; i < 20; ++i)
    {
        std::fill (left.begin(), left.end(), 0.4f);
        std::fill (right.begin(), right.end(), 0.4f);
        slot.process (left, right);
    }

    REQUIRE (left.back() == Catch::Approx (0.4f * 0.75f + 0.25f));
    REQUIRE (right.back() == Catch::Approx (0.4f * 0.75f - 0.25f));

    // FREEZE : la nappe s'ajoute au jeu, qui n'est jamais atténué.
    slot.setWetAddsToDry (true);
    std::fill (left.begin(), left.end(), 0.4f);
    std::fill (right.begin(), right.end(), 0.4f);
    slot.process (left, right);

    REQUIRE (left.back() == Catch::Approx (0.4f + 0.25f));
    REQUIRE (right.back() == Catch::Approx (0.4f - 0.25f));
}
