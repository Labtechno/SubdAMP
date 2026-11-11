#include "TestSignals.h"

#include "subdamp/dsp/DelayLineShifter.h"
#include "subdamp/dsp/OnsetDetector.h"
#include "subdamp/dsp/Whammy.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace subdamp::dsp;
using namespace subdamp::test;

namespace
{
    constexpr double sr = 48000.0;
    constexpr int blockSize = 64;

    std::span<const float> lastHalf (const std::vector<float>& signal)
    {
        return { signal.data() + signal.size() / 2, signal.size() / 2 };
    }

    /** Fréquence où l'énergie est maximale, cherchée par pas de 1 Hz autour d'une estimation. */
    double dominantFrequencyNear (std::span<const float> signal, double guess)
    {
        double best = guess, bestMagnitude = 0.0;

        for (auto f = guess * 0.9; f <= guess * 1.1; f += 1.0)
            if (const auto m = magnitudeAt (signal, f, sr); m > bestMagnitude)
            {
                bestMagnitude = m;
                best = f;
            }

        return best;
    }
}

//==============================================================================
TEST_CASE ("DelayLineShifter : transpose d'un intervalle donné", "[shifter]")
{
    const auto semitones = GENERATE (-24.0f, -12.0f, -7.0f, 5.0f, 12.0f, 24.0f);

    DelayLineShifter shifter;
    shifter.prepare (sr);

    auto signal = sine (220.0, sr, 48000);
    const auto ratio = std::exp2 (semitones / 12.0f);

    for (auto& s : signal)
        s = shifter.process (s, ratio);

    const auto expected = 220.0 * ratio;
    INFO ("Transposition : " << semitones << " demi-tons");
    REQUIRE (dominantFrequencyNear (lastHalf (signal), expected) == Catch::Approx (expected).margin (2.0));

    // L'essentiel de l'énergie est bien à la nouvelle hauteur, pas à l'ancienne.
    REQUIRE (magnitudeAt (lastHalf (signal), expected, sr) > 4.0 * magnitudeAt (lastHalf (signal), 220.0, sr));
}

TEST_CASE ("DelayLineShifter : latence moyenne d'une demi-fenêtre", "[shifter]")
{
    DelayLineShifter shifter;
    shifter.prepare (sr, 0.03);
    REQUIRE (shifter.averageLatencySamples() == Catch::Approx (722).margin (2)); // 15 ms
}

//==============================================================================
TEST_CASE ("OnsetDetector : une attaque par note pincée, aucune pendant la tenue", "[onset]")
{
    OnsetDetector onset;
    onset.prepare (sr);
    onset.setSensitivity (0.5f);

    // Trois notes pincées à 0, 0,5 et 1 s, puis 1 s de silence.
    std::vector<float> signal (static_cast<std::size_t> (sr * 2), 0.0f);

    for (int note = 0; note < 3; ++note)
    {
        const auto pluck = pluckedString (200 + 40 * note, sr, static_cast<int> (sr * 0.5), 0.5f, static_cast<unsigned> (note + 1));
        std::copy (pluck.begin(), pluck.end(), signal.begin() + static_cast<std::ptrdiff_t> (note * sr * 0.5));
    }

    std::vector<std::size_t> onsets;

    for (std::size_t i = 0; i < signal.size(); ++i)
        if (onset.process (signal[i]))
            onsets.push_back (i);

    REQUIRE (onsets.size() == 3);

    for (std::size_t note = 0; note < 3; ++note)
    {
        const auto expected = static_cast<double> (note) * sr * 0.5;
        REQUIRE (static_cast<double> (onsets[note]) == Catch::Approx (expected).margin (sr * 0.005)); // à 5 ms près
    }
}

TEST_CASE ("OnsetDetector : le bruit de fond ne déclenche rien", "[onset]")
{
    OnsetDetector onset;
    onset.prepare (sr);
    onset.setSensitivity (1.0f);

    auto noise = pluckedString (100, sr, 48000, 0.001f); // ≈ -60 dBFS
    int count = 0;

    for (const auto s : noise)
        count += onset.process (s) ? 1 : 0;

    REQUIRE (count == 0);
}

