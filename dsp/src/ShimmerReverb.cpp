#include "subdamp/dsp/ShimmerReverb.h"

#include "subdamp/dsp/Interpolation.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

namespace subdamp::dsp
{

namespace
{
    // Longueurs des lignes (ms) : réparties entre 30 et 75 ms, sans multiples communs.
    constexpr std::array<double, 8> lineMs { 29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.1, 73.3 };
    constexpr std::array<double, 4> diffuserMs { 4.77, 3.59, 12.73, 9.31 };
    constexpr float diffusion = 0.65f;

    // Signes d'injection et de sortie : motifs orthogonaux pour une stéréo décorrélée.
    constexpr std::array<float, 8> inputSigns { 1, -1, 1, -1, 1, -1, 1, -1 };
    constexpr std::array<float, 8> leftSigns  { 1, -1, 1, -1, 1, -1, 1, -1 };
    constexpr std::array<float, 8> rightSigns { 1, 1, -1, -1, 1, 1, -1, -1 };

    constexpr float injection = 0.5f;
    constexpr float outputGain = 0.3f;

    /** Transformée de Walsh-Hadamard rapide, normalisée (matrice orthogonale). */
    void hadamard8 (std::array<float, 8>& x) noexcept
    {
        for (std::size_t length = 1; length < 8; length <<= 1)
            for (std::size_t start = 0; start < 8; start += 2 * length)
                for (std::size_t i = start; i < start + length; ++i)
                {
                    const auto a = x[i];
                    const auto b = x[i + length];
                    x[i] = a + b;
                    x[i + length] = a - b;
                }

        constexpr auto scale = 0.35355339059f; // 1/√8

        for (auto& value : x)
            value *= scale;
    }
}

void ShimmerReverb::prepare (const ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    modulationDepth = 6.0 * sampleRate / 48000.0;

    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        auto& line = lines[i];
        line.length = std::round (lineMs[i] * 0.001 * sampleRate);
        line.buffer.assign (std::bit_ceil (static_cast<std::size_t> (line.length + modulationDepth) + 8), 0.0f);
        line.mask = line.buffer.size() - 1;
    }

    for (std::size_t i = 0; i < diffusers.size(); ++i)
        diffusers[i].buffer.assign (static_cast<std::size_t> (std::round (diffuserMs[i] * 0.001 * sampleRate)), 0.0f);

    // Fenêtre plus longue que celle du WHAMMY : la nappe est plus douce, la latence n'importe pas ici.
    shifter.prepare (sampleRate, 0.05);
    shimmer.prepare (sampleRate, 0.05);

    // Modulation lente des longueurs (0,4 Hz)
    const auto step = 2.0 * std::numbers::pi * 0.4 / sampleRate;
    stepCos = std::cos (step);
    stepSin = std::sin (step);

    updateDecayGains();
    setToneHz (6000.0f);
    reset();
}

void ShimmerReverb::reset() noexcept
{
    for (auto& line : lines)
    {
        std::fill (line.buffer.begin(), line.buffer.end(), 0.0f);
        line.damped = 0.0f;
    }

    for (auto& diffuser : diffusers)
    {
        std::fill (diffuser.buffer.begin(), diffuser.buffer.end(), 0.0f);
        diffuser.index = 0;
    }

    written = 0;
    shifter.reset();
    lastShifted = 0.0f;
    shimmer.setCurrentAndTarget (shimmer.getTarget());
    modulationCos = 1.0;
    modulationSin = 0.0;
}

void ShimmerReverb::setDecaySeconds (float seconds) noexcept
{
    const auto clamped = std::clamp (seconds, 0.5f, 30.0f);

    if (clamped != decaySeconds)
    {
        decaySeconds = clamped;
        updateDecayGains();
    }
}

