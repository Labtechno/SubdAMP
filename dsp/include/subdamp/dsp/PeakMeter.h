#pragma once

#include <cmath>
#include <span>

namespace subdamp::dsp
{

/** Valeur crête absolue d'un bloc (pour les vumètres). */
inline float blockPeak (std::span<const float> block) noexcept
{
    float peak = 0.0f;

    for (const auto sample : block)
        peak = std::fmax (peak, std::fabs (sample));

    return peak;
}

} // namespace subdamp::dsp
