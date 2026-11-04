#pragma once

#include "subdamp/dsp/LinearSmoother.h"
#include "subdamp/dsp/ProcessSpec.h"

#include <span>

namespace subdamp::dsp
{

/**
    Gain lissé appliqué sur place à un bloc mono.

    Le lissage se fait sur le gain linéaire (20 ms par défaut) : assez court pour suivre
    une automation, assez long pour ne jamais produire de clic.
*/
class GainStage
{
public:
    void prepare (const ProcessSpec& spec, double rampSeconds = 0.02) noexcept;

    /** Saute directement au gain cible (après un reset du transport, par exemple). */
    void reset() noexcept;

    void setGainDecibels (float decibels) noexcept;

    void process (std::span<float> block) noexcept;

private:
    LinearSmoother gain;
};

} // namespace subdamp::dsp
