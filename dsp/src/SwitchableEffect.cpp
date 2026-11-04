#include "subdamp/dsp/SwitchableEffect.h"

#include <algorithm>
#include <cassert>

namespace subdamp::dsp
{

void SwitchableEffect::prepare (const ProcessSpec& spec)
{
    effect.prepare (spec);
    fade.prepare (spec);
    dry.assign (static_cast<std::size_t> (spec.maxBlockSize), 0.0f);
}

void SwitchableEffect::reset() noexcept
{
    effect.reset();
    fade.reset();
}

void SwitchableEffect::setEnabled (bool shouldBeEnabled) noexcept
{
    fade.setBypassed (! shouldBeEnabled);
}

void SwitchableEffect::process (std::span<float> block) noexcept
{
    assert (block.size() <= dry.size());

    if (fade.isFullyBypassed())
    {
        wasFullyOff = true;
        return;
    }

    // Remise en marche : on efface la mémoire du module (filtres, enveloppes) accumulée avant l'arrêt.
    if (wasFullyOff)
    {
        effect.reset();
        wasFullyOff = false;
    }

    const std::span<float> dryBlock { dry.data(), block.size() };
    std::copy (block.begin(), block.end(), dryBlock.begin());

    effect.process (block);
    fade.process (dryBlock, block);
}

} // namespace subdamp::dsp
