#pragma once

#include <algorithm>
#include <cmath>

namespace subdamp::dsp
{

/** En dessous de ce niveau, un gain en dB est considéré comme un silence total. */
inline constexpr float minusInfinityDb = -100.0f;

/** Convertit des décibels en gain linéaire. Renvoie 0 à partir de minusInfinityDb. */
inline float decibelsToGain (float decibels) noexcept
{
    return decibels > minusInfinityDb ? std::pow (10.0f, decibels * 0.05f) : 0.0f;
}

/** Convertit un gain linéaire en décibels, borné à minusInfinityDb pour le silence. */
inline float gainToDecibels (float gain) noexcept
{
    return gain > 0.0f ? std::max (minusInfinityDb, 20.0f * std::log10 (gain)) : minusInfinityDb;
}

} // namespace subdamp::dsp
