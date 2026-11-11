#include "TestSignals.h"

#include "subdamp/dsp/Harmonizer.h"
#include "subdamp/dsp/PitchTracker.h"
#include "subdamp/dsp/ScaleHarmony.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace subdamp::dsp;
using namespace subdamp::test;

namespace
{
    constexpr int blockSize = 64;

    /** Fait tourner le détecteur sur tout le signal et renvoie la dernière fréquence détectée (0 si aucune). */
    double trackedFrequency (const std::vector<float>& signal, double sampleRate)
    {
        PitchTracker tracker;
        tracker.prepare (sampleRate);

        for (const auto s : signal)
            tracker.push (s);

        return tracker.isVoiced() ? sampleRate / tracker.getPeriod() : 0.0;
    }

    double cents (double measured, double expected)
    {
        return 1200.0 * std::log2 (measured / expected);
    }

    /** Signal produit par l'harmonizer seul (son direct coupé). */
    std::vector<float> harmonyOnly (std::vector<float> input, double sampleRate, int key, Scale scale,
                                    int steps, float formantSemitones = 0.0f)
    {
        Harmonizer harmonizer;
        harmonizer.prepare ({ sampleRate, blockSize });
        harmonizer.setKey (key);
        harmonizer.setScale (scale);
        harmonizer.setVoiceInterval (0, steps);
        harmonizer.setVoiceInterval (1, std::nullopt);
        harmonizer.setVoiceLevel (0, 1.0f);
        harmonizer.setDirectLevel (0.0f);
        harmonizer.setFormantSemitones (formantSemitones);
        harmonizer.reset();

        processInBlocks (harmonizer, input, blockSize);
        return input;
    }

    double midiToHz (double midi) { return 440.0 * std::exp2 ((midi - 69.0) / 12.0); }
}

//==============================================================================
TEST_CASE ("Théorie : intervalles diatoniques", "[scale]")
{
    // Mi mineur (Mi = tonique) : Mi→Sol (+3), Fa#→La (+3), Sol→Si (+4)
    REQUIRE (diatonicShift (0, Scale::naturalMinor, 2) == 3);
    REQUIRE (diatonicShift (2, Scale::naturalMinor, 2) == 3);
    REQUIRE (diatonicShift (3, Scale::naturalMinor, 2) == 4);

    // Do majeur : quinte au-dessus de Si = Fa (quinte diminuée, +6) ; Do→Do une octave au-dessus
    REQUIRE (diatonicShift (11, Scale::major, 4) == 6);
    REQUIRE (diatonicShift (0, Scale::major, 7) == 12);

    // Vers le grave : tierce en dessous de Mi en Mi mineur = Do (-4)
    REQUIRE (diatonicShift (0, Scale::naturalMinor, -2) == -4);
    REQUIRE (diatonicShift (0, Scale::naturalMinor, -14) == -24);

    // Phrygien dominant (metal) : tierce au-dessus de la tonique = tierce majeure
    REQUIRE (diatonicShift (0, Scale::phrygianDominant, 2) == 4);

    // Note hors gamme (Do# en Do majeur) : même écart que Do (tierce majeure, +4)
    REQUIRE (diatonicShift (1, Scale::major, 2) == 4);

    // Parallèle : mêmes intervalles quelle que soit la note
    REQUIRE (diatonicShift (5, Scale::parallel, 4) == 7);
    REQUIRE (diatonicShift (9, Scale::parallel, -4) == -7);
    REQUIRE (diatonicShift (3, Scale::parallel, 7) == 12);
}

//==============================================================================
TEST_CASE ("PitchTracker : notes de guitare, du drop C au Mi aigu", "[tracker]")
{
    const auto sampleRate = GENERATE (44100.0, 48000.0, 96000.0);
    const auto frequency = GENERATE (65.41, 82.41, 110.0, 196.0, 329.63, 659.26, 1318.5);

    const auto signal = sawtooth (frequency, sampleRate, static_cast<int> (sampleRate * 0.3));
    const auto detected = trackedFrequency (signal, sampleRate);

    INFO (frequency << " Hz à " << sampleRate << " Hz");
    REQUIRE (detected > 0.0);
    REQUIRE (std::abs (cents (detected, frequency)) < 3.0);
}

