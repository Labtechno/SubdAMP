#include "subdamp/dsp/ShimmerReverb.h"

#include "subdamp/dsp/Interpolation.h"

#include <signalsmith-stretch/signalsmith-stretch.h>

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

    // Les aigus (au-dessus de TONE) décroissent deux fois plus vite que les graves.
    constexpr double highDecayRatio = 0.5;

    // Gain d'énergie de la réverbe, de l'entrée à la somme mono, pour une ligne moyenne :
    //   Γ ≈ outputGain² · 4/8 · 2 · (injection · inputScale)² · 8 / (1 − g²) ≈ 0,09 · s² / (1 − g²)
    // La normalisation inputScale le rend à peu près constant pour les graves : 0,09 / 0,292.
    // Au maximum, chaque génération transposée garde 80 % de l'énergie de la précédente.
    constexpr float maxEnergyPerGeneration = 0.8f;
    constexpr float reverbEnergyGain = 0.09f / 0.292f;
    const float loopGain = std::sqrt (maxEnergyPerGeneration / reverbEnergyGain);

    constexpr double inputHighPassHz = 50.0;
    constexpr double loopHighPassHz = 150.0;

    /** Transparent jusqu'à 0,7, puis arrondi vers 1 : n'agit que sur les crêtes extrêmes. */
    float softLimit (float x) noexcept
    {
        constexpr float knee = 0.7f;
        const auto magnitude = std::abs (x);

        if (magnitude <= knee)
            return x;

        return std::copysign (knee + (1.0f - knee) * std::tanh ((magnitude - knee) / (1.0f - knee)), x);
    }

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

struct ShimmerReverb::LoopShifter
{
    signalsmith::stretch::SignalsmithStretch<float> stretch { 4321 }; // graine fixe : rendu reproductible
    float appliedSemitones = 1000.0f;
};

ShimmerReverb::ShimmerReverb() : loopShifter (std::make_unique<LoopShifter>()) {}
ShimmerReverb::~ShimmerReverb() = default;

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

    loopShifter->stretch.presetDefault (1, static_cast<float> (sampleRate), true);
    loopShifter->appliedSemitones = 1000.0f; // force la configuration au premier tour
    shimmer.prepare (sampleRate, 0.05);

    inputHighPass.setCoefficients (BiquadCoefficients::highPass (sampleRate, inputHighPassHz, butterworthQ));
    loopHighPass[0].setCoefficients (BiquadCoefficients::highPass (sampleRate, loopHighPassHz, butterworth4Q1));
    loopHighPass[1].setCoefficients (BiquadCoefficients::highPass (sampleRate, loopHighPassHz, butterworth4Q2));

    // Modulation lente des longueurs (0,4 Hz)
    const auto step = 2.0 * std::numbers::pi * 0.4 / sampleRate;
    stepCos = std::cos (step);
    stepSin = std::sin (step);

    updateDecayGains();
    const auto tone = toneHz > 0.0f ? toneHz : 6000.0f;
    toneHz = -1.0f; // recalcule les coefficients pour la nouvelle fréquence d'échantillonnage
    setToneHz (tone);
    reset();
}

void ShimmerReverb::reset() noexcept
{
    for (auto& line : lines)
    {
        std::fill (line.buffer.begin(), line.buffer.end(), 0.0f);
        line.lowBand = 0.0f;
    }

    for (auto& diffuser : diffusers)
    {
        std::fill (diffuser.buffer.begin(), diffuser.buffer.end(), 0.0f);
        diffuser.index = 0;
    }

    written = 0;
    inputHighPass.reset();
    inputLowPass.reset();

    loopShifter->stretch.reset();
    loopRunning = false;
    loopInput.fill (0.0f);
    loopOutput.fill (0.0f);
    loopPosition = 0;

    for (auto& filter : loopHighPass)
        filter.reset();

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
    // Les aigus ont leur propre gain, pour un T60 plus court (amortissement à deux bandes).
    double sumOfSquares = 0.0;
    const auto highDecaySeconds = std::max (0.25, decaySeconds * highDecayRatio);

    for (auto& line : lines)
    {
        line.decayGain = static_cast<float> (std::pow (10.0, -3.0 * line.length / (decaySeconds * sampleRate)));
        line.highGain = static_cast<float> (std::pow (10.0, -3.0 * line.length / (highDecaySeconds * sampleRate)));
        sumOfSquares += static_cast<double> (line.decayGain) * line.decayGain;
    }

    // Les longues décroissances accumulent l'énergie (puissance ∝ 1 / (1 - g²)) : au-delà de
    // 2 s, l'injection est réduite d'autant pour que le niveau de la nappe reste comparable.
    const auto meanSquare = sumOfSquares / numLines;
    const auto referenceGain = std::pow (10.0, -3.0 * 0.05 / 2.0); // ligne de 50 ms, T60 de 2 s
    const auto reference = 1.0 - referenceGain * referenceGain;
    inputScale = static_cast<float> (std::min (1.0, std::sqrt ((1.0 - meanSquare) / reference)));
}

