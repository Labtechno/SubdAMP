#include "subdamp/dsp/StereoTexture.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace subdamp::dsp
{

void TextureSlot::prepare (const ProcessSpec& spec)
{
    texture.prepare (spec);
    inputGate.prepare (spec.sampleRate, 0.02);
    level.prepare (spec.sampleRate, 0.02);

    const auto size = static_cast<std::size_t> (spec.maxBlockSize);
    input.assign (size, 0.0f);
    wetLeft.assign (size, 0.0f);
    wetRight.assign (size, 0.0f);

    sleepAfter = static_cast<int> (0.5 * spec.sampleRate);
    reset();
}

void TextureSlot::reset() noexcept
{
    texture.reset();
    inputGate.setCurrentAndTarget (enabled ? 1.0f : 0.0f);
    level.setCurrentAndTarget (level.getTarget());
    sleeping = ! enabled;
    silentSamples = 0;
}

void TextureSlot::setEnabled (bool shouldBeEnabled) noexcept
{
    enabled = shouldBeEnabled;
    inputGate.setTarget (enabled ? 1.0f : 0.0f);
}

void TextureSlot::process (std::span<const float> source, std::span<float> left, std::span<float> right) noexcept
{
    assert (source.size() <= input.size());

    if (sleeping)
    {
        if (! enabled)
            return;

        // Réveil : on repart d'une texture vierge, sans reste de la dernière utilisation.
        texture.reset();
        sleeping = false;
        silentSamples = 0;
    }

    const auto size = source.size();
    const std::span<float> in { input.data(), size };
    const std::span<float> wetL { wetLeft.data(), size };
    const std::span<float> wetR { wetRight.data(), size };

    for (std::size_t i = 0; i < size; ++i)
        in[i] = source[i] * inputGate.getNext();

    texture.process (in, wetL, wetR);

    float peak = 0.0f;

    for (std::size_t i = 0; i < size; ++i)
    {
        peak = std::max (peak, std::max (std::fabs (wetL[i]), std::fabs (wetR[i])));

        const auto gain = level.getNext();
        left[i] += gain * wetL[i];
        right[i] += gain * wetR[i];
    }

    if (enabled)
        return;

    silentSamples = peak < silence ? silentSamples + static_cast<int> (size) : 0;

    if (silentSamples >= sleepAfter)
        sleeping = true;
}

} // namespace subdamp::dsp