//==============================================================================
TEST_CASE ("WHAMMY fixe : transpose à l'intervalle du potard", "[whammy]")
{
    Whammy whammy;
    whammy.prepare ({ sr, blockSize });
    whammy.setInterval (12.0f);
    whammy.setMix (1.0f);
    whammy.reset();

    auto signal = sine (220.0, sr, 48000);
    processInBlocks (whammy, signal, blockSize);

    REQUIRE (whammy.getCurrentSemitones() == 12.0f);
    REQUIRE (magnitudeAt (lastHalf (signal), 440.0, sr) > 0.3);
}

TEST_CASE ("WHAMMY à l'unisson : le son direct passe intact, sans latence", "[whammy]")
{
    Whammy whammy;
    whammy.prepare ({ sr, blockSize });
    whammy.setInterval (0.0f);
    whammy.setMix (1.0f);
    whammy.reset();

    auto signal = sine (220.0, sr, 4800);
    const auto original = signal;
    processInBlocks (whammy, signal, blockSize);

    REQUIRE (signal == original);
}

TEST_CASE ("WHAMMY mix à 0 : le son direct passe intact", "[whammy]")
{
    Whammy whammy;
    whammy.prepare ({ sr, blockSize });
    whammy.setInterval (-12.0f);
    whammy.setMix (0.0f);
    whammy.reset();

    auto signal = sine (220.0, sr, 4800);
    const auto original = signal;
    processInBlocks (whammy, signal, blockSize);

    REQUIRE (signal == original);
}

TEST_CASE ("WHAMMY à l'attaque : chaque attaque relance le glissement vers l'intervalle", "[whammy]")
{
    Whammy whammy;
    whammy.prepare ({ sr, blockSize });
    whammy.setInterval (-24.0f);
    whammy.setMode (Whammy::Mode::attackToInterval);
    whammy.setGlideMilliseconds (200.0f);
    whammy.setSensitivity (0.5f);
    whammy.setMix (1.0f);
    whammy.reset();

    // Avant toute attaque : position de fin de glissement.
    std::vector<float> silence (480, 0.0f);
    processInBlocks (whammy, silence, blockSize);
    REQUIRE (whammy.getCurrentSemitones() == -24.0f);

    auto note = pluckedString (220, sr, static_cast<int> (sr * 0.6));

    // 100 ms après l'attaque : à mi-chemin (glissement de 200 ms).
    std::vector<float> start (note.begin(), note.begin() + 4800);
    processInBlocks (whammy, start, blockSize);
    REQUIRE (whammy.getCurrentSemitones() == Catch::Approx (-12.0f).margin (0.5f));

    // Après 300 ms : arrivé à l'intervalle, et il y reste pendant la tenue de la note.
    std::vector<float> rest (note.begin() + 4800, note.end());
    processInBlocks (whammy, rest, blockSize);
    REQUIRE (whammy.getCurrentSemitones() == -24.0f);

    // Nouvelle attaque : le glissement repart de la note jouée.
    auto second = pluckedString (180, sr, 960, 0.5f, 9);
    processInBlocks (whammy, second, blockSize);
    REQUIRE (whammy.getCurrentSemitones() > -3.0f);
}

TEST_CASE ("WHAMMY à l'attaque depuis l'intervalle : la note arrive en glissant puis redevient directe", "[whammy]")
{
    Whammy whammy;
    whammy.prepare ({ sr, blockSize });
    whammy.setInterval (-5.0f);
    whammy.setMode (Whammy::Mode::attackFromInterval);
    whammy.setGlideMilliseconds (100.0f);
    whammy.setMix (1.0f);
    whammy.reset();

    auto note = pluckedString (220, sr, static_cast<int> (sr * 0.5));
    std::vector<float> attack (note.begin(), note.begin() + 96);
    processInBlocks (whammy, attack, blockSize);
    REQUIRE (whammy.getCurrentSemitones() < -4.5f);

    std::vector<float> rest (note.begin() + 96, note.end());
    const std::vector<float> original (rest);
    processInBlocks (whammy, rest, blockSize);

    REQUIRE (whammy.getCurrentSemitones() == 0.0f);

    // Une fois arrivé, le signal est exactement le son direct.
    for (std::size_t i = rest.size() - 4800; i < rest.size(); ++i)
        REQUIRE (rest[i] == original[i]);
}
