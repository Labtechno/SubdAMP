#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace subdamp::dsp
{

/**
    Pitch shifter à ligne à retard avec raccords alignés (type WSOLA) : polyphonique, sans
    détection de hauteur, base du WHAMMY.

    Une tête de lecture lit l'entrée à la vitesse « rapport » : son retard varie en continu
    dans une fenêtre de 30 ms. Avant qu'elle n'en sorte, une seconde tête prend le relais par
    un court fondu enchaîné. La position de cette seconde tête est choisie par corrélation
    (± 8 ms) pour que les deux lisent la même phase de l'onde : pas d'annulation pendant le
    fondu, beaucoup moins de « battement » qu'avec deux têtes à écart fixe.

    Le rapport peut changer à chaque échantillon sans discontinuité : les glissements de
    hauteur sont fluides. À l'unisson, une seule tête lit à retard constant (pas de peigne).
*/
class DelayLineShifter
{
public:
    /** Peut allouer. */
    void prepare (double sampleRate, double windowSeconds = 0.03);
    void reset() noexcept;

    /** Ajoute un échantillon et renvoie l'échantillon transposé (rapport = 2^(demi-tons/12)). */
    float process (float input, float ratio) noexcept;

    /** Retard moyen de lecture, en échantillons. */
    int averageLatencySamples() const noexcept;

private:
    void startSplice (double rate, double crossfadeLength) noexcept;
    double crossfadeLengthFor (double rate) const noexcept;
    float correlation (std::int64_t newest, double delayA, double delayB, int stride) const noexcept;

    static constexpr double margin = 3.0; // retard minimal (interpolation)

    std::vector<float> ring;
    std::size_t mask = 0;
    std::int64_t written = 0;

    double window = 1440.0;
    double minDelay = margin, maxDelay = margin + 1440.0;
    int search = 384, correlationLength = 480;
    int coarseStep = 4, coarseStride = 2;
    double maxCrossfade = 384.0;

    std::array<double, 2> delays { margin, margin };
    int active = 0;
    bool crossfading = false;
    double crossfadeProgress = 0.0, crossfadeStep = 0.0;
};

} // namespace subdamp::dsp
