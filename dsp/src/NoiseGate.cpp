#include "subdamp/dsp/NoiseGate.h"

#include "subdamp/dsp/Decibels.h"

#include <cmath>

namespace subdamp::dsp
{

float NoiseGate::coefficientFor (double milliseconds) const noexcept
{
    // Coefficient d'un lissage exponentiel atteignant ~63 % du chemin en `milliseconds`.
    return static_cast<float> (std::exp (-1000.0 / (milliseconds * sampleRate)));
}

void NoiseGate::prepare (const ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    detectorCoefficient = coefficientFor (detectorReleaseMs);
    openCoefficient = coefficientFor (openMs);
    closeCoefficient = coefficientFor (releaseMs);
    holdSamples = static_cast<int> (holdMs * 0.001 * sampleRate);
    reset();
}

void NoiseGate::reset() noexcept
{
    envelope = 0.0f;
    gain = 0.0f;
    holdRemaining = 0;
    open = false;
}

void NoiseGate::setThresholdDecibels (float decibels) noexcept
{
    openThreshold = decibelsToGain (decibels);
    closeThreshold = decibelsToGain (decibels - hysteresisDb);
}

void NoiseGate::setReleaseMilliseconds (float milliseconds) noexcept
{
    if (milliseconds == releaseMs)
        return;

    releaseMs = milliseconds;
    closeCoefficient = coefficientFor (milliseconds);
}

void NoiseGate::process (std::span<float> block) noexcept
{
    for (auto& sample : block)
    {
        envelope = std::fmax (std::fabs (sample), envelope * detectorCoefficient);

        if (envelope > openThreshold)
        {
            open = true;
            holdRemaining = holdSamples;
        }
        else if (envelope < closeThreshold)
        {
            if (holdRemaining > 0)
                --holdRemaining;
            else
                open = false;
        }

        const auto target = open ? 1.0f : 0.0f;
        const auto coefficient = target > gain ? openCoefficient : closeCoefficient;
        gain = target + (gain - target) * coefficient;

        sample *= gain;
    }
}

} // namespace subdamp::dsp
