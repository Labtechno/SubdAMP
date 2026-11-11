#include "subdamp/dsp/BypassCrossfade.h"

#include <algorithm>
#include <cassert>

namespace subdamp::dsp
{

void BypassCrossfade::prepare (const ProcessSpec& spec, double fadeSeconds) noexcept
{
    bypassAmount.prepare (spec.sampleRate, fadeSeconds);
}

void BypassCrossfade::reset() noexcept
{
    bypassAmount.setCurrentAndTarget (bypassAmount.getTarget());
}

void BypassCrossfade::setBypassed (bool shouldBeBypassed) noexcept
{
    bypassAmount.setTarget (shouldBeBypassed ? 1.0f : 0.0f);
}

bool BypassCrossfade::isFullyBypassed() const noexcept
{
    return ! bypassAmount.isSmoothing() && bypassAmount.getCurrent() == 1.0f;
}

void BypassCrossfade::process (std::span<const float> dry, std::span<float> processed) noexcept
{
    assert (dry.size() == processed.size());

    if (! bypassAmount.isSmoothing())
    {
        if (bypassAmount.getCurrent() == 1.0f)
            std::copy (dry.begin(), dry.end(), processed.begin());

        return;
    }

    for (std::size_t i = 0; i < processed.size(); ++i)
    {
        const auto b = bypassAmount.getNext();
        processed[i] += b * (dry[i] - processed[i]);
    }
}

void BypassCrossfade::processStereo (std::span<const float> dry, std::span<float> left, std::span<float> right) noexcept
{
    assert (dry.size() == left.size() && dry.size() == right.size());

    if (! bypassAmount.isSmoothing())
    {
        if (bypassAmount.getCurrent() == 1.0f)
        {
            std::copy (dry.begin(), dry.end(), left.begin());
            std::copy (dry.begin(), dry.end(), right.begin());
        }

        return;
    }

    for (std::size_t i = 0; i < dry.size(); ++i)
    {
        const auto b = bypassAmount.getNext();
        left[i] += b * (dry[i] - left[i]);
        right[i] += b * (dry[i] - right[i]);
    }
}

} // namespace subdamp::dsp
