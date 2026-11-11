#include "subdamp/dsp/ScaleHarmony.h"

#include <cstdlib>

namespace subdamp::dsp
{

namespace
{
    using Degrees = std::array<int, 7>;

    constexpr std::array<Degrees, numScales> scaleDegrees {{
        { 0, 2, 4, 5, 7, 9, 11 }, // majeur
        { 0, 2, 3, 5, 7, 8, 10 }, // mineur naturel
        { 0, 2, 3, 5, 7, 8, 11 }, // mineur harmonique
        { 0, 2, 3, 5, 7, 9, 10 }, // dorien
        { 0, 1, 3, 5, 7, 8, 10 }, // phrygien
        { 0, 1, 4, 5, 7, 8, 10 }, // phrygien dominant
        { 0, 2, 4, 6, 7, 9, 11 }, // lydien
        { 0, 2, 4, 5, 7, 9, 10 }, // mixolydien
        { 0, 2, 4, 5, 7, 9, 11 }  // parallèle : intervalles de la gamme majeure, depuis chaque note
    }};

    int floorDivide (int value, int divisor)
    {
        const auto quotient = value / divisor;
        return (value % divisor != 0 && (value < 0) != (divisor < 0)) ? quotient - 1 : quotient;
    }

    int degreeToSemitones (const Degrees& degrees, int degree)
    {
        const auto octave = floorDivide (degree, 7);
        return degrees[static_cast<std::size_t> (degree - 7 * octave)] + 12 * octave;
    }
}

int diatonicShift (int noteRelativeToKey, Scale scale, int steps) noexcept
{
    const auto& degrees = scaleDegrees[static_cast<std::size_t> (scale)];

    // Sans gamme : l'intervalle est le même quelle que soit la note jouée.
    if (scale == Scale::parallel)
        return steps >= 0 ? degreeToSemitones (degrees, steps) : -degreeToSemitones (degrees, -steps);

    const auto note = ((noteRelativeToKey % 12) + 12) % 12;

    // Degré de la gamme égal ou juste en dessous de la note jouée.
    int degree = 0;

    for (int d = 6; d >= 0; --d)
    {
        if (degrees[static_cast<std::size_t> (d)] <= note)
        {
            degree = d;
            break;
        }
    }

    return degreeToSemitones (degrees, degree + steps) - degrees[static_cast<std::size_t> (degree)];
}

} // namespace subdamp::dsp
