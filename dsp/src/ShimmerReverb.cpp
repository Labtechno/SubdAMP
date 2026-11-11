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
    // Longueurs des lignes (ms, à la taille 1) : réparties entre 30 et 75 ms, sans multiples communs.
    constexpr std::array<double, 8> lineMs { 29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.1, 73.3 };
    constexpr std::array<double, 4> diffuserMs { 4.77, 3.59, 12.73, 9.31 };

    constexpr double minSizeFactor = 0.5;
    constexpr double maxSizeFactor = 2.0;

    // Le second oscillateur tourne un peu plus lentement : les deux moitiés du réseau ne
    // bougent jamais ensemble, la modulation ne s'entend pas comme un vibrato régulier.
    constexpr double secondRotorRatio = 0.73;

    // Signes d'injection et de sortie : motifs orthogonaux pour une stéréo décorrélée.
    constexpr std::array<float, 8> inputSigns { 1, -1, 1, -1, 1, -1, 1, -1 };
    constexpr std::array<float, 8> leftSigns  { 1, -1, 1, -1, 1, -1, 1, -1 };
    constexpr std::array<float, 8> rightSigns { 1, 1, -1, -1, 1, 1, -1, -1 };

    constexpr float injection = 0.5f;
    constexpr float outputGain = 0.3f;

    // Gain d'énergie de la réverbe, de l'entrée à la somme mono, pour une ligne moyenne :
    //   Γ ≈ outputGain² · 4/8 · 2 · (injection · inputScale)² · 8 / (1 − g²) ≈ 0,09 · s² / (1 − g²)
    // La normalisation inputScale le rend à peu près constant pour les graves : 0,09 / 0,292.
    // Au maximum, chaque génération transposée garde 80 % de l'énergie de la précédente.
    constexpr float reverbEnergyGain = 0.09f / 0.292f;
    const float loopGain = std::sqrt (ShimmerReverb::energyPerGenerationAtMaximum / reverbEnergyGain);

    // La boucle garde toujours au moins ce passe-haut : c'est lui qui empêche le grave de s'y accumuler.
    constexpr double minLoopHighPassHz = 150.0;

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

    void prepare (double sampleRate)
    {
        stretch.presetDefault (1, static_cast<float> (sampleRate), true);
        appliedSemitones = 1000.0f; // force la configuration au premier tour
    }

    void process (const float* input, float* output, float semitones) noexcept
    {
        if (semitones != appliedSemitones)
        {
            stretch.setTransposeSemitones (semitones);
            appliedSemitones = semitones;
        }

        const std::array<const float*, 1> inputs { input };
        const std::array<float*, 1> outputs { output };
        stretch.process (inputs, static_cast<int> (loopBlock), outputs, static_cast<int> (loopBlock));
    }
};

double ShimmerReverb::sizeFactorFor (float amount) noexcept
{
    // Géométrique : 0,5 → 1 → 2, la moitié de la course pour chaque octave de longueur.
    return minSizeFactor * std::pow (maxSizeFactor / minSizeFactor, static_cast<double> (std::clamp (amount, 0.0f, 1.0f)));
}

ShimmerReverb::ShimmerReverb()
    : upShifter (std::make_unique<LoopShifter>()),
      downShifter (std::make_unique<LoopShifter>())
{
    // Valeurs par défaut des réglages lissés (le son de la version d'origine).
    size.setCurrentAndTarget (1.0f);
    diffusion.setCurrentAndTarget (0.65f);
    width.setCurrentAndTarget (1.0f);
    modulationDepth.setCurrentAndTarget (6.0f);
}

ShimmerReverb::~ShimmerReverb() = default;

