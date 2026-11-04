#include "subdamp/dsp/GainStage.h"

#include "subdamp/dsp/Decibels.h"

namespace subdamp::dsp
{

void GainStage::prepare (const ProcessSpec& spec, double rampSeconds) noexcept
{
    gain.prepare (spec.sampleRate, rampSeconds);
}

void GainStage::reset() noexcept
{
    gain.setCurrentAndTarget (gain.getTarget());
}

void GainStage::setGainDecibels (float decibels) noexcept
{
    gain.setTarget (decibelsToGain (decibels));
}

void GainStage::process (std::span<float> block) noexcept
{
    if (gain.isSmoothing())
    {
        for (auto& sample : block)
            sample *= gain.getNext();

        return;
    }

    const auto constantGain = gain.getCurrent();

    if (constantGain == 1.0f)
        return;

    for (auto& sample : block)
        sample *= constantGain;
}

} // namespace subdamp::dsp
