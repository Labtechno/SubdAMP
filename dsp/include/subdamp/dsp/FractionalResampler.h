#pragma once

#include <cstdint>
#include <vector>

namespace subdamp::dsp
{

/**
    Rééchantillonneur en flux, rapport quelconque (88,2 kHz → 48 kHz, par exemple).

    Interpolation à sinus cardinal fenêtré (Blackman-Harris), avec une coupure placée à 92 %
    de la fréquence de Nyquist la plus basse des deux : le même filtre sert d'anti-repliement
    en descente et d'anti-image en montée. Le noyau est précalculé dans une table.

    Les échantillons produits ne sont pas retardés en eux-mêmes : la sortie n° j correspond à
    l'instant j / rapport en entrée. Mais elle n'est disponible qu'une fois reçus les
    échantillons d'entrée nécessaires au calcul, soit halfWidth() échantillons plus tard.
*/
class FractionalResampler
{
public:
    /** Peut allouer. maxInputBlock : nombre maximal d'échantillons fournis par appel à process(). */
    void prepare (double inputRate, double outputRate, int maxInputBlock, int halfTaps = 16);

    void reset() noexcept;

    /**
        Ajoute numInput échantillons et écrit au plus maxOutput échantillons produits.
        Renvoie le nombre d'échantillons produits.
    */
    int process (const float* input, int numInput, float* output, int maxOutput) noexcept;

    /** Demi-largeur du noyau, en échantillons d'entrée. */
    double halfWidth() const noexcept { return width; }

    /** Pas entre deux échantillons de sortie, en échantillons d'entrée. */
    double step() const noexcept { return outputStep; }

private:
    float interpolate (double time) const noexcept;

    static constexpr int tableResolution = 512; // points de noyau par échantillon d'entrée

    std::vector<float> kernel;
    std::vector<float> history;
    std::size_t historyMask = 0;

    double width = 0.0;
    double outputStep = 1.0;

    std::int64_t written = 0;  // nombre total d'échantillons d'entrée reçus
    double nextOutputTime = 0.0; // instant (en échantillons d'entrée) du prochain échantillon à produire
};

} // namespace subdamp::dsp