void ShimmerReverb::prepare (const ProcessSpec& spec)
{
    // Les réglages exprimés en échantillons sont convertis pour la nouvelle fréquence.
    const auto previousRate = sampleRate;
    const auto preDelayMs = preDelayTarget * 1000.0 / previousRate;
    const auto depthAt48k = modulationDepth.getTarget() * 48000.0 / previousRate;

    sampleRate = spec.sampleRate;

    const auto maxModulation = maxModulationSamplesAt48k * sampleRate / 48000.0;

    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        auto& line = lines[i];
        line.baseLength = std::round (lineMs[i] * 0.001 * sampleRate);
        line.buffer.assign (std::bit_ceil (static_cast<std::size_t> (line.baseLength * maxSizeFactor + maxModulation) + 8), 0.0f);
        line.mask = line.buffer.size() - 1;
    }

    for (std::size_t i = 0; i < diffusers.size(); ++i)
        diffusers[i].buffer.assign (static_cast<std::size_t> (std::round (diffuserMs[i] * 0.001 * sampleRate)), 0.0f);

    preDelayBuffer.assign (std::bit_ceil (static_cast<std::size_t> (maxPreDelayMs * 0.001 * sampleRate) + 8), 0.0f);
    preDelayMask = preDelayBuffer.size() - 1;

    upShifter->prepare (sampleRate);
    downShifter->prepare (sampleRate);

    shimmer.prepare (sampleRate, 0.05);
    dual.prepare (sampleRate, 0.05);
    size.prepare (sampleRate, 0.5);
    diffusion.prepare (sampleRate, 0.05);
    width.prepare (sampleRate, 0.05);
    modulationDepth.prepare (sampleRate, 0.1);

    preDelayTarget = static_cast<int> (std::lround (preDelayMs * 0.001 * sampleRate));
    preDelayFadeLength = static_cast<int> (std::lround (0.03 * sampleRate));
    modulationDepth.setCurrentAndTarget (static_cast<float> (depthAt48k * sampleRate / 48000.0));

    const auto rate = modulationRateHz;
    modulationRateHz = -1.0f; // recalcule les pas pour la nouvelle fréquence d'échantillonnage
    setModulationRate (rate);

    gainsSize = -1.0;
    updateDecayGains();

    appliedLowCutHz = -1.0f;
    updateLowCut();

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

    std::fill (preDelayBuffer.begin(), preDelayBuffer.end(), 0.0f);
    preDelayCurrent = preDelayNext = preDelayTarget;
    preDelayFadeRemaining = 0;

    written = 0;
    inputHighPass.reset();
    inputLowPass.reset();

    upShifter->stretch.reset();
    downShifter->stretch.reset();
    loopRunning = false;
    downRunning = false;
    loopInput.fill (0.0f);
    loopOutput.fill (0.0f);
    downOutput.fill (0.0f);
    loopPosition = 0;

    for (auto& filter : loopHighPass)
        filter.reset();

    for (auto* smoother : { &shimmer, &dual, &size, &diffusion, &width, &modulationDepth })
        smoother->setCurrentAndTarget (smoother->getTarget());

    updateDecayGains();

    for (auto& rotor : rotors)
    {
        rotor.cos = 1.0;
        rotor.sin = 0.0;
    }
}

void ShimmerReverb::setDecaySeconds (float seconds) noexcept
{
    const auto clamped = std::clamp (seconds, 0.5f, 30.0f);

    if (clamped != decaySeconds)
    {
        decaySeconds = clamped;
        gainsSize = -1.0; // force le recalcul
        updateDecayGains();
    }
}

