#include "TestSignals.h"

#include "subdamp/dsp/FrequencyShifter.h"
#include "subdamp/dsp/NoiseGate.h"
#include "subdamp/dsp/OctaveDivider.h"
#include "subdamp/dsp/ChainSlot.h"

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
}

//==============================================================================
TEST_CASE ("NoiseGate : un signal au-dessus du seuil passe intact", "[gate]")
{
    NoiseGate gate;
    gate.prepare ({ sr, blockSize });
    gate.setThresholdDecibels (-50.0f);

    auto signal = sine (220.0, sr, 24000, 0.3f);
    processInBlocks (gate, signal, blockSize);

    REQUIRE (gate.isOpen());
    REQUIRE (magnitudeAt (lastHalf (signal), 220.0, sr) == Catch::Approx (0.3).margin (0.01));
}

TEST_CASE ("NoiseGate : un bruit sous le seuil est coupé", "[gate]")
{
    NoiseGate gate;
    gate.prepare ({ sr, blockSize });
    gate.setThresholdDecibels (-50.0f);
    gate.setReleaseMilliseconds (20.0f);

    auto noise = sine (3000.0, sr, 24000, 0.0005f); // ≈ -66 dBFS
    processInBlocks (gate, noise, blockSize);

    REQUIRE_FALSE (gate.isOpen());
    REQUIRE (rms (lastHalf (noise)) < 1.0e-6);
}

TEST_CASE ("NoiseGate : la fin d'une note se ferme progressivement, sans coupure nette", "[gate]")
{
    NoiseGate gate;
    gate.prepare ({ sr, blockSize });
    gate.setThresholdDecibels (-40.0f);
    gate.setReleaseMilliseconds (100.0f);

    // Note de 0,5 s, puis une petite traîne sous le seuil, qui doit s'éteindre en fondu.
    auto signal = sine (220.0, sr, 96000, 0.3f);
    std::fill (signal.begin() + 24000, signal.end(), 0.001f);

    processInBlocks (gate, signal, blockSize);

    // 150 ms après l'arrêt (maintien de 50 ms + 100 ms de relâchement) : gain encore partiel
    REQUIRE (signal[24000 + 7200] > 0.0001f);
    REQUIRE (signal[24000 + 7200] < 0.001f);
    // Bien plus tard : complètement fermé
    REQUIRE (signal.back() < 1.0e-6f);
}

//==============================================================================
TEST_CASE ("SUB : produit l'octave inférieure d'une note", "[octave]")
{
    OctaveDivider sub;
    sub.prepare ({ sr, blockSize });
    sub.setToneFrequency (2000.0f);
    sub.setLevels (0.0f, 1.0f, 0.0f, 0.0f);

    auto signal = sine (110.0, sr, 48000); // La grave (corde de La à vide)
    processInBlocks (sub, signal, blockSize);

    const auto octaveDown = magnitudeAt (lastHalf (signal), 55.0, sr);
    const auto original = magnitudeAt (lastHalf (signal), 110.0, sr);

    REQUIRE (octaveDown > 0.1);
    REQUIRE (octaveDown > 3.0 * original);
}

TEST_CASE ("SUB : produit deux octaves en dessous", "[octave]")
{
    OctaveDivider sub;
    sub.prepare ({ sr, blockSize });
    sub.setToneFrequency (2000.0f);
    sub.setLevels (0.0f, 0.0f, 1.0f, 0.0f);

    auto signal = sine (164.81, sr, 96000); // Mi de la 4e corde
    processInBlocks (sub, signal, blockSize);

    REQUIRE (magnitudeAt (lastHalf (signal), 164.81 / 4.0, sr) > 0.05);
    REQUIRE (magnitudeAt (lastHalf (signal), 164.81 / 4.0, sr) > 2.0 * magnitudeAt (lastHalf (signal), 164.81, sr));
}

TEST_CASE ("SUB : l'octave haute double la fréquence", "[octave]")
{
    OctaveDivider sub;
    sub.prepare ({ sr, blockSize });
    sub.setToneFrequency (8000.0f);
    sub.setLevels (0.0f, 0.0f, 0.0f, 1.0f);

    auto signal = sine (220.0, sr, 48000);
    processInBlocks (sub, signal, blockSize);

    REQUIRE (magnitudeAt (lastHalf (signal), 440.0, sr) > 5.0 * magnitudeAt (lastHalf (signal), 220.0, sr));
}

