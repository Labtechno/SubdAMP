#include "TestSignals.h"

#include "subdamp/dsp/Fft.h"
#include "subdamp/dsp/GranularCloud.h"
#include "subdamp/dsp/PolyShifter.h"
#include "subdamp/dsp/ShimmerReverb.h"
#include "subdamp/dsp/SpectralFreeze.h"
#include "subdamp/dsp/StereoTexture.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <complex>
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

TEST_CASE ("SHIMMER : même au maximum, la boucle reste bornée", "[shimmer]")
{
    ShimmerReverb reverb;
    reverb.prepare ({ sr, blockSize });
    reverb.setDecaySeconds (30.0f);
    reverb.setShimmer (1.0f);
    reverb.setToneHz (16000.0f);
    reverb.reset();

    const auto out = render (reverb, noise (static_cast<std::size_t> (sr * 8.0), 0.5f));

    REQUIRE (std::all_of (out.left.begin(), out.left.end(), [] (float s) { return std::isfinite (s); }));
    REQUIRE (peakAbs (out.left) < 10.0);
    REQUIRE (peakAbs (out.right) < 10.0);
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
TEST_CASE ("TextureSlot : arrêtée, la texture laisse finir sa queue puis ne coûte plus rien", "[texture-slot]")
{
    ShimmerReverb reverb;
    TextureSlot slot (reverb);
    slot.prepare ({ sr, blockSize });
    reverb.setDecaySeconds (0.5f);
    reverb.setShimmer (0.0f);
    slot.setLevel (1.0f);

    std::vector<float> left (blockSize), right (blockSize);
    const auto block = sine (220.0, sr, blockSize, 0.3f);

    // Arrêtée dès le départ : aucun effet.
    slot.process (block, left, right);
    REQUIRE (slot.isSleeping());
    REQUIRE (peakAbs (left) == 0.0);

    // En marche : la réverbe s'ajoute.
    slot.setEnabled (true);

    for (int i = 0; i < 200; ++i)
    {
        std::fill (left.begin(), left.end(), 0.0f);
        slot.process (block, left, right);
    }

    REQUIRE (peakAbs (left) > 0.01);

    // Arrêt : la queue continue un moment...
    slot.setEnabled (false);
    std::fill (left.begin(), left.end(), 0.0f);

    for (int i = 0; i < 20; ++i)
        slot.process (block, left, right);

    REQUIRE (peakAbs (left) > 0.0);
    REQUIRE_FALSE (slot.isSleeping());

    // ...puis, une fois éteinte, la texture n'est plus calculée.
    for (int i = 0; i < 2000; ++i)
        slot.process (block, left, right);

    REQUIRE (slot.isSleeping());
}
