#include "subdamp/dsp/AmpSection.h"

namespace subdamp::dsp
{

void AmpSection::prepare (const ProcessSpec& spec)
{
    drive.prepare (spec);
    model.prepare (spec);
    tone.prepare (spec);
    level.prepare (spec);
}

void AmpSection::reset() noexcept
{
    drive.reset();
    model.reset();
    tone.reset();
    level.reset();
}

void AmpSection::process (std::span<float> block) noexcept
{
    drive.process (block);
    model.process (block);
    tone.process (block);
    level.process (block);
}

} // namespace subdamp::dsp