TEST_CASE ("SUB : le silence ne déclenche aucune octave (pas de bruit de bascule)", "[octave]")
{
    OctaveDivider sub;
    sub.prepare ({ sr, blockSize });
    sub.setLevels (0.0f, 1.0f, 1.0f, 0.0f);

    auto noise = sine (5000.0, sr, 24000, 0.0001f);
    processInBlocks (sub, noise, blockSize);

    REQUIRE (rms (lastHalf (noise)) < 1.0e-5);
}

//==============================================================================
TEST_CASE ("FREQ SHIFT : décalage vers l'aigu", "[freqshift]")
{
    FrequencyShifter shifter;
    shifter.prepare ({ sr, blockSize });
    shifter.setShiftHz (200.0f);
    shifter.setMix (1.0f);

    auto signal = sine (1000.0, sr, 48000);
    processInBlocks (shifter, signal, blockSize);

    const auto shifted = magnitudeAt (lastHalf (signal), 1200.0, sr);
    REQUIRE (shifted == Catch::Approx (0.5).margin (0.02));
    REQUIRE (magnitudeAt (lastHalf (signal), 800.0, sr) < shifted * 0.01); // bande latérale inverse rejetée (< -40 dB)
    REQUIRE (magnitudeAt (lastHalf (signal), 1000.0, sr) < shifted * 0.01);
}

TEST_CASE ("FREQ SHIFT : décalage vers le grave", "[freqshift]")
{
    FrequencyShifter shifter;
    shifter.prepare ({ sr, blockSize });
    shifter.setShiftHz (-300.0f);
    shifter.setMix (1.0f);

    auto signal = sine (2000.0, sr, 48000);
    processInBlocks (shifter, signal, blockSize);

    const auto shifted = magnitudeAt (lastHalf (signal), 1700.0, sr);
    REQUIRE (shifted == Catch::Approx (0.5).margin (0.02));
    REQUIRE (magnitudeAt (lastHalf (signal), 2300.0, sr) < shifted * 0.01);
}

TEST_CASE ("FREQ SHIFT : la réinjection reste stable", "[freqshift]")
{
    FrequencyShifter shifter;
    shifter.prepare ({ sr, blockSize });
    shifter.setShiftHz (37.0f);
    shifter.setMix (1.0f);
    shifter.setFeedback (0.95f);

    auto signal = sine (440.0, sr, 4 * 48000);
    processInBlocks (shifter, signal, blockSize);

    for (const auto s : signal)
        REQUIRE (std::isfinite (s));

    REQUIRE (rms (lastHalf (signal)) < 5.0);
}

//==============================================================================
namespace
{
    /** Module de test : inverse la polarité, pour voir clairement le fondu et le mélange. */
    struct Inverter final : MonoEffect
    {
        void prepare (const ProcessSpec&) override {}
        void reset() noexcept override {}
        void process (std::span<float> block) noexcept override { for (auto& s : block) s = -s; }
        LatencyTier latencyTier() const noexcept override { return LatencyTier::live; }
    };

    /** Module de test : retarde le signal d'un nombre fixe d'échantillons. */
    struct Delay final : MonoEffect
    {
        explicit Delay (std::size_t samples) : line (samples, 0.0f) {}
        void prepare (const ProcessSpec&) override {}
        void reset() noexcept override { std::fill (line.begin(), line.end(), 0.0f); position = 0; }
        void process (std::span<float> block) noexcept override
        {
            for (auto& s : block)
            {
                std::swap (s, line[position]);
                position = (position + 1) % line.size();
            }
        }
        LatencyTier latencyTier() const noexcept override { return LatencyTier::live; }

        std::vector<float> line;
        std::size_t position = 0;
    };

    /** Traite un signal stéréo dans un emplacement, par blocs. */
    void processInBlocks (ChainSlot& slot, std::vector<float>& left, std::vector<float>& right, int size)
    {
        for (std::size_t start = 0; start < left.size(); start += static_cast<std::size_t> (size))
        {
            const auto n = std::min<std::size_t> (static_cast<std::size_t> (size), left.size() - start);
            slot.process (std::span<float> { left.data() + start, n }, std::span<float> { right.data() + start, n });
        }
    }
}