void ShimmerReverb::setShimmer (float amount) noexcept
{
    shimmer.setTarget (std::clamp (amount, 0.0f, 1.0f));
}

void ShimmerReverb::setInterval (float semitones) noexcept
{
    intervalSemitones = std::clamp (semitones, -24.0f, 24.0f);
}

void ShimmerReverb::setToneHz (float hz) noexcept
{
    if (hz == toneHz)
        return;

    toneHz = hz;
    const auto cutoff = std::clamp (static_cast<double> (hz), 200.0, 0.45 * sampleRate);
    dampingCoefficient = static_cast<float> (1.0 - std::exp (-2.0 * std::numbers::pi * cutoff / sampleRate));
    inputLowPass.setCoefficients (BiquadCoefficients::lowPass (sampleRate, cutoff, butterworthQ));
}

int ShimmerReverb::loopDelaySamples() const noexcept
{
    const auto& stretch = loopShifter->stretch;
    return stretch.inputLatency() + stretch.outputLatency() + static_cast<int> (loopBlock);
}

void ShimmerReverb::runLoop() noexcept
{
    auto& engine = *loopShifter;

    // Boucle coupée et vide : pas de calcul. Elle repartira d'un état propre.
    if (shimmer.getTarget() <= 0.0f && ! shimmer.isSmoothing() && shimmer.getCurrent() <= 0.0f)
    {
        if (loopRunning)
        {
            engine.stretch.reset();

            for (auto& filter : loopHighPass)
                filter.reset();

            loopOutput.fill (0.0f);
            loopRunning = false;
        }

        return;
    }

    loopRunning = true;

    if (intervalSemitones != engine.appliedSemitones)
    {
        engine.stretch.setTransposeSemitones (intervalSemitones);
        engine.appliedSemitones = intervalSemitones;
    }

    const std::array<const float*, 1> inputs { loopInput.data() };
    const std::array<float*, 1> outputs { loopOutput.data() };
    engine.stretch.process (inputs, static_cast<int> (loopBlock), outputs, static_cast<int> (loopBlock));

    for (auto& sample : loopOutput)
        sample = softLimit (loopHighPass[1].processSample (loopHighPass[0].processSample (sample)));
}

void ShimmerReverb::process (std::span<const float> input, std::span<float> left, std::span<float> right) noexcept
{
    std::array<float, numLines> outputs {}, mixed {};

    for (std::size_t n = 0; n < input.size(); ++n)
    {
        // 1. Entrée + génération précédente transposée, bornées par les passe-haut et passe-bas.
        //    Le passe-bas TONE est dans la boucle : chaque génération plus aiguë est plus atténuée.
        const auto feedback = shimmer.getNext() * loopGain * loopOutput[loopPosition];
        auto x = inputLowPass.processSample (inputHighPass.processSample (input[n] + feedback));

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

            // Amortissement à deux bandes (Jot) : sous TONE, décroissance réglée ; au-dessus,
            // deux fois plus rapide. Le gain ne dépasse jamais celui des graves : stable.
            line.lowBand += dampingCoefficient * (outputs[i] - line.lowBand);
            mixed[i] = line.highGain * outputs[i] + (line.decayGain - line.highGain) * line.lowBand;
        }

        // 4. Mélange orthogonal, puis réécriture avec l'entrée diffusée
        hadamard8 (mixed);

        for (std::size_t i = 0; i < lines.size(); ++i)
            lines[i].buffer[static_cast<std::size_t> (written) & lines[i].mask] = mixed[i] + injection * inputScale * inputSigns[i] * x;

        ++written;

        // 5. Sorties stéréo
        float outLeft = 0.0f, outRight = 0.0f;

        for (std::size_t i = 0; i < lines.size(); ++i)
        {
            outLeft += leftSigns[i] * outputs[i];
            outRight += rightSigns[i] * outputs[i];
        }

        outLeft *= outputGain;
        outRight *= outputGain;

        left[n] = outLeft;
        right[n] = outRight;

        // 6. Boucle de transposition : la sortie part au transpositeur par blocs de 32
        loopInput[loopPosition] = 0.5f * (outLeft + outRight);

        if (++loopPosition == loopBlock)
        {
            loopPosition = 0;
            runLoop();
        }
    }

    // Renormalisation de l'oscillateur de modulation (dérive lente de la récurrence)
    const auto norm = std::sqrt (modulationCos * modulationCos + modulationSin * modulationSin);
    modulationCos /= norm;
    modulationSin /= norm;
}

} // namespace subdamp::dsp
