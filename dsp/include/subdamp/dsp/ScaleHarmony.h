#pragma once

#include <array>

namespace subdamp::dsp
{

/** Gammes proposées par l'HARMONIZER. L'ordre est celui du paramètre : ne pas le changer. */
enum class Scale
{
    major = 0,
    naturalMinor,
    harmonicMinor,
    dorian,
    phrygian,
    phrygianDominant,
    lydian,
    mixolydian,
    parallel // pas de gamme : intervalles fixes, ceux de la gamme majeure
};

inline constexpr int numScales = 9;

/**
    Transposition, en demi-tons, d'une note pour obtenir l'intervalle diatonique voulu.

    noteRelativeToKey : classe de hauteur de la note jouée par rapport à la tonique (0 à 11).
    steps : nombre de degrés de la gamme (2 = tierce, 4 = quinte, 7 = octave, négatif vers le grave).

    Exemple en Mi mineur : une tierce au-dessus de Mi donne Sol (+3), au-dessus de Sol donne
    Si (+4). Une note hors gamme garde le même écart que la note de la gamme juste en dessous.
*/
int diatonicShift (int noteRelativeToKey, Scale scale, int steps) noexcept;

} // namespace subdamp::dsp
