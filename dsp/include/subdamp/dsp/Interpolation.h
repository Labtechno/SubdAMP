#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace subdamp::dsp
{

/**
    Lecture interpolée (Hermite 4 points, Catmull-Rom) dans un tampon circulaire de taille
    puissance de 2. `position` est un index absolu (nombre d'échantillons écrits depuis le
    début) : le masque gère le repliement, y compris pour les index négatifs.

    Nécessite les échantillons position - 1 à position + 2.
*/
inline float readHermite (const float* ring, std::size_t mask, double position) noexcept
{
    const auto floorPosition = std::floor (position);
    const auto i = static_cast<std::int64_t> (floorPosition);
    const auto t = static_cast<float> (position - floorPosition);

    const auto at = [ring, mask] (std::int64_t index) { return ring[static_cast<std::size_t> (index) & mask]; };

    const auto xm1 = at (i - 1);
    const auto x0 = at (i);
    const auto x1 = at (i + 1);
    const auto x2 = at (i + 2);

    const auto c1 = 0.5f * (x1 - xm1);
    const auto c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const auto c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);

    return ((c3 * t + c2) * t + c1) * t + x0;
}

/** Rapport de fréquence correspondant à un intervalle en demi-tons. */
inline float semitonesToRatio (float semitones) noexcept
{
    return std::exp2 (semitones / 12.0f);
}

} // namespace subdamp::dsp