void ShimmerReverb::updateDecayGains() noexcept
{
    // Gain par passage pour perdre 60 dB en decaySeconds : 10^(-3 · longueur / (T60 · fe)).
    double sumOfSquares = 0.0;

    for (auto& line : lines)
    {
        line.decayGain = static_cast<float> (std::pow (10.0, -3.0 * line.length / (decaySeconds * sampleRate)));
        sumOfSquares += static_cast<double> (line.decayGain) * line.decayGain;
    }

    // Les longues décroissances accumulent l'énergie (puissance ∝ 1 / (1 - g²)) : au-delà de
    // 2 s, l'injection est réduite d'autant pour que le niveau de la nappe reste comparable.
    const auto meanSquare = sumOfSquares / numLines;
    const auto referenceGain = std::pow (10.0, -3.0 * 0.05 / 2.0); // ligne de 50 ms, T60 de 2 s
    const auto reference = 1.0 - referenceGain * referenceGain;
    inputScale = static_cast<float> (std::min (1.0, std::sqrt ((1.0 - meanSquare) / reference)));
}

void ShimmerReverb::setInterval (float semitones) noexcept
{
    shifterRatio = semitonesToRatio (semitones);
}

void ShimmerReverb::setToneHz (float hz) noexcept
{
    const auto cutoff = std::clamp (static_cast<double> (hz), 200.0, 0.45 * sampleRate);
    dampingCoefficient = static_cast<float> (1.0 - std::exp (-2.0 * std::numbers::pi * cutoff / sampleRate));
}

void ShimmerReverb::process (std::span<const float> input, std::span<float> left, std::span<float> right) noexcept
{
    std::array<float, numLines> outputs {}, mixed {};

    for (std::size_t n = 0; n < input.size(); ++n)
    {
        // 1. Entrée + boucle transposée (saturée en douceur : la boucle reste bornée)
        auto x = input[n] + shimmer.getNext() * std::tanh (lastShifted);

        // 2. Diffusion de l'attaque (passe-tout de Schroeder)
        for (auto& diffuser : diffusers)
        {
            auto& stored = diffuser.buffer[diffuser.index];
            const auto delayed = stored;
            const auto v = x + diffusion * delayed;
            stored = v;
            x = delayed - diffusion * v;

            if (++diffuser.index == diffuser.buffer.size())
                diffuser.index = 0;
        }

        // 3. Lecture des lignes, avec modulation lente sur quatre phases
        const auto c = modulationCos * stepCos - modulationSin * stepSin;
        const auto s = modulationSin * stepCos + modulationCos * stepSin;
        modulationCos = c;
        modulationSin = s;

        const std::array<double, 4> modulation { c, s, -c, -s };
        const auto newest = static_cast<double> (written - 1);

        for (std::size_t i = 0; i < lines.size(); ++i)
        {
            auto& line = lines[i];
            const auto delay = line.length + modulationDepth * modulation[i % 4];
            outputs[i] = readHermite (line.buffer.data(), line.mask, newest + 1.0 - delay);

            // Amortissement : les aigus s'éteignent plus vite, comme dans une vraie pièce.
            line.damped += dampingCoefficient * (outputs[i] - line.damped);
            mixed[i] = line.damped * line.decayGain;
        }

        // 4. Mélange orthogonal, puis réécriture avec l'entrée diffusée
        hadamard8 (mixed);

        for (std::size_t i = 0; i < lines.size(); ++i)
            lines[i].buffer[static_cast<std::size_t> (written) & lines[i].mask] = mixed[i] + injection * inputScale * inputSigns[i] * x;

        ++written;

        // 5. Sorties stéréo et boucle de transposition
        float outLeft = 0.0f, outRight = 0.0f;

        for (std::size_t i = 0; i < lines.size(); ++i)
        {
            outLeft += leftSigns[i] * outputs[i];
            outRight += rightSigns[i] * outputs[i];
        }

        outLeft *= outputGain;
        outRight *= outputGain;

        lastShifted = shifter.process (0.5f * (outLeft + outRight), shifterRatio);

        left[n] = outLeft;
        right[n] = outRight;
    }

    // Renormalisation de l'oscillateur de modulation (dérive lente de la récurrence)
    const auto norm = std::sqrt (modulationCos * modulationCos + modulationSin * modulationSin);
    modulationCos /= norm;
    modulationSin /= norm;
}

} // namespace subdamp::dsp
