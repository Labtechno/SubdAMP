#include "subdamp/dsp/LinearSmoother.h"

#include <algorithm>
#include <cmath>

namespace subdamp::dsp
{

void LinearSmoother::prepare (double sampleRate, double rampSeconds) noexcept
{
    rampLength = std::max (1, static_cast<int> (std::lround (sampleRate * rampSeconds)));
    setCurrentAndTarget (target);
}

void LinearSmoother::setCurrentAndTarget (float value) noexcept
{
    current = value;
    target = value;
    remaining = 0;
    step = 0.0f;
}

void LinearSmoother::setTarget (float newTarget) noexcept
{
    if (newTarget == target)
        return;

    target = newTarget;
    remaining = rampLength;
    step = (target - current) / static_cast<float> (rampLength);
}

float LinearSmoother::getNext() noexcept
{
    if (remaining > 0)
    {
        --remaining;
        current = remaining == 0 ? target : current + step;
    }

    return current;
}

void LinearSmoother::skip (int numSamples) noexcept
{
    if (numSamples <= 0 || remaining == 0)
        return;

    if (numSamples >= remaining)
    {
        setCurrentAndTarget (target);
        return;
    }

    remaining -= numSamples;
    current += step * static_cast<float> (numSamples);
}

} // namespace subdamp::dsp
