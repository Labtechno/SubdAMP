#pragma once

#include "subdamp/dsp/DelayLineShifter.h"
#include "subdamp/dsp/LinearSmoother.h"
#include "subdamp/dsp/StereoTexture.h"

#include <array>
#include <cstdint>
#include <vector>

namespace subdamp::dsp
{

/**
    SHIMMER : réverbe dont la queue est transposée et réinjectée en boucle. Chaque passage
    dans la boucle monte d'un intervalle (octave par défaut) : la nappe « s'élève » en continu.

    Réverbe à réseau de retards (FDN) :
      - 4 passe-tout en série diffusent l'attaque ;
      - 8 lignes à retard de longueurs premières entre elles, mélangées par une matrice de
        Hadamard (orthogonale : aucune énergie créée par le mélange) ;
      - chaque ligne est amortie (passe-bas) et atténuée selon le temps de décroissance voulu ;
      - une légère modulation des longueurs évite la sonorité métallique.
    Les sorties gauche et droite combinent les lignes avec des signes différents : la nappe
    est large et décorrélée.

    La réinjection transposée passe par une saturation douce (tanh) : même au maximum, la
    boucle ne peut pas s'emballer.
*/
class ShimmerReverb final : public StereoTexture
{
public:
    void prepare (const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process (std::span<const float> input, std::span<float> left, std::span<float> right) noexcept override;

    /** Temps de décroissance de 60 dB, en secondes (0,5 à 30). */
    void setDecaySeconds (float seconds) noexcept;
    /** Quantité de son transposé réinjecté (0 = réverbe simple, 1 = maximum). */
    void setShimmer (float amount) noexcept { shimmer.setTarget (amount); }
    /** Intervalle de transposition de la boucle, en demi-tons. */
    void setInterval (float semitones) noexcept;
    /** Fréquence de coupure de l'amortissement (son plus sombre en dessous). */
    void setToneHz (float hz) noexcept;

private:
    static constexpr int numLines = 8;
    static constexpr int numDiffusers = 4;

    struct Line
    {
        std::vector<float> buffer;
        std::size_t mask = 0;
        double length = 1000.0;
        float decayGain = 0.9f;
        float damped = 0.0f;
    };

    struct Diffuser
    {
        std::vector<float> buffer;
        std::size_t index = 0;
    };

    void updateDecayGains() noexcept;

    double sampleRate = 48000.0;

    std::array<Line, numLines> lines;
    std::array<Diffuser, numDiffusers> diffusers;
    std::int64_t written = 0;

    DelayLineShifter shifter;
    float shifterRatio = 2.0f;
    float lastShifted = 0.0f;

    LinearSmoother shimmer;
    float decaySeconds = 6.0f;
    float inputScale = 1.0f; // normalisation d'énergie des longues décroissances
    float dampingCoefficient = 0.5f;

    // Oscillateur de modulation (rotation, pas de sin par échantillon)
    double modulationCos = 1.0, modulationSin = 0.0, stepCos = 1.0, stepSin = 0.0;
    double modulationDepth = 6.0;
};

} // namespace subdamp::dsp