void ShimmerReverb::updateDecayGains() noexcept
{
    const auto factor = static_cast<double> (size.getCurrent());

    if (factor == gainsSize)
        return;

    gainsSize = factor;

    // Gain par passage pour perdre 60 dB en decaySeconds : 10^(-3 · longueur / (T60 · fe)).
    // Les aigus ont leur propre gain, pour un T60 plus court (amortissement à deux bandes).
    double sumOfSquares = 0.0;
    const auto highDecaySeconds = std::max (0.25, decaySeconds * ShimmerReverb::highDecayRatio);

    for (auto& line : lines)
    {
        const auto length = line.baseLength * factor;
        line.decayGain = static_cast<float> (std::pow (10.0, -3.0 * length / (decaySeconds * sampleRate)));
        line.highGain = static_cast<float> (std::pow (10.0, -3.0 * length / (highDecaySeconds * sampleRate)));
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

void ShimmerReverb::setPitchMode (PitchMode mode) noexcept
{
    dual.setTarget (mode == PitchMode::dual ? 1.0f : 0.0f);
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

void ShimmerReverb::setLowCutHz (float hz) noexcept
{
    lowCutHz = std::clamp (hz, 20.0f, 1000.0f);
    updateLowCut();
}

void ShimmerReverb::updateLowCut() noexcept
{
    if (lowCutHz == appliedLowCutHz)
        return;

    appliedLowCutHz = lowCutHz;
    inputHighPass.setCoefficients (BiquadCoefficients::highPass (sampleRate, lowCutHz, butterworthQ));

    const auto loopCut = std::max (minLoopHighPassHz, static_cast<double> (lowCutHz));
    loopHighPass[0].setCoefficients (BiquadCoefficients::highPass (sampleRate, loopCut, butterworth4Q1));
    loopHighPass[1].setCoefficients (BiquadCoefficients::highPass (sampleRate, loopCut, butterworth4Q2));
}

void ShimmerReverb::setSize (float amount) noexcept
{
    size.setTarget (static_cast<float> (sizeFactorFor (amount)));
}

void ShimmerReverb::setDiffusion (float amount) noexcept
{
    diffusion.setTarget (maxDiffusionCoefficient * std::clamp (amount, 0.0f, 1.0f));
}

void ShimmerReverb::setPreDelayMs (float milliseconds) noexcept
{
    preDelayTarget = static_cast<int> (std::lround (std::clamp (milliseconds, 0.0f, maxPreDelayMs) * 0.001 * sampleRate));
}

void ShimmerReverb::setModulationRate (float hz) noexcept
{
    const auto clamped = std::clamp (hz, 0.05f, 5.0f);

    if (clamped == modulationRateHz)
        return;

    modulationRateHz = clamped;

    // Changer le pas d'une rotation ne fait pas sauter sa phase : aucun clic.
    for (std::size_t i = 0; i < rotors.size(); ++i)
    {
        const auto step = 2.0 * std::numbers::pi * clamped * (i == 0 ? 1.0 : secondRotorRatio) / sampleRate;
        rotors[i].stepCos = std::cos (step);
        rotors[i].stepSin = std::sin (step);
    }
}

void ShimmerReverb::setModulationDepth (float amount) noexcept
{
    const auto samples = maxModulationSamplesAt48k * sampleRate / 48000.0 * std::clamp (amount, 0.0f, 1.0f);
    modulationDepth.setTarget (static_cast<float> (samples));
}

void ShimmerReverb::setWidth (float amount) noexcept
{
    width.setTarget (std::clamp (amount, 0.0f, 1.0f));
}

int ShimmerReverb::loopDelaySamples() const noexcept
{
    const auto& stretch = upShifter->stretch;
    return stretch.inputLatency() + stretch.outputLatency() + static_cast<int> (loopBlock);
}

void ShimmerReverb::runLoop() noexcept
{
    // Boucle coupée et vide : pas de calcul. Elle repartira d'un état propre.
    if (shimmer.getTarget() <= 0.0f && ! shimmer.isSmoothing() && shimmer.getCurrent() <= 0.0f)
    {
        if (loopRunning)
        {
            upShifter->stretch.reset();
            downShifter->stretch.reset();

            for (auto& filter : loopHighPass)
                filter.reset();

            loopOutput.fill (0.0f);
            downOutput.fill (0.0f);
            loopRunning = false;
            downRunning = false;
        }

        return;
    }

    loopRunning = true;
    upShifter->process (loopInput.data(), loopOutput.data(), intervalSemitones);

    // Second transpositeur : calculé seulement en mode double (et pendant le fondu de sortie).
    const auto dualActive = dual.getTarget() > 0.0f || dual.isSmoothing() || dual.getCurrent() > 0.0f;

    if (dualActive)
    {
        downShifter->process (loopInput.data(), downOutput.data(), -intervalSemitones);
        downRunning = true;
    }
    else if (downRunning)
    {
        downShifter->stretch.reset();
        downOutput.fill (0.0f);
        downRunning = false;
    }

    for (std::size_t i = 0; i < loopBlock; ++i)
    {
        auto sample = loopOutput[i];

        if (dualActive)
        {
            // Somme à puissance constante (deux transpositions décorrélées : √2 de trop sinon),
            // et calibrage d'énergie propre au mode double.
            const auto d = dual.getNext();
            const auto energy = energyPerGenerationAtMaximum + d * (energyPerGenerationAtMaximumDual - energyPerGenerationAtMaximum);
            sample = (sample + d * downOutput[i]) * std::sqrt (energy / (energyPerGenerationAtMaximum * (1.0f + d * d)));
        }

        loopOutput[i] = softLimit (loopHighPass[1].processSample (loopHighPass[0].processSample (sample)));
    }
}

void ShimmerReverb::process (std::span<const float> input, std::span<float> left, std::span<float> right) noexcept
{
    std::array<float, numLines> outputs {}, mixed {};

    for (std::size_t n = 0; n < input.size(); ++n)
    {
        // 1. Pré-délai de l'entrée. Un nouveau réglage démarre un fondu enchaîné de 30 ms
        //    entre les deux lectures ; s'il change encore pendant le fondu, le suivant attend.
        preDelayBuffer[static_cast<std::size_t> (written) & preDelayMask] = input[n];
        const auto tap = [this] (int delay) { return preDelayBuffer[static_cast<std::size_t> (written - delay) & preDelayMask]; };

        if (preDelayFadeRemaining == 0 && preDelayTarget != preDelayCurrent)
        {
            preDelayNext = preDelayTarget;
            preDelayFadeRemaining = preDelayFadeLength;
        }

        auto delayedInput = tap (preDelayCurrent);

        if (preDelayFadeRemaining > 0)
        {
            const auto t = 1.0f - static_cast<float> (--preDelayFadeRemaining) / static_cast<float> (preDelayFadeLength);
            delayedInput += t * (tap (preDelayNext) - delayedInput);

            if (preDelayFadeRemaining == 0)
                preDelayCurrent = preDelayNext;
        }

        // 2. Entrée + génération précédente transposée, bornées par COUPE-BAS et COUPE-HAUT.
        //    Les filtres sont dans la boucle : chaque génération plus aiguë est plus atténuée.
        const auto feedback = shimmer.getNext() * loopGain * loopOutput[loopPosition];
        auto x = inputLowPass.processSample (inputHighPass.processSample (delayedInput + feedback));

        // 3. Diffusion de l'attaque (passe-tout de Schroeder)
        const auto g = diffusion.getNext();

        for (auto& diffuser : diffusers)
        {
            auto& stored = diffuser.buffer[diffuser.index];
            const auto delayed = stored;
            const auto v = x + g * delayed;
            stored = v;
            x = delayed - g * v;

            if (++diffuser.index == diffuser.buffer.size())
                diffuser.index = 0;
        }

        // 4. Lecture des lignes : longueur × taille, modulée par deux oscillateurs en quadrature
        for (auto& rotor : rotors)
        {
            const auto c = rotor.cos * rotor.stepCos - rotor.sin * rotor.stepSin;
            const auto s = rotor.sin * rotor.stepCos + rotor.cos * rotor.stepSin;
            rotor.cos = c;
            rotor.sin = s;
        }

        const std::array<double, 8> modulation { rotors[0].cos, rotors[0].sin, -rotors[0].cos, -rotors[0].sin,
                                                 rotors[1].cos, rotors[1].sin, -rotors[1].cos, -rotors[1].sin };
        const auto factor = static_cast<double> (size.getNext());
        const auto depth = static_cast<double> (modulationDepth.getNext());
        const auto newest = static_cast<double> (written - 1);

        for (std::size_t i = 0; i < lines.size(); ++i)
        {
            auto& line = lines[i];
            const auto delay = line.baseLength * factor + depth * modulation[i];
            outputs[i] = readHermite (line.buffer.data(), line.mask, newest + 1.0 - delay);

            // Amortissement à deux bandes (Jot) : sous COUPE-HAUT, décroissance réglée ; au-dessus,
            // deux fois plus rapide. Le gain ne dépasse jamais celui des graves : stable.
            line.lowBand += dampingCoefficient * (outputs[i] - line.lowBand);
            mixed[i] = line.highGain * outputs[i] + (line.decayGain - line.highGain) * line.lowBand;
        }

        // 5. Mélange orthogonal, puis réécriture avec l'entrée diffusée
        hadamard8 (mixed);

        for (std::size_t i = 0; i < lines.size(); ++i)
            lines[i].buffer[static_cast<std::size_t> (written) & lines[i].mask] = mixed[i] + injection * inputScale * inputSigns[i] * x;

        ++written;

        // 6. Sorties stéréo, resserrées par LARGEUR (milieu / côtés)
        float outLeft = 0.0f, outRight = 0.0f;

        for (std::size_t i = 0; i < lines.size(); ++i)
        {
            outLeft += leftSigns[i] * outputs[i];
            outRight += rightSigns[i] * outputs[i];
        }

        const auto mid = 0.5f * outputGain * (outLeft + outRight);
        const auto side = 0.5f * outputGain * (outLeft - outRight) * width.getNext();

        left[n] = mid + side;
        right[n] = mid - side;

        // 7. Boucle de transposition : la sortie part au transpositeur par blocs de 32
        loopInput[loopPosition] = mid;

        if (++loopPosition == loopBlock)
        {
            loopPosition = 0;
            runLoop();

            // La taille glisse : les gains suivent, au rythme de la boucle (pas à chaque échantillon).
            updateDecayGains();
        }
    }

    // Renormalisation des oscillateurs de modulation (dérive lente de la récurrence)
    for (auto& rotor : rotors)
    {
        const auto norm = std::sqrt (rotor.cos * rotor.cos + rotor.sin * rotor.sin);
        rotor.cos /= norm;
        rotor.sin /= norm;
    }
}

} // namespace subdamp::dsp
