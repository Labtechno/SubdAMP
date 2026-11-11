#pragma once

namespace subdamp::dsp
{

/**
    Détecteur d'attaque (coup de médiator) pour déclencher des effets.

    Compare deux enveloppes : une rapide (1 ms à la montée) et une lente (40 ms). Une attaque
    fait monter la rapide bien au-dessus de la lente ; une note tenue ou qui s'éteint, non.
    La sensibilité règle l'écart exigé : de 12 dB (0 %) à 2 dB (100 %). Après une attaque,
    60 ms d'insensibilité évitent les doubles déclenchements sur une même note.
*/
class OnsetDetector
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;

    /** 0 = seules les attaques franches, 1 = presque toutes les notes (legato compris). */
    void setSensitivity (float amount) noexcept;

    /** Renvoie vrai sur l'échantillon où une attaque est détectée. */
    bool process (float sample) noexcept;

private:
    static constexpr float floorLevel = 0.003f; // ≈ -50 dBFS : en dessous, rien ne déclenche

    float fastAttack = 0.0f, fastRelease = 0.0f, slowCoefficient = 0.0f;
    float fast = 0.0f, slow = 0.0f;
    float ratio = 2.0f;
    int refractorySamples = 0, refractoryRemaining = 0;
};

} // namespace subdamp::dsp