TEST_CASE ("ChainSlot : à l'arrêt, le module n'est pas calculé et le signal est intact", "[slot]")
{
    FrequencyShifter shifter;
    ChainSlot slot (shifter);
    slot.prepare ({ sr, blockSize });
    slot.setEnabled (false);
    slot.reset();
    shifter.setShiftHz (500.0f);
    shifter.setMix (1.0f);

    auto left = sine (1000.0, sr, 4096);
    auto right = sine (700.0, sr, 4096);
    const auto originalLeft = left, originalRight = right;
    processInBlocks (slot, left, right, blockSize);

    REQUIRE (slot.isIdle());
    REQUIRE (left == originalLeft);
    REQUIRE (right == originalRight);
}

TEST_CASE ("ChainSlot : la mise en marche se fait en fondu de 15 ms, sans saut", "[slot]")
{
    Inverter inverter;
    ChainSlot slot (inverter);
    slot.prepare ({ sr, blockSize });
    slot.setMix (1.0f);
    slot.setEnabled (false);
    slot.reset();

    std::vector<float> left (2048, 0.5f), right (2048, 0.5f);
    slot.setEnabled (true);
    processInBlocks (slot, left, right, blockSize);

    // Fondu linéaire de +0,5 (sec) à -0,5 (traité) en 720 échantillons.
    for (std::size_t i = 1; i < 720; ++i)
        REQUIRE (left[i] < left[i - 1]);

    REQUIRE (left[359] == Catch::Approx (0.0f).margin (0.01f));
    REQUIRE (left.back() == Catch::Approx (-0.5f));
    REQUIRE (right == left);
}

TEST_CASE ("ChainSlot : sortie = sec × (1 − MIX) + traité × MIX", "[slot]")
{
    const auto mixValue = GENERATE (0.0f, 0.25f, 0.5f, 1.0f);

    Inverter inverter;
    ChainSlot slot (inverter);
    slot.prepare ({ sr, blockSize });
    slot.setMix (mixValue);
    slot.setEnabled (true);
    slot.reset();

    std::vector<float> left (1024, 0.5f), right (1024, 0.5f);
    processInBlocks (slot, left, right, blockSize);

    // Traité = -0,5 : sortie = 0,5 (1 − m) − 0,5 m = 0,5 − m.
    REQUIRE (left.back() == Catch::Approx (0.5f - mixValue).margin (1.0e-6f));
}

TEST_CASE ("ChainSlot : un module mono après une texture garde la stéréo de la partie sèche", "[slot]")
{
    Inverter inverter;
    ChainSlot slot (inverter);
    slot.prepare ({ sr, blockSize });
    slot.setMix (0.5f);
    slot.setEnabled (true);
    slot.reset();

    // Gauche et droite différentes : le module traite la somme mono (0,3), le sec reste séparé.
    std::vector<float> left (512, 0.5f), right (512, 0.1f);
    processInBlocks (slot, left, right, blockSize);

    REQUIRE (left.back() == Catch::Approx (0.25f - 0.15f));
    REQUIRE (right.back() == Catch::Approx (0.05f - 0.15f));
}

TEST_CASE ("ChainSlot : le sec retardé du retard du module ne filtre pas en peigne", "[slot]")
{
    constexpr int latency = 37;
    Delay delay (latency);
    ChainSlot slot (delay);
    slot.prepare ({ sr, blockSize });
    slot.setMix (0.5f);
    slot.setDryDelaySamples (latency);
    slot.setEnabled (true);
    slot.reset();

    // Mélange 50 / 50 d'un signal et de sa copie retardée : sans compensation, creux en peigne
    // (annulation à 648 Hz pour 37 échantillons) ; avec, le niveau reste celui du signal.
    auto left = sine (sr / (2.0 * latency), sr, 8192);
    auto right = left;
    const auto inputLevel = rms (std::span<const float> { left }.subspan (4096));
    processInBlocks (slot, left, right, blockSize);

    REQUIRE (rms (std::span<const float> { left }.subspan (4096)) == Catch::Approx (inputLevel).epsilon (0.01));
}