TEST_CASE ("PitchTracker : corde pincée réaliste", "[tracker]")
{
    const auto delay = GENERATE (580, 435, 290, 145, 72); // ≈ 82 à 660 Hz à 48 kHz
    const auto signal = pluckedString (delay, 48000.0, 24000);
    const auto expected = pluckedStringFrequency (delay, 48000.0);

    INFO ("Corde à " << expected << " Hz");
    REQUIRE (std::abs (cents (trackedFrequency (signal, 48000.0), expected)) < 5.0);
}

TEST_CASE ("PitchTracker : le silence et le bruit ne sont pas des notes", "[tracker]")
{
    REQUIRE (trackedFrequency (std::vector<float> (48000, 0.0f), 48000.0) == 0.0);

    std::vector<float> noise (48000);
    unsigned state = 7;

    for (auto& s : noise)
    {
        state = state * 1664525u + 1013904223u;
        s = 0.3f * (static_cast<float> (state >> 8) / static_cast<float> (1u << 24) * 2.0f - 1.0f);
    }

    REQUIRE (trackedFrequency (noise, 48000.0) == 0.0);
}

//==============================================================================
TEST_CASE ("HARMONIZER : la voix joue l'intervalle diatonique, juste au centième", "[harmonizer]")
{
    struct Case { double inputMidi; int key; Scale scale; int steps; double expectedMidi; };

    const auto c = GENERATE (
        Case { 57.0, 0, Scale::major, 2, 60.0 },          // La → Do (tierce mineure en Do majeur)
        Case { 60.0, 0, Scale::major, 2, 64.0 },          // Do → Mi (tierce majeure)
        Case { 52.0, 4, Scale::naturalMinor, 4, 59.0 },   // Mi → Si (quinte, Mi mineur)
        Case { 64.0, 4, Scale::naturalMinor, 7, 76.0 },   // octave au-dessus
        Case { 64.0, 4, Scale::naturalMinor, -7, 52.0 },  // octave en dessous
        Case { 45.0, 4, Scale::naturalMinor, -2, 42.0 }); // La → Fa# en dessous (tierce mineure en Mi mineur)

    const auto inputHz = midiToHz (c.inputMidi);
    const auto expectedHz = midiToHz (c.expectedMidi);

    const auto harmony = harmonyOnly (sawtooth (inputHz, 48000.0, 48000), 48000.0, c.key, c.scale, c.steps);
    const std::vector<float> steady (harmony.begin() + 24000, harmony.end());

    INFO ("Note jouée " << inputHz << " Hz, harmonie attendue " << expectedHz << " Hz");
    REQUIRE (std::abs (cents (trackedFrequency (steady, 48000.0), expectedHz)) < 5.0);
}

TEST_CASE ("HARMONIZER : sur une corde pincée, l'harmonie suit la note", "[harmonizer]")
{
    // Corde à ≈ 164,7 Hz (Mi), tierce au-dessus en Mi mineur → Sol (+3)
    const auto input = pluckedString (291, 48000.0, 36000);
    const auto inputHz = pluckedStringFrequency (291, 48000.0);
    const auto harmony = harmonyOnly (input, 48000.0, 4, Scale::naturalMinor, 2);

    const std::vector<float> steady (harmony.begin() + 12000, harmony.begin() + 30000);
    REQUIRE (std::abs (cents (trackedFrequency (steady, 48000.0), inputHz * std::exp2 (3.0 / 12.0))) < 5.0);
}

