#include "subdamp/dsp/OnsetDetector.h"

#include <algorithm>
#include <cmath>

namespace subdamp::dsp
{

namespace
{
    float coefficient (double sampleRate, double seconds)
    {
        return static_cast<float> (std::exp (-1.0 / (seconds * sampleRate)));
    }
}

void OnsetDetector::prepare (double sampleRate)
{
    fastAttack = coefficient (sampleRate, 0.001);
    fastRelease = coefficient (sampleRate, 0.02);
    slowCoefficient = coefficient (sampleRate, 0.04);
    refractorySamples = static_cast<int> (0.06 * sampleRate);
    reset();
}

void OnsetDetector::reset() noexcept
{
    fast = 0.0f;
    slow = 0.0f;
    refractoryRemaining = 0;
}

void OnsetDetector::setSensitivity (float amount) noexcept
{
    // Écart d'enveloppes exigé, interpolé en dB : 12 dB (insensible) à 2 dB (très sensible).
    const auto thresholdDb = 12.0f - 10.0f * std::clamp (amount, 0.0f, 1.0f);
    ratio = std::pow (10.0f, thresholdDb / 20.0f);
}

bool OnsetDetector::process (float sample) noexcept
{
    const auto level = std::fabs (sample);
    const auto fastCoefficient = level > fast ? fastAttack : fastRelease;
    fast = level + (fast - level) * fastCoefficient;
    slow = fast + (slow - fast) * slowCoefficient;

    if (refractoryRemaining > 0)
    {
        --refractoryRemaining;
        return false;
    }

    if (fast > floorLevel && fast > slow * ratio)
    {
        refractoryRemaining = refractorySamples;
        return true;
    }

    return false;
}

} // namespace subdamp::dsp
