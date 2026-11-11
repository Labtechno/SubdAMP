#include "subdamp/dsp/FrequencyShifter.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace subdamp::dsp
{

namespace
{
    // Coefficients a des deux chaînes (Olli Niemitalo, « Hilbert transform IIR filters »).
    // On stocke a², seul utilisé dans le calcul.
    constexpr std::array<double, 4> inPhaseA    { 0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737 };
    constexpr std::array<double, 4> quadratureA { 0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278 };

    std::array<float, 4> squared (const std::array<double, 4>& a)
    {
        std::array<float, 4> result {};

        for (std::size_t i = 0; i < a.size(); ++i)
            result[i] = static_cast<float> (a[i] * a[i]);

        return result;
    }
}

float FrequencyShifter::AllpassChain::process (float x) noexcept
{
    for (std::size_t i = 0; i < coefficients.size(); ++i)
    {
        auto& [x1, x2, y1, y2] = state[i];
        const auto y = coefficients[i] * (x + y2) - x2;

        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        x = y;
    }

    return x;
}

void FrequencyShifter::prepare (const ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    inPhase.coefficients = squared (inPhaseA);
    quadrature.coefficients = squared (quadratureA);

    mix.prepare (sampleRate, 0.02);
    feedback.prepare (sampleRate, 0.02);

    updateOscillatorStep();
    reset();
}

void FrequencyShifter::reset() noexcept
{
    inPhase.reset();
    quadrature.reset();
    delayedInPhase = 0.0f;
    oscillatorCos = 1.0;
    oscillatorSin = 0.0;
    previousOutput = 0.0f;
}

void FrequencyShifter::setShiftHz (float hz) noexcept
{
    if (static_cast<double> (hz) == shiftHz)
        return;

    shiftHz = hz;
    updateOscillatorStep();
}

void FrequencyShifter::updateOscillatorStep() noexcept
{
    const auto step = 2.0 * std::numbers::pi * shiftHz / sampleRate;
    stepCos = std::cos (step);
    stepSin = std::sin (step);
}

void FrequencyShifter::setMix (float newMix) noexcept
{
    mix.setTarget (std::clamp (newMix, 0.0f, 1.0f));
}

void FrequencyShifter::setFeedback (float newFeedback) noexcept
{
    feedback.setTarget (std::clamp (newFeedback, 0.0f, 0.95f));
}

void FrequencyShifter::process (std::span<float> block) noexcept
{
    for (auto& sample : block)
    {
        const auto dry = sample;
        const auto input = dry + feedback.getNext() * previousOutput;

        // La chaîne « en phase » est retardée d'un échantillon pour être en quadrature exacte.
        const auto i = delayedInPhase;
        delayedInPhase = inPhase.process (input);
        const auto q = quadrature.process (input);

        // Rotation de l'oscillateur (récurrence) plutôt qu'un sin/cos par échantillon.
        const auto c = oscillatorCos * stepCos - oscillatorSin * stepSin;
        const auto s = oscillatorSin * stepCos + oscillatorCos * stepSin;
        oscillatorCos = c;
        oscillatorSin = s;

        // Bande latérale unique : décalage de +shiftHz (la chaîne « quadrature » est en avance de 90°).
        const auto shifted = static_cast<float> (i * c + q * s);
        previousOutput = shifted;

        // Puissance constante : le décalé (autres fréquences) s'ajoute au sec sans perte de niveau.
        const auto angle = mix.getNext() * 1.57079632679f;
        sample = dry * std::cos (angle) + shifted * std::sin (angle);
    }

    // Renormalisation de l'oscillateur : la récurrence dérive lentement en amplitude.
    const auto norm = std::sqrt (oscillatorCos * oscillatorCos + oscillatorSin * oscillatorSin);
    oscillatorCos /= norm;
    oscillatorSin /= norm;
}

} // namespace subdamp::dsp