TEST_CASE ("HARMONIZER : le niveau de la voix est proche de celui de la note jouée", "[harmonizer]")
{
    const auto steps = GENERATE (-7, -2, 2, 4, 7);

    const auto input = sawtooth (196.0, 48000.0, 48000);
    const auto harmony = harmonyOnly (input, 48000.0, 7, Scale::major, steps);

    const auto inputRms = rms (std::span<const float> { input.data() + 24000, 24000 });
    const auto harmonyRms = rms (std::span<const float> { harmony.data() + 24000, 24000 });
    const auto differenceDb = 20.0 * std::log10 (harmonyRms / inputRms);

    INFO ("Intervalle de " << steps << " degrés : écart " << differenceDb << " dB");
    REQUIRE (std::abs (differenceDb) < 4.0);
}

TEST_CASE ("HARMONIZER : le son direct passe sans retard ni modification", "[harmonizer]")
{
    Harmonizer harmonizer;
    harmonizer.prepare ({ 48000.0, blockSize });
    harmonizer.setVoiceInterval (0, std::nullopt);
    harmonizer.setVoiceInterval (1, std::nullopt);
    harmonizer.setDirectLevel (1.0f);
    harmonizer.reset();

    auto signal = sawtooth (110.0, 48000.0, 9600);
    const auto original = signal;
    processInBlocks (harmonizer, signal, blockSize);

    REQUIRE (signal == original);
}

TEST_CASE ("HARMONIZER : latence des voix entre 1 et 3 périodes de la note", "[harmonizer]")
{
    const auto frequency = GENERATE (82.41, 329.63);

    Harmonizer harmonizer;
    harmonizer.prepare ({ 48000.0, blockSize });
    harmonizer.setVoiceInterval (0, 4);
    harmonizer.setDirectLevel (1.0f);
    harmonizer.reset();

    auto signal = sawtooth (frequency, 48000.0, 24000);
    processInBlocks (harmonizer, signal, blockSize);

    const auto period = 48000.0 / frequency;
    INFO ("Note à " << frequency << " Hz : latence " << harmonizer.getLatencySamples() / 48.0 << " ms");
    REQUIRE (harmonizer.getLatencySamples() > period);
    REQUIRE (harmonizer.getLatencySamples() < 3.0 * period);
}

TEST_CASE ("HARMONIZER : le réglage de formants déplace le timbre, pas la hauteur", "[harmonizer]")
{
    // Son à formant marqué : harmoniques de 146,83 Hz (Ré) renforcées autour de 1 kHz.
    std::vector<float> input (48000, 0.0f);

    for (int k = 1; k * 146.83 < 20000.0; ++k)
    {
        const auto f = k * 146.83;
        const auto amplitude = 0.02 + 0.2 * std::exp (-std::pow ((f - 1000.0) / 250.0, 2.0));

        for (std::size_t n = 0; n < input.size(); ++n)
            input[n] += static_cast<float> (amplitude * std::sin (2.0 * std::numbers::pi * f * static_cast<double> (n) / 48000.0));
    }

    // Plus forte harmonique de la voix (quinte, 220 Hz) dans une bande de fréquences.
    const auto strongestIn = [] (const std::vector<float>& signal, double low, double high)
    {
        const std::span<const float> steady { signal.data() + 24000, 24000 };
        double strongest = 0.0;

        for (auto f = 220.0; f < high; f += 220.0)
            if (f >= low)
                strongest = std::max (strongest, magnitudeAt (steady, f, 48000.0));

        return strongest;
    };

    const auto neutral = harmonyOnly (input, 48000.0, 2, Scale::major, 4, 0.0f);
    const auto bright = harmonyOnly (input, 48000.0, 2, Scale::major, 4, 12.0f);

    // Sans réglage, le formant reste vers 1 kHz ; à +12, il monte vers 2 kHz.
    REQUIRE (strongestIn (neutral, 800.0, 1300.0) > 2.0 * strongestIn (neutral, 1800.0, 2300.0));
    REQUIRE (strongestIn (bright, 1800.0, 2300.0) > 2.0 * strongestIn (bright, 800.0, 1300.0));

    // La hauteur, elle, ne bouge pas : quinte juste au-dessus de Ré = La.
    const std::vector<float> brightSteady (bright.begin() + 24000, bright.end());
    REQUIRE (std::abs (cents (trackedFrequency (brightSteady, 48000.0), 220.0)) < 5.0);
}
